#include "proc/util.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <charconv>

namespace proc
{

    FileDescriptor::~FileDescriptor()
    {
        reset();
    }

    FileDescriptor::FileDescriptor(FileDescriptor &&other) noexcept : fd_(other.fd_)
    {
        other.fd_ = -1;
    }

    FileDescriptor &FileDescriptor::operator=(FileDescriptor &&other) noexcept
    {
        if (this != &other)
        {
            reset(other.fd_);
            other.fd_ = -1;
        }
        return *this;
    }

    void FileDescriptor::reset(int fd)
    {
        if (fd_ >= 0)
        {
            ::close(fd_);
        }
        fd_ = fd;
    }

    int FileDescriptor::release()
    {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }

    FileDescriptor FileDescriptor::open(const std::string &path, int flags)
    {
        return FileDescriptor(::open(path.c_str(), flags));
    }

    std::optional<std::string> read_file(const std::string &path)
    {
        FileDescriptor fd = FileDescriptor::open(path, O_RDONLY);
        if (!fd.valid())
        {
            return std::nullopt;
        }

        std::string out;
        char buf[4096];
        ssize_t n;
        while ((n = ::read(fd.get(), buf, sizeof(buf))) > 0)
        {
            out.append(buf, static_cast<size_t>(n));
        }
        // If the process died mid-read we may hold partial data; return it anyway.
        if (n < 0 && out.empty())
        {
            return std::nullopt;
        }
        return out;
    }

    std::vector<std::string> read_lines(const std::string &path)
    {
        std::vector<std::string> lines;
        const auto content = read_file(path);
        if (!content)
        {
            return lines;
        }

        size_t start = 0;
        while (start < content->size())
        {
            const size_t end = content->find('\n', start);
            if (end == std::string::npos)
            {
                lines.emplace_back(content->substr(start));
                break;
            }
            lines.emplace_back(content->substr(start, end - start));
            start = end + 1;
        }
        return lines;
    }

    std::vector<std::string_view> split_ws(std::string_view s)
    {
        std::vector<std::string_view> parts;
        size_t i = 0;
        while (i < s.size())
        {
            while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
            {
                ++i;
            }
            const size_t start = i;
            while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])))
            {
                ++i;
            }
            if (i > start)
            {
                parts.push_back(s.substr(start, i - start));
            }
        }
        return parts;
    }

    std::string_view trim(std::string_view s)
    {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        {
            s.remove_prefix(1);
        }
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        {
            s.remove_suffix(1);
        }
        return s;
    }

    std::unordered_map<std::string, std::string> parse_kv(const std::string &path)
    {
        std::unordered_map<std::string, std::string> kv;
        for (const auto &line : read_lines(path))
        {
            const size_t colon = line.find(':');
            if (colon == std::string::npos)
            {
                continue;
            }
            std::string key(trim(std::string_view(line).substr(0, colon)));
            std::string value(trim(std::string_view(line).substr(colon + 1)));
            kv.emplace(std::move(key), std::move(value));
        }
        return kv;
    }

    std::optional<long long> to_ll(std::string_view s)
    {
        s = trim(s);
        if (s.empty())
        {
            return std::nullopt;
        }
        long long value = 0;
        // from_chars stops at the non-numeric suffix ("1234 kB") instead of failing.
        const auto result = std::from_chars(s.data(), s.data() + s.size(), value);
        if (result.ec != std::errc())
        {
            return std::nullopt;
        }
        return value;
    }

    bool is_number(std::string_view s)
    {
        return !s.empty() && std::all_of(s.begin(), s.end(),
                                         [](unsigned char c) { return std::isdigit(c) != 0; });
    }

    long page_size()
    {
        static const long value = ::sysconf(_SC_PAGESIZE);
        return value;
    }

    long clock_ticks_per_sec()
    {
        static const long value = ::sysconf(_SC_CLK_TCK);
        return value;
    }

} // namespace proc
