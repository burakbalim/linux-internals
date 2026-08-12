#include "mc/cgroup.hpp"

#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "mc/util.hpp"

namespace mc
{
    namespace
    {

        constexpr const char *kRoot = "/sys/fs/cgroup";
        constexpr const char *kParent = "/sys/fs/cgroup/minicontainer";
        constexpr unsigned int kCgroup2Magic = 0x63677270;

        // cgroup v2 enforces a "no internal processes" rule: a cgroup may hold processes, or
        // delegate controllers to children, but not both. Only the true system root is exempt.
        //
        // The root we see here is a cgroup namespace root, but still a regular cgroup on the
        // host, so the rule applies - and it already contains the shell we were started from.
        // Enabling controllers therefore fails with EBUSY until those processes are moved into
        // a leaf cgroup. systemd and every container runtime perform this same dance.
        bool vacate_root(std::string *error)
        {
            const auto procs = read_file(std::string(kRoot) + "/cgroup.procs");
            if (!procs || procs->empty())
            {
                return true;
            }

            const std::string leaf = std::string(kRoot) + "/init";
            if (::mkdir(leaf.c_str(), 0755) != 0 && errno != EEXIST)
            {
                if (error)
                    *error = errno_message("mkdir " + leaf);
                return false;
            }

            // cgroup.procs accepts exactly one pid per write.
            size_t start = 0;
            while (start < procs->size())
            {
                size_t end = procs->find('\n', start);
                if (end == std::string::npos)
                {
                    end = procs->size();
                }
                const std::string pid = procs->substr(start, end - start);
                if (!pid.empty())
                {
                    // A process may exit between reading the list and moving it; that is fine.
                    write_file(leaf + "/cgroup.procs", pid, nullptr);
                }
                start = end + 1;
            }
            return true;
        }

        // Enabling a controller for children means writing "+memory" to the parent's
        // cgroup.subtree_control. Without this the child directory simply has no memory.max
        // file, which reads as "cgroups do not work here" if you do not know to look.
        bool enable_controllers(const std::string &dir, std::string *error)
        {
            const auto available = read_file(dir + "/cgroup.controllers");
            if (!available)
            {
                if (error)
                    *error = "cannot read " + dir + "/cgroup.controllers";
                return false;
            }

            std::string enable;
            if (available->find("memory") != std::string::npos)
            {
                enable += "+memory ";
            }
            if (available->find("pids") != std::string::npos)
            {
                enable += "+pids";
            }
            if (enable.empty())
            {
                if (error)
                    *error = "neither memory nor pids controller is available in " + dir;
                return false;
            }

            std::string write_error;
            if (!write_file(dir + "/cgroup.subtree_control", enable, &write_error))
            {
                // EBUSY here usually means this cgroup still holds processes: cgroup v2 forbids
                // a cgroup from having both member processes and controller-enabled children.
                if (error)
                {
                    *error = write_error;
                    if (errno == EBUSY)
                    {
                        *error += "\n  (cgroup v2 forbids enabling controllers while this cgroup "
                                  "still has processes in it; try --cgroupns=private)";
                    }
                }
                return false;
            }
            return true;
        }

    } // namespace

    bool cgroup_v2_available()
    {
        struct statfs fs
        {
        };
        if (::statfs(kRoot, &fs) != 0)
        {
            return false;
        }
        return static_cast<unsigned int>(fs.f_type) == kCgroup2Magic;
    }

    Cgroup::~Cgroup()
    {
        if (!path_.empty())
        {
            // Only succeeds once every process has left; that is the intended behaviour.
            ::rmdir(path_.c_str());
        }
    }

    bool Cgroup::create(const std::string &name, std::string *error)
    {
        if (!cgroup_v2_available())
        {
            if (error)
            {
                *error = std::string(kRoot) +
                         " is not a cgroup2 filesystem (this build only supports cgroup v2)";
            }
            return false;
        }

        if (!vacate_root(error))
        {
            return false;
        }
        if (!enable_controllers(kRoot, error))
        {
            return false;
        }

        if (::mkdir(kParent, 0755) != 0 && errno != EEXIST)
        {
            if (error)
                *error = errno_message(std::string("mkdir ") + kParent);
            return false;
        }
        if (!enable_controllers(kParent, error))
        {
            return false;
        }

        const std::string dir = std::string(kParent) + "/" + name;
        if (::mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST)
        {
            if (error)
                *error = errno_message("mkdir " + dir);
            return false;
        }

        path_ = dir;
        return true;
    }

    bool Cgroup::set_memory_limit(long long bytes, std::string *error)
    {
        if (!write_file(path_ + "/memory.max", std::to_string(bytes), error))
        {
            return false;
        }

        // memory.max caps RAM only. Left alone, a process that exceeds it simply has its
        // pages pushed to swap and keeps allocating - measured here as 3.9 GB allocated
        // under a 64 MB limit, bounded by the size of swap rather than by the limit.
        // Capping memory.swap.max is what makes the limit mean what it appears to mean.
        // Not fatal if absent: kernels built without swap accounting have no such file.
        write_file(path_ + "/memory.swap.max", "0", nullptr);
        return true;
    }

    bool Cgroup::set_pids_limit(long long count, std::string *error)
    {
        return write_file(path_ + "/pids.max", std::to_string(count), error);
    }

    bool Cgroup::add_process(int pid, std::string *error)
    {
        return write_file(path_ + "/cgroup.procs", std::to_string(pid), error);
    }

    long long Cgroup::oom_kill_count() const
    {
        const auto events = read_file(path_ + "/memory.events");
        if (!events)
        {
            return -1;
        }
        // memory.events is "key value" lines; we want oom_kill.
        const size_t pos = events->find("oom_kill ");
        if (pos == std::string::npos)
        {
            return -1;
        }
        return std::strtoll(events->c_str() + pos + 9, nullptr, 10);
    }

    long long Cgroup::current_memory() const
    {
        const auto current = read_file(path_ + "/memory.current");
        if (!current)
        {
            return -1;
        }
        return std::strtoll(current->c_str(), nullptr, 10);
    }

} // namespace mc
