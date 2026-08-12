// g++ defines _GNU_SOURCE itself, which is what makes clone(2) and MNT_DETACH visible.
#include "mc/container.hpp"

#include <fcntl.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

#include "mc/cgroup.hpp"
#include "mc/util.hpp"

namespace mc
{
    namespace
    {

        // glibc has no wrapper for pivot_root, so it is called through syscall(2) directly.
        int pivot_root(const char *new_root, const char *put_old)
        {
            return static_cast<int>(::syscall(SYS_pivot_root, new_root, put_old));
        }

        struct ChildArgs
        {
            const ContainerConfig *cfg;
            int ready_read_fd;
            int ready_write_fd; // the child's own copy, which it must close to ever see EOF
        };

        bool setup_rootfs(const std::string &rootfs, std::string *error)
        {
            // Every mount below must stay inside the container. Without this the new mounts
            // propagate back to the host, and pivot_root refuses to run at all.
            if (::mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) != 0)
            {
                if (error)
                    *error = errno_message("mount(/, MS_REC|MS_PRIVATE)");
                return false;
            }

            // pivot_root requires the new root to be a mount point, so bind it onto itself.
            if (::mount(rootfs.c_str(), rootfs.c_str(), nullptr, MS_BIND | MS_REC, nullptr) != 0)
            {
                if (error)
                    *error = errno_message("bind mount " + rootfs);
                return false;
            }

            const std::string put_old = rootfs + "/old_root";
            if (::mkdir(put_old.c_str(), 0700) != 0 && errno != EEXIST)
            {
                if (error)
                    *error = errno_message("mkdir " + put_old);
                return false;
            }

            if (pivot_root(rootfs.c_str(), put_old.c_str()) != 0)
            {
                if (error)
                    *error = errno_message("pivot_root");
                return false;
            }

            if (::chdir("/") != 0)
            {
                if (error)
                    *error = errno_message("chdir(/)");
                return false;
            }

            // Detach the old root. Until this happens the host filesystem is still reachable
            // from inside the container, which would make the whole exercise pointless.
            if (::umount2("/old_root", MNT_DETACH) != 0)
            {
                if (error)
                    *error = errno_message("umount2(/old_root)");
                return false;
            }
            ::rmdir("/old_root");
            return true;
        }

        bool mount_proc(std::string *error)
        {
            if (::mkdir("/proc", 0555) != 0 && errno != EEXIST)
            {
                if (error)
                    *error = errno_message("mkdir /proc");
                return false;
            }
            // A fresh procfs instance. Because we are in a new PID namespace, this /proc
            // shows only the processes of that namespace - the same filtering procmon saw.
            if (::mount("proc", "/proc", "proc", 0, nullptr) != 0)
            {
                if (error)
                    *error = errno_message("mount /proc");
                return false;
            }
            return true;
        }

        int child_main(void *raw)
        {
            auto *args = static_cast<ChildArgs *>(raw);
            const ContainerConfig &cfg = *args->cfg;

            // clone() without CLONE_VM copies the descriptor table, so this process holds its
            // own copy of BOTH pipe ends. EOF only arrives once every write end is closed, so
            // failing to close ours here deadlocks the child forever - the parent closing its
            // end is not enough.
            ::close(args->ready_write_fd);

            // Block until the parent has put us in the cgroup. The read returns 0 (EOF) when
            // the parent closes its write end; the pipe is a barrier, no data is ever sent.
            char byte;
            while (::read(args->ready_read_fd, &byte, 1) < 0 && errno == EINTR)
            {
            }
            ::close(args->ready_read_fd);

            std::string error;

            if (::sethostname(cfg.hostname.c_str(), cfg.hostname.size()) != 0)
            {
                std::cerr << "minicontainer: " << errno_message("sethostname") << '\n';
                return 1;
            }

            if (!cfg.rootfs.empty())
            {
                if (!setup_rootfs(cfg.rootfs, &error))
                {
                    std::cerr << "minicontainer: " << error << '\n';
                    return 1;
                }
            }

            if (!mount_proc(&error))
            {
                std::cerr << "minicontainer: " << error << '\n';
                return 1;
            }

            std::vector<char *> argv;
            argv.reserve(cfg.command.size() + 1);
            for (const auto &arg : cfg.command)
            {
                argv.push_back(const_cast<char *>(arg.c_str()));
            }
            argv.push_back(nullptr);

            ::execvp(argv[0], argv.data());

            // Only reached when exec failed.
            std::cerr << "minicontainer: " << errno_message(std::string("exec ") + argv[0]) << '\n';
            return 127;
        }

    } // namespace

    void print_namespaces(const std::string &pid, const std::string &label)
    {
        static const char *kinds[] = {"pid", "mnt", "net", "uts", "ipc", "cgroup", "user"};
        std::cout << "  " << label << '\n';
        for (const char *kind : kinds)
        {
            const auto id = namespace_id(pid, kind);
            std::printf("    %-7s %s\n", kind, id ? id->c_str() : "(unavailable)");
        }
    }

    int run_container(const ContainerConfig &cfg, std::string *error)
    {
        int ready_pipe[2];
        if (::pipe(ready_pipe) != 0)
        {
            if (error)
                *error = errno_message("pipe");
            return -1;
        }

        // CLONE_NEWPID   the child becomes PID 1 of a new process-id namespace
        // CLONE_NEWNS    a private mount table, so pivot_root does not touch the host
        // CLONE_NEWUTS   its own hostname
        // CLONE_NEWIPC   separate System V IPC and POSIX message queues
        // CLONE_NEWNET   an empty network stack: only a down loopback interface
        int flags = CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | SIGCHLD;
        if (cfg.isolate_network)
        {
            flags |= CLONE_NEWNET;
        }

        // clone() needs a stack for the child; it grows down, so we hand over the top.
        static std::vector<char> stack(1024 * 1024);
        ChildArgs args{&cfg, ready_pipe[0], ready_pipe[1]};

        const pid_t pid = ::clone(child_main, stack.data() + stack.size(), flags, &args);
        if (pid < 0)
        {
            if (error)
                *error = errno_message("clone");
            return -1;
        }
        ::close(ready_pipe[0]);

        Cgroup cgroup;
        if (cfg.memory_limit > 0 || cfg.pids_limit > 0)
        {
            std::string cgroup_error;
            if (!cgroup.create("mc-" + std::to_string(pid), &cgroup_error))
            {
                std::cerr << "minicontainer: cgroup setup failed: " << cgroup_error << '\n';
            }
            else
            {
                if (cfg.memory_limit > 0 &&
                    !cgroup.set_memory_limit(cfg.memory_limit, &cgroup_error))
                {
                    std::cerr << "minicontainer: " << cgroup_error << '\n';
                }
                if (cfg.pids_limit > 0 && !cgroup.set_pids_limit(cfg.pids_limit, &cgroup_error))
                {
                    std::cerr << "minicontainer: " << cgroup_error << '\n';
                }
                if (!cgroup.add_process(pid, &cgroup_error))
                {
                    std::cerr << "minicontainer: " << cgroup_error << '\n';
                }
            }
        }

        std::cout << "  container pid on host   " << pid << '\n';
        print_namespaces(std::to_string(pid), "container namespaces");
        std::cout << '\n';

        // Releasing the barrier: the child now runs with its limits already in place.
        ::close(ready_pipe[1]);

        int status = 0;
        while (::waitpid(pid, &status, 0) < 0)
        {
            if (errno != EINTR)
            {
                if (error)
                    *error = errno_message("waitpid");
                return -1;
            }
        }

        if (cgroup.valid())
        {
            const long long oom = cgroup.oom_kill_count();
            if (oom > 0)
            {
                std::cout << "\n  cgroup OOM kills: " << oom
                          << "  (the memory limit was enforced)\n";
            }
        }

        if (WIFEXITED(status))
        {
            return WEXITSTATUS(status);
        }
        if (WIFSIGNALED(status))
        {
            const int sig = WTERMSIG(status);
            std::cout << "  container killed by signal " << sig << " (" << ::strsignal(sig)
                      << ")\n";
            return 128 + sig;
        }
        return -1;
    }

} // namespace mc
