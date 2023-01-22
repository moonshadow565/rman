#define FUSE_USE_VERSION 31
#include <errno.h>
#include <fuse3/fuse.h>
#include <fuse3/fuse_common.h>
#include <sys/stat.h>

#include <argparse.hpp>
#include <chrono>
#include <cinttypes>
#include <compare>
#include <cstring>
#include <ctime>
#include <rlib/common.hpp>
#include <rlib/iofile.hpp>
#include <rlib/rcache.hpp>
#include <rlib/rcdn.hpp>
#include <rlib/rdir.hpp>
#include <rlib/rfile.hpp>

#ifdef _WIN32
#    define S_IFLNK 0120000
#    define O_RDONLY 0
#    define O_WRONLY 1
#    define O_RDWR 2
#    define O_ACCMODE (O_RDONLY | O_WRONLY | O_RDWR)
#    define stat fuse_stat
#    define off_t fuse_off_t
#else
#    include <fcntl.h>
#endif

using namespace rlib;

struct Main {
    struct CLI {
        std::string output = {};
        RCache::Options cache = {};
        RCDN::Options cdn = {};
        std::vector<std::string> manifests = {};
        RFile::Match match = {};
        bool with_prefix = {};
    } cli = {};
    fuse_args fargs = {};
    std::unique_ptr<RCache> cache = {};
    std::unique_ptr<RCDN> cdn = {};
    std::unique_ptr<RDirEntry> root = {};

    auto parse_args(int argc, char **argv) -> void {
        argparse::ArgumentParser program(fs::path(argv[0]).filename().generic_string());
        program.add_description("Mounts manifests.");

        // Common options
        program.add_argument("output").help("output directory to mount in.").required();
        program.add_argument("manifests").help("Manifest files to read from.").remaining().required();

        program.add_argument("--fuse-debug").help("FUSE debug").default_value(false).implicit_value(true);

        program.add_argument("--with-prefix")
            .help("Prefix file paths with manifest name")
            .default_value(false)
            .implicit_value(true);

        // Filter options
        program.add_argument("-l", "--filter-lang")
            .help("Filter by language(none for international files) with regex match.")
            .default_value(std::optional<std::regex>{})
            .action([](std::string const &value) -> std::optional<std::regex> {
                if (value.empty()) {
                    return std::nullopt;
                } else {
                    return std::regex{value, std::regex::optimize | std::regex::icase};
                }
            });
        program.add_argument("-p", "--filter-path")
            .help("Filter by path with regex match.")
            .default_value(std::optional<std::regex>{})
            .action([](std::string const &value) -> std::optional<std::regex> {
                if (value.empty()) {
                    return std::nullopt;
                } else {
                    return std::regex{value, std::regex::optimize | std::regex::icase};
                }
            });

        // Cache options
        program.add_argument("--cache").help("Cache file path.").default_value(std::string{""});
        program.add_argument("--cache-readonly")
            .help("Do not write to cache.")
            .default_value(false)
            .implicit_value(true);
        program.add_argument("--cache-newonly")
            .help("Force create new part regardless of size.")
            .default_value(false)
            .implicit_value(true);
        program.add_argument("--cache-buffer")
            .help("Size for cache buffer in megabytes [1, 4096]")
            .default_value(std::uint32_t{32})
            .action([](std::string const &value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 1u, 4096u);
            });
        program.add_argument("--cache-limit")
            .help("Size for cache bundle limit in gigabytes [0, 4096]")
            .default_value(std::uint32_t{4})
            .action([](std::string const &value) -> std::uint32_t {
                return std::clamp((std::uint32_t)std::stoul(value), 0u, 4096u);
            });

        // CDN options
        program.add_argument("--cdn")
            .help("Source url to download files from.")
            .default_value(std::string("http://lol.secure.dyn.riotcdn.net/channels/public"));
        program.add_argument("--cdn-lowspeed-time")
            .help("Curl seconds that the transfer speed should be below.")
            .default_value(std::size_t{0})
            .action([](std::string const &value) -> std::size_t { return (std::size_t)std::stoul(value); });
        program.add_argument("--cdn-lowspeed-limit")
            .help("Curl average transfer speed in killobytes per second that the transfer should be above.")
            .default_value(std::size_t{64})
            .action([](std::string const &value) -> std::size_t { return (std::size_t)std::stoul(value); });
        program.add_argument("--cdn-verbose").help("Curl: verbose logging.").default_value(false).implicit_value(true);
        program.add_argument("--cdn-buffer")
            .help("Curl buffer size in killobytes [1, 512].")
            .default_value(long{512})
            .action(
                [](std::string const &value) -> long { return std::clamp((long)std::stoul(value), 1l, 512l) * 1024; });
        program.add_argument("--cdn-proxy").help("Curl: proxy.").default_value(std::string{});
        program.add_argument("--cdn-useragent").help("Curl: user agent string.").default_value(std::string{});
        program.add_argument("--cdn-cookiefile")
            .help("Curl cookie file or '-' to disable cookie engine.")
            .default_value(std::string{});
        program.add_argument("--cdn-cookielist").help("Curl: cookie list string.").default_value(std::string{});

        program.parse_args(argc, argv);

        cli.output = program.get<std::string>("output");
        cli.manifests = program.get<std::vector<std::string>>("manifests");
        cli.match.langs = program.get<std::optional<std::regex>>("--filter-lang");
        cli.match.path = program.get<std::optional<std::regex>>("--filter-path");
        cli.with_prefix = program.get<bool>("--with-prefix");

        cli.cache = {
            .path = program.get<std::string>("--cache"),
            .readonly = program.get<bool>("--cache-readonly"),
            .newonly = program.get<bool>("--cache-newonly"),
            .flush_size = program.get<std::uint32_t>("--cache-buffer") * MiB,
            .max_size = program.get<std::uint32_t>("--cache-limit") * GiB,
        };

        cli.cdn = {
            .url = clean_path(program.get<std::string>("--cdn")),
            .verbose = program.get<bool>("--cdn-verbose"),
            .buffer = program.get<long>("--cdn-buffer"),
            .proxy = program.get<std::string>("--cdn-proxy"),
            .useragent = program.get<std::string>("--cdn-useragent"),
            .cookiefile = program.get<std::string>("--cdn-cookiefile"),
            .cookielist = program.get<std::string>("--cdn-cookielist"),
            .low_speed_limit = program.get<std::size_t>("--cdn-lowspeed-limit") * KiB,
            .low_speed_time = program.get<std::size_t>("--cdn-lowspeed-time"),
        };

        [this](auto... args) mutable {
            fargs.argc = sizeof...(args);
            fargs.argv = (char **)calloc(sizeof...(args) + 1, sizeof(char *));
            auto i = 0;
            (void)((fargs.argv[i++] = strdup(args)), ...);
        }(argv[0],
          program.get<bool>("--fuse-debug") ? "-d" : "-f",
          "-o",
          "auto_unmount",
          "-o",
          "noforget",
#ifdef _WIN32
          "-o",
          "ThreadCount=16",
#endif
          cli.output.c_str());
    }

    auto run() -> void {
        root = std::make_unique<RDirEntry>();

        std::cerr << "Collecting input manifests ... " << std::endl;
        auto paths = collect_files(cli.manifests, {});

        std::cerr << "Parsing input manifests ... " << std::endl;
        auto builder = root->builder();
        for (auto const &p : paths) {
            auto const name = p.filename().replace_extension("").generic_string() + '/';
            auto const time_file = fs::last_write_time(p);
#ifdef _MSC_VER
            auto const time_sys = decltype(time_file)::clock::to_utc(time_file).time_since_epoch();
#else
            auto const time_sys = decltype(time_file)::clock::to_sys(time_file).time_since_epoch();
#endif
            auto const time_sec = std::chrono::duration_cast<std::chrono::seconds>(time_sys).count();
            auto bundle_status = RFile::read_file(p, [&, this](RFile &rfile) {
                if (this->cli.with_prefix) {
                    rfile.path.insert(rfile.path.begin(), name.begin(), name.end());
                }
                if (cli.match(rfile)) {
                    rfile.time = time_sec;
                    builder(rfile);
                }
                return true;
            });
            if (bundle_status != RFile::KNOWN_BUNDLE) {
                cli.cdn.url.clear();
            }
        }
        builder = nullptr;

        if (cli.cdn.url.empty()) {
            cli.cache.readonly = true;
        }

        if (!cli.cache.path.empty()) {
            std::cerr << "Parsing bundle ... " << std::endl;
            cache = std::make_unique<RCache>(cli.cache);
        }

        cdn = std::make_unique<RCDN>(cli.cdn, cache.get());

        std::cerr << "Mounted!" << std::endl;
    }
};

static Main main_ = {};

static auto find_chunks_in_range(std::span<RChunk::Dst const> chunks, std::size_t offset, std::size_t size) noexcept
    -> std::span<RChunk::Dst const> {
    auto start =
        std::upper_bound(chunks.begin(), chunks.end(), RChunk::Dst{{}, {}, offset}, [](auto const &l, auto const &r) {
            return l.uncompressed_offset + l.uncompressed_size < r.uncompressed_offset + r.uncompressed_size;
        });
    auto stop =
        std::lower_bound(start, chunks.end(), RChunk::Dst{{}, {}, offset + size}, [](auto const &l, auto const &r) {
            return l.uncompressed_offset < r.uncompressed_offset;
        });
    return std::span(start, stop);
}

static auto get_stats(RDirEntry const *entry, struct stat *stbuf) -> void {
    memset(stbuf, 0, sizeof(struct stat));
    stbuf->st_mode = 0444;
    if (entry->is_dir()) {
        stbuf->st_mode |= S_IFDIR;
    } else if (entry->is_link()) {
        stbuf->st_mode |= S_IFLNK;
    } else {
        stbuf->st_mode |= S_IFREG;
    }
    stbuf->st_nlink = entry->nlink();
    stbuf->st_size = entry->size();
    auto const time_sec = entry->time();
    stbuf->st_mtim.tv_sec = stbuf->st_ctim.tv_sec = time_sec;
}

static auto get_entry(const char *cpath, struct fuse_file_info const *fi) -> RDirEntry const * {
    if (fi && fi->fh) {
        return (RDirEntry const *)(void const *)(std::uintptr_t)fi->fh;
    }
    return cpath ? main_.root->find(cpath + 1) : nullptr;
}

static void *impl_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
    (void)conn;
    cfg->kernel_cache = 1;
    return NULL;
}

static int impl_getattr(const char *cpath, struct stat *stbuf, struct fuse_file_info *fi) {
    auto const entry = get_entry(cpath, fi);
    if (!entry) {
        return -ENOENT;
    }
    get_stats(entry, stbuf);
    return 0;
}

static int impl_opendir(const char *cpath, struct fuse_file_info *fi) {
    auto const entry = get_entry(cpath, fi);
    if (!entry) {
        return -ENOENT;
    }
    if (!entry->is_dir()) {
        return -ENOTDIR;
    }
    fi->fh = (std::uintptr_t)(void const *)entry;
    return 0;
}

static int impl_readlink(const char *cpath, char *buf, size_t size) {
    auto const entry = get_entry(cpath, nullptr);
    if (!entry) {
        return -ENOENT;
    }
    if (!entry->is_link()) {
        return -EINVAL;
    }
    auto n = std::min(entry->link().size() + 1, size);
    std::memcpy(buf, entry->link().data(), n);
    buf[n - 1] = '\0';
    return 0;
}

static int impl_readdir(const char *cpath,
                        void *buf,
                        fuse_fill_dir_t filler,
                        off_t offset,
                        struct fuse_file_info *fi,
                        enum fuse_readdir_flags flags) {
    auto const entry = get_entry(cpath, fi);
    if (!entry) {
        return -ENOENT;
    }
    if (!entry->is_dir()) {
        return -ENOTDIR;
    }
    auto children = entry->children();
    struct stat statbuf;
    for (auto i = offset; i < children.size(); ++i) {
        get_stats(&children[i], &statbuf);
        if (filler(buf, children[i].name().data(), &statbuf, i + 1, FUSE_FILL_DIR_PLUS)) {
            return 0;
        }
    }
    return 0;
}

static int impl_releasedir(const char *, struct fuse_file_info *) { return 0; }

static int impl_fsyncdir(const char *, int, struct fuse_file_info *) { return 0; }

static int impl_open(const char *cpath, struct fuse_file_info *fi) {
    auto const entry = get_entry(cpath, fi);
    if (!entry) {
        return -ENOENT;
    }
    if (entry->is_dir()) {
        return -EISDIR;
    }
    if ((fi->flags & O_ACCMODE) != O_RDONLY) {
        return -EROFS;
    }
    fi->keep_cache = 1;
    fi->fh = (std::uintptr_t)(void const *)entry;
    entry->open();
    return 0;
}

static int impl_read(const char *cpath, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    auto const entry = get_entry(cpath, fi);
    if (!entry) {
        return -ENOENT;
    }
    if (entry->is_dir()) {
        return -EISDIR;
    }
    auto real_size = entry->size();
    if (offset >= real_size) {
        return 0;
    }
    if (offset + size > real_size) {
        size = real_size - offset;
    }
    if (size == 0) {
        return 0;
    }
    thread_local struct Last {
        ChunkID id = {};
        Buffer buffer = {};
    } last = {};
    auto done = std::size_t{};
    try {
        rlib_trace("offset: 0x%llx, size: 0x%llx, done: %llx", offset, size, done);
        auto chunks = entry->chunks([](FileID fileId) { return main_.cache->get_chunks(fileId); });
        rlib_assert(chunks && !chunks->empty());
        for (RChunk::Dst const &chunk : find_chunks_in_range(*chunks, offset, size)) {
            rlib_trace("chunkId: %016llX, offset: 0x%llx, size: 0x%0llx",
                       chunk.chunkId,
                       chunk.uncompressed_offset,
                       chunk.uncompressed_size);
            if (fuse_interrupted()) {
                return -EINTR;
            }
            if (chunk.uncompressed_offset == offset + done && size - done >= chunk.uncompressed_size) {
                rlib_assert(main_.cdn->get_into(chunk, {buf + done, chunk.uncompressed_size}));
                done += chunk.uncompressed_size;
                continue;
            }
            if (last.id != chunk.chunkId) {
                fflush(stdout);
                last.id = ChunkID::None;
                rlib_assert(last.buffer.resize_destroy(chunk.uncompressed_size));
                rlib_assert(main_.cdn->get_into(chunk, last.buffer));
                last.id = chunk.chunkId;
            }
            auto src = std::span<char const>(last.buffer);
            if (auto const pos = (offset + done); pos > chunk.uncompressed_offset) {
                src = src.subspan(pos - chunk.uncompressed_offset);
            }
            if (auto const remain = size - done; remain < src.size()) {
                src = src.subspan(0, remain);
            }
            std::memcpy(buf + done, src.data(), src.size());
            done += src.size();
        }
        rlib_assert(done == size);
        return done;
    } catch (std::exception const &e) {
        std::cerr << e.what() << std::endl;
        for (auto const &error : error_stack()) {
            std::cerr << error << std::endl;
        }
        error_stack().clear();
        return -EAGAIN;
    }
}

static int impl_flush(const char *, struct fuse_file_info *) { return 0; }

static int impl_release(const char *cpath, struct fuse_file_info *fi) {
    auto const entry = get_entry(cpath, fi);
    if (!entry) {
        return -ENOENT;
    }
    entry->close();
    return 0;
}

static int impl_fsync(const char *, int, struct fuse_file_info *) { return 0; }

static const struct fuse_operations impl_oper = {
    .getattr = impl_getattr,
    .readlink = impl_readlink,

    .open = impl_open,
    .read = impl_read,
    .flush = impl_flush,
    .release = impl_release,
    .fsync = impl_fsync,

    .opendir = impl_opendir,
    .readdir = impl_readdir,
    .releasedir = impl_releasedir,
    .fsyncdir = impl_fsyncdir,

    .init = impl_init,
};

int main(int argc, char *argv[]) {
    try {
        main_.parse_args(argc, argv);
        main_.run();
    } catch (std::exception const &e) {
        std::cerr << e.what() << std::endl;
        for (auto const &error : error_stack()) {
            std::cerr << error << std::endl;
        }
        error_stack().clear();
        return EXIT_FAILURE;
    }
    return fuse_main(main_.fargs.argc, main_.fargs.argv, &impl_oper, NULL);
}
