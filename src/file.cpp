#include "file.hpp"
#include "error.hpp"
#include <json.hpp>
#include <sha2.hpp>
#include <cstring>
#include <filesystem>
#include <regex>
#include <charconv>
#include <type_traits>
#include <fstream>
#include <set>
#include <limits>
#include <tuple>

namespace fs = std::filesystem;
using json = nlohmann::json;
using namespace sha2;
using namespace rman;

/// Helper functions

namespace rman {
    template <typename T>
    inline auto from_hex(std::string const& str) {
        using type = std::conditional_t<std::is_enum_v<T>, std::underlying_type_t<T>, T>;
        static_assert (std::is_unsigned_v<type>);
        uint64_t result = 0;
        auto ret = std::from_chars(str.data(), str.data() + str.size(), result, 16);
        rman_assert(ret.ec != std::errc{} || ret.ptr != str.data() + str.size());
        return static_cast<T>(result);
    };

    inline void to_json(json &j, FileChunk const& chunk) {
        j = {
            { "id", to_hex(chunk.id) },
            { "compressed_size", chunk.compressed_size },
            { "uncompressed_size", chunk.uncompressed_size },
            { "bunlde_id", to_hex(chunk.bundle_id) },
            { "compressed_offset", chunk.compressed_offset },
            { "uncompressed_offset", chunk.uncompressed_offset },
        };
    }

    inline void from_json(json const& j, FileChunk& chunk) {
        chunk.id = from_hex<ChunkID>(j.at("id"));
        chunk.compressed_size = j.at("compressed_size");
        chunk.uncompressed_size = j.at("uncompressed_size");
        chunk.bundle_id = from_hex<BundleID>(j.at("bunlde_id"));
        chunk.compressed_offset = j.at("compressed_offset");
        chunk.uncompressed_offset = j.at("uncompressed_offset");
    }

    inline void to_json(json &j, FileInfo const& info) {
        j = {
            { "id", to_hex(info.id) },
            { "path", info.path },
            { "size", info.size },
            { "langs", info.langs },
            { "chunks", info.chunks },
            { "link", info.link },
            { "permissions", info.permissions },
            { "unk0", info.params.unk0 },
            { "hash_type", info.params.hash_type },
            { "unk2", info.params.unk2 },
            { "unk3", info.params.unk3 },
            { "max_uncompressed", info.params.max_uncompressed },
            { "unk5", info.unk5 },
            { "unk6", info.unk6 },
            { "unk8", info.unk8 },
            { "unk10", info.unk10 },
        };
    }

    inline void from_json(json const& j, FileInfo& info) {
        info.id = from_hex<FileID>(j.at("id"));
        info.path = j.at("path");
        info.size = j.at("size");
        info.langs = static_cast<std::unordered_set<std::string>>(j.at("langs"));
        info.chunks = static_cast<std::vector<FileChunk>>(j.at("chunks"));
        info.link = j.at("link");
        info.permissions = j.at("permissions");
        info.params.unk0 = j.at("unk0");
        info.params.hash_type = j.at("hash_type");
        info.params.unk2 = j.at("unk2");
        info.params.unk3 = j.at("unk3");
        info.params.max_uncompressed = j.at("max_uncompressed");
        info.unk5 = j.at("unk5");
        info.unk6 = j.at("unk6");
        info.unk8 = j.at("unk8");
        info.unk10 = j.at("unk10");
    }
}

static void RITO_HKDF(uint8_t const* src, size_t size, uint8_t* output) noexcept {
    auto ctx = SHA256_CTX {};
    auto key = std::array<uint8_t, 64>{};
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, src, size);
    SHA256_Final(key.data(), &ctx);
    auto ipad = key;
    for (auto& p: ipad) {
        p ^= 0x36u;
    }
    auto opad = key;
    for (auto& p: opad) {
        p ^= 0x5Cu;
    }
    auto buffer = std::array<uint8_t, 32> {};
    auto index = std::array<uint8_t, 4>{0x00, 0x00, 0x00, 0x01};
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, ipad.data(), ipad.size());
    SHA256_Update(&ctx, index.data(), index.size());
    SHA256_Final(buffer.data(), &ctx);
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, opad.data(), opad.size());
    SHA256_Update(&ctx, buffer.data(), buffer.size());
    SHA256_Final(buffer.data(), &ctx);
    auto result = buffer;
    for (uint32_t rounds = 31; rounds; rounds--) {
        SHA256_Init(&ctx);
        SHA256_Update(&ctx, ipad.data(), ipad.size());
        SHA256_Update(&ctx, buffer.data(), buffer.size());
        SHA256_Final(buffer.data(), &ctx);
        SHA256_Init(&ctx);
        SHA256_Update(&ctx, opad.data(), opad.size());
        SHA256_Update(&ctx, buffer.data(), buffer.size());
        SHA256_Final(buffer.data(), &ctx);
        for (size_t i = 0; i != 8; ++i) {
            result[i] ^= buffer[i];
        }
    }
    for (size_t i = 0; i != 8; ++i) {
        output[i] = result[i];
    }
}

static inline constexpr int32_t ZSTD_COMPRESSBOUND(int32_t ssrcSize) noexcept {
    size_t srcSize = (size_t)ssrcSize;
    size_t outSize = srcSize + (srcSize >> 8);
    if (srcSize < (128 << 10)) {
        outSize += ((128 << 10) - srcSize) >> 11;
    }
    return (int32_t)outSize;
}

template<typename T, typename F>
static bool remove_if(std::vector<T>& vec, F&& func) noexcept {
    vec.erase(std::remove_if(vec.begin(), vec.end(), std::forward<F>(func)), vec.end());
    return vec.empty();
}

/// Conversion functions

FileList FileList::from_manifest(RManifest const &manifest) {
    auto result = FileList {};
    auto dir_lookup = std::unordered_map<DirID, RMANDir> {};
    for (auto const& dir: manifest.dirs) {
        dir_lookup[dir.id] = dir;
    }
    auto lang_lookup = std::unordered_map<LangID, std::string> {};
    for (auto const& lang: manifest.langs) {
        lang_lookup[lang.id] = lang.name;
    }
    auto chunk_lookup = std::unordered_map<ChunkID, FileChunk> {};
    for (auto const& bundle: manifest.bundles) {
        int32_t compressed_offset = 0;
        for (auto const& chunk: bundle.chunks) {
            chunk_lookup[chunk.id] = FileChunk{chunk, bundle.id, compressed_offset, {}};
            compressed_offset += chunk.compressed_size;
        }
    }
    auto visited = std::unordered_set<DirID>();
    visited.reserve(dir_lookup.size());
    for(auto const& file: manifest.files) {
        rman_trace("FileID: %016llX", (unsigned long long)file.id);
        auto& file_info = result.files.emplace_back();
        file_info.id = file.id;
        file_info.size = file.size;
        file_info.link = file.link;
        file_info.permissions = file.permissions;
        file_info.params = rman_rethrow(manifest.params.at(file.params_index));
        file_info.unk5 = file.unk5;
        file_info.unk6 = file.unk6;
        file_info.unk8 = file.unk8;
        file_info.unk10 = file.unk10;
        auto path = fs::path{file.name};
        auto parent_dir_id = file.parent_dir_id;
        visited.clear();
        while (parent_dir_id != DirID::None) {
            rman_trace("DirID: %llu", (unsigned long long)parent_dir_id);
            rman_assert(visited.find(parent_dir_id) == visited.end());
            visited.insert(parent_dir_id);
            auto const& dir = rman_rethrow(dir_lookup.at(parent_dir_id));
            if (!dir.name.empty()) {
                path = dir.name / path;
                parent_dir_id = dir.parent_dir_id;
            }
        }
        file_info.path = path.generic_string();
        for(size_t i = 0; i != 32; i++) {
            if (file.locale_flags & (1u << i)) {
                rman_trace("LangID: %u", (unsigned int)i);
                auto const& lang = rman_rethrow(lang_lookup.at((LangID)(i + 1)));
                file_info.langs.insert(lang);
            }
        }
        if (file_info.langs.empty()) {
            file_info.langs.insert("none");
        }
        int32_t uncompressed_offset = 0;
        file_info.chunks.reserve(file.chunk_ids.size());
        for (auto chunk_id: file.chunk_ids) {
            rman_trace("ChunkID: %016llX", (unsigned long long)chunk_id);
            auto& chunk = file_info.chunks.emplace_back();
            rman_rethrow(chunk = chunk_lookup.at(chunk_id));
            chunk.uncompressed_offset = uncompressed_offset;
            uncompressed_offset += chunk.uncompressed_size;
        }
    }
    return result;
}

FileList FileList::read(const char *data, size_t size) {
    if (!size) {
        throw std::runtime_error("File size is 0!");
    } else if (*data == 'R') {
        return FileList::from_manifest(RManifest::read(data, size));
    } else if (*data == '[') {
        auto j = json::parse(data, data + size);
        return FileList { j };
    } else {
        rman_error("Unrecognized manifest format!");
    }
}

std::string FileInfo::to_csv() const noexcept {
    auto result = std::string {};
    result += path;
    result += ',';
    result += std::to_string(size);
    result += ',';
    result += to_hex(id);
    result += ',';
    bool lang_first = true;
    for (auto const& lang: langs) {
        if (lang_first) {
            lang_first = false;
        } else {
            result += ' ';
        }
        result += lang;
    }
    return result;
}

std::string FileInfo::to_json(int indent) const noexcept {
    return json{*this}.dump(indent);
}

/// Validity verification

void FileList::filter_path(std::optional<std::regex> const &pat) noexcept {
    if (!pat) {
        return;
    }
    files.remove_if([&pat](FileInfo const& file){
        return !std::regex_match(file.path, *pat);
    });
}

void FileList::filter_langs(std::vector<std::string> const& langs) noexcept {
    if (langs.empty()) {
        return;
    }
    files.remove_if([&](FileInfo const& file) {
        for (auto const& lang: langs) {
            if (file.langs.find(lang) != file.langs.end()) {
                return false;
            }
        }
        return true;
    });
}

void FileList::remove_uptodate(FileList const& old_list) noexcept {
    auto old_lookup = std::unordered_map<std::string_view, FileInfo const*>{};
    for (auto const& file: old_list.files) {
        old_lookup[file.path] = &file;
    }
    files.remove_if([&](FileInfo& file) {
        auto old = old_lookup.find(file.path);
        if (old == old_lookup.end()) {
            return false;
        }
        if (old->second->id == file.id) {
            return true;
        }
        return file.remove_uptodate(*old->second);
    });
}

void FileList::sanitize() const {
    constexpr int32_t CHUNK_LIMIT = 16 * 1024 * 1024;
    auto re_name = std::regex("[\\w\\.\\- ]+", std::regex::optimize);
    for(auto const& file: files) {
        rman_trace("File id: %016llX", (unsigned long long)file.id);
        rman_assert(file.id != FileID::None);
        rman_assert(file.link.empty());
        rman_assert(!file.path.empty());
        rman_assert(file.path.size() < 256);
        auto path = fs::path(file.path, fs::path::generic_format);
        auto path_normalized = path.lexically_normal();
        rman_assert(path == path_normalized);
        rman_assert(!path.is_absolute());
        for (auto const& component: path) {
            auto path_component = component.generic_string();
            rman_assert(!path_component.empty());
            rman_assert(path_component != ".." && path_component != ".");
            rman_assert(std::regex_match(path_component, re_name));
        }
        for (auto const& lang: file.langs) {
            rman_assert(std::regex_match(lang, re_name));
        }
        rman_assert(file.size > 0);
        rman_assert(file.params.hash_type <= HashType::RITO_HKDF);
        auto max_uncompressed = file.params.max_uncompressed;
        rman_assert(max_uncompressed > 0 && max_uncompressed <= CHUNK_LIMIT);
        rman_assert(file.size <= (INT32_MAX - max_uncompressed));
        auto max_compressed = ZSTD_COMPRESSBOUND(max_uncompressed);
        auto next_min_uncompressed_offset = int32_t{0};
        for (auto const& chunk: file.chunks) {
            rman_trace("Chunk id: %016llX", (unsigned long long)chunk.id);
            rman_assert(chunk.id != ChunkID::None);
            rman_assert(chunk.compressed_size >= 4);
            rman_assert(chunk.compressed_size <= max_compressed);
            rman_assert(chunk.compressed_offset >= 0);
            rman_assert(chunk.bundle_id != BundleID::None);
            rman_assert(chunk.uncompressed_size > 0);
            rman_assert(chunk.uncompressed_size <= max_uncompressed);
            rman_assert(chunk.uncompressed_offset >= next_min_uncompressed_offset);
            rman_assert(chunk.uncompressed_offset + chunk.uncompressed_size <= file.size);
            next_min_uncompressed_offset = chunk.uncompressed_offset + chunk.uncompressed_size;
        }
    }
}

/// Integrity verifcation functions

bool FileChunk::verify(std::vector<uint8_t> const& buffer, HashType type) const noexcept {
    std::array<uint8_t, 64> output = {};
    switch(type) {
    case HashType::SHA512:
        SHA512(buffer.data(), buffer.size(), output.data());
        break;
    case HashType::SHA256:
        SHA256(buffer.data(), buffer.size(), output.data());
        break;
    case HashType::RITO_HKDF:
        RITO_HKDF(buffer.data(), buffer.size(), output.data());
        break;
    default:
        return false;
    }
    return memcmp(output.data(), &id, sizeof(ChunkID)) == 0;
}

bool FileInfo::remove_exist(std::string const& folder_name) noexcept {
    auto file_path = fs::path(folder_name) / fs::path(path, fs::path::generic_format);
    if (!fs::exists(file_path) || !fs::is_regular_file(file_path)) {
        return true;
    }
    return false;
}

bool FileInfo::remove_verified(std::string const& folder_name) noexcept {
    if (params.hash_type == HashType::None) {
        return false;
    }
    auto file_path = fs::path(folder_name) / fs::path(path, fs::path::generic_format);
    auto infile = std::ifstream(file_path, std::ios::binary);
    if (!infile) {
        return false;
    }
    infile.seekg(0, std::ios::end);
    auto end = infile.tellg();
    infile.seekg(0, std::ios::beg);
    auto start = infile.tellg();
    if ((end - start) >= INT32_MAX) {
        return false;
    }
    auto file_size = (int32_t)(end - start);
    auto buffer = std::vector<uint8_t>();
    buffer.resize((size_t)params.max_uncompressed);
    remove_if(chunks, [&](FileChunk const& chunk) -> bool {
        if (chunk.uncompressed_offset + chunk.uncompressed_size > file_size) {
            return false;
        }
        buffer.resize((size_t)chunk.uncompressed_size);
        infile.clear();
        infile.seekg(chunk.uncompressed_offset, std::ios::beg);
        infile.read((char*)buffer.data(), buffer.size());
        if (chunk.verify(buffer, params.hash_type)) {
            return true;
        }
        return false;
    });
    if (file_size != size || !chunks.empty()) {
        return false;
    }
    return true;
}

bool FileInfo::remove_uptodate(FileInfo const& old_file) noexcept {
    using key_t = std::tuple<int32_t, int32_t, ChunkID>;
    auto old_lookup = std::set<key_t>{};
    for(auto const& old: old_file.chunks) {
        auto key = key_t{old.uncompressed_offset, old.uncompressed_offset, old.id};
        old_lookup.insert(key);
    }
    remove_if(chunks, [&](FileChunk const& chunk) {
        auto key = key_t{chunk.uncompressed_offset, chunk.uncompressed_offset, chunk.id};
        return old_lookup.find(key) != old_lookup.end();
    });
    return old_file.chunks.empty();
}

