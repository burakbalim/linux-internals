#include "proc/mapped_file.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <vector>

namespace proc
{

    MappedFile::~MappedFile()
    {
        reset();
    }

    MappedFile::MappedFile(MappedFile &&other) noexcept
        : data_(other.data_), size_(other.size_), fd_(std::move(other.fd_)),
          error_(std::move(other.error_))
    {
        other.data_ = nullptr;
        other.size_ = 0;
    }

    MappedFile &MappedFile::operator=(MappedFile &&other) noexcept
    {
        if (this != &other)
        {
            reset();
            data_ = other.data_;
            size_ = other.size_;
            fd_ = std::move(other.fd_);
            error_ = std::move(other.error_);
            other.data_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    void MappedFile::reset()
    {
        if (data_ != nullptr)
        {
            ::munmap(data_, size_);
            data_ = nullptr;
            size_ = 0;
        }
        fd_.reset();
    }

    MappedFile MappedFile::open(const std::string &path, bool writable, bool shared)
    {
        MappedFile mf;

        mf.fd_ = FileDescriptor::open(path, writable ? O_RDWR : O_RDONLY);
        if (!mf.fd_.valid())
        {
            mf.error_ = "open: " + std::string(std::strerror(errno));
            return mf;
        }

        struct stat st
        {
        };
        if (::fstat(mf.fd_.get(), &st) != 0)
        {
            mf.error_ = "fstat: " + std::string(std::strerror(errno));
            return mf;
        }
        if (st.st_size <= 0)
        {
            mf.error_ = "file is empty";
            return mf;
        }

        const int prot = writable ? (PROT_READ | PROT_WRITE) : PROT_READ;
        const int flags = shared ? MAP_SHARED : MAP_PRIVATE;
        void *addr = ::mmap(nullptr, static_cast<size_t>(st.st_size), prot, flags, mf.fd_.get(), 0);
        if (addr == MAP_FAILED)
        {
            mf.error_ = "mmap: " + std::string(std::strerror(errno));
            return mf;
        }

        mf.data_ = static_cast<unsigned char *>(addr);
        mf.size_ = static_cast<size_t>(st.st_size);
        return mf;
    }

    size_t MappedFile::page_count() const
    {
        if (size_ == 0)
        {
            return 0;
        }
        const size_t ps = static_cast<size_t>(page_size());
        return (size_ + ps - 1) / ps;
    }

    long MappedFile::resident_pages() const
    {
        if (!valid())
        {
            return -1;
        }
        // mincore reports, for each mapped page, whether it is resident in the page cache.
        std::vector<unsigned char> vec(page_count(), 0);
        if (::mincore(data_, size_, vec.data()) != 0)
        {
            return -1;
        }
        long resident = 0;
        for (unsigned char b : vec)
        {
            if ((b & 1) != 0)
            {
                ++resident;
            }
        }
        return resident;
    }

} // namespace proc
