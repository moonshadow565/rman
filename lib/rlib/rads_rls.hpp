#pragma once
#include <rlib/common.hpp>
#include <rlib/iofile.hpp>

namespace rlib::rads {
    struct RLS {
        struct File {
            std::string name;
            std::string version;
        };

        std::string name;
        std::string version;
        std::string langs;
        std::vector<File> files;

        static auto read(std::span<char const> src) -> RLS;
    private:
        struct Raw;
    };
}