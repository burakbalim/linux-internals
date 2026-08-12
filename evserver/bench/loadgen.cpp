// Load generator for evserver.
//
// Two knobs matter, and they exercise different things:
//
//   --threads N   active connections doing strict request/response. Each one
//   waits for
//                 its reply before sending again, so the recorded latency is
//                 real.
//
//   --idle M      connections that are opened and then left silent. They cost
//   the
//                 server nothing under epoll, but under thread-per-connection
//                 each one pins a thread and its stack. This is where the
//                 models diverge.
//
// While it runs, inspect the server with:
//   procmon show <server pid>       RSS and thread count
//   procmon threads <server pid>    per-thread context switches

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "ev/socket.hpp"

namespace
{

    using Clock = std::chrono::steady_clock;

    struct Options
    {
        uint16_t port = 9000;
        int threads = 8;
        int idle = 0;
        int seconds = 5;
        size_t payload = 64;
    };

    struct Result
    {
        uint64_t requests = 0;
        std::vector<double> latencies_us;
    };

    void worker(const Options &opt, std::atomic<bool> &stop, Result *out)
    {
        std::string error;
        ev::Socket sock = ev::connect_tcp(opt.port, &error);
        if (!sock.valid())
        {
            return;
        }
        ev::set_nodelay(sock.get());

        const std::string payload(opt.payload, 'x');
        std::vector<char> reply(opt.payload);
        out->latencies_us.reserve(1 << 16);

        while (!stop.load(std::memory_order_relaxed))
        {
            const auto start = Clock::now();

            if (::write(sock.get(), payload.data(), payload.size()) !=
                static_cast<ssize_t>(payload.size()))
            {
                return;
            }

            // Read until the full echo is back; TCP may split it across segments.
            size_t got = 0;
            while (got < payload.size())
            {
                const ssize_t n = ::read(sock.get(), reply.data() + got, payload.size() - got);
                if (n <= 0)
                {
                    return;
                }
                got += static_cast<size_t>(n);
            }

            const auto elapsed = Clock::now() - start;
            out->latencies_us.push_back(std::chrono::duration<double, std::micro>(elapsed).count());
            ++out->requests;
        }
    }

    double percentile(std::vector<double> &sorted, double p)
    {
        if (sorted.empty())
        {
            return 0.0;
        }
        const size_t index =
            static_cast<size_t>(p / 100.0 * static_cast<double>(sorted.size() - 1));
        return sorted[std::min(index, sorted.size() - 1)];
    }

} // namespace

int main(int argc, char **argv)
{
    Options opt;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg.rfind("--port=", 0) == 0)
        {
            opt.port = static_cast<uint16_t>(std::stoi(arg.substr(7)));
        }
        else if (arg.rfind("--threads=", 0) == 0)
        {
            opt.threads = std::stoi(arg.substr(10));
        }
        else if (arg.rfind("--idle=", 0) == 0)
        {
            opt.idle = std::stoi(arg.substr(7));
        }
        else if (arg.rfind("--seconds=", 0) == 0)
        {
            opt.seconds = std::stoi(arg.substr(10));
        }
        else if (arg.rfind("--payload=", 0) == 0)
        {
            opt.payload = static_cast<size_t>(std::stoul(arg.substr(10)));
        }
        else
        {
            std::cerr << "Usage: loadgen [--port=N] [--threads=N] [--idle=N] "
                         "[--seconds=N] [--payload=N]\n";
            return 2;
        }
    }

    // Idle connections are opened first and held open by keeping the sockets
    // alive.
    std::vector<ev::Socket> idle;
    idle.reserve(static_cast<size_t>(opt.idle));
    for (int i = 0; i < opt.idle; ++i)
    {
        std::string error;
        ev::Socket s = ev::connect_tcp(opt.port, &error);
        if (!s.valid())
        {
            std::cerr << "idle connection " << i << " failed: " << error << '\n';
            break;
        }
        idle.push_back(std::move(s));
    }

    std::cout << "loadgen: " << opt.threads << " active, " << idle.size() << " idle, "
              << opt.payload << " byte payload, " << opt.seconds << "s\n";
    if (!idle.empty())
    {
        // Give the server a moment to finish accepting before load starts.
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    std::atomic<bool> stop{false};
    std::vector<Result> results(static_cast<size_t>(opt.threads));
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(opt.threads));

    const auto started = Clock::now();
    for (int i = 0; i < opt.threads; ++i)
    {
        workers.emplace_back(worker, std::cref(opt), std::ref(stop),
                             &results[static_cast<size_t>(i)]);
    }

    std::this_thread::sleep_for(std::chrono::seconds(opt.seconds));
    stop = true;
    for (auto &t : workers)
    {
        t.join();
    }
    const double elapsed = std::chrono::duration<double>(Clock::now() - started).count();

    uint64_t total = 0;
    std::vector<double> all;
    for (auto &r : results)
    {
        total += r.requests;
        all.insert(all.end(), r.latencies_us.begin(), r.latencies_us.end());
    }
    std::sort(all.begin(), all.end());

    std::printf("\n  requests      %llu\n", (unsigned long long)total);
    std::printf("  throughput    %.0f req/s\n", static_cast<double>(total) / elapsed);
    if (!all.empty())
    {
        std::printf("  latency p50   %.1f us\n", percentile(all, 50));
        std::printf("  latency p99   %.1f us\n", percentile(all, 99));
        std::printf("  latency max   %.1f us\n", all.back());
    }
    return 0;
}
