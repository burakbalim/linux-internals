#include <liburing.h>
#include <poll.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

#include "ev/server.hpp"
#include "ev/signals.hpp"
#include "ev/socket.hpp"

namespace ev
{
    namespace
    {

        // io_uring turns I/O from "one syscall per operation" into two shared ring
        // buffers.
        //
        // The submission queue holds requests the program writes; the completion queue
        // holds results the kernel writes. Both live in memory mapped into both address
        // spaces, so filling in a request costs no syscall at all. Only
        // io_uring_enter() actually enters the kernel, and a single call can carry an
        // arbitrary number of operations.
        //
        // That is the whole idea: with epoll, N ready connections cost 1 epoll_wait + N
        // reads
        // + N writes. Here they cost one enter, because the reads and writes were
        // queued as memory writes. The counters printed on exit measure exactly this
        // difference.
        //
        // The flow per connection is a small state machine driven by completions:
        //   accept -> recv -> send -> recv -> ...
        // Each completion says which operation finished, via the user_data field.

        enum class OpType
        {
            Accept,
            Recv,
            Send,
            Signal,
        };

        struct Conn;

        struct Op
        {
            OpType type;
            Conn *conn = nullptr;
        };

        struct Conn
        {
            int fd = -1;
            std::vector<char> buffer;
            size_t pending = 0; // bytes still to send back
            Op recv_op{OpType::Recv, nullptr};
            Op send_op{OpType::Send, nullptr};
        };

        class UringServer : public Server
        {
        public:
            explicit UringServer(const Config &cfg) : cfg_(cfg) {}

            ~UringServer() override
            {
                if (ring_initialised_)
                {
                    io_uring_queue_exit(&ring_);
                }
            }

            bool start(std::string *error) override
            {
                listener_ = listen_tcp(cfg_.port, 1024, error);
                if (!listener_.valid())
                {
                    return false;
                }

                signal_ = signal_fd(error);
                if (!signal_.valid())
                {
                    return false;
                }

                const int rc = io_uring_queue_init(kQueueDepth, &ring_, 0);
                if (rc < 0)
                {
                    if (error)
                    {
                        *error = std::string("io_uring_queue_init: ") + std::strerror(-rc);
                    }
                    return false;
                }
                ring_initialised_ = true;

                submit_accept();
                submit_signal_poll();
                return true;
            }

            void run() override
            {
                while (running_)
                {
                    // One kernel entry: it flushes everything queued and waits for a result.
                    // This is the only syscall in the loop, no matter how many operations
                    // were queued since the last call.
                    const int rc = io_uring_submit_and_wait(&ring_, 1);
                    stats_.bump(stats_.wait_calls);
                    stats_.bump(stats_.syscalls);

                    if (rc < 0 && rc != -EINTR)
                    {
                        break;
                    }

                    io_uring_cqe *cqe;
                    unsigned head;
                    unsigned count = 0;

                    // Draining the completion queue costs nothing: it is a memory read.
                    io_uring_for_each_cqe(&ring_, head, cqe)
                    {
                        ++count;
                        handle_completion(cqe);
                    }
                    io_uring_cq_advance(&ring_, count);
                }
            }

            StatsSnapshot stats() const override { return stats_.snapshot(); }

        private:
            static constexpr unsigned kQueueDepth = 256;

            io_uring_sqe *next_sqe()
            {
                io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
                if (sqe == nullptr)
                {
                    // The submission queue is full; flush it and try once more.
                    io_uring_submit(&ring_);
                    stats_.bump(stats_.syscalls);
                    sqe = io_uring_get_sqe(&ring_);
                }
                return sqe;
            }

            void submit_accept()
            {
                io_uring_sqe *sqe = next_sqe();
                if (sqe == nullptr)
                {
                    return;
                }
                io_uring_prep_accept(sqe, listener_.get(), nullptr, nullptr, 0);
                io_uring_sqe_set_data(sqe, &accept_op_);
            }

            void submit_signal_poll()
            {
                io_uring_sqe *sqe = next_sqe();
                if (sqe == nullptr)
                {
                    return;
                }
                // The signalfd sits in the ring like any other descriptor, exactly as it
                // did in the epoll backend - shutdown stays part of the normal completion
                // flow.
                io_uring_prep_poll_add(sqe, signal_.get(), POLLIN);
                io_uring_sqe_set_data(sqe, &signal_op_);
            }

            void submit_recv(Conn *conn)
            {
                io_uring_sqe *sqe = next_sqe();
                if (sqe == nullptr)
                {
                    close_connection(conn);
                    return;
                }
                io_uring_prep_recv(sqe, conn->fd, conn->buffer.data(), conn->buffer.size(), 0);
                io_uring_sqe_set_data(sqe, &conn->recv_op);
                stats_.bump(stats_.read_calls); // a queue entry, not a kernel entry
            }

            void submit_send(Conn *conn, size_t length)
            {
                io_uring_sqe *sqe = next_sqe();
                if (sqe == nullptr)
                {
                    close_connection(conn);
                    return;
                }
                conn->pending = length;
                io_uring_prep_send(sqe, conn->fd, conn->buffer.data(), length, 0);
                io_uring_sqe_set_data(sqe, &conn->send_op);
                stats_.bump(stats_.write_calls);
            }

            void handle_completion(io_uring_cqe *cqe)
            {
                auto *op = static_cast<Op *>(io_uring_cqe_get_data(cqe));
                if (op == nullptr)
                {
                    return;
                }
                const int result = cqe->res;

                switch (op->type)
                {
                case OpType::Signal:
                    running_ = false;
                    return;

                case OpType::Accept:
                    submit_accept(); // keep one accept outstanding at all times
                    if (result >= 0)
                    {
                        set_nodelay(result);
                        auto conn = std::make_unique<Conn>();
                        conn->fd = result;
                        conn->buffer.resize(cfg_.buffer_size);
                        conn->recv_op.conn = conn.get();
                        conn->send_op.conn = conn.get();

                        Conn *raw = conn.get();
                        connections_[result] = std::move(conn);
                        stats_.bump(stats_.accepted);
                        submit_recv(raw);
                    }
                    return;

                case OpType::Recv:
                    if (result > 0)
                    {
                        stats_.bump(stats_.requests);
                        stats_.bump(stats_.bytes, static_cast<uint64_t>(result));
                        submit_send(op->conn, static_cast<size_t>(result));
                    }
                    else
                    {
                        close_connection(op->conn); // 0 = peer closed, <0 = error
                    }
                    return;

                case OpType::Send:
                    if (result > 0 && static_cast<size_t>(result) < op->conn->pending)
                    {
                        // Short write: send the remainder before reading again.
                        const size_t sent = static_cast<size_t>(result);
                        std::memmove(op->conn->buffer.data(), op->conn->buffer.data() + sent,
                                     op->conn->pending - sent);
                        submit_send(op->conn, op->conn->pending - sent);
                    }
                    else if (result >= 0)
                    {
                        submit_recv(op->conn);
                    }
                    else
                    {
                        close_connection(op->conn);
                    }
                    return;
                }
            }

            void close_connection(Conn *conn)
            {
                if (conn == nullptr || conn->fd < 0)
                {
                    return;
                }
                const int fd = conn->fd;
                conn->fd = -1;
                ::close(fd);
                stats_.bump(stats_.syscalls); // close() really is a syscall
                stats_.bump(stats_.closed);
                connections_.erase(fd);
            }

            Config cfg_;
            Socket listener_;
            Socket signal_;
            io_uring ring_{};
            bool ring_initialised_ = false;
            bool running_ = true;

            Op accept_op_{OpType::Accept, nullptr};
            Op signal_op_{OpType::Signal, nullptr};
            std::unordered_map<int, std::unique_ptr<Conn>> connections_;
            Stats stats_;
        };

    } // namespace

    std::unique_ptr<Server> make_uring_server(const Config &cfg)
    {
        return std::make_unique<UringServer>(cfg);
    }

} // namespace ev
