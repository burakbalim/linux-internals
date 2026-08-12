#include "mc/util.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace mc {

std::string errno_message(const std::string& what) {
    return what + ": " + std::strerror(errno);
}

bool write_file(const std::string& path, const std::string& value, std::string* error) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        if (error) *error = errno_message("open " + path);
        return false;
    }

    const ssize_t written = ::write(fd, value.data(), value.size());
    const int saved = errno;
    ::close(fd);

    if (written != static_cast<ssize_t>(value.size())) {
        errno = saved;
        if (error) *error = errno_message("write " + path);
        return false;
    }
    return true;
}

std::optional<std::string> read_file(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return std::nullopt;
    }

    std::string out;
    char buf[4096];
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof(buf))) > 0) {
        out.append(buf, static_cast<size_t>(n));
    }
    ::close(fd);

    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) {
        out.pop_back();
    }
    return out;
}

std::optional<std::string> namespace_id(const std::string& pid, const std::string& ns) {
    const std::string path = "/proc/" + pid + "/ns/" + ns;
    char buf[128];
    const ssize_t n = ::readlink(path.c_str(), buf, sizeof(buf) - 1);
    if (n < 0) {
        return std::nullopt;
    }
    buf[n] = '\0';
    return std::string(buf);
}

std::optional<long long> parse_size(const std::string& text) {
    if (text.empty()) {
        return std::nullopt;
    }

    size_t i = 0;
    long long value = 0;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])) != 0) {
        value = value * 10 + (text[i] - '0');
        ++i;
    }
    if (i == 0) {
        return std::nullopt;
    }

    if (i == text.size()) {
        return value;
    }
    if (i + 1 != text.size()) {
        return std::nullopt;
    }

    switch (std::toupper(static_cast<unsigned char>(text[i]))) {
        case 'K': return value * 1024;
        case 'M': return value * 1024 * 1024;
        case 'G': return value * 1024 * 1024 * 1024;
        default:  return std::nullopt;
    }
}

std::string human_bytes(long long bytes) {
    if (bytes < 0) {
        return "max";
    }
    static const char* units[] = {"B", "K", "M", "G"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }

    char buf[32];
    if (unit == 0) {
        std::snprintf(buf, sizeof(buf), "%lld%s", bytes, units[unit]);
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f%s", value, units[unit]);
    }
    return buf;
}

}  // namespace mc
