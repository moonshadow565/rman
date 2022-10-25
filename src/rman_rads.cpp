#include <fmt/args.h>
#include <fmt/format.h>

#include <argparse.hpp>
#include <iostream>
#include <rlib/common.hpp>
#include <rlib/iofile.hpp>
#include <rlib/rads_rls.hpp>
#include <rlib/rads_sln.hpp>
#include <rlib/rcache.hpp>
#include <rlib/rmanifest.hpp>

using namespace rlib;

struct Main {
    struct CLI {
        std::string outmanifest = {};
        std::string inbundle = {};
        std::string inmanifest = {};
        std::string inrelease = {};
        bool no_progress = {};
    } cli = {};

    auto parse_args(int argc, char** argv) -> void {
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description("Splits JRMAN .");
        program.add_argument("outmanifest").help("Manifest to write into.").required();
        program.add_argument("inmanifest").help("Manifest to read from.").required();
        program.add_argument("inbundle").help("Source bundle to read from.").required();
        program.add_argument("inrelease").help("Project or solution path inside bundle.").required();
        program.parse_args(argc, argv);

        cli.outmanifest = program.get<std::string>("outmanifest");
        cli.inbundle = program.get<std::string>("inbundle");
        cli.inmanifest = program.get<std::string>("inmanifest");
        cli.inrelease = program.get<std::string>("inrelease");
    }

    auto run() -> void {
        std::cerr << "Processing input bundle ... " << std::endl;
        auto inbundle = RCache({.path = cli.inbundle, .readonly = true});

        rlib_trace("Manifest file: %s", cli.inmanifest.c_str());
        std::cerr << "Reading input manifest ... " << std::endl;
        auto manifest = RMAN::read_file(cli.inmanifest);

        std::cerr << "Indexing input manifest ... " << std::endl;
        auto lookup = manifest.lookup();

        std::cerr << "Create output manifest..." << std::endl;
        auto outfile = IO::File(cli.outmanifest, IO::WRITE);
        outfile.resize(0, 0);
        outfile.write(0, {"JRMAN\n", 6});

        std::cerr << "Processing release..." << std::endl;
        process(cli.inrelease, lookup, inbundle, [&](RMAN::File&& file) {
            auto outjson = file.dump();
            outfile.write(outfile.size(), outjson);
        });
    }

    auto find_file(std::string path, auto const& lookup) const noexcept -> RMAN::File const* {
        std::transform(path.begin(), path.end(), path.begin(), ::tolower);
        if (auto i = lookup.find(path); i != lookup.end()) {
            return i->second;
        }
        return nullptr;
    }

    auto read_file(std::string const& path, auto const& lookup, auto const& provider) const -> std::vector<char> {
        auto file = find_file(path, lookup);
        rlib_assert(file);
        auto result = std::vector<char>(file->size);
        auto bad_chunks = provider.get(file->chunks, [&result](RChunk::Dst const& chunk, std::span<char const> data) {
            std::memcpy(result.data() + chunk.uncompressed_offset, data.data(), data.size());
        });
        rlib_assert(bad_chunks.empty());
        return result;
    }

    auto process(auto const& path, auto const& lookup, auto const& provider, auto&& cb) const -> void {
        if (path.find("projects/") != std::string::npos) {
            return process_rls(path, lookup, provider, cb);
        }
        if (path.find("solutions/") != std::string::npos) {
            return process_sln(path, lookup, provider, cb);
        }
        rlib_assert(!"inversion must contain projects/ or solutions/");
    }

    auto process_file(auto const& path, auto const& lookup, auto const& provider, auto&& cb) const -> void {
        try {
            rlib_trace("path: %s", path.c_str());
            auto file = find_file(path, lookup);
            rlib_assert(file);
            cb(RMAN::File(*file));
        } catch (std::exception const& e) {
            std::cout << e.what() << std::endl;
            for (auto const& error : error_stack()) {
                std::cout << error << std::endl;
            }
            error_stack().clear();
        }
    }

    auto process_rls(auto const& path, auto const& lookup, auto const& provider, auto&& cb) const -> void {
        try {
            rlib_trace("path: %s", path.c_str());
            auto data = read_file(path, lookup, provider);
            auto [realm, rest] = str_split(path, "projects/");
            auto rls = rads::RLS::read(data);
            std::cerr << "START RLS: " << rls.name << " " << rls.version << std::endl;
            for (auto const& f : rls.files) {
                process_file(fmt::format("{}projects/{}/releases/{}/files/{}", realm, rls.name, f.version, f.name),
                             lookup,
                             provider,
                             [&](RMAN::File&& rfile) {
                                 rfile.path = f.name;
                                 cb(std::move(rfile));
                             });
            }
        } catch (std::exception const& e) {
            std::cout << e.what() << std::endl;
            for (auto const& error : error_stack()) {
                std::cout << error << std::endl;
            }
            error_stack().clear();
        }
    }

    auto process_sln(auto const& path, auto const& lookup, auto const& provider, auto&& cb) const -> void {
        try {
            rlib_trace("path: %s", path.c_str());
            auto data = read_file(path, lookup, provider);
            auto [realm, rest] = str_split(path, "solutions/");
            auto sln = rads::SLN::read(data);
            std::cerr << "START SLN: " << sln.name << " " << sln.version << std::endl;
            for (auto const& rls : sln.projects) {
                process_rls(fmt::format("{}projects/{}/releases/{}/releasemanifest", realm, rls.name, rls.version),
                            lookup,
                            provider,
                            [&](RMAN::File&& rfile) {
                                rfile.langs = rls.langs;
                                cb(std::move(rfile));
                            });
            }
        } catch (std::exception const& e) {
            std::cout << e.what() << std::endl;
            for (auto const& error : error_stack()) {
                std::cout << error << std::endl;
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
