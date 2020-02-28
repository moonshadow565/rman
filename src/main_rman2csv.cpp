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
    try {
        if (input.empty()) {
            throw std::runtime_error("./rman2csv <manifest file>");
        }
        auto manifest = Manifest::read(input.c_str());
        auto info = ManifestInfo::from_manifest(manifest);
        puts(info.to_csv().c_str());
    } catch (std::exception const& error) {
        fputs(error.what(), stderr);
        return 1;
    }
    return 0;
}
