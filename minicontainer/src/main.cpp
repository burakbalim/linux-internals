#include <unistd.h>

#include <iostream>
#include <string>
#include <vector>

#include "mc/cgroup.hpp"
#include "mc/container.hpp"
#include "mc/util.hpp"

namespace
{

    void usage()
    {
        std::cout << R"(minicontainer - what Docker does, reduced to the parts that matter

Usage:
  minicontainer run [options] -- COMMAND [ARGS...]
  minicontainer ns [pid]        Show the namespace ids of a process (default: self)

Options for run:
  --rootfs=DIR      pivot_root into DIR (see scripts/make-rootfs.sh)
  --hostname=NAME   hostname inside the container      (default: container)
  --memory=SIZE     memory limit, e.g. 64M             (cgroup v2)
  --pids=N          maximum number of processes        (cgroup v2)
  --share-net       keep the host network namespace    (default: isolated)

Requires root and a writable cgroup2 filesystem:
  docker run --rm -it --privileged --cgroupns=private ...
)";
    }

    int cmd_ns(const std::string &pid)
    {
        mc::print_namespaces(pid, "namespaces of pid " + pid);
        std::cout << "\n  Two processes share a namespace exactly when these ids match.\n"
                  << "  Compare this against a container to see which ones were replaced.\n";
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

    if (args[0] == "ns")
    {
        return cmd_ns(args.size() > 1 ? args[1] : "self");
    }

    if (args[0] != "run")
    {
        std::cerr << "Unknown command: " << args[0] << "\n\n";
        usage();
        return 2;
    }

    mc::ContainerConfig cfg;
    size_t i = 1;
    for (; i < args.size(); ++i)
    {
        const std::string &arg = args[i];
        if (arg == "--")
        {
            ++i;
            break;
        }
        if (arg.rfind("--rootfs=", 0) == 0)
        {
            cfg.rootfs = arg.substr(9);
        }
        else if (arg.rfind("--hostname=", 0) == 0)
        {
            cfg.hostname = arg.substr(11);
        }
        else if (arg.rfind("--memory=", 0) == 0)
        {
            const auto bytes = mc::parse_size(arg.substr(9));
            if (!bytes)
            {
                std::cerr << "Invalid size: " << arg.substr(9) << '\n';
                return 2;
            }
            cfg.memory_limit = *bytes;
        }
        else if (arg.rfind("--pids=", 0) == 0)
        {
            cfg.pids_limit = std::stoll(arg.substr(7));
        }
        else if (arg == "--share-net")
        {
            cfg.isolate_network = false;
        }
        else
        {
            std::cerr << "Unknown option: " << arg << "\n\n";
            usage();
            return 2;
        }
    }

    for (; i < args.size(); ++i)
    {
        cfg.command.push_back(args[i]);
    }
    if (cfg.command.empty())
    {
        std::cerr << "No command given. Use -- to separate it from the options.\n";
        return 2;
    }

    if (::geteuid() != 0)
    {
        std::cerr << "minicontainer: needs root to create namespaces and cgroups.\n";
        return 1;
    }

    std::cout << "minicontainer\n";
    mc::print_namespaces("self", "host namespaces");
    std::cout << '\n';
    if (cfg.memory_limit > 0)
    {
        std::cout << "  memory limit            " << mc::human_bytes(cfg.memory_limit) << '\n';
    }
    if (cfg.pids_limit > 0)
    {
        std::cout << "  pids limit              " << cfg.pids_limit << '\n';
    }

    std::string error;
    const int status = mc::run_container(cfg, &error);
    if (status < 0)
    {
        std::cerr << "minicontainer: " << error << '\n';
        return 1;
    }
    return status;
}
