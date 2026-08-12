#pragma once

#include <string>
#include <vector>

namespace mc {

struct ContainerConfig {
    std::string rootfs;                 // empty = keep the host filesystem
    std::string hostname = "container";
    long long memory_limit = -1;        // bytes, -1 = unlimited
    long long pids_limit = -1;
    bool isolate_network = true;
    std::vector<std::string> command;
};

// Runs the command inside fresh namespaces and returns its exit status.
// Returns -1 if the container could not be started.
int run_container(const ContainerConfig& cfg, std::string* error);

// Prints the namespace ids of a process, so host and container can be compared.
void print_namespaces(const std::string& pid, const std::string& label);

}  // namespace mc
