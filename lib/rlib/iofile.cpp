#include "iofile.hpp"

#include <atomic>
#include <bit>
#include <cstring>
#include <stdexcept>

#include "common.hpp"

using namespace rlib;

#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>

static std::atomic_int NoInterupt_lock_ = 0;
static std::atomic_int NoInterupt_count_ = [] {
    SetConsoleCtrlHandler(
        +[](DWORD) -> BOOL {
            ++NoInterupt_lock_;
            if (NoInterupt_count_) {
                --NoInterupt_lock_;
                return TRUE;
            }
            return FALSE;
        },
        TRUE);
    return 0;
}();

struct NoInterupt {
    NoInterupt(bool no_interupt) : no_interupt(no_interupt) {
        if (no_interupt) {
            while (NoInterupt_lock_)
                ;
            ++NoInterupt_count_;
        }
    }
    ~NoInterupt() {
        if (no_interupt) {
            while (NoInterupt_lock_)
                ;
            --NoInterupt_count_;
        }
    }

private:
    bool no_interupt;
};

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

auto IO::File::copy(std::size_t offset, std::size_t count) const -> std::span<char const> {
    thread_local auto result = std::vector<char>();
    if (result.size() < count) {
        result.clear();
        result.resize(count);
    }
    rlib_assert(this->read(offset, {result.data(), count}));
    return {result.data(), count};
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
#    error "TODO: implement linux version"
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
