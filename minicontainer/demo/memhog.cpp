// Allocates memory in chunks until it is killed, so a cgroup memory limit can be
// seen doing its job.
//
// Pages are written to, not just allocated: malloc only reserves address space, and
// an unwritten page never becomes resident, so a limit would never be reached.

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

int main(int argc, char** argv) {
    const size_t chunk_mb = (argc > 1) ? std::strtoul(argv[1], nullptr, 10) : 10;
    const size_t chunk = chunk_mb * 1024 * 1024;

    std::vector<char*> blocks;
    size_t total = 0;

    for (;;) {
        char* block = static_cast<char*>(std::malloc(chunk));
        if (block == nullptr) {
            std::printf("malloc failed after %zu MB\n", total / (1024 * 1024));
            return 1;
        }
        std::memset(block, 1, chunk);  // touch every page so it becomes resident
        blocks.push_back(block);
        total += chunk;

        std::printf("allocated %4zu MB\n", total / (1024 * 1024));
        std::fflush(stdout);
    }
}
