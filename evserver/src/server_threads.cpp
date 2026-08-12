#include <poll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <thread>
#include <vector>

#include "ev/server.hpp"
#include "ev/signals.hpp"
#include "ev/socket.hpp"

namespace ev
{
    namespace
    {

        // The baseline: one blocking thread per connection.
        //
        // This is the model epoll replaced, and it is worth building so the comparison
        // is concrete rather than folklore. It is genuinely simpler - blocking
        // read/write, no state machine, no readiness handling - and for a small number
        // of busy connections it performs perfectly well.
        //
        // What it cannot do is scale to many mostly-idle connections: every connection
        // costs a thread, and a thread costs a stack (8 MB of address space reserved by
        // default) plus a scheduler entry. Run the load generator with --idle and watch
        // RSS and thread count with procmon to see where it goes wrong.
        class ThreadServer : public Server
        {
        public:
            explicit ThreadServer(const Config &cfg) : cfg_(cfg) {}

            ~ThreadServer() override
            {
                running_ = false;
                for (auto &t : workers_)
                {
                    if (t.joinable())
                    {
                        t.join();
                    }
                }
            }

            bool start(std::string *error) override
            {
                listener_ = listen_tcp(cfg_.port, 1024, error);
                if (!listener_.valid())
                {
                    return false;
                }
                // Signals are blocked here, before any thread is spawned, so the mask is
                // inherited by every worker and only this loop reacts to shutdown.
                signal_ = signal_fd(error);
                return signal_.valid();
            }

            void run() override
            {
                // poll() on both descriptors, so a shutdown signal interrupts the accept
                // wait.
                pollfd fds[2];
                fds[0] = {listener_.get(), POLLIN, 0};
                fds[1] = {signal_.get(), POLLIN, 0};

                while (running_)
                {
                    const int n = ::poll(fds, 2, -1);
                    stats_.bump(stats_.wait_calls);
                    stats_.bump(stats_.syscalls);
                    if (n < 0)
                    {
                        if (errno == EINTR)
                            continue;
                        break;
                    }

                    if ((fds[1].revents & POLLIN) != 0)
                    {
                        signalfd_siginfo info{};
                        while (::read(signal_.get(), &info, sizeof(info)) == sizeof(info))
                        {
                            running_ = false;
                        }
                        break;
                    }

                    if ((fds[0].revents & POLLIN) != 0)
                    {
                        const int fd = ::accept4(listener_.get(), nullptr, nullptr, SOCK_CLOEXEC);
                        if (fd < 0)
                        {
                            continue;
                        }
                        set_nodelay(fd);
                        stats_.bump(stats_.accepted);
                        stats_.bump(stats_.threads_spawned);
                        workers_.emplace_back([this, fd] { serve(fd); });
                    }
                }
            }

            StatsSnapshot stats() const override { return stats_.snapshot(); }

        private:
            // One of these runs per connection, blocking on read until the peer goes
            // away.
            void serve(int fd)
            {
                Socket conn(fd);
                std::vector<char> buffer(cfg_.buffer_size);

                while (running_)
                {
                    const ssize_t n = ::read(conn.get(), buffer.data(), buffer.size());
                    stats_.bump(stats_.read_calls);
                    stats_.bump(stats_.syscalls);
                    if (n <= 0)
                    {
                        if (n < 0 && errno == EINTR)
                            continue;
                        break;
                    }

                    stats_.bump(stats_.requests);
                    stats_.bump(stats_.bytes, static_cast<uint64_t>(n));

                    size_t written = 0;
                    while (written < static_cast<size_t>(n))
                    {
                        const ssize_t w = ::write(conn.get(), buffer.data() + written,
                                                  static_cast<size_t>(n) - written);
                        stats_.bump(stats_.write_calls);
                        stats_.bump(stats_.syscalls);
                        if (w <= 0)
                        {
                            if (w < 0 && errno == EINTR)
                                continue;
                            stats_.bump(stats_.closed);
                            return;
                        }
                        written += static_cast<size_t>(w);
                    }
                }
                stats_.bump(stats_.closed);
            }

            Config cfg_;
            Socket listener_;
            Socket signal_;
            Stats stats_;
            std::atomic<bool> running_{true};
            std::vector<std::thread> workers_;
        };

    } // namespace

    std::unique_ptr<Server> make_thread_server(const Config &cfg)
    {
        return std::make_unique<ThreadServer>(cfg);
    }

} // namespace ev
