#pragma once

#include <optional>
#include <string>

namespace mc {

// Writes a whole string to a file in one call. Most files under /sys/fs/cgroup and
// /proc must be written this way: the kernel parses one write as one command, so a
// buffered stream that splits the value would be rejected.
bool write_file(const std::string& path, const std::string& value, std::string* error);

std::optional<std::string> read_file(const std::string& path);

// Reads the inode behind /proc/<pid>/ns/<name>, e.g. "pid:[4026531836]".
// Two processes share a namespace exactly when these values match, which makes it
// the simplest way to prove isolation actually happened.
std::optional<std::string> namespace_id(const std::string& pid, const std::string& ns);

std::string errno_message(const std::string& what);

// Parses sizes like "64M", "512K", "1G" into bytes.
std::optional<long long> parse_size(const std::string& text);

std::string human_bytes(long long bytes);

}  // namespace mc
