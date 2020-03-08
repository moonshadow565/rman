#include "manifest.hpp"
#include "error.hpp"
#include <fltbf.hpp>
#include <cstring>
#include <zstd.h>
#include <limits>

using namespace rman;
using namespace fltbf;

static inline RManifest RManifest_unpack(Offset offset) {
    auto body = RManifest{};
    auto body_table = offset.as<Table>();
    for (auto bundle_table : body_table[0].as<std::vector<Table>>()) {
        auto &bundle = body.bundles.emplace_back();
        bundle.id = bundle_table[0].as<BundleID>();
        for (auto chunk_table : bundle_table[1].as<std::vector<Table>>()) {
            auto &chunk = bundle.chunks.emplace_back();
            chunk.id = chunk_table[0].as<ChunkID>();
            chunk.compressed_size = chunk_table[1].as<int32_t>();
            chunk.uncompressed_size = chunk_table[2].as<int32_t>();
        }
    }
    for (auto lang_table : body_table[1].as<std::vector<Table>>()) {
        auto &lang = body.langs.emplace_back();
        lang.id = lang_table[0].as<LangID>();
        lang.name = lang_table[1].as<std::string>();
    }
    for (auto file_table : body_table[2].as<std::vector<Table>>()) {
        auto &file = body.files.emplace_back();
        file.id = file_table[0].as<FileID>();
        file.parent_dir_id = file_table[1].as<DirID>();
        file.size = file_table[2].as<int32_t>();
        file.name = file_table[3].as<std::string>();
        file.locale_flags = file_table[4].as<uint64_t>();
        file.unk5 = file_table[5].as<uint8_t>();                    // ???, unk size
        file.unk6 = file_table[6].as<uint8_t>();                    // ???, unk size
        file.chunk_ids = file_table[7].as<std::vector<ChunkID>>();
        file.unk8 = file_table[8].as<uint8_t>();                    // set to 1 when part of .app
        file.link = file_table[9].as<std::string>();
        file.unk10 = file_table[10].as<uint8_t>();                  // ???, unk size
        file.params_index = file_table[11].as<uint8_t>();
        file.permissions = file_table[12].as<uint8_t>();
    }
    for (auto dir_table : body_table[3].as<std::vector<Table>>()) {
        auto &dir = body.dirs.emplace_back();
        dir.id = dir_table[0].as<DirID>();
        dir.parent_dir_id = dir_table[1].as<DirID>();
        dir.name = dir_table[2].as<std::string>();
    }
    for (auto key_table : body_table[4].as<std::vector<Table>>()) {
    }
    for (auto param_table : body_table[5].as<std::vector<Table>>()) {
        auto &params = body.params.emplace_back();
        params.unk0 = param_table[0].as<uint16_t>();                   // ? sometimes 1
        params.hash_type = param_table[1].as<HashType>();
        params.unk2 = param_table[2].as<uint8_t>();                    // ???
        params.unk3 = param_table[3].as<int32_t>();                    // ?? < max_uncompressed
        params.max_uncompressed = param_table[4].as<int32_t>();
    }
    return body;
}

RManifest RManifest::read(char const *start, size_t size) {
    uint32_t magic;
    uint8_t version_major;
    uint8_t version_minor;
    uint16_t flags;
    int32_t offset;
    int32_t length;
    ManifestID id;
    int32_t body_length;
    std::vector<char> compressed_data = {};
    std::vector<char> uncompressed_data = {};
    char const* cur = start;
    char const* end = start + size;
    auto read = [&cur, &end](auto& value, size_t length = 0) {
        using type = std::remove_reference_t<decltype(value)>;
        if constexpr (std::is_arithmetic_v<type> || std::is_enum_v<type>) {
            rman_assert(cur + sizeof(type) <= end);
            memcpy(&value, cur, sizeof(type));
            cur += sizeof(type);
        } else {
            static_assert (std::is_same_v<type, std::vector<char>>);
            rman_assert(cur + length <= end);
            value.resize(length);
            memcpy(value.data(), cur, length);
            cur += length;
        }
    };
    read(magic);
    rman_assert(magic == 0x4e414d52u); // RMAN
    read(version_major);
    rman_assert(version_major == 2);
    read(version_minor);
    read(flags);
    read(offset);
    read(length);
    rman_assert(length >= 4);
    read(id);
    read(body_length);
    rman_assert(body_length >= 4);
    rman_assert(offset >= 0 && start + offset <= end);
    cur = start + offset;
    read(compressed_data, (size_t)length);
    uncompressed_data.resize((size_t)body_length);
    auto zstd_result = ZSTD_decompress(uncompressed_data.data(), uncompressed_data.size(),
                                       compressed_data.data(), compressed_data.size());
    rman_assert(!ZSTD_isError(zstd_result));
    rman_assert(zstd_result == uncompressed_data.size());
    auto flatbuffer = Offset{uncompressed_data.data(), 0, (int32_t)uncompressed_data.size()};
    return RManifest_unpack(flatbuffer);
}
