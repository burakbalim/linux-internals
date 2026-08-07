#include "proc/system.hpp"

#include <cstdlib>
#include <string>
#include <unordered_map>

#include "proc/util.hpp"

namespace proc {
namespace {

long long field(const std::unordered_map<std::string, std::string>& kv, const std::string& key) {
    const auto it = kv.find(key);
    if (it == kv.end()) {
        return -1;
    }
    return to_ll(it->second).value_or(-1);  // "16384 kB" -> 16384
}

}  // namespace

std::optional<MemInfo> read_meminfo() {
    const auto kv = parse_kv("/proc/meminfo");
    if (kv.empty()) {
        return std::nullopt;
    }

    MemInfo m;
    m.mem_total = field(kv, "MemTotal");
    m.mem_free = field(kv, "MemFree");
    m.mem_available = field(kv, "MemAvailable");
    m.buffers = field(kv, "Buffers");
    m.cached = field(kv, "Cached");
    m.swap_cached = field(kv, "SwapCached");
    m.active = field(kv, "Active");
    m.inactive = field(kv, "Inactive");
    m.dirty = field(kv, "Dirty");
    m.writeback = field(kv, "Writeback");
    m.anon_pages = field(kv, "AnonPages");
    m.mapped = field(kv, "Mapped");
    m.shmem = field(kv, "Shmem");
    m.slab = field(kv, "Slab");
    m.swap_total = field(kv, "SwapTotal");
    m.swap_free = field(kv, "SwapFree");
    return m;
}

std::optional<LoadAvg> read_loadavg() {
    // Format: 0.12 0.20 0.18 1/234 5678
    const auto content = read_file("/proc/loadavg");
    if (!content) {
        return std::nullopt;
    }

    const auto tokens = split_ws(*content);
    if (tokens.size() < 4) {
        return std::nullopt;
    }

    LoadAvg la;
    la.one = std::strtod(std::string(tokens[0]).c_str(), nullptr);
    la.five = std::strtod(std::string(tokens[1]).c_str(), nullptr);
    la.fifteen = std::strtod(std::string(tokens[2]).c_str(), nullptr);

    const std::string ratio(tokens[3]);
    const size_t slash = ratio.find('/');
    if (slash != std::string::npos) {
        la.runnable = std::atoi(ratio.substr(0, slash).c_str());
        la.total = std::atoi(ratio.substr(slash + 1).c_str());
    }
    return la;
}

std::optional<double> read_uptime_seconds() {
    const auto content = read_file("/proc/uptime");
    if (!content) {
        return std::nullopt;
    }
    const auto tokens = split_ws(*content);
    if (tokens.empty()) {
        return std::nullopt;
    }
    return std::strtod(std::string(tokens[0]).c_str(), nullptr);
}

}  // namespace proc
