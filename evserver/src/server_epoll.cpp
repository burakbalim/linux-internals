#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <vector>

#include "ev/server.hpp"
#include "ev/signals.hpp"
#include "ev/socket.hpp"

namespace ev
{
    namespace
    {

        // A single-threaded epoll echo server, in either level- or edge-triggered mode.
        //
        // The difference between the two modes is the whole point of this file:
        //
        //   Level-triggered (default): epoll_wait reports a descriptor as ready as long as
        //   data remains buffered. Reading once per wakeup is correct - the next epoll_wait
        //   will report it again if there is more.
        //
        //   Edge-triggered (EPOLLET): epoll_wait reports readiness only when the state
        //   CHANGES, i.e. when new data arrives. If you do not drain the socket until EAGAIN,
        //   the leftover bytes will never be reported again and the connection hangs. Same
        //   applies to accept(): you must loop until EAGAIN or you silently drop connections.
        class EpollServer : public Server
        {
        public:
            explicit EpollServer(const Config &cfg) : cfg_(cfg), buffer_(cfg.buffer_size) {}

            bool start(std::string *error) override
            {
                listener_ = listen_tcp(cfg_.port, 1024, error);
                if (!listener_.valid())
                {
                    return false;
                }
                if (!set_nonblocking(listener_.get()))
                {
                    if (error)
                        *error = "set_nonblocking(listener) failed";
                    return false;
                }

                signal_ = signal_fd(error);
                if (!signal_.valid())
                {
                    return false;
                }

                epoll_ = Socket(::epoll_create1(EPOLL_CLOEXEC));
                if (!epoll_.valid())
                {
                    if (error)
                        *error = std::string("epoll_create1: ") + std::strerror(errno);
                    return false;
                }

                // The listener is registered level-triggered even in ET mode; the accept loop
                // below drains it anyway, and this keeps the failure mode less surprising.
                if (!add_to_epoll(listener_.get(), EPOLLIN, error))
                {
                    return false;
                }
                return add_to_epoll(signal_.get(), EPOLLIN, error);
            }

            void run() override
            {
                std::vector<epoll_event> events(cfg_.max_events);

                while (running_)
                {
                    const int n = ::epoll_wait(epoll_.get(), events.data(),
                                               static_cast<int>(events.size()), -1);
                    stats_.bump(stats_.wait_calls);

                    if (n < 0)
                    {
                        if (errno == EINTR)
                        {
                            continue;
                        }
                        break;
                    }

                    for (int i = 0; i < n; ++i)
                    {
                        const int fd = events[i].data.fd;
                        if (fd == signal_.get())
                        {
                            drain_signal();
                        }
                        else if (fd == listener_.get())
                        {
                            accept_connections();
                        }
                        else
                        {
                            handle_connection(fd, events[i].events);
                        }
                    }
                }
            }

            StatsSnapshot stats() const override { return stats_.snapshot(); }

        private:
            bool add_to_epoll(int fd, uint32_t events, std::string *error)
            {
                epoll_event ev{};
                ev.events = events;
                ev.data.fd = fd;
                if (::epoll_ctl(epoll_.get(), EPOLL_CTL_ADD, fd, &ev) != 0)
                {
                    if (error)
                        *error = std::string("epoll_ctl(ADD): ") + std::strerror(errno);
                    return false;
                }
                return true;
            }

            void drain_signal()
            {
                signalfd_siginfo info{};
                while (::read(signal_.get(), &info, sizeof(info)) == sizeof(info))
                {
                    running_ = false;
                }
            }

            void accept_connections()
            {
                // Loop until EAGAIN. Several connections can arrive between two wakeups, and
                // in edge-triggered mode a single accept() would leave the rest unreported.
                for (;;)
                {
                    const int fd =
                        ::accept4(listener_.get(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
                    if (fd < 0)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            return;
                        }
                        if (errno == EINTR)
                        {
                            continue;
                        }
                        return;
                    }

                    set_nodelay(fd);
                    const uint32_t flags = edge_triggered() ? (EPOLLIN | EPOLLET) : EPOLLIN;
                    if (!add_to_epoll(fd, flags, nullptr))
                    {
                        ::close(fd);
                        continue;
                    }
                    stats_.bump(stats_.accepted);
                }
            }

            void handle_connection(int fd, uint32_t events)
            {
                if ((events & (EPOLLHUP | EPOLLERR)) != 0)
                {
                    close_connection(fd);
                    return;
                }

                for (;;)
                {
                    const ssize_t n = ::read(fd, buffer_.data(), buffer_.size());
                    stats_.bump(stats_.read_calls);

                    if (n > 0)
                    {
                        stats_.bump(stats_.requests);
                        stats_.bump(stats_.bytes, static_cast<uint64_t>(n));
                        if (!write_all(fd, buffer_.data(), static_cast<size_t>(n)))
                        {
                            close_connection(fd);
                            return;
                        }
                        // Level-triggered: one read per wakeup is enough, epoll will tell us
                        // again if more is buffered. Edge-triggered: we must keep going.
                        if (!edge_triggered())
                        {
                            return;
                        }
                        continue;
                    }

                    if (n == 0)
                    {
                        close_connection(fd); // peer closed
                        return;
                    }

                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        stats_.bump(stats_.eagain);
                        return; // socket drained
                    }
                    if (errno == EINTR)
                    {
                        continue;
                    }
                    close_connection(fd);
                    return;
                }
            }

            bool write_all(int fd, const char *data, size_t len)
            {
                size_t written = 0;
                while (written < len)
                {
                    const ssize_t n = ::write(fd, data + written, len - written);
                    stats_.bump(stats_.write_calls);
                    if (n > 0)
                    {
                        written += static_cast<size_t>(n);
                        continue;
                    }
                    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                    {
                        // A production server would buffer the remainder and wait for EPOLLOUT.
                        // Echo replies here are small enough to fit the socket buffer, so we
                        // spin instead of adding a write queue; see NOTES.md.
                        continue;
                    }
                    if (n < 0 && errno == EINTR)
                    {
                        continue;
                    }
                    return false;
                }
                return true;
            }

            void close_connection(int fd)
            {
                // epoll_ctl(DEL) is implicit on close, but being explicit documents the intent.
                ::epoll_ctl(epoll_.get(), EPOLL_CTL_DEL, fd, nullptr);
                ::close(fd);
                stats_.bump(stats_.closed);
            }

            bool edge_triggered() const { return cfg_.backend == Backend::EpollEdge; }

            Config cfg_;
            std::vector<char> buffer_;
            Socket listener_;
            Socket signal_;
            Socket epoll_;
            Stats stats_;
            bool running_ = true;
        };

    } // namespace

    std::unique_ptr<Server> make_epoll_server(const Config &cfg)
    {
        return std::make_unique<EpollServer>(cfg);
    }

} // namespace ev
