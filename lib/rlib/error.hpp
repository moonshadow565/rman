#pragma once
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

#define rlib_paste_impl(x, y) x##y
#define rlib_paste(x, y) rlib_paste_impl(x, y)

#define rlib_error(msg) ::rlib::throw_error(__func__, msg)

#define rlib_assert(...)                                      \
    do {                                                      \
        if (!(__VA_ARGS__)) {                                 \
            ::rlib::throw_error(__func__, ": " #__VA_ARGS__); \
        }                                                     \
    } while (false)

#define rlib_rethrow(...)                                 \
    [&, func = __func__]() -> decltype(auto) {            \
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

namespace rlib {
    [[noreturn]] extern void throw_error(char const* from, char const* msg);

    [[noreturn]] inline void throw_error(char const* from, std::error_code const& ec) {
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
}
