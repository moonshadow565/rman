#include "manifest_info.hpp"
#include "json.hpp"
#include <filesystem>
#include <unordered_map>
#include <regex>
#include <filesystem>

using namespace rman;
using json = nlohmann::json;
namespace fs = std::filesystem;

ManifestInfo ManifestInfo::filter_path(std::string pattern) const {
    auto result = ManifestInfo{};
    result.id = id;
    namespace re = std::regex_constants;
    auto pat = std::regex{pattern, re::ECMAScript | re::icase | re::optimize};
    for (auto const& file: files) {
        if (!std::regex_match(file.path, pat)) {
            continue;
        }
        result.files.push_back(file);
    }
    return result;
}

ManifestInfo ManifestInfo::filter_lang(std::string lang) const {
    auto result = ManifestInfo{};
    result.id = id;
    for (auto const& file: files) {
        bool ok = file.langs.size() == 0;
        for (auto const& try_lang: file.langs) {
            if (try_lang == lang) {
                ok = true;
                break;
            }
        }
        if (!ok) {
            continue;
        }
        result.files.push_back(file);
    }
    return result;
}

ManifestInfo ManifestInfo::from_manifest(Manifest const &manifest) {
    auto result = ManifestInfo {};
    result.id = manifest.id;
    auto const& body = manifest.body;
    auto dirs = std::unordered_map<DirID, Dir> {};
    for (auto const& dir: body.dirs) {
        dirs.insert_or_assign(dir.id, dir);
    }
    auto langs = std::unordered_map<LangID, Lang> {};
    for (auto const& lang: body.langs) {
        langs.insert_or_assign(lang.id, lang);
    }
    struct ChunkRef {
        Chunk chunk;
        BundleID bundle_id;
        uint32_t compressed_offset;
    };
    auto chunks = std::unordered_map<ChunkID, ChunkRef> {};
    for (auto const& bundle: body.bundles) {
        uint32_t compressed_offset = 0;
        for (auto const& chunk: bundle.chunks) {
            chunks.insert_or_assign(chunk.id, ChunkRef { chunk, bundle.id, compressed_offset });
            compressed_offset += chunk.compressed_size;
        }
    }
    for(auto const& file: body.files) {
        auto& file_info = result.files.emplace_back();
        file_info.id = file.id;
        file_info.size = file.size;
        file_info.link = file.link;
        file_info.permissions = file.permissions;
        file_info.params = ParamsInfo { body.params.at(file.params_index) };
        file_info.unk5 = file.unk5;
        file_info.unk6 = file.unk6;
        file_info.unk8 = file.unk8;
        file_info.unk10 = file.unk10;
        auto path = fs::path{file.name};
        auto parent_dir_id = file.parent_dir_id;
        while(parent_dir_id != DirID::None) {
            auto const& dir = dirs[parent_dir_id];
            if (dir.name.empty()) {
                break;
            }
            path = dir.name / path;
            parent_dir_id = dir.parent_dir_id;
        }
        file_info.path = path.generic_string();
        for(size_t i = 0; i != 64; i++) {
            if (!(file.locale_flags & (1 << i))) {
                continue;
            }
            auto const& lang = langs[(LangID)(i + 1)];
            if (lang.name.empty()) {
                continue;
            }
            file_info.langs.push_back(lang.name);
        }
        auto index_lookup = std::unordered_map<BundleID, size_t>();
        uint32_t uncompressed_offset = 0;
        for (auto chunk_id: file.chunk_ids) {
            auto const& ref = chunks[chunk_id];
            size_t bundle_idx;
            if (auto i = index_lookup.find(ref.bundle_id); i != index_lookup.end()) {
                bundle_idx = i->second;
            } else {
                file_info.bundles.emplace_back(BundleInfo { ref.bundle_id, {} });
                i = index_lookup.emplace_hint(i, std::pair { ref.bundle_id, index_lookup.size() });
                bundle_idx = i->second;
            }
            auto& bundle_info = file_info.bundles[bundle_idx];
            bundle_info.chunks.push_back(ChunkInfo {
                                             ref.chunk,
                                             ref.compressed_offset,
                                             uncompressed_offset
                                         });
            uncompressed_offset += ref.chunk.uncompressed_size;
        }
        for (auto& bundle: file_info.bundles) {
            std::sort(bundle.chunks.begin(), bundle.chunks.end(),
                      [](auto const& l, auto const& r){
                return l.compressed_offset < r.compressed_offset;
            });
        }
    }
    return result;
}

namespace rman {
    static void to_json(json &j, ChunkInfo const& chunk) {
        j = {
            { "id", to_hex(chunk.id) },
            { "compressed_size", chunk.compressed_size },
            { "uncompressed_size", chunk.uncompressed_size },
            { "compressed_offset", chunk.compressed_offset },
            { "uncompressed_offset", chunk.uncompressed_offset },
        };
    }

    static void to_json(json &j, BundleInfo const& bundle) {
        j = {
            { "id", to_hex(bundle.id) },
            { "chunks", bundle.chunks },
        };
    }

    static void to_json(json &j, ParamsInfo const& info) {
        j = {
            { "unk0", info.unk0 },
            { "unk1", info.unk1 },
            { "unk2", info.unk2 },
            { "unk3", info.unk3 },
            { "max_uncompressed_size", info.max_uncompressed_size },
        };
    }

    static void to_json(json &j, FileInfo const& file) {
        j = {
            { "id", to_hex(file.id) },
            { "path", file.path },
            { "size", file.size },
            { "langs", file.langs },
            { "bundles", file.bundles },
            { "link", file.link },
            { "params", file.params },
            { "permissions", file.permissions },
            { "unk5", file.unk5 },
            { "unk6", file.unk6 },
            { "unk8", file.unk8 },
            { "unk10", file.unk10 },
         };
    }

    static void to_json(json &j, ManifestInfo const& info) {
        j = {
            { "id", info.id },
            { "files", info.files },
        };
    }
}

std::string ManifestInfo::to_json() const {
    json j = *this;
    return j.dump(2);
}

std::string ManifestInfo::to_csv() const {
    auto result = std::string { R"(path,size,id,langs)" };
    for(auto const& file: files) {
        result += '\n';
        result += file.path;
        result += ',';
        result += std::to_string(file.size);
        result += ',';
        result += to_hex(file.id);
        result += ',';
        bool lang_first = true;
        for (auto const& lang: file.langs) {
            if (lang_first) {
                lang_first = false;
            } else {
                result += ';';
            }
            result += lang;
        }
        if (lang_first) {
            result += "all";
        }
    }
    return result;
}
