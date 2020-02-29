#include <cstdio>
#include <manifest.hpp>
#include <manifest_info.hpp>

using namespace rman;

int main(int argc, char** argv) {
#ifdef NDEBUG
    std::string input = argc > 1 ? argv[1] : "";
#else
    std::string input = argc > 1 ? argv[1] : "manifest.manifest";
#endif
    std::string filter = argc > 2 ? argv[2] : "";
    std::string lang = argc > 3 ? argv[3] : "en_US";
    try {
        if (input.empty()) {
            throw std::runtime_error("./rman2json <manifest file>"
                                     "<optional: regex filter, default empty> "
                                     "<optional lang filter, default: en_US> ");
        }
        auto manifest = Manifest::read(input.c_str());
        auto info = ManifestInfo::from_manifest(manifest);
        if (!filter.empty()) {
            info = info.filter_path(filter);
        }
        if (!lang.empty()) {
            info = info.filter_lang(lang);
        }
        puts(info.to_json().c_str());
    } catch (std::exception const& error) {
        fputs(error.what(), stderr);
        return 1;
    }
    return 0;
}
