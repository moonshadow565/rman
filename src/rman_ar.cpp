#include <common/xxhash.h>
#include <fmt/args.h>
#include <fmt/format.h>

#include <argparse.hpp>
#include <iostream>
#include <rlib/ar.hpp>
#include <rlib/common.hpp>
#include <rlib/iofile.hpp>
#include <rlib/rcache.hpp>
#include <rlib/rmanifest.hpp>

using namespace rlib;

struct Main {
    struct CLI {
        std::string rootfolder = {};
        std::vector<std::string> inputs = {};
        Ar ar = {};
    } cli = {};

    auto parse_args(int argc, char** argv) -> void {
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description("Cuts chunks of files.");
        program.add_argument("rootfolder").help("Root folder to rebase from.").required();
        program.add_argument("input")
            .help("Files or folders for manifest.")
            .remaining()
            .default_value(std::vector<std::string>{});

        program.add_argument("--ar-dyn")
            .help("Dynamic chunker provided from lua script.")
            .default_value(std::string{""});
        program.add_argument("--cdc")
            .help("Dumb chunking fallback algorithm " + Ar::PROCESSORS_LIST(true))
            .default_value(std::string{"fixed"});
        program.add_argument("--no-ar")
            .help("Regex of disable smart chunkers, can be any of: " + Ar::PROCESSORS_LIST())
            .default_value(std::string{});
        program.add_argument("--ar-strict")
            .help("Do not fallback to dumb chunking on ar errors.")
            .default_value(false)
            .implicit_value(true);
        program.add_argument("--ar-min")
            .default_value(std::uint32_t{4})
            .help("Smart chunking minimum size in killobytes [1, 4096].")
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 1u, 4096u);
            });
        program.add_argument("--chunk-size")
            .default_value(std::uint32_t{1024})
            .help("Chunk max size in killobytes [1, 8096].")
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 1u, 8096u);
            });

        program.parse_args(argc, argv);

        cli.rootfolder = program.get<std::string>("rootfolder");
        cli.inputs = program.get<std::vector<std::string>>("input");
        if (cli.inputs.empty() && !cli.rootfolder.empty()) {
            cli.inputs.push_back(cli.rootfolder);
        }

        cli.ar = Ar{
            .chunk_min = program.get<std::uint32_t>("--ar-min") * KiB,
            .chunk_max = program.get<std::uint32_t>("--chunk-size") * KiB,
            .disabled = Ar::PROCESSOR_PARSE(program.get<std::string>("--no-ar")),
            .cdc = Ar::PROCESSOR_PARSE(program.get<std::string>("--cdc"), true),
            .strict = program.get<bool>("--ar-strict"),
            .ardyn = make_ardyn(program.get<std::string>("--ar-dyn")),
        };
    }

    auto run() -> void {
        std::cerr << "Collecting input ... " << std::endl;
        auto paths = collect_files(
            cli.inputs,
            [](fs::path const& p) { return true; },
            true);

        std::cerr << "Processing input files ... " << std::endl;
        for (std::uint32_t index = paths.size(); auto const& path : paths) {
            add_file(path, index--);
        }
    }

    auto add_file(fs::path const& path, std::uint32_t index) -> void {
        std::cerr << "START: " << path << std::endl;
        auto infile = IO::MMap(path, IO::READ);
        cli.ar(infile, [&](Ar::Entry const& entry) {
            auto src = infile.copy(entry.offset, entry.size);
            auto id = RChunk::hash(src, HashType::RITO_HKDF);
            std::cout << fmt::format("\toffset={:x} size={:x} id={} high_entropy={}",
                                     entry.offset,
                                     entry.size,
                                     id,
                                     entry.high_entropy)
                      << std::endl;
        });
        if (!cli.ar.errors.empty()) {
            std::cerr << "Smart chunking failed for:\n";
            for (auto const& error : cli.ar.errors) {
                std::cerr << "\t" << error << "\n";
            }
            cli.ar.errors.clear();
            std::cerr << std::flush;
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
