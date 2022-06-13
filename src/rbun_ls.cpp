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
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description(
            "Lists contents of one or more bundles."
            "\n"
            "Output is in CSV format as follows:\n"
            "BundlID,ChunkID,SizeCompressed,SizeUncompressed");
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
            list_bundle(path);
        }
    }

    auto list_bundle(fs::path const& path) noexcept -> void {
        try {
            rlib_trace("path: %s\n", path.generic_string().c_str());
            auto infile = IOFile(path, true);
            auto bundle = RBUN::read(infile);
            for (std::uint64_t offset = 0; auto const& chunk : bundle.chunks) {
                if (!in_range(offset, chunk.compressed_size, bundle.toc_offset)) break;
                printf("%016llx,%016llX,%llu,%llu\n",
                       (unsigned long long)bundle.bundleId,
                       (unsigned long long)chunk.chunkId,
                       (unsigned long long)chunk.compressed_size,
                       (unsigned long long)chunk.uncompressed_size);
                offset += chunk.compressed_size;
            }
        } catch (std::exception const& e) {
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
