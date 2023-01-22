#include <fmt/args.h>
#include <fmt/format.h>

#include <argparse.hpp>
#include <iostream>
#include <rlib/common.hpp>
#include <rlib/iofile.hpp>
#include <rlib/rcache.hpp>
#include <rlib/rfile.hpp>

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
        std::cerr << "Processing files..." << std::endl;
        RFile::read_file(cli.inmanifest, [&, this](RFile& rfile) -> bool {
            std::cout << "Processing: " << rfile.path << std::endl;
            if (verify_file(rfile, inbundle)) {
                std::cout << "OK!" << std::endl;
                return true;
            }
            return false;
        });
    }

    auto verify_file(RFile const& file, RCache const& provider) const -> bool {
        if (file.size == 0 || !file.link.empty()) {
            return true;
        } else if (file.chunks) {
            return verify_file_chunks(*file.chunks, provider);
        } else {
            auto chunks = provider.get_chunks(file.fileId);
            if (chunks.empty()) {
                std::cout << fmt::format("Error: missing chunks: {}", file.fileId) << std::endl;
                return false;
            }
            return verify_file_chunks(chunks, provider);
        }
    }

    auto verify_file_chunks(std::vector<RChunk::Dst> const& chunks, RCache const& provider) const -> bool {
        for (auto const& chunk : chunks) {
            if (!provider.contains(chunk.chunkId)) {
                std::cout << fmt::format("Error: missing chunk: {}", chunk.chunkId) << std::endl;
                return false;
            }
        }
        return true;
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
