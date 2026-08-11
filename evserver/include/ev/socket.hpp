#pragma once

#include <cstdint>
#include <string>

namespace ev {

// RAII wrapper around a file descriptor. Non-copyable, movable: exactly one
// object owns the descriptor and closes it on destruction.
class Socket {
public:
    Socket() = default;
    explicit Socket(int fd) : fd_(fd) {}
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    int get() const { return fd_; }
    bool valid() const { return fd_ >= 0; }
    void reset(int fd = -1);
    int release();

private:
    int fd_ = -1;
};

// Creates a listening TCP socket bound to port, with SO_REUSEADDR set.
Socket listen_tcp(uint16_t port, int backlog, std::string* error);

// Connects to 127.0.0.1:port. Used by the load generator.
Socket connect_tcp(uint16_t port, std::string* error);

bool set_nonblocking(int fd);
bool set_nodelay(int fd);

}  // namespace ev
