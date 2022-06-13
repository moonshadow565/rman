#include <argparse.hpp>
#include <iostream>
#include <rlib/common.hpp>
#include <rlib/iofile.hpp>
#include <rlib/rbundle.hpp>
#include <unordered_map>
#include <unordered_set>

using namespace rlib;

struct Main {
    struct CLI {
        std::vector<std::string> inputs = {};
    } cli = {};
    struct Usage {
        std::size_t count;
        std::size_t size_uncompressed;
        std::map<std::size_t, std::size_t> count_per_size_compressed;
    };
    std::unordered_map<ChunkID, Usage> usage_per_chunk = {};

    auto parse_args(int argc, char** argv) -> void {
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description("Collects size usage statistics on one or more bundle.");
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
            process_bundle(path);
        }
        std::cerr << "Callculating usage ... " << std::endl;
        std::size_t total_count_all = 0;
        std::size_t total_count_uncompressed_uniq = 0;
        std::size_t total_count_compressed_uniq = 0;
        std::size_t total_size_uncompressed = 0;
        std::size_t total_size_uncompressed_uniq = 0;
        std::size_t total_size_compressed = 0;
        std::size_t total_size_compressed_uniq = 0;
        std::size_t total_size_compressed_min = 0;
        std::size_t total_size_compressed_min_uniq = 0;
        std::size_t total_size_compressed_max = 0;
        std::size_t total_size_compressed_max_uniq = 0;

        for (auto const& [id, usage] : usage_per_chunk) {
            total_count_all += usage.count;
            total_size_uncompressed += usage.count * usage.size_uncompressed;

            total_count_uncompressed_uniq += 1;
            total_size_uncompressed_uniq = usage.size_uncompressed;

            for (auto [size_compressed, count] : usage.count_per_size_compressed) {
                total_size_compressed += count * size_compressed;

                total_count_compressed_uniq += 1;
                total_size_compressed_uniq += size_compressed;
            }

            {
                auto [size_compressed, count] = *usage.count_per_size_compressed.begin();
                total_size_compressed_min += usage.count * size_compressed;
                total_size_compressed_min_uniq += size_compressed;
            }

            {
                auto [size_compressed, count] = *usage.count_per_size_compressed.begin();
                total_size_compressed_max += usage.count * size_compressed;
                total_size_compressed_max_uniq += size_compressed;
            }
        }
        std::cout << "count_all = " << total_count_all << std::endl;
        std::cout << "count_uncompressed_uniq = " << total_count_uncompressed_uniq << std::endl;
        std::cout << "count_compressed_uniq = " << total_count_compressed_uniq << std::endl;
        std::cout << "size_uncompressed = " << total_size_uncompressed << std::endl;
        std::cout << "size_uncompressed_uniq = " << total_size_uncompressed_uniq << std::endl;
        std::cout << "size_compressed = " << total_size_compressed << std::endl;
        std::cout << "size_compressed_uniq = " << total_size_compressed_uniq << std::endl;
        std::cout << "size_compressed_min = " << total_size_compressed_min << std::endl;
        std::cout << "size_compressed_min_uniq = " << total_size_compressed_min_uniq << std::endl;
        std::cout << "size_compressed_max = " << total_size_compressed_max << std::endl;
        std::cout << "size_compressed_max_uniq = " << total_size_compressed_max_uniq << std::endl;
    }

    auto process_bundle(fs::path const& path) noexcept -> void {
        try {
            rlib_trace("path: %s", path.generic_string().c_str());
            auto infile = IOFile(path, false);
            auto bundle = RBUN::read(infile, true);
            for (std::uint64_t offset = 0; auto const& chunk : bundle.chunks) {
                auto& usage = usage_per_chunk[chunk.chunkId];
                usage.count += 1;
                usage.size_uncompressed = chunk.uncompressed_size;
                usage.count_per_size_compressed[chunk.compressed_size] += 1;
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
