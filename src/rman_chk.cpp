#include <fmt/args.h>
#include <fmt/format.h>

#include <argparse.hpp>
#include <iostream>
#include <rlib/common.hpp>
#include <rlib/iofile.hpp>
#include <rlib/rcache.hpp>
#include <rlib/rmanifest.hpp>

using namespace rlib;

struct Main {
    struct CLI {
        std::string inmanifest = {};
        std::string inbundle = {};
    } cli = {};

    auto parse_args(int argc, char** argv) -> void {
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description("Splits JRMAN .");
        program.add_argument("inmanifest").help("Manifest to read from.").required();
        program.add_argument("inbundle").help("Source bundle to read from.").required();
        program.parse_args(argc, argv);

        cli.inmanifest = program.get<std::string>("inmanifest");
        cli.inbundle = program.get<std::string>("inbundle");
    }

    auto run() -> void {
        std::cerr << "Processing input bundle ... " << std::endl;
        auto inbundle = RCache({.path = cli.inbundle, .readonly = true});

        rlib_trace("Manifest file: %s", cli.inmanifest.c_str());
        std::cerr << "Reading input manifest ... " << std::endl;
        auto manifest = RMAN::read_file(cli.inmanifest);

        std::cerr << "Processing files..." << std::endl;
        auto count = manifest.files.size();
        for (auto const& file : manifest.files) {
            std::cout << "Processing #" << count << ": " << file.path << std::endl;
            verify_file_fast(file, inbundle);
            --count;
        }
    }

    auto verify_file_fast(RMAN::File const& file, RCache const& provider) const -> void {
        for (auto const& chunk : file.chunks) {
            if (!provider.contains(chunk.chunkId)) {
                std::cout << fmt::format("Error: missing chunk: {}", chunk.chunkId) << std::endl;
                return;
            }
        }
        std::cout << "OK!" << std::endl;
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
