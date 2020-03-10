#ifndef RMAN_ERROR_HPP
#define RMAN_ERROR_HPP
#include <stdexcept>
#include <string_view>
#include <vector>

#define rman_paste_impl(x, y) x ## y
#define rman_paste(x, y) rman_paste_impl(x, y)
#define rman_error(msg) ::rman::throw_error(__func__, ": " msg)
#define rman_assert(...) do {                                   \
        if(!(__VA_ARGS__)) {                                    \
            ::rman::throw_error(__func__, ": " #__VA_ARGS__);   \
        }                                                       \
    } while(false)
#define rman_rethrow(...)                                 \
    [&, func = __func__]() -> decltype(auto) {            \
        try {                                             \
            return __VA_ARGS__;                           \
        } catch (std::exception const &) {                \
            ::rman::throw_error(func, ": " #__VA_ARGS__); \
        }                                                 \
    }()
#define rman_trace(...) ::rman::ErrorTrace rman_paste(_trace_,__LINE__) {   \
        [&] { ::rman::push_error_msg(__VA_ARGS__); }                        \
    }

namespace rman {
    [[noreturn]] extern void throw_error(char const* from, char const* msg);
    using error_stack_t = std::vector<std::string>;
    extern error_stack_t& error_stack() noexcept;
    extern void push_error_msg(char const* fmt, ...) noexcept;
    template<typename Func>
    struct ErrorTrace : Func {
        inline ErrorTrace(Func&& func) noexcept : Func(std::move(func)) {}
        inline ~ErrorTrace() noexcept {
            if (std::uncaught_exceptions()) {
                Func::operator()();
            }
        }
    };
}
#endif // RMAN_ERROR_HPP
