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
    } cli = {};

    auto parse_args(int argc, char** argv) -> void {
        argparse::ArgumentParser program("rman-bl");
        program.add_argument("input").help("Bundle file or folder to read from.").remaining();

        program.parse_args(argc, argv);

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
            auto infile = IOFile(path, false);
            auto bundle = RBUN::read(infile, true);
            printf("Start(%s)\n", path.filename().generic_string().c_str());
            for (std::uint64_t offset = 0; auto const& chunk : bundle.chunks) {
                rlib_assert(in_range(offset, chunk.compressed_size, bundle.toc_offset));
                auto src = infile.copy(offset, chunk.compressed_size);
                auto dst = try_zstd_decompress(src, chunk.uncompressed_size);
                rlib_assert(dst.size() == chunk.uncompressed_size);
                auto hash_type = RBUN::Chunk::hash_type(dst, chunk.chunkId);
                rlib_assert(hash_type != HashType::None);
                offset += chunk.compressed_size;
            }
            printf("Ok(%s)\n", path.filename().generic_string().c_str());
        } catch (std::exception const& e) {
            printf("Failed(%s): %s\n", path.filename().generic_string().c_str(), e.what());
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
