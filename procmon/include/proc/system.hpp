#pragma once

#include <optional>

namespace proc
{

    // Fields of /proc/meminfo, all in kB. -1 when absent.
    struct MemInfo
    {
        long long mem_total = -1;
        long long mem_free = -1;
        long long mem_available = -1;
        long long buffers = -1; // block device metadata cache
        long long cached = -1;  // page cache (file contents)
        long long swap_cached = -1;
        long long active = -1;
        long long inactive = -1;
        long long dirty = -1; // pages awaiting writeback
        long long writeback = -1;
        long long anon_pages = -1; // not file-backed (heap/stack)
        long long mapped = -1;     // file pages currently mmap'ed
        long long shmem = -1;
        long long slab = -1;
        long long swap_total = -1;
        long long swap_free = -1;
    };

    struct LoadAvg
    {
        double one = 0;
        double five = 0;
        double fifteen = 0;
        int runnable = 0;
        int total = 0;
    };

    std::optional<MemInfo> read_meminfo();
    std::optional<LoadAvg> read_loadavg();
    std::optional<double> read_uptime_seconds();

} // namespace proc
