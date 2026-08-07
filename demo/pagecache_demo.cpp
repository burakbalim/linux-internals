// Page cache demo.
//
// Uses mincore(2) to count how many pages of a file are resident in the kernel's page
// cache. posix_fadvise(POSIX_FADV_DONTNEED) drops them, and reading the file back shows
// the cache being refilled.
//
// No root required: there is no need to write to /proc/sys/vm/drop_caches, which would
// affect the entire system rather than just this file.

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "proc/format.hpp"
#include "proc/mapped_file.hpp"
#include "proc/system.hpp"
#include "proc/util.hpp"

namespace {

void create_file(const std::string& path, size_t megabytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    const std::string chunk(1024 * 1024, 'B');
    for (size_t i = 0; i < megabytes; ++i) {
        out.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    }
}

void report(const std::string& label, const proc::MappedFile& mapping) {
    const long resident = mapping.resident_pages();
    const size_t total = mapping.page_count();
    const double pct =
        total > 0 ? (100.0 * static_cast<double>(resident) / static_cast<double>(total)) : 0.0;

    std::cout << "  " << label << '\n';
    if (resident < 0) {
        std::cout << "    mincore failed\n";
        return;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), " (%.1f%%)", pct);
    std::cout << "    " << resident << " / " << total << " pages cached" << buf << "  = "
              << proc::human_bytes(resident * proc::page_size()) << '\n';

    if (const auto mem = proc::read_meminfo()) {
        std::cout << "    system Cached: " << proc::human_kb(mem->cached) << '\n';
    }
}

void drop_from_cache(const std::string& path) {
    // POSIX_FADV_DONTNEED asks the kernel to evict this file's clean pages from the cache.
    proc::FileDescriptor fd = proc::FileDescriptor::open(path, O_RDONLY);
    if (!fd.valid()) {
        return;
    }
    ::posix_fadvise(fd.get(), 0, 0, POSIX_FADV_DONTNEED);
}

}  // namespace

int main(int argc, char** argv) {
    const size_t megabytes = (argc > 1) ? std::strtoul(argv[1], nullptr, 10) : 64;
    const std::string path = "/tmp/pagecache_demo.bin";

    std::cout << "Page cache demo (" << megabytes << " MB)\n\n";

    create_file(path, megabytes);
    ::sync();  // flush dirty pages so DONTNEED can actually evict them

    {
        auto mapping = proc::MappedFile::open(path, /*writable=*/false, /*shared=*/true);
        if (!mapping.valid()) {
            std::cerr << "mmap failed: " << mapping.error() << '\n';
            return 1;
        }
        report("Right after writing (the write itself filled the cache)", mapping);
    }

    drop_from_cache(path);

    {
        auto mapping = proc::MappedFile::open(path, /*writable=*/false, /*shared=*/true);
        report("After posix_fadvise(DONTNEED)", mapping);

        // Reading the file end to end repopulates the page cache.
        std::vector<char> buffer(1 << 20);
        proc::FileDescriptor fd = proc::FileDescriptor::open(path, O_RDONLY);
        ssize_t n;
        while ((n = ::read(fd.get(), buffer.data(), buffer.size())) > 0) {
        }
        (void)n;

        report("After reading it back with read()", mapping);
    }

    std::cout << "\n  Takeaway: file contents live in RAM as \"Cached\". The first read hits\n"
              << "  the disk (a major fault, real I/O); later reads are served from the cache.\n"
              << "  Under memory pressure the kernel reclaims those pages.\n\n"
              << "  Note that writing also fills the cache - which is why a benchmark that\n"
              << "  writes a file and immediately reads it back never measures the disk.\n";

    ::unlink(path.c_str());
    return 0;
}
