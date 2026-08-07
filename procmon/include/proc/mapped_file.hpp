#pragma once

#include <cstddef>
#include <string>

#include "proc/util.hpp"

namespace proc {

// RAII wrapper that munmaps the region on destruction.
// The demos use it so the mapping can be matched against /proc/<pid>/maps.
class MappedFile {
public:
    MappedFile() = default;
    ~MappedFile();

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;

    // shared=false selects MAP_PRIVATE (copy-on-write).
    static MappedFile open(const std::string& path, bool writable, bool shared);

    bool valid() const { return data_ != nullptr; }
    unsigned char* data() const { return data_; }
    size_t size() const { return size_; }
    const std::string& error() const { return error_; }

    // Number of pages resident in the page cache (mincore). -1 on error.
    long resident_pages() const;
    size_t page_count() const;

private:
    void reset();

    unsigned char* data_ = nullptr;
    size_t size_ = 0;
    FileDescriptor fd_;
    std::string error_;
};

}  // namespace proc
