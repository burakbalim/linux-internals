// mmap demo: maps a file into memory and then waits, so that
//   procmon maps <pid>
// can be run from another shell to see how the region appears in /proc/<pid>/maps.
//
// What to watch for:
//   - Creating the mapping raises VSZ but not RSS (lazy allocation).
//   - Touching pages drives up the minor page fault count and RSS.

#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "proc/format.hpp"
#include "proc/mapped_file.hpp"
#include "proc/process.hpp"

namespace {

void create_file(const std::string& path, size_t megabytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    const std::string chunk(1024 * 1024, 'A');
    for (size_t i = 0; i < megabytes; ++i) {
        out.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    }
}

void report(const std::string& label, int pid) {
    const auto info = proc::read_process(pid);
    if (!info) {
        return;
    }
    std::cout << "  " << label << "\n"
              << "    VSZ=" << proc::human_bytes(info->vsize_bytes)
              << "  RSS=" << proc::human_bytes(info->rss_bytes)
              << "  minflt=" << info->minor_faults << "  majflt=" << info->major_faults << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    const size_t megabytes = (argc > 1) ? std::strtoul(argv[1], nullptr, 10) : 64;
    const std::string path = "/tmp/mmap_demo.bin";
    const int pid = static_cast<int>(::getpid());

    std::cout << "mmap demo (pid " << pid << ")\n\n";
    report("Start", pid);

    std::cout << "\n  Creating a " << megabytes << " MB file: " << path << '\n';
    create_file(path, megabytes);
    report("After writing the file", pid);

    auto mapping = proc::MappedFile::open(path, /*writable=*/false, /*shared=*/true);
    if (!mapping.valid()) {
        std::cerr << "mmap failed: " << mapping.error() << '\n';
        return 1;
    }

    std::cout << "\n  Mapped " << proc::human_bytes(static_cast<long long>(mapping.size()))
              << " at 0x" << proc::hex_address(reinterpret_cast<unsigned long long>(mapping.data()))
              << '\n';
    report("After mmap, before touching anything", pid);
    std::cout << "    -> VSZ grew, RSS barely moved: no physical pages are attached yet.\n";

    // Reading one byte per page triggers a page fault for that page.
    const size_t ps = static_cast<size_t>(proc::page_size());
    volatile unsigned long long sum = 0;
    for (size_t offset = 0; offset < mapping.size(); offset += ps) {
        sum += mapping.data()[offset];
    }
    (void)sum;

    report("After touching every page", pid);
    std::cout << "    -> RSS grew by the size of the mapping as faults attached the pages.\n"
              << "       Note minflt rose by far less than the page count: on each fault the\n"
              << "       kernel maps a batch of surrounding pages (fault-around).\n";

    std::cout << "\n  Now try this from another shell:\n"
              << "    ./build/procmon maps " << pid << " | grep mmap_demo\n"
              << "    ./build/procmon show " << pid << "\n\n"
              << "  Press Enter to unmap and exit...\n";
    std::cin.get();

    // MappedFile's destructor calls munmap; no manual cleanup needed.
    return 0;
}
