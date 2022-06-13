#include <argparse.hpp>
#include <cstdio>
#include <rlib/common.hpp>
#include <rlib/error.hpp>
#include <rlib/iofile.hpp>
#include <rlib/rbundle.hpp>
#include <rlib/rcache.hpp>

using namespace rlib;

struct Main {
    struct CLI {
        std::string output = {};
        std::vector<std::string> inputs = {};
        std::uint32_t buffer = {};
    } cli = {};

    auto parse_args(int argc, char** argv) -> void {
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description("Adds one or more bundles into first first bundle.");
        program.add_argument("output").help("Bundle file to write into.").required();
        program.add_argument("input").help("Bundle file or folder to write from.").remaining();
        program.add_argument("--buffer")
            .help("Size for buffer before flush to disk in killobytes [64, 1048576]")
            .default_value(std::uint32_t{32 * 1024 * 1024u})
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 64u, 1024u * 1024) * 1024u;
            });

        program.parse_args(argc, argv);

        cli.output = program.get<std::string>("output");
        cli.inputs = program.get<std::vector<std::string>>("input");
        cli.buffer = program.get<std::uint32_t>("--buffer");
    }

    auto run() {
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
        if (paths.empty()) {
            return;
        }
        auto output = RCache(RCache::Options{.path = cli.output, .readonly = false, .flush_size = cli.buffer});
        for (auto const& path : paths) {
            add_bundle(path, output);
        }
    }

    auto add_bundle(fs::path const& path, RCache& output) -> void {
        try {
            rlib_trace("path: %s\n", path.generic_string().c_str());
            printf("Start %s\n", path.filename().generic_string().c_str());
            auto infile = IOFile(path, false);
            auto bundle = RBUN::read(infile, true);
            printf(" ... ");
            for (std::uint64_t offset = 0; auto const& chunk : bundle.chunks) {
                rlib_assert(in_range(offset, chunk.compressed_size, bundle.toc_offset));
                auto src = infile.copy(offset, chunk.compressed_size);
                auto dst = try_zstd_decompress(src, chunk.uncompressed_size);
                rlib_assert(dst.size() == chunk.uncompressed_size);
                auto hash_type = RBUN::Chunk::hash_type(dst, chunk.chunkId);
                rlib_assert(hash_type != HashType::None);
                offset += chunk.compressed_size;
                output.add(chunk, src);
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
