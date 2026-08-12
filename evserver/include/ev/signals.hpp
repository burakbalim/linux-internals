#pragma once

#include <string>

#include "ev/socket.hpp"

namespace ev
{

    // Blocks SIGINT and SIGTERM and returns a signalfd that reports them instead.
    //
    // The point: a signal handler runs asynchronously and may only call
    // async-signal-safe functions, which makes it awkward to stop an event loop
    // cleanly. signalfd turns the signal into a readable descriptor, so it can sit
    // in epoll next to every other event and be handled in the normal flow of the
    // loop.
    //
    // Must be called before spawning threads: the signal mask is inherited, and
    // blocking it everywhere is what stops the default "terminate immediately"
    // action from firing.
    Socket signal_fd(std::string *error);

} // namespace ev
