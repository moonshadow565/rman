#include <argparse.hpp>
#include <iostream>
#include <rlib/common.hpp>
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
        program.add_argument("input").help("Bundle file(s) or folder(s) to read from.").remaining().required();

        program.parse_args(argc, argv);

        cli.inputs = program.get<std::vector<std::string>>("input");
    }

    auto run() -> void {
        auto paths = std::vector<fs::path>();
        std::cerr << "Collecting input bundles ... " << std::endl;
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
        std::cerr << "Processing input bundles ... " << std::endl;
        for (auto const& path : paths) {
            list_bundle(path);
        }
    }

    auto list_bundle(fs::path const& path) noexcept -> void {
        try {
            rlib_trace("path: %s", path.generic_string().c_str());
            auto infile = IOFile(path, true);
            auto bundle = RBUN::read(infile);
            for (std::uint64_t offset = 0; auto const& chunk : bundle.chunks) {
                if (!in_range(offset, chunk.compressed_size, bundle.toc_offset)) break;
                std::cout                                    //
                    << to_hex(bundle.bundleId) << ','        //
                    << to_hex(chunk.chunkId) << ','          //
                    << chunk.compressed_size << ','          //
                    << chunk.uncompressed_size << std::endl  //
                    ;
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
