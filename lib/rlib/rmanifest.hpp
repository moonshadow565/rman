#pragma once
#include <cinttypes>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "rbundle.hpp"
#include "rchunk.hpp"
#include "rfile.hpp"

namespace rlib {
    namespace fs = std::filesystem;

    enum class ManifestID : std::uint64_t { None };

    struct RMAN {
        ManifestID manifestId;
        std::vector<RFile> files;
        std::vector<RBUN> bundles;

        static auto read(std::span<char const> data) -> RMAN;
        static auto read_file(fs::path const& path) -> RMAN;

    private:
        struct Raw;
    };
}
