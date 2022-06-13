#include <argparse.hpp>
#include <cstdio>
#include <rlib/common.hpp>
#include <rlib/error.hpp>
#include <rlib/iofile.hpp>
#include <rlib/rbundle.hpp>

using namespace rlib;

struct Main {
    struct CLI {
        std::vector<std::string> inputs = {};
        bool no_hash = {};
        bool no_extract = {};
    } cli = {};

    auto parse_args(int argc, char** argv) -> void {
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description("Checks one or more bundles for errors.");
        program.add_argument("input").help("Bundle file or folder to read from.").remaining();

        program.add_argument("--no-extract")
            .help("Do not even attempt to extract chunk.")
            .default_value(false)
            .implicit_value(true);
        program.add_argument("--no-hash").help("Do not verify hash.").default_value(false).implicit_value(true);

        program.parse_args(argc, argv);

        cli.no_hash = program.get<bool>("--no-extract");
        cli.no_extract = program.get<bool>("--no-hash");

        cli.inputs = program.get<std::vector<std::string>>("input");
    }

    auto run() -> void {
        auto paths = std::vector<fs::path>();
        for (auto const& input : cli.inputs) {
            rlib_assert(fs::exists(input));
            if (fs::is_regular_file(input)) {
                paths.push_back(input);
            } else {
                for (auto const& entry : fs::directory_iterator(input)) {
                    if (!entry.is_regular_file()) {
                        continue;
                    }
                    if (entry.path().extension() != ".bundle") {
                        continue;
                    }
                    paths.push_back(entry.path());
                }
            }
        }
        for (auto const& path : paths) {
            verify_bundle(path);
        }
    }

    auto verify_bundle(fs::path const& path) -> void {
        try {
            rlib_trace("path: %s\n", path.generic_string().c_str());
            printf("Start %s\n", path.filename().generic_string().c_str());
            auto infile = IOFile(path, false);
            auto bundle = RBUN::read(infile, true);
            printf(" ... ");
            for (std::uint64_t offset = 0; auto const& chunk : bundle.chunks) {
                rlib_assert(in_range(offset, chunk.compressed_size, bundle.toc_offset));
                if (!cli.no_extract) {
                    auto src = infile.copy(offset, chunk.compressed_size);
                    auto dst = try_zstd_decompress(src, chunk.uncompressed_size);
                    rlib_assert(dst.size() == chunk.uncompressed_size);
                    if (!cli.no_hash) {
                        auto hash_type = RBUN::Chunk::hash_type(dst, chunk.chunkId);
                        rlib_assert(hash_type != HashType::None);
                    }
                }
                offset += chunk.compressed_size;
            }
            printf("Ok!\n");
        } catch (std::exception const& e) {
            printf("Failed!\n");
            std::cerr << e.what() << std::endl;
            for (auto const& error : error_stack()) {
                std::cerr << error << std::endl;
            }
            error_stack().clear();
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
