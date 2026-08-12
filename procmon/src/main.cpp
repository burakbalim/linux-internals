#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "proc/format.hpp"
#include "proc/process.hpp"
#include "proc/system.hpp"
#include "proc/util.hpp"

namespace
{

    using namespace proc;

    void usage()
    {
        std::cout << R"(procmon - inspect processes, threads and memory through /proc

Usage:
  procmon list [--sort=rss|cpu|pid|threads] [--limit=N]
  procmon show <pid>          Detailed breakdown of one process
  procmon threads <pid>       Threads of a process (/proc/<pid>/task)
  procmon maps <pid>          Virtual memory regions (/proc/<pid>/maps)
  procmon mem                 System memory and page cache (/proc/meminfo)

"self" may be used in place of <pid>.
)";
    }

    int resolve_pid(const std::string &arg)
    {
        if (arg == "self")
        {
            return static_cast<int>(::getpid());
        }
        if (!is_number(arg))
        {
            return -1;
        }
        return std::stoi(arg);
    }

    int cmd_list(const std::vector<std::string> &args)
    {
        std::string sort_key = "rss";
        size_t limit = 20;

        for (const auto &arg : args)
        {
            if (arg.rfind("--sort=", 0) == 0)
            {
                sort_key = arg.substr(7);
            }
            else if (arg.rfind("--limit=", 0) == 0)
            {
                limit = static_cast<size_t>(std::stoul(arg.substr(8)));
            }
            else
            {
                std::cerr << "Unknown option: " << arg << '\n';
                return 2;
            }
        }

        std::vector<ProcessInfo> processes;
        for (const int pid : list_pids())
        {
            if (auto info = read_process(pid))
            {
                processes.push_back(std::move(*info));
            }
            // Processes that exit while we scan are skipped: /proc is inherently racy.
        }

        if (sort_key == "rss")
        {
            std::sort(processes.begin(), processes.end(),
                      [](const auto &a, const auto &b) { return a.rss_bytes > b.rss_bytes; });
        }
        else if (sort_key == "cpu")
        {
            std::sort(processes.begin(), processes.end(),
                      [](const auto &a, const auto &b)
                      { return a.cpu_seconds() > b.cpu_seconds(); });
        }
        else if (sort_key == "threads")
        {
            std::sort(processes.begin(), processes.end(),
                      [](const auto &a, const auto &b) { return a.num_threads > b.num_threads; });
        }
        else if (sort_key == "pid")
        {
            std::sort(processes.begin(), processes.end(),
                      [](const auto &a, const auto &b) { return a.pid < b.pid; });
        }
        else
        {
            std::cerr << "Invalid sort key: " << sort_key << '\n';
            return 2;
        }

        const size_t total = processes.size();
        if (processes.size() > limit)
        {
            processes.resize(limit);
        }

        Table table({"PID", "PPID", "S", "THR", "RSS", "VSZ", "CPU", "MAJFLT", "COMMAND"});
        for (size_t i = 0; i < 8; ++i)
        {
            table.right_align(i);
        }

        for (const auto &p : processes)
        {
            table.add_row({std::to_string(p.pid), std::to_string(p.ppid), std::string(1, p.state),
                           std::to_string(p.num_threads), human_bytes(p.rss_bytes),
                           human_bytes(p.vsize_bytes), human_duration(p.cpu_seconds()),
                           std::to_string(p.major_faults), p.comm});
        }
        table.print(std::cout);

        std::cout << "\n"
                  << total << " processes, showing " << processes.size() << " (sorted by "
                  << sort_key << ").\n";
        return 0;
    }

    int cmd_show(int pid)
    {
        const auto info = read_process(pid);
        if (!info)
        {
            std::cerr << "Cannot read PID " << pid << " (gone, or insufficient permissions).\n";
            return 1;
        }
        const auto &p = *info;

        print_section(std::cout, "Identity");
        print_field(std::cout, "PID / PPID",
                    std::to_string(p.pid) + " / " + std::to_string(p.ppid));
        print_field(std::cout, "Command (comm)", p.comm);
        if (!p.cmdline.empty())
        {
            print_field(std::cout, "Cmdline", p.cmdline);
        }
        print_field(std::cout, "State", std::string(1, p.state) + " - " + state_name(p.state));

        print_section(std::cout, "Scheduling");
        print_field(std::cout, "Policy", policy_name(p.policy));
        print_field(std::cout, "Priority / nice",
                    std::to_string(p.priority) + " / " + std::to_string(p.nice));
        print_field(std::cout, "Last CPU", std::to_string(p.last_cpu));
        print_field(std::cout, "Threads", std::to_string(p.num_threads));
        print_field(std::cout, "CPU time (user)",
                    human_duration(static_cast<double>(p.utime_ticks) / clock_ticks_per_sec()));
        print_field(std::cout, "CPU time (kernel)",
                    human_duration(static_cast<double>(p.stime_ticks) / clock_ticks_per_sec()));
        if (p.voluntary_ctxt >= 0)
        {
            // Voluntary: the task blocked itself (I/O, lock). Involuntary: preempted by the
            // scheduler.
            print_field(std::cout, "Ctx switches (voluntary)", std::to_string(p.voluntary_ctxt));
            print_field(std::cout, "Ctx switches (involuntary)",
                        std::to_string(p.nonvoluntary_ctxt));
        }

        print_section(std::cout, "Memory");
        print_field(std::cout, "VSZ (virtual)", human_bytes(p.vsize_bytes));
        print_field(std::cout, "RSS (resident)", human_bytes(p.rss_bytes));
        if (p.vm_hwm_kb >= 0)
        {
            print_field(std::cout, "Peak RSS (VmHWM)", human_kb(p.vm_hwm_kb));
        }
        if (p.rss_anon_kb >= 0)
        {
            print_field(std::cout, "  anon (heap/stack)", human_kb(p.rss_anon_kb));
            print_field(std::cout, "  file (mapped files)", human_kb(p.rss_file_kb));
            print_field(std::cout, "  shmem", human_kb(p.rss_shmem_kb));
        }
        if (p.vm_data_kb >= 0)
        {
            print_field(std::cout, "VmData / VmStk",
                        human_kb(p.vm_data_kb) + " / " + human_kb(p.vm_stk_kb));
            print_field(std::cout, "VmExe / VmLib",
                        human_kb(p.vm_exe_kb) + " / " + human_kb(p.vm_lib_kb));
        }
        if (p.vm_swap_kb >= 0)
        {
            print_field(std::cout, "Swap", human_kb(p.vm_swap_kb));
        }

        print_section(std::cout, "Page faults");
        print_field(std::cout, "Minor (served from RAM)", std::to_string(p.minor_faults));
        print_field(std::cout, "Major (needed disk I/O)", std::to_string(p.major_faults));

        const auto regions = read_maps(pid);
        if (!regions.empty())
        {
            unsigned long long anon = 0;
            unsigned long long file_backed = 0;
            unsigned long long exec = 0;
            for (const auto &r : regions)
            {
                if (r.is_anonymous() || r.is_special())
                {
                    anon += r.size();
                }
                else
                {
                    file_backed += r.size();
                }
                if (r.perms.size() > 2 && r.perms[2] == 'x')
                {
                    exec += r.size();
                }
            }
            print_section(std::cout, "Address space summary");
            print_field(std::cout, "Regions", std::to_string(regions.size()));
            print_field(std::cout, "Anonymous + special",
                        human_bytes(static_cast<long long>(anon)));
            print_field(std::cout, "File-backed", human_bytes(static_cast<long long>(file_backed)));
            print_field(std::cout, "Executable", human_bytes(static_cast<long long>(exec)));
            std::cout << "\n  For detail: procmon maps " << pid << '\n';
        }

        return 0;
    }

    int cmd_threads(int pid)
    {
        const auto threads = read_threads(pid);
        if (threads.empty())
        {
            std::cerr << "Cannot read threads for PID " << pid << ".\n";
            return 1;
        }

        Table table({"TID", "S", "CPU", "CPU#", "PRI", "NI", "VOL-CTX", "INVOL-CTX", "NAME"});
        for (size_t i = 0; i < 8; ++i)
        {
            table.right_align(i);
        }

        for (const auto &t : threads)
        {
            table.add_row(
                {std::to_string(t.tid), std::string(1, t.state), human_duration(t.cpu_seconds()),
                 std::to_string(t.last_cpu), std::to_string(t.priority), std::to_string(t.nice),
                 std::to_string(t.voluntary_ctxt), std::to_string(t.nonvoluntary_ctxt), t.comm});
        }
        table.print(std::cout);
        std::cout << "\n"
                  << threads.size() << " threads, each under /proc/" << pid << "/task/<tid>.\n"
                  << "  Names are truncated because the kernel caps comm at 15 characters.\n";
        return 0;
    }

    int cmd_maps(int pid)
    {
        const auto regions = read_maps(pid);
        if (regions.empty())
        {
            std::cerr << "Cannot read maps for PID " << pid << ".\n";
            return 1;
        }

        Table table({"START", "END", "PERMS", "SIZE", "OFFSET", "PATH"});
        table.right_align(3);

        for (const auto &r : regions)
        {
            std::string path = r.path;
            if (path.empty())
            {
                path = "[anonymous]";
            }
            table.add_row({hex_address(r.start), hex_address(r.end), r.perms,
                           human_bytes(static_cast<long long>(r.size())), hex_address(r.offset),
                           path});
        }
        table.print(std::cout);

        // Group regions by backing file to show what dominates the address space.
        std::map<std::string, unsigned long long> by_path;
        unsigned long long total = 0;
        for (const auto &r : regions)
        {
            by_path[r.path.empty() ? "[anonymous]" : r.path] += r.size();
            total += r.size();
        }

        std::vector<std::pair<std::string, unsigned long long>> sorted(by_path.begin(),
                                                                       by_path.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto &a, const auto &b) { return a.second > b.second; });

        print_section(std::cout, "Virtual size by backing file");
        for (size_t i = 0; i < sorted.size() && i < 10; ++i)
        {
            print_field(std::cout, human_bytes(static_cast<long long>(sorted[i].second)),
                        sorted[i].first);
        }
        std::cout << "\n  Total VSZ: " << human_bytes(static_cast<long long>(total)) << " ("
                  << regions.size() << " regions)\n";
        std::cout << "  perms: r=read w=write x=execute p=private(COW) s=shared\n";
        return 0;
    }

    int cmd_mem()
    {
        const auto mem = read_meminfo();
        if (!mem)
        {
            std::cerr << "Cannot read /proc/meminfo.\n";
            return 1;
        }
        const auto &m = *mem;

        print_section(std::cout, "System memory");
        print_field(std::cout, "Total", human_kb(m.mem_total));
        print_field(std::cout, "Free", human_kb(m.mem_free));
        print_field(std::cout, "Available", human_kb(m.mem_available));

        print_section(std::cout, "Page cache");
        print_field(std::cout, "Cached (file contents)", human_kb(m.cached));
        print_field(std::cout, "Buffers (block metadata)", human_kb(m.buffers));
        print_field(std::cout, "Mapped (mmap'ed)", human_kb(m.mapped));
        print_field(std::cout, "Shmem", human_kb(m.shmem));
        print_field(std::cout, "Dirty (awaiting writeback)", human_kb(m.dirty));
        print_field(std::cout, "Writeback (in flight)", human_kb(m.writeback));

        print_section(std::cout, "Anonymous memory and swap");
        print_field(std::cout, "AnonPages (heap/stack)", human_kb(m.anon_pages));
        print_field(std::cout, "Swap total / free",
                    human_kb(m.swap_total) + " / " + human_kb(m.swap_free));
        print_field(std::cout, "SwapCached", human_kb(m.swap_cached));

        print_section(std::cout, "Kernel");
        print_field(std::cout, "Slab", human_kb(m.slab));
        print_field(std::cout, "Active / Inactive",
                    human_kb(m.active) + " / " + human_kb(m.inactive));

        if (const auto la = read_loadavg())
        {
            print_section(std::cout, "Load");
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.2f  %.2f  %.2f", la->one, la->five, la->fifteen);
            print_field(std::cout, "Load average (1/5/15)", buf);
            print_field(std::cout, "Runnable / total",
                        std::to_string(la->runnable) + " / " + std::to_string(la->total));
        }
        if (const auto up = read_uptime_seconds())
        {
            print_field(std::cout, "Uptime", human_duration(*up));
        }

        std::cout << "\n  Note: \"Cached\" is disk content the kernel keeps in RAM. It does not\n"
                  << "  count as free, but it is reclaimed under memory pressure - which is why\n"
                  << "  MemAvailable, not MemFree, is the number that matters.\n";
        return 0;
    }

} // namespace

int main(int argc, char **argv)
{
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "-h" || args[0] == "--help")
    {
        usage();
        return args.empty() ? 2 : 0;
    }

    const std::string &command = args[0];

    if (command == "list")
    {
        return cmd_list({args.begin() + 1, args.end()});
    }
    if (command == "mem")
    {
        return cmd_mem();
    }

    if (command == "show" || command == "threads" || command == "maps")
    {
        if (args.size() < 2)
        {
            std::cerr << command << " requires a pid.\n";
            return 2;
        }
        const int pid = resolve_pid(args[1]);
        if (pid < 0)
        {
            std::cerr << "Invalid pid: " << args[1] << '\n';
            return 2;
        }
        if (command == "show")
            return cmd_show(pid);
        if (command == "threads")
            return cmd_threads(pid);
        return cmd_maps(pid);
    }

    std::cerr << "Unknown command: " << command << "\n\n";
    usage();
    return 2;
}
