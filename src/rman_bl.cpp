#include <argparse.hpp>
#include <cstdio>
#include <rlib/error.hpp>
#include <rlib/iofile.hpp>
#include <rlib/rmanifest.hpp>

using namespace rlib;

struct Main {
    struct CLI {
        std::string manifest = {};
        RMAN::Filter filter = {};
    } cli = {};

    auto parse_args(int argc, char** argv) -> void {
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description("Lists bundle names used in manifest.");
        program.add_argument("manifest").help("Manifest file to read from.").required();

        program.parse_args(argc, argv);

        cli.manifest = program.get<std::string>("manifest");
    }

    auto run() -> void {
        rlib_trace("Manifest file: %s", cli.manifest.c_str());
        auto infile = IOFile(cli.manifest, false);
        auto manifest = RMAN::read(infile.copy(0, infile.size()));

        for (auto const& bundle : manifest.bundles) {
            printf("/%016llX.bundle\n", (unsigned long long)bundle.bundleId);
        }
    }
};

int main(int argc, char** argv) {
    auto main = Main{};
    try {
        main.parse_args(argc, argv);
        main.run();
    } catch (std::exception const& e) {
        std::cerr << e.what() << std::endl;
        for (auto const& error : error_stack()) {
            std::cerr << error << std::endl;
        }
        error_stack().clear();
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
