#pragma once
#include <rlib/common.hpp>
#include <rlib/iofile.hpp>
#include <rlib/rads_rls.hpp>

namespace rlib::rads {
    struct SLN {
        std::string name;
        std::string version;
        std::vector<RLS> projects;

        static auto read(std::span<char const> src) -> SLN;

    private:
        struct Raw;
    };
}