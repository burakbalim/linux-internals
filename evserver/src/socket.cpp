#include "ev/socket.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace ev
{
    namespace
    {

        std::string errno_message(const char *what)
        {
            return std::string(what) + ": " + std::strerror(errno);
        }

    } // namespace

    Socket::~Socket()
    {
        reset();
    }

    Socket::Socket(Socket &&other) noexcept : fd_(other.fd_)
    {
        other.fd_ = -1;
    }

    Socket &Socket::operator=(Socket &&other) noexcept
    {
        if (this != &other)
        {
            reset(other.fd_);
            other.fd_ = -1;
        }
        return *this;
    }

    void Socket::reset(int fd)
    {
        if (fd_ >= 0)
        {
            ::close(fd_);
        }
        fd_ = fd;
    }

    int Socket::release()
    {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }

    Socket listen_tcp(uint16_t port, int backlog, std::string *error)
    {
        Socket sock(::socket(AF_INET, SOCK_STREAM, 0));
        if (!sock.valid())
        {
            if (error)
                *error = errno_message("socket");
            return Socket();
        }

        // Without SO_REUSEADDR a restart fails while old connections sit in TIME_WAIT.
        int on = 1;
        if (::setsockopt(sock.get(), SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0)
        {
            if (error)
                *error = errno_message("setsockopt(SO_REUSEADDR)");
            return Socket();
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);

        if (::bind(sock.get(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
        {
            if (error)
                *error = errno_message("bind");
            return Socket();
        }
        if (::listen(sock.get(), backlog) != 0)
        {
            if (error)
                *error = errno_message("listen");
            return Socket();
        }
        return sock;
    }

    Socket connect_tcp(uint16_t port, std::string *error)
    {
        Socket sock(::socket(AF_INET, SOCK_STREAM, 0));
        if (!sock.valid())
        {
            if (error)
                *error = errno_message("socket");
            return Socket();
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);

        if (::connect(sock.get(), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0)
        {
            if (error)
                *error = errno_message("connect");
            return Socket();
        }
        return sock;
    }

    bool set_nonblocking(int fd)
    {
        const int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags < 0)
        {
            return false;
        }
        return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
    }

    bool set_nodelay(int fd)
    {
        // Disables Nagle: without it small echo replies get delayed waiting for more data.
        int on = 1;
        return ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)) == 0;
    }

} // namespace ev
