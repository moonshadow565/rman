#include "error.hpp"
#include <cstdio>
#include <cstdarg>

using namespace rman;
void rman::throw_error(char const* from, char const* msg) {
    throw std::runtime_error(std::string(from) + msg);
}

error_stack_t& rman::error_stack() noexcept {
    thread_local error_stack_t instance = {};
    return instance;
}

void rman::push_error_msg(char const* fmt, ...) noexcept {
    va_list args;
    char buffer[4096];
    int result;
    va_start(args, fmt);
    result = vsnprintf(buffer, 4096, fmt, args);
    va_end(args);
    if (result >= 0) {
        error_stack().push_back({buffer, buffer + result});
    }
}
