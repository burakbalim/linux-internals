#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace proc {

// RAII wrapper that closes the descriptor when it leaves scope.
// Non-copyable, movable: ownership always lives in exactly one object.
class FileDescriptor {
public:
    FileDescriptor() = default;
    explicit FileDescriptor(int fd) : fd_(fd) {}
    ~FileDescriptor();

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    FileDescriptor(FileDescriptor&& other) noexcept;
    FileDescriptor& operator=(FileDescriptor&& other) noexcept;

    static FileDescriptor open(const std::string& path, int flags);

    int get() const { return fd_; }
    bool valid() const { return fd_ >= 0; }
    void reset(int fd = -1);
    int release();

private:
    int fd_ = -1;
};

// procfs files report size 0 via stat(), so we must read until EOF.
std::optional<std::string> read_file(const std::string& path);
std::vector<std::string> read_lines(const std::string& path);

// The returned views point into the input buffer and are valid only while it lives.
std::vector<std::string_view> split_ws(std::string_view s);
std::string_view trim(std::string_view s);

// For "Key: value" style files (/proc/meminfo, /proc/<pid>/status).
std::unordered_map<std::string, std::string> parse_kv(const std::string& path);

std::optional<long long> to_ll(std::string_view s);
bool is_number(std::string_view s);

long page_size();
long clock_ticks_per_sec();

}  // namespace proc
