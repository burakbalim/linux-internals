#pragma once

#include <optional>
#include <string>
#include <vector>

namespace proc
{

    // Sourced from /proc/<pid>/stat, /proc/<pid>/status and /proc/<pid>/cmdline
    struct ProcessInfo
    {
        int pid = 0;
        int ppid = 0;
        std::string comm;
        std::string cmdline;
        char state = '?';

        long num_threads = 0;
        long long vsize_bytes = 0; // virtual address space (VSZ)
        long long rss_bytes = 0;   // resident in physical memory (RSS)

        long long utime_ticks = 0; // time spent in user mode
        long long stime_ticks = 0; // time spent in kernel mode
        long long start_time_ticks = 0;

        long priority = 0;
        long nice = 0;
        long policy = 0;
        int last_cpu = -1;

        long long minor_faults = 0; // page was already in RAM, no disk I/O
        long long major_faults = 0; // page had to be read from disk

        // From /proc/<pid>/status; -1 when absent
        long long rss_anon_kb = -1;
        long long rss_file_kb = -1;
        long long rss_shmem_kb = -1;
        long long vm_data_kb = -1;
        long long vm_stk_kb = -1;
        long long vm_exe_kb = -1;
        long long vm_lib_kb = -1;
        long long vm_swap_kb = -1;
        long long vm_hwm_kb = -1;
        long long voluntary_ctxt = -1;
        long long nonvoluntary_ctxt = -1;

        double cpu_seconds() const;
    };

    // Lines of /proc/<pid>/maps: the virtual memory regions of the address space
    struct MemoryRegion
    {
        unsigned long long start = 0;
        unsigned long long end = 0;
        std::string perms;
        unsigned long long offset = 0;
        std::string dev;
        unsigned long long inode = 0;
        std::string path;

        unsigned long long size() const { return end - start; }
        bool is_anonymous() const { return path.empty(); }
        bool is_special() const { return !path.empty() && path.front() == '['; }
    };

    struct ThreadInfo
    {
        int tid = 0;
        std::string comm;
        char state = '?';
        long long utime_ticks = 0;
        long long stime_ticks = 0;
        int last_cpu = -1;
        long priority = 0;
        long nice = 0;
        long long voluntary_ctxt = -1;
        long long nonvoluntary_ctxt = -1;

        double cpu_seconds() const;
    };

    std::vector<int> list_pids();
    std::optional<ProcessInfo> read_process(int pid);
    std::vector<MemoryRegion> read_maps(int pid);
    std::vector<ThreadInfo> read_threads(int pid);

    std::string state_name(char state);
    std::string policy_name(long policy);

} // namespace proc
