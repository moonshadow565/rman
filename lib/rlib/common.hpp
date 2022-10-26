#pragma once
#include <fmt/args.h>
#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <vector>

#ifdef _MSC_VER
#    define __PRETTY_FUNCTION__ __FUNCTION__
#endif

#define rlib_paste_impl(x, y) x##y
#define rlib_paste(x, y) rlib_paste_impl(x, y)

#define rlib_error(msg) ::rlib::throw_error(__PRETTY_FUNCTION__, msg)

#define rlib_assert(...)                                                 \
    do {                                                                 \
        if (!(__VA_ARGS__)) [[unlikely]] {                               \
            ::rlib::throw_error(__PRETTY_FUNCTION__, ": " #__VA_ARGS__); \
        }                                                                \
    } while (false)

#define rlib_rethrow(...)                                 \
    [&, func = __PRETTY_FUNCTION__]() -> decltype(auto) { \
        try {                                             \
            return __VA_ARGS__;                           \
        } catch (std::exception const&) {                 \
            ::rlib::throw_error(func, ": " #__VA_ARGS__); \
        }                                                 \
    }()

#define rlib_trace(...)                                \
    ::rlib::ErrorTrace rlib_paste(_trace_, __LINE__) { \
        [&] { ::rlib::push_error_msg(__VA_ARGS__); }   \
    }

#define rlib_assert_zstd(...)                                                      \
    [&, func = __PRETTY_FUNCTION__]() -> std::size_t {                             \
        if (std::size_t result = __VA_ARGS__; ZSTD_isError(result)) [[unlikely]] { \
            throw_error(func, ZSTD_getErrorName(result));                          \
        } else {                                                                   \
            return result;                                                         \
        }                                                                          \
    }()

namespace rlib {
    static std::size_t KiB = 1024;
    static std::size_t MiB = KiB * 1024;
    static std::size_t GiB = MiB * 1024;
    static std::size_t TiB = GiB * 1024;

    namespace fs = std::filesystem;
    using namespace std::literals::string_view_literals;

    [[noreturn]] extern void throw_error(std::string_view from, char const* msg);

    [[noreturn]] inline void throw_error(std::string_view from, std::error_code const& ec) {
        throw_error(from, ec.message().c_str());
    }

    using error_stack_t = std::vector<std::string>;

    extern error_stack_t& error_stack() noexcept;

    extern void push_error_msg(char const* fmt, ...) noexcept;

    template <typename Func>
    struct ErrorTrace : Func {
        inline ErrorTrace(Func&& func) noexcept : Func(std::move(func)) {}
        inline ~ErrorTrace() noexcept {
            if (std::uncaught_exceptions()) {
                Func::operator()();
            }
        }
    };

    struct progress_bar {
        static constexpr auto MB = 1024.0 * 1024.0;

        progress_bar(char const* banner,
                     bool disabled,
                     std::uint32_t index,
                     std::uint64_t done,
                     std::uint64_t total) noexcept;
        ~progress_bar() noexcept;

        auto update(std::uint64_t done) noexcept -> void;

    private:
        auto render() const noexcept -> void;

        char const* banner_;
        bool disabled_ = {};
        std::uint32_t index_;
        std::uint64_t done_;
        std::uint64_t total_;
        std::uint64_t percent_;
    };

    extern auto from_hex(std::string name) noexcept -> std::optional<std::uint64_t>;

    extern auto clean_path(std::string path) noexcept -> std::string;

    extern auto zstd_decompress(std::span<char const> src, std::size_t count) -> std::span<char const>;

    extern auto zstd_frame_decompress_size(std::span<char const> src) -> std::size_t;

    template <typename T, std::size_t S>
    inline auto to_array(std::span<T const> src) noexcept -> std::array<T, S> {
        auto result = std::array<T, S>{};
        std::copy_n(src.data(), S, result.data());
        return result;
    }

    template <auto... M>
    inline auto sort_by(auto beg, auto end) noexcept -> void {
        return std::sort(beg, end, [](auto const& l, auto const& r) {
            return std::tie((l.*M)...) < std::tie((r.*M)...);
        });
    }

    template <auto... M>
    inline auto uniq_by(auto beg, auto end) noexcept -> auto{
        return std::unique(beg, end, [](auto const& l, auto const& r) {
            return std::tie((l.*M)...) == std::tie((r.*M)...);
        });
    }

    template <typename F>
    inline auto remove_if(auto& container, F&& func) noexcept -> bool {
        container.erase(std::remove_if(container.begin(), container.end(), std::forward<F>(func)), container.end());
        return container.empty();
    }

    inline auto in_range(std::size_t offset, std::size_t size, std::size_t target) noexcept -> bool {
        return target >= offset && target - offset >= size;
    }

    inline auto in_range(char const* ptr, std::size_t size, std::span<char const> target) noexcept -> bool {
        return ptr >= target.data() && (target.data() + target.size() - ptr) >= size;
    }

    template <typename T>
    inline auto str_split(std::string_view str, T&& s) noexcept -> std::pair<std::string_view, std::string_view> {
        if (auto n = str.find(std::forward<T>(s)); n != std::string_view::npos) {
            return {str.substr(0, n), str.substr(n + 1)};
        }
        return {str, {}};
    }

    inline auto str_strip(std::string_view str) noexcept -> std::string_view {
        while (str.empty() && ::isspace(str.front())) str.remove_prefix(1);
        while (str.empty() && ::isspace(str.back())) str.remove_suffix(1);
        return str;
    }

    template <typename Signature>
    struct function_ref;

    template <typename Ret, typename... Args>
    struct function_ref<Ret(Args...)> {
        constexpr function_ref() noexcept = default;

        template <typename Func>
            requires(std::is_invocable_r_v<Ret, Func, Args...>)
        function_ref(Func* func)
        noexcept
            : ref_((void*)func),
              invoke_(+[](void* ref, Args... args) -> Ret { return std::invoke(*(Func*)ref, args...); }) {}

        template <typename Func>
            requires(std::is_invocable_r_v<Ret, Func, Args...>)
        function_ref(Func&& func)
        noexcept : function_ref(&func) {}

        explicit constexpr operator bool() const noexcept { return ref_; }

        constexpr bool operator!() const noexcept { return !ref_; }

        auto operator()(Args... args) -> Ret { return invoke_(ref_, args...); }

    private:
        Ret (*invoke_)(void* ref, Args...) = nullptr;
        void* ref_ = nullptr;
    };

    extern auto collect_files(std::vector<std::string> const& inputs,
                              function_ref<bool(fs::path const& path)> filter,
                              bool recursive = false) -> std::vector<fs::path>;
}