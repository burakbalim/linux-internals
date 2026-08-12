#pragma once

#include <string>

namespace mc {

// A cgroup v2 directory, removed on destruction.
//
// cgroup v2 is a single unified hierarchy under /sys/fs/cgroup, unlike v1 where every
// controller had its own tree. Limits are set by writing plain text to files:
//   memory.max   hard memory limit; exceeding it triggers the OOM killer for this cgroup
//   pids.max     maximum number of tasks, the defence against fork bombs
//   cgroup.procs write a pid here to move that process (and its future children) in
//
// A controller is only usable in a child cgroup if the parent lists it in
// cgroup.subtree_control - that delegation step is the part most people miss.
class Cgroup {
public:
    Cgroup() = default;
    ~Cgroup();

    Cgroup(const Cgroup&) = delete;
    Cgroup& operator=(const Cgroup&) = delete;

    // Creates /sys/fs/cgroup/<parent>/<name>, enabling controllers along the way.
    bool create(const std::string& name, std::string* error);

    bool set_memory_limit(long long bytes, std::string* error);
    bool set_pids_limit(long long count, std::string* error);

    // Moves a process into this cgroup. Its children are placed here automatically.
    bool add_process(int pid, std::string* error);

    // Number of times this cgroup hit its memory limit hard enough to be OOM-killed.
    long long oom_kill_count() const;
    long long current_memory() const;

    const std::string& path() const { return path_; }
    bool valid() const { return !path_.empty(); }

private:
    std::string path_;
};

// True when /sys/fs/cgroup is a cgroup2 filesystem.
bool cgroup_v2_available();

}  // namespace mc
