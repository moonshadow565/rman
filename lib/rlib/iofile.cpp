#include "iofile.hpp"

#include <atomic>
#include <bit>
#include <cstring>
#include <stdexcept>

#include "common.hpp"

using namespace rlib;

struct NoInterupt {
    NoInterupt(bool no_interupt) : no_interupt(no_interupt) {
        if (no_interupt) {
            while (lock_)
                ;
            ++count_;
        }
    }
    ~NoInterupt() {
        if (no_interupt) {
            while (lock_)
                ;
            --count_;
        }
    }

private:
    bool no_interupt;
    static std::atomic_int lock_;
    static std::atomic_int count_;
};

auto IO::File::shrink_to_fit() noexcept -> bool {
    if (!impl_.fd || !(impl_.flags & WRITE)) {
        return false;
    }
    return true;
}

auto IO::File::reserve(std::size_t offset, std::size_t count) noexcept -> bool {
    if (!impl_.fd || !(impl_.flags & WRITE)) {
        return false;
    }
    return true;
}

auto IO::File::copy(std::size_t offset, std::size_t count) const -> std::span<char const> {
    thread_local auto result = std::vector<char>();
    if (result.size() < count) {
        result.clear();
        result.resize(count);
    }
    rlib_assert(this->read(offset, {result.data(), count}));
    return {result.data(), count};
}

#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>

std::atomic_int NoInterupt::lock_ = 0;
std::atomic_int NoInterupt::count_ = [] {
    SetConsoleCtrlHandler(
        +[](DWORD) -> BOOL {
            ++lock_;
            if (count_) {
                --lock_;
                return TRUE;
            }
            return FALSE;
        },
        TRUE);
    return 0;
}();

IO::File::File(fs::path const& path, Flags flags) {
    rlib_trace("path: %s\n", path.generic_string().c_str());
    if ((flags & WRITE) && path.has_parent_path()) {
        fs::create_directories(path.parent_path());
    }
    DWORD access = (flags & WRITE) ? (GENERIC_READ | GENERIC_WRITE) : GENERIC_READ;
    DWORD share = (flags & WRITE) ? 0 : FILE_SHARE_READ;
    DWORD disposition = (flags & WRITE) ? OPEN_ALWAYS : OPEN_EXISTING;
    DWORD attributes = FILE_ATTRIBUTE_NORMAL;
    if (flags & SEQUENTIAL) {
        attributes |= FILE_FLAG_SEQUENTIAL_SCAN;
    }
    if (flags & RANDOM_ACCESS) {
        attributes |= FILE_FLAG_RANDOM_ACCESS;
    }
    auto const fd = ::CreateFile(path.string().c_str(), access, share, 0, disposition, attributes, 0);
    if (!fd || fd == INVALID_HANDLE_VALUE) [[unlikely]] {
        auto ec = std::error_code((int)GetLastError(), std::system_category());
        throw_error("CreateFile: ", ec);
    }
    LARGE_INTEGER size = {};
    if (::GetFileSizeEx(fd, &size) == FALSE) [[unlikely]] {
        auto ec = std::error_code((int)GetLastError(), std::system_category());
        ::CloseHandle(fd);
        throw_error("GetFileSizeEx: ", ec);
    }
    impl_ = {.fd = (std::intptr_t)fd, .size = (std::size_t)size.QuadPart, .flags = flags};
}

IO::File::~File() noexcept {
    if (auto impl = std::exchange(impl_, {}); impl.fd) {
        ::CloseHandle((HANDLE)impl.fd);
    }
}

auto IO::File::resize(std::size_t offset, std::size_t count) noexcept -> bool {
    if (!impl_.fd || !(impl_.flags & WRITE)) {
        return false;
    }
    std::uint64_t const total = (std::uint64_t)offset + count;
    if (total < offset || total < count) {
        return false;
    }
    if (impl_.size == total) {
        return true;
    }
    FILE_END_OF_FILE_INFO i = {.EndOfFile = {.QuadPart = (LONGLONG)total}};
    if (::SetFileInformationByHandle((HANDLE)impl_.fd, FileEndOfFileInfo, &i, sizeof(i)) == FALSE) [[unlikely]] {
        return false;
    }
    impl_.size = total;
    return true;
}

auto IO::File::read(std::size_t offset, std::span<char> dst) const noexcept -> bool {
    constexpr std::size_t CHUNK = 0x1000'0000;
    if (!impl_.fd) {
        return false;
    }
    while (!dst.empty()) {
        DWORD wanted = (DWORD)std::min(CHUNK, dst.size());
        OVERLAPPED off = {.Offset = (std::uint32_t)offset, .OffsetHigh = (std::uint32_t)(offset >> 32)};
        DWORD got = {};
        ::ReadFile((HANDLE)impl_.fd, dst.data(), wanted, &got, &off);
        if (!got || got > wanted) {
            return false;
        }
        dst = dst.subspan(got);
        offset += got;
    }
    return true;
}

auto IO::File::write(std::uint64_t offset, std::span<char const> src) noexcept -> bool {
    constexpr std::size_t CHUNK = 0x4000'0000;
    if (!impl_.fd || !(impl_.flags & WRITE)) {
        return false;
    }
    std::size_t const write_end = offset + src.size();
    if (write_end < offset || write_end < src.size()) {
        return false;
    }
    NoInterupt no_interupt_lock(impl_.flags & NO_INTERUPT);
    while (!src.empty()) {
        DWORD wanted = (DWORD)std::min(CHUNK, src.size());
        OVERLAPPED off = {.Offset = (std::uint32_t)offset, .OffsetHigh = (std::uint32_t)(offset >> 32)};
        DWORD got = {};
        ::WriteFile((HANDLE)impl_.fd, src.data(), wanted, &got, &off);
        if (got > wanted) {
            return false;
        }
        if (!got) {
            return false;
        }
        src = src.subspan(got);
        offset += got;
    }
    if (write_end > impl_.size) {
        impl_.size = write_end;
    }
    return true;
}

auto IO::MMap::Impl::remap(std::size_t count) noexcept -> bool {
    void* data = nullptr;
    if (count) {
        auto const fd = this->file.fd();
        auto const flags = this->file.flags();
        DWORD protect = (flags & WRITE) ? PAGE_READWRITE : PAGE_READONLY;
        auto mapping = ::CreateFileMappingA((HANDLE)fd, 0, protect, count >> 32, count, 0);
        if (!mapping || mapping == INVALID_HANDLE_VALUE) [[unlikely]] {
            return false;
        }
        DWORD access = (flags & WRITE) ? FILE_MAP_READ | FILE_MAP_WRITE : FILE_MAP_READ;
        data = ::MapViewOfFile(mapping, access, 0, 0, count);
        if (!data || data == INVALID_HANDLE_VALUE) [[unlikely]] {
            return false;
        }
        CloseHandle(mapping);
    }
    if (this->data) {
        ::UnmapViewOfFile(this->data);
    }
    this->data = data;
    this->capacity = count;
    return true;
}

#else
#    include <fcntl.h>
#    include <signal.h>
#    include <sys/mman.h>
#    include <sys/param.h>
#    include <sys/stat.h>
#    include <unistd.h>
std::atomic_int NoInterupt::lock_ = 0;
std::atomic_int NoInterupt::count_ = [] {
    signal(SIGINT, [](int) {
        ++lock_;
        if (count_) {
            --lock_;
        } else {
            exit(1);
        }
    });
    return 0;
}();

IO::File::File(fs::path const& path, Flags flags) {
    rlib_trace("path: %s\n", path.generic_string().c_str());
    if ((flags & WRITE) && path.has_parent_path()) {
        fs::create_directories(path.parent_path());
    }
    int fd = 0;
    if (flags & WRITE) {
        fd = ::open(path.string().c_str(), O_RDWR | O_CREAT, 0644);
    } else {
        fd = ::open(path.string().c_str(), O_RDONLY);
    }
    if (!fd || fd == -1) [[unlikely]] {
        auto ec = std::error_code((int)errno, std::system_category());
        throw_error("::open: ", ec);
    }
    struct ::stat size = {};
    if (::fstat(fd, &size) == -1) [[unlikely]] {
        auto ec = std::error_code((int)errno, std::system_category());
        throw_error("::fstat: ", ec);
    }
    impl_ = {.fd = (std::intptr_t)fd, .size = (std::size_t)size.st_size, .flags = flags};
}

IO::File::~File() noexcept {
    if (auto impl = std::exchange(impl_, {}); impl.fd) {
        ::close((int)impl.fd);
    }
}

auto IO::File::resize(std::size_t offset, std::size_t count) noexcept -> bool {
    if (!impl_.fd || !(impl_.flags & WRITE)) {
        return false;
    }
    std::uint64_t const total = (std::uint64_t)offset + count;
    if (total < offset || total < count) {
        return false;
    }
    if (impl_.size == total) {
        return true;
    }
    if (::ftruncate((int)impl_.fd, (off_t)total) == -1) [[unlikely]] {
        return false;
    }
    impl_.size = total;
    return true;
}

auto IO::File::read(std::size_t offset, std::span<char> dst) const noexcept -> bool {
    if (!impl_.fd) {
        return false;
    }
    while (!dst.empty()) {
        auto got = ::pread((int)impl_.fd, dst.data(), dst.size(), offset);
        if (got <= 0 || (std::size_t)got > dst.size()) {
            return false;
        }
        dst = dst.subspan(got);
        offset += got;
    }
    return true;
}

auto IO::File::write(std::uint64_t offset, std::span<char const> src) noexcept -> bool {
    constexpr std::size_t CHUNK = 0x4000'0000;
    if (!impl_.fd || !(impl_.flags & WRITE)) {
        return false;
    }
    std::size_t const write_end = offset + src.size();
    if (write_end < offset || write_end < src.size()) {
        return false;
    }
    NoInterupt no_interupt_lock(impl_.flags & NO_INTERUPT);
    while (!src.empty()) {
        auto got = ::pwrite((int)impl_.fd, src.data(), src.size(), offset);
        if (got <= 0 || (std::size_t)got > src.size()) {
            return false;
        }
        src = src.subspan(got);
        offset += got;
    }
    if (write_end > impl_.size) {
        impl_.size = write_end;
    }
    return true;
}

auto IO::MMap::Impl::remap(std::size_t count) noexcept -> bool {
    void* data = nullptr;
    if (count) {
        auto const fd = this->file.fd();
        auto const flags = this->file.flags();
        auto const prot = (flags & WRITE) ? PROT_READ | PROT_WRITE : PROT_READ;
        data = ::mmap(0, count, prot, MAP_SHARED, (int)fd, 0);
        if (!data || (std::intptr_t)data == -1) [[unlikely]] {
            return false;
        }
    }
    if (this->data) {
        ::munmap(this->data, this->capacity);
    }
    this->data = data;
    this->capacity = count;
    return true;
}

#endif

IO::MMap::MMap(fs::path const& path, Flags flags) {
    auto impl = Impl{.file = IO::File(path, flags)};
    if (auto size = impl.file.size()) {
        rlib_assert(impl.remap(size));
        impl.size = size;
    }
    impl_ = std::move(impl);
}

IO::MMap::~MMap() noexcept {
    auto impl = std::exchange(impl_, {});
    impl.remap(0);
    if (impl.file.flags() & WRITE && impl.size != impl.file.size()) {
        impl.file.resize(0, impl.size);
    }
}

auto IO::MMap::shrink_to_fit() noexcept -> bool {
    if (!impl_.file.fd() || !(impl_.file.flags() & WRITE)) {
        return false;
    }
    if (impl_.size != impl_.capacity) {
        if (!impl_.remap(impl_.size)) {
            return false;
        }
    }
    if (impl_.file.size() != impl_.size) {
        if (!impl_.file.resize(0, impl_.size)) {
            return false;
        }
    }
    return true;
}

auto IO::MMap::reserve(std::size_t offset, std::size_t count) noexcept -> bool {
    if (!impl_.file.fd() || !(impl_.file.flags() & WRITE)) {
        return false;
    }
    std::size_t total = offset + count;
    if (total < offset || total < count) {
        return false;
    }
    if (total > impl_.file.size()) {
        if (!(impl_.file.flags() & NO_OVERGROW)) {
            total = std::max(total, std::bit_ceil(std::max(std::size_t{0x1000}, total)));
        }
        if (!impl_.file.resize(0, total)) {
            return false;
        }
    }
    if (total > impl_.capacity) {
        if (!impl_.remap(total)) {
            return false;
        }
    }
    return true;
}

auto IO::MMap::read(std::size_t offset, std::span<char> dst) const noexcept -> bool {
    if (in_range(offset, dst.size(), impl_.size)) {
        return false;
    }
    std::memcpy(dst.data(), (char const*)impl_.data + offset, dst.size());
    return true;
}

auto IO::MMap::write(std::size_t offset, std::span<char const> src) noexcept -> bool {
    if (!impl_.file.fd() || !(impl_.file.flags() & WRITE)) {
        return false;
    }
    std::size_t total = offset + src.size();
    if (total < offset || total < src.size()) {
        return false;
    }
    NoInterupt no_interupt_lock(impl_.file.flags() & NO_INTERUPT);
    if (!this->reserve(offset, src.size())) {
        return false;
    }
    std::memcpy((char*)impl_.data + offset, src.data(), src.size());
    impl_.size = std::max(impl_.size, total);
    return true;
}

auto IO::MMap::copy(std::size_t offset, std::size_t count) const -> std::span<char const> {
    rlib_assert(in_range(offset, count, impl_.size));
    return {(char const*)impl_.data + offset, count};
}

auto IO::MMap::resize(std::size_t offset, std::size_t count) noexcept -> bool {
    if (!this->reserve(offset, count)) {
        return false;
    }
    impl_.size = (std::uint64_t)offset + count;
    return true;
}

IO::Reader::Reader(IO const& io, std::size_t pos, std::size_t size)
    : io_(&io), start_(std::min(pos, io_->size())), pos_(start_), end_(pos_ + std::min(size, io_->size() - pos_)) {}

auto IO::Reader::skip(std::size_t size) noexcept -> bool {
    if (!size) return true;
    if (remains() < size) [[unlikely]]
        return false;
    pos_ += size;
    return true;
}

auto IO::Reader::seek(std::size_t pos) noexcept -> bool {
    if (size() < pos) [[unlikely]]
        return false;
    pos_ = start_ + pos;
    return true;
}

auto IO::Reader::read_within(Reader& reader, std::size_t size) noexcept -> bool {
    if (remains() < size) [[unlikely]]
        return false;
    reader.io_ = io_;
    reader.start_ = pos_;
    reader.pos_ = pos_;
    reader.end_ = pos_ + size;
    pos_ += size;
    return true;
}

auto IO::Reader::read_raw(void* dst, std::size_t size) noexcept -> bool {
    if (!size) return true;
    if (remains() < size || !io_->read(pos_, {(char*)dst, size})) [[unlikely]]
        return false;
    pos_ += size;
    return true;
}
