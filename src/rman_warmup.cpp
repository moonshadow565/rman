#include <argparse.hpp>
#include <iostream>
#include <rlib/common.hpp>
#include <rlib/iofile.hpp>
#include <rlib/rcdn.hpp>
#include <rlib/rfile.hpp>
#include <rlib/rmanifest.hpp>
#include <execution>

using namespace rlib;

struct Main {
    struct CLI {
        std::vector<std::string> inputs = {};
        bool no_progress = {};
        size_t batch_size = {1};
        RCache::Options cache = {};
        RCache::Options mirror = {};
        RCDN::Options cdn = {};
        std::string migrate = {};
    } cli = {};
    std::unique_ptr<RCache> cache = {};
    std::unique_ptr<RCDN> cdn = {};
    std::unique_ptr<RCache> migrate = {};

    auto parse_args(int argc, char** argv) -> void {
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description("Warms up cache from manifests.");

        program.add_argument("--no-progress").help("Do not print progress.").default_value(false).implicit_value(true);

        // Cache options
        program.add_argument("--cache").help("Cache file path.").required();
        program.add_argument("--cache-readonly")
            .help("Do not write to cache.")
            .default_value(false)
            .implicit_value(true);
        program.add_argument("--cache-newonly")
            .help("Force create new part regardless of size.")
            .default_value(false)
            .implicit_value(true);
        program.add_argument("--cache-buffer")
            .help("Size for mirror buffer in megabytes [1, 4096]")
            .default_value(std::uint32_t{32})
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 1u, 4096u);
            });
        program.add_argument("--cache-limit")
            .help("Size for mirror bundle limit in gigabytes [0, 4096]")
            .default_value(std::uint32_t{4})
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 0u, 4096u);
            });

        // CDN options
        program.add_argument("--cdn")
            .help("Source url to download files from.")
            .default_value(std::string("http://lol.secure.dyn.riotcdn.net/channels/public"));
        program.add_argument("--cdn-lowspeed-time")
            .help("Curl seconds that the transfer speed should be below.")
            .default_value(std::size_t{0})
            .action([](std::string const& value) -> std::size_t { return (std::size_t)std::stoul(value); });
        program.add_argument("--cdn-lowspeed-limit")
            .help("Curl average transfer speed in killobytes per second that the transfer should be above.")
            .default_value(std::size_t{64})
            .action([](std::string const& value) -> std::size_t { return (std::size_t)std::stoul(value); });
        program.add_argument("--cdn-retry")
            .help("Number of retries to download from url.")
            .default_value(std::uint32_t{3})
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 0u, 8u);
            });
        program.add_argument("--cdn-workers")
            .default_value(std::uint32_t{32})
            .help("Number of connections per downloaded file.")
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 1u, 64u);
            });
        program.add_argument("--cdn-interval")
            .help("Curl poll interval in miliseconds.")
            .default_value(int{100})
            .action([](std::string const& value) -> int { return std::clamp((int)std::stoul(value), 0, 30000); });
        program.add_argument("--cdn-verbose").help("Curl: verbose logging.").default_value(false).implicit_value(true);
        program.add_argument("--cdn-buffer")
            .help("Curl buffer size in killobytes [1, 512].")
            .default_value(long{512})
            .action(
                [](std::string const& value) -> long { return std::clamp((long)std::stoul(value), 1l, 512l) * 1024; });
        program.add_argument("--cdn-proxy").help("Curl: proxy.").default_value(std::string{});
        program.add_argument("--cdn-useragent").help("Curl: user agent string.").default_value(std::string{});
        program.add_argument("--cdn-cookiefile")
            .help("Curl cookie file or '-' to disable cookie engine.")
            .default_value(std::string{});
        program.add_argument("--cdn-cookielist").help("Curl: cookie list string.").default_value(std::string{});

        program.add_argument("--batch-size")
            .help("Batch size to download before scanning more manifests.")
            .default_value(std::uint32_t{1'000'000})
            .action([](std::string const& value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), std::uint32_t{0u}, std::uint32_t{1u << 31});
            });

        // Migrated from.
        program.add_argument("--migrate").help("Old cache/bundle file path.").default_value(std::string(""));

        program.add_argument("input").help("Manifest file(s) or folder(s) to read from.").remaining().required();

        program.parse_args(argc, argv);

        cli.no_progress = program.get<bool>("--no-progress");

        cli.cache = {
            .path = program.get<std::string>("cache"),
            .readonly = program.get<bool>("--cache-readonly"),
            .newonly = program.get<bool>("--cache-newonly"),
            .flush_size = program.get<std::uint32_t>("--cache-buffer") * MiB,
            .max_size = program.get<std::uint32_t>("--cache-limit") * GiB,
        };

        cli.cdn = {
            .url = clean_path(program.get<std::string>("--cdn")),
            .verbose = program.get<bool>("--cdn-verbose"),
            .buffer = program.get<long>("--cdn-buffer"),
            .interval = program.get<int>("--cdn-interval"),
            .retry = program.get<std::uint32_t>("--cdn-retry"),
            .workers = program.get<std::uint32_t>("--cdn-workers"),
            .proxy = program.get<std::string>("--cdn-proxy"),
            .useragent = program.get<std::string>("--cdn-useragent"),
            .cookiefile = program.get<std::string>("--cdn-cookiefile"),
            .cookielist = program.get<std::string>("--cdn-cookielist"),
            .low_speed_limit = program.get<std::size_t>("--cdn-lowspeed-limit") * KiB,
            .low_speed_time = program.get<std::size_t>("--cdn-lowspeed-time"),
        };

        cli.batch_size = program.get<std::uint32_t>("--batch-size");

        cli.migrate = program.get<std::string>("--migrate");

        cli.inputs = program.get<std::vector<std::string>>("input");
    }

    auto run() -> void {
        cache = std::make_unique<RCache>(cli.cache);
        cdn = std::make_unique<RCDN>(cli.cdn, cache.get());
        if (!cli.migrate.empty()) {
            migrate = std::make_unique<RCache>(RCache::Options{
                .path = cli.migrate,
                .readonly = true,
                .newonly = true,
                .flush_size = 32 * MiB,
                .max_size = 4 * GiB,
            });
        }

        std::vector<RChunk::Dst> queued;
        size_t index = 0;
        {
            auto paths =
                rlib::collect_files(cli.inputs, [](fs::path const& p) { return p.extension() == ".manifest"; });
            index = paths.size();
            progress_bar p("COLLECT", cli.no_progress, index, 0, index * MiB);

            std::unordered_map<ChunkID, RChunk::Src> collected;
            size_t done = 0;
            if (cli.batch_size == 0) {
                std::mutex m;
                std::for_each(std::execution::par_unseq, paths.begin(), paths.end(), [&](const auto& path) {
                    rlib_trace("Manifest file: %s", path.generic_string().c_str());
                    auto chunks = RMAN::read_chunks_file(path);
                    chunks = cache->missing(std::move(chunks));

                    {
                        std::lock_guard lock_guard(m);
                        collected.merge(std::move(chunks));
                        p.update(++done * MiB);
                    }
                });
            } else {
                for (auto const& path : paths) {
                    rlib_trace("Manifest file: %s", path.generic_string().c_str());
                    auto chunks = RMAN::read_chunks_file(path);

                    chunks = cache->missing(std::move(chunks));
                    collected.merge(std::move(chunks));

                    p.update(++done * MiB);
                    if (collected.size() > cli.batch_size) {
                        transform(collected, queued);
                        download(queued, index);
                    }
                    --index;
                }
            }

            transform(collected, queued);
        }
        download(queued, index);

        std::cout << "ALL DONE!" << std::endl;
    }

private:
    void download(std::vector<RChunk::Dst>& queued, size_t index = 0) {
        if (queued.empty()) return;

        size_t total = 0;
        size_t done = 0;
        for (const auto& q : queued) total += q.compressed_size;

        if (migrate) {
            progress_bar p("MIGRATE", cli.no_progress, index, done, total);
            queued = migrate->get(
                std::move(queued),
                [&](RChunk::Dst const& c, std::span<char const> data) {
                    cache->add(c, data);
                    done += c.compressed_size;
                    p.update(done);
                },
                true);
        }

        if (!queued.empty()) {
            progress_bar p("DOWNLOAD", cli.no_progress, index, done, total);
            queued = cdn->get(std::move(queued), [&](RChunk::Dst const& c, std::span<char const>) {
                done += c.compressed_size;
                p.update(done);
            });
        }

        if (!queued.empty()) {
            std::cout << "FAIL " << queued.size() << std::endl;
        }
    }

    void transform(std::unordered_map<ChunkID, RChunk::Src>& collected, std::vector<RChunk::Dst>& queued) {
        queued.clear();
        queued.resize(collected.size());
        for (size_t i = 0; const auto& [key, value] : collected) {
            const auto index = i++;
            queued[index] = RChunk::Dst{value, HashType::None, index};
        }
        sort_by<&RChunk::Src::bundleId, &RChunk::Dst::compressed_offset, &RChunk::Dst::uncompressed_offset>(
            queued.begin(),
            queued.end());
        collected.clear();
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
