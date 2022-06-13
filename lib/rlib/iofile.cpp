#include "iofile.hpp"

#include <atomic>
#include <bit>
#include <cstring>
#include <stdexcept>

#include "error.hpp"

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

IOFile::IOFile(fs::path const& path, bool write) {
    rlib_trace("path: %s\n", path.generic_string().c_str());
    if (write && path.has_parent_path()) {
        fs::create_directories(path.parent_path());
    }
    auto const fd = ::CreateFile(path.string().c_str(),
                                 write ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,
                                 write ? 0 : FILE_SHARE_READ,
                                 0,
                                 write ? OPEN_ALWAYS : OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL,
                                 0);
    if ((std::intptr_t)fd == 0 || (std::intptr_t)fd == -1) [[unlikely]] {
        auto ec = std::error_code((int)GetLastError(), std::system_category());
        throw_error("CreateFile: ", ec);
    }
    LARGE_INTEGER size = {};
    if (::GetFileSizeEx(fd, &size) == FALSE) [[unlikely]] {
        auto ec = std::error_code((int)GetLastError(), std::system_category());
        ::CloseHandle(fd);
        throw_error("GetFileSizeEx: ", ec);
    }
    impl_ = {.fd = (std::intptr_t)fd, .size = (std::size_t)size.QuadPart};
}

IOFile::~IOFile() noexcept {
    if (auto impl = std::exchange(impl_, {}); impl.fd) {
        ::CloseHandle((HANDLE)impl.fd);
    }
}

auto IOFile::resize(std::size_t offset, std::size_t count) noexcept -> bool {
    constexpr std::uint64_t MASK = 1ull << 63;
    if (!impl_.fd) {
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

auto IOFile::read(std::size_t offset, std::span<char> dst) const noexcept -> bool {
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

auto IOFile::write(std::uint64_t offset, std::span<char const> src, bool no_interupt) noexcept -> bool {
    constexpr std::size_t CHUNK = 0x4000'0000;
    if (!impl_.fd) {
        return false;
    }
    std::size_t const write_end = offset + src.size();
    if (write_end < offset || write_end < src.size()) {
        return false;
    }
    NoInterupt no_interupt_lock(no_interupt);
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

auto IOFile::copy(std::size_t offset, std::size_t count) const -> std::span<char const> {
    thread_local auto result = std::vector<char>();
    if (result.size() < count) {
        result.clear();
        result.resize(count);
    }
    rlib_assert(this->read(offset, {result.data(), count}));
    return {result.data(), count};
}

#else
#    error "TODO: implement linux version"
#endif
