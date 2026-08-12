#include "ev/signals.hpp"

#include <sys/signalfd.h>

#include <cerrno>
#include <csignal>
#include <cstring>

namespace ev
{

    Socket signal_fd(std::string *error)
    {
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGTERM);

        // Blocking first is essential: otherwise the default action kills the process
        // before signalfd ever gets a chance to report the signal.
        if (::sigprocmask(SIG_BLOCK, &mask, nullptr) != 0)
        {
            if (error)
                *error = std::string("sigprocmask: ") + std::strerror(errno);
            return Socket();
        }

        Socket fd(::signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC));
        if (!fd.valid() && error)
        {
            *error = std::string("signalfd: ") + std::strerror(errno);
        }
        return fd;
    }

} // namespace ev
