#include <argparse.hpp>
#include <charconv>
#include <iostream>
#include <rlib/common.hpp>
#include <rlib/iofile.hpp>
#include <rlib/rbundle.hpp>
#include <unordered_set>

using namespace rlib;

struct Main {
    struct CLI {
        std::string output = {};
        std::vector<std::string> inputs = {};
        bool force = {};
        bool no_hash = {};
        bool no_progress = {};
    } cli = {};
    std::unordered_map<ChunkID, std::size_t> seen = {};

    auto parse_args(int argc, char** argv) -> void {
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description("Extracts one or more bundles.");
        program.add_argument("output").help("Directory to write chunks into.").required();
        program.add_argument("input").help("Bundle file(s) or folder(s) to read from.").remaining().required();

        program.add_argument("--force")
            .help("Force overwrite existing files.")
            .default_value(false)
            .implicit_value(true);
        program.add_argument("--no-hash").help("Do not verify hash.").default_value(false).implicit_value(true);
        program.add_argument("--no-progress")
            .help("Do not print progress to cerr.")
            .default_value(false)
            .implicit_value(true);

        program.parse_args(argc, argv);

        cli.force = program.get<bool>("--force");
        cli.no_hash = program.get<bool>("--no-hash");
        cli.no_progress = program.get<bool>("--no-progress");

        cli.output = program.get<std::string>("output");
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
        if (!paths.empty()) {
            std::cerr << "Processing existing chunks ... " << std::endl;
            fs::create_directories(cli.output);
            for (auto const& entry : fs::directory_iterator(cli.output)) {
                if (!entry.is_regular_file()) {
                    continue;
                }
                if (entry.path().extension() != ".chunk") {
                    continue;
                }
                auto name = entry.path().filename().replace_extension("").generic_string();
                if (auto id = from_hex<ChunkID>(name)) {
                    seen[*id] = entry.file_size();
                }
            }
        }
        std::cerr << "Processing input bundles ... " << std::endl;
        for (std::uint32_t index = paths.size(); auto const& path : paths) {
            verify_bundle(path, index--);
        }
    }

    auto verify_bundle(fs::path const& path, std::uint32_t index) -> void {
        try {
            rlib_trace("path: %s", path.generic_string().c_str());
            std::cout << "START:" << path.filename().generic_string() << std::endl;
            auto infile = IOFile(path, false);
            auto bundle = RBUN::read(infile, true);
            {
                std::uint64_t offset = 0;
                progress_bar p("EXTRACTED", cli.no_progress, index, offset, bundle.toc_offset);
                for (auto const& chunk : bundle.chunks) {
                    if (!seen.contains(chunk.chunkId)) {
                        auto src = infile.copy(offset, chunk.compressed_size);
                        auto dst = zstd_decompress(src, chunk.uncompressed_size);
                        if (!cli.no_hash) {
                            auto hash_type = RChunk::hash_type(dst, chunk.chunkId);
                            rlib_assert(hash_type != HashType::None);
                        }
                        auto outfile = IOFile(fs::path(cli.output) / (to_hex(chunk.chunkId) + ".chunk"), true);
                        outfile.resize(0, 0);
                        outfile.write(0, dst, true);
                        seen[chunk.chunkId] = dst.size();
                    }
                    offset += chunk.compressed_size;
                    p.update(offset);
                }
            }
            std::cout << "OK!" << std::endl;
        } catch (std::exception const& e) {
            std::cout << "FAIL!" << std::endl;
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
