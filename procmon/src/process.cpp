#include "proc/process.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

#include "proc/util.hpp"

namespace proc {
namespace {

// /proc/<pid>/stat is a single line whose comm field is wrapped in parentheses.
// comm may contain spaces and ')', so we anchor on the LAST ')'; the first field
// after it is state, which is stat field 3.
struct StatFields {
    std::string comm;
    std::vector<std::string_view> after_comm;
    std::string buffer;  // after_comm points into this buffer
};

std::optional<StatFields> parse_stat(const std::string& path) {
    auto content = read_file(path);
    if (!content) {
        return std::nullopt;
    }

    const size_t open_paren = content->find('(');
    const size_t close_paren = content->rfind(')');
    if (open_paren == std::string::npos || close_paren == std::string::npos ||
        close_paren < open_paren) {
        return std::nullopt;
    }

    StatFields fields;
    fields.comm = content->substr(open_paren + 1, close_paren - open_paren - 1);
    fields.buffer = content->substr(close_paren + 1);
    fields.after_comm = split_ws(fields.buffer);
    return fields;
}

// Lets callers keep using the 1-based field numbers from the stat documentation.
long long stat_field(const StatFields& f, size_t one_based_index) {
    const size_t i = one_based_index - 3;  // field 3 (state) -> after_comm[0]
    if (i >= f.after_comm.size()) {
        return 0;
    }
    return to_ll(f.after_comm[i]).value_or(0);
}

long long kv_number(const std::unordered_map<std::string, std::string>& kv,
                    const std::string& key) {
    const auto it = kv.find(key);
    if (it == kv.end()) {
        return -1;
    }
    return to_ll(it->second).value_or(-1);
}

}  // namespace

double ProcessInfo::cpu_seconds() const {
    return static_cast<double>(utime_ticks + stime_ticks) /
           static_cast<double>(clock_ticks_per_sec());
}

double ThreadInfo::cpu_seconds() const {
    return static_cast<double>(utime_ticks + stime_ticks) /
           static_cast<double>(clock_ticks_per_sec());
}

std::vector<int> list_pids() {
    std::vector<int> pids;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/proc", ec)) {
        const std::string name = entry.path().filename().string();
        if (!is_number(name)) {
            continue;  // non-numeric entries under /proc are not processes
        }
        pids.push_back(std::stoi(name));
    }
    std::sort(pids.begin(), pids.end());
    return pids;
}

std::optional<ProcessInfo> read_process(int pid) {
    const std::string base = "/proc/" + std::to_string(pid);

    const auto fields = parse_stat(base + "/stat");
    if (!fields) {
        return std::nullopt;
    }

    ProcessInfo p;
    p.pid = pid;
    p.comm = fields->comm;
    if (!fields->after_comm.empty() && !fields->after_comm[0].empty()) {
        p.state = fields->after_comm[0][0];
    }

    p.ppid = static_cast<int>(stat_field(*fields, 4));
    p.minor_faults = stat_field(*fields, 10);
    p.major_faults = stat_field(*fields, 12);
    p.utime_ticks = stat_field(*fields, 14);
    p.stime_ticks = stat_field(*fields, 15);
    p.priority = static_cast<long>(stat_field(*fields, 18));
    p.nice = static_cast<long>(stat_field(*fields, 19));
    p.num_threads = static_cast<long>(stat_field(*fields, 20));
    p.start_time_ticks = stat_field(*fields, 22);
    p.vsize_bytes = stat_field(*fields, 23);
    p.rss_bytes = stat_field(*fields, 24) * page_size();  // stat reports RSS in pages
    p.last_cpu = static_cast<int>(stat_field(*fields, 39));
    p.policy = static_cast<long>(stat_field(*fields, 41));

    const auto status = parse_kv(base + "/status");
    p.rss_anon_kb = kv_number(status, "RssAnon");
    p.rss_file_kb = kv_number(status, "RssFile");
    p.rss_shmem_kb = kv_number(status, "RssShmem");
    p.vm_data_kb = kv_number(status, "VmData");
    p.vm_stk_kb = kv_number(status, "VmStk");
    p.vm_exe_kb = kv_number(status, "VmExe");
    p.vm_lib_kb = kv_number(status, "VmLib");
    p.vm_swap_kb = kv_number(status, "VmSwap");
    p.vm_hwm_kb = kv_number(status, "VmHWM");
    p.voluntary_ctxt = kv_number(status, "voluntary_ctxt_switches");
    p.nonvoluntary_ctxt = kv_number(status, "nonvoluntary_ctxt_switches");

    // cmdline arguments are NUL-separated.
    if (auto cmdline = read_file(base + "/cmdline"); cmdline && !cmdline->empty()) {
        std::replace(cmdline->begin(), cmdline->end(), '\0', ' ');
        p.cmdline = std::string(trim(*cmdline));
    }

    return p;
}

std::vector<MemoryRegion> read_maps(int pid) {
    std::vector<MemoryRegion> regions;
    for (const auto& line : read_lines("/proc/" + std::to_string(pid) + "/maps")) {
        if (line.empty()) {
            continue;
        }
        // Format: start-end perms offset dev inode [pathname]
        const auto tokens = split_ws(line);
        if (tokens.size() < 5) {
            continue;
        }

        MemoryRegion r;
        const std::string range(tokens[0]);
        const size_t dash = range.find('-');
        if (dash == std::string::npos) {
            continue;
        }
        r.start = std::strtoull(range.substr(0, dash).c_str(), nullptr, 16);
        r.end = std::strtoull(range.substr(dash + 1).c_str(), nullptr, 16);
        r.perms = std::string(tokens[1]);
        r.offset = std::strtoull(std::string(tokens[2]).c_str(), nullptr, 16);
        r.dev = std::string(tokens[3]);
        r.inode = std::strtoull(std::string(tokens[4]).c_str(), nullptr, 10);

        // A path may contain spaces, so take everything after the 5th token.
        const size_t path_start =
            static_cast<size_t>(tokens[4].data() + tokens[4].size() - line.data());
        if (path_start < line.size()) {
            r.path = std::string(trim(std::string_view(line).substr(path_start)));
        }

        regions.push_back(std::move(r));
    }
    return regions;
}

std::vector<ThreadInfo> read_threads(int pid) {
    std::vector<ThreadInfo> threads;
    const std::string task_dir = "/proc/" + std::to_string(pid) + "/task";

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(task_dir, ec)) {
        const std::string name = entry.path().filename().string();
        if (!is_number(name)) {
            continue;
        }

        const auto fields = parse_stat(entry.path().string() + "/stat");
        if (!fields) {
            continue;  // the thread may have exited while we were reading
        }

        ThreadInfo t;
        t.tid = std::stoi(name);
        t.comm = fields->comm;
        if (!fields->after_comm.empty() && !fields->after_comm[0].empty()) {
            t.state = fields->after_comm[0][0];
        }
        t.utime_ticks = stat_field(*fields, 14);
        t.stime_ticks = stat_field(*fields, 15);
        t.priority = static_cast<long>(stat_field(*fields, 18));
        t.nice = static_cast<long>(stat_field(*fields, 19));
        t.last_cpu = static_cast<int>(stat_field(*fields, 39));

        const auto status = parse_kv(entry.path().string() + "/status");
        t.voluntary_ctxt = kv_number(status, "voluntary_ctxt_switches");
        t.nonvoluntary_ctxt = kv_number(status, "nonvoluntary_ctxt_switches");

        threads.push_back(std::move(t));
    }

    std::sort(threads.begin(), threads.end(),
              [](const ThreadInfo& a, const ThreadInfo& b) { return a.tid < b.tid; });
    return threads;
}

std::string state_name(char state) {
    switch (state) {
        case 'R': return "Running (on CPU or runnable)";
        case 'S': return "Sleeping (interruptible)";
        case 'D': return "Disk sleep (uninterruptible, usually I/O)";
        case 'Z': return "Zombie (exited, not yet reaped)";
        case 'T': return "Stopped (SIGSTOP)";
        case 't': return "Tracing stop (debugger)";
        case 'X': return "Dead";
        case 'I': return "Idle (idle kernel thread)";
        default:  return "Unknown";
    }
}

std::string policy_name(long policy) {
    switch (policy) {
        case 0: return "SCHED_OTHER (CFS)";
        case 1: return "SCHED_FIFO (real-time)";
        case 2: return "SCHED_RR (real-time)";
        case 3: return "SCHED_BATCH";
        case 5: return "SCHED_IDLE";
        case 6: return "SCHED_DEADLINE";
        default: return "Unknown(" + std::to_string(policy) + ")";
    }
}

}  // namespace proc
