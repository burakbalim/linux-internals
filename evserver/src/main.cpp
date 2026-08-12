#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "ev/server.hpp"

namespace
{

    void usage()
    {
        std::cout << R"(evserver - the same TCP echo server written four ways

Usage:
  evserver [--backend=threads|epoll|epoll-et|uring] [--port=N] [--buffer=N] [--max-events=N]

Backends:
  threads    one blocking thread per connection (the baseline epoll replaced)
  epoll      epoll, level-triggered (default)
  epoll-et   epoll, edge-triggered - drains each socket until EAGAIN
  uring      io_uring: operations batched into a shared ring, one kernel entry

Stop with Ctrl-C: SIGINT arrives through a signalfd inside the event loop,
so shutdown is part of the normal flow rather than an async handler.

Counters are printed on exit. Compare them across backends with bench/loadgen.
)";
    }

} // namespace

int main(int argc, char **argv)
{
    ev::Config cfg;
    const std::vector<std::string> args(argv + 1, argv + argc);

    for (const auto &arg : args)
    {
        if (arg == "-h" || arg == "--help")
        {
            usage();
            return 0;
        }
        if (arg.rfind("--backend=", 0) == 0)
        {
            if (!ev::parse_backend(arg.substr(10), &cfg.backend))
            {
                std::cerr << "Unknown backend: " << arg.substr(10) << '\n';
                return 2;
            }
        }
        else if (arg.rfind("--port=", 0) == 0)
        {
            cfg.port = static_cast<uint16_t>(std::stoi(arg.substr(7)));
        }
        else if (arg.rfind("--buffer=", 0) == 0)
        {
            cfg.buffer_size = static_cast<size_t>(std::stoul(arg.substr(9)));
        }
        else if (arg.rfind("--max-events=", 0) == 0)
        {
            cfg.max_events = std::stoi(arg.substr(13));
        }
        else
        {
            std::cerr << "Unknown option: " << arg << "\n\n";
            usage();
            return 2;
        }
    }

    auto server = ev::make_server(cfg);
    std::string error;
    if (!server->start(&error))
    {
        std::cerr << "Failed to start: " << error << '\n';
        return 1;
    }

    std::cout << "evserver listening on port " << cfg.port << '\n'
              << "  backend: " << ev::backend_name(cfg.backend) << '\n'
              << "  pid:     " << ::getpid() << "  (inspect with: procmon show " << ::getpid()
              << ")\n\n"
              << "Ctrl-C to stop.\n";

    server->run();

    const auto s = server->stats();
    std::cout << "\n--- " << ev::backend_name(cfg.backend) << " ---\n";
    std::printf("  connections accepted   %llu\n", (unsigned long long)s.accepted);
    std::printf("  connections closed     %llu\n", (unsigned long long)s.closed);
    std::printf("  requests echoed        %llu\n", (unsigned long long)s.requests);
    std::printf("  bytes echoed           %llu\n", (unsigned long long)s.bytes);
    std::printf("  read() calls           %llu\n", (unsigned long long)s.read_calls);
    std::printf("  write() calls          %llu\n", (unsigned long long)s.write_calls);
    std::printf("  wait calls             %llu   (epoll_wait / poll)\n",
                (unsigned long long)s.wait_calls);
    std::printf("  reads returning EAGAIN %llu\n", (unsigned long long)s.eagain);
    if (s.threads_spawned > 0)
    {
        std::printf("  threads spawned        %llu\n", (unsigned long long)s.threads_spawned);
    }

    if (s.requests > 0)
    {
        // Counted by each backend rather than derived: under io_uring a read is a
        // ring entry, so summing read+write+wait would overstate the kernel entries
        // badly.
        std::printf("  kernel entries         %llu\n", (unsigned long long)s.syscalls);
        std::printf("\n  syscalls per request   %.2f\n",
                    static_cast<double>(s.syscalls) / static_cast<double>(s.requests));
    }
    return 0;
}
