#pragma once

#include <atomic>
#include <cstdint>

namespace ev
{

    // Plain copy of the counters, for printing.
    struct StatsSnapshot
    {
        uint64_t accepted = 0;
        uint64_t closed = 0;
        uint64_t requests = 0;
        uint64_t bytes = 0;
        uint64_t read_calls = 0;
        uint64_t write_calls = 0;
        uint64_t wait_calls = 0; // epoll_wait / io_uring_enter / accept blocking
        uint64_t eagain = 0;     // reads that returned EAGAIN
        uint64_t threads_spawned = 0;
        uint64_t syscalls = 0; // actual kernel entries, counted by each backend
    };

    // Atomic because the thread-per-connection backend updates these from many
    // threads. relaxed ordering is enough: these are counters, nothing is
    // synchronised through them.
    struct Stats
    {
        std::atomic<uint64_t> accepted{0};
        std::atomic<uint64_t> closed{0};
        std::atomic<uint64_t> requests{0};
        std::atomic<uint64_t> bytes{0};
        std::atomic<uint64_t> read_calls{0};
        std::atomic<uint64_t> write_calls{0};
        std::atomic<uint64_t> wait_calls{0};
        std::atomic<uint64_t> eagain{0};
        std::atomic<uint64_t> threads_spawned{0};

        // Counted explicitly rather than derived: under io_uring a read is a queue
        // entry, not a kernel entry, so summing read+write+wait would be nonsense.
        std::atomic<uint64_t> syscalls{0};

        void bump(std::atomic<uint64_t> &counter, uint64_t by = 1)
        {
            counter.fetch_add(by, std::memory_order_relaxed);
        }

        StatsSnapshot snapshot() const
        {
            StatsSnapshot s;
            s.accepted = accepted.load(std::memory_order_relaxed);
            s.closed = closed.load(std::memory_order_relaxed);
            s.requests = requests.load(std::memory_order_relaxed);
            s.bytes = bytes.load(std::memory_order_relaxed);
            s.read_calls = read_calls.load(std::memory_order_relaxed);
            s.write_calls = write_calls.load(std::memory_order_relaxed);
            s.wait_calls = wait_calls.load(std::memory_order_relaxed);
            s.eagain = eagain.load(std::memory_order_relaxed);
            s.threads_spawned = threads_spawned.load(std::memory_order_relaxed);
            s.syscalls = syscalls.load(std::memory_order_relaxed);
            return s;
        }
    };

} // namespace ev
