#include <sol/sol.hpp>

#include "ar.hpp"

using namespace rlib;

struct rlib::ArDyn {
    sol::state lua;
    sol::function chunker;
};

auto ArDynDeleter::operator()(ArDyn *ptr) noexcept -> void { delete ptr; }

struct IOReader {
    IO::Reader reader;

    auto start() const noexcept -> std::size_t { return reader.start(); }

    auto offset() const noexcept -> std::size_t { return reader.offset(); }

    auto absolute() const noexcept -> std::size_t { return reader.start() + reader.offset(); }

    auto size() const noexcept -> std::size_t { return reader.size(); }

    auto remains() const noexcept -> std::size_t { return reader.remains(); }

    auto contains(std::size_t pos, std::size_t count) const noexcept -> bool { return reader.contains(pos, count); }

    auto skip(std::size_t size) noexcept -> bool { return reader.skip(size); }

    auto seek(std::size_t pos) noexcept -> bool { return reader.seek(pos); }

    auto subreader(size_t n) noexcept -> std::optional<IOReader> {
        if (IOReader tmp; reader.read_within(tmp.reader, n)) {
            return tmp;
        }
        return std::nullopt;
    }

    template <typename CharT>
    auto read_nstr(size_t n) noexcept -> std::optional<std::basic_string<CharT>> {
        if (reader.remains() / sizeof(CharT) < n) {
            return std::nullopt;
        }
        if (std::basic_string<CharT> tmp(n, '\0'); reader.read_raw(tmp.data(), n * sizeof(CharT))) {
            return tmp;
        }
        return std::nullopt;
    };

    template <typename CharT>
    auto read_zstr() noexcept -> std::optional<std::basic_string<CharT>> {
        std::basic_string<CharT> tmp;
        while (auto c = read_le<CharT>()) {
            if (!*c) return tmp;
            tmp.push_back(*c);
        }
        return std::nullopt;
    };

    template <typename T>
        requires(std::is_trivially_copyable_v<T>)
    auto read_le() noexcept -> std::optional<T> {
        if (T result; reader.read_raw(&result, sizeof(result))) {
            return result;
        }
        return std::nullopt;
    }

    template <typename T>
        requires(std::is_trivially_copyable_v<T>)
    auto read_be() noexcept -> std::optional<T> {
        if (uint8_t result[sizeof(T)]; reader.read_raw(&result, sizeof(result))) {
            std::reverse(std::begin(result), std::end(result));
            return std::bit_cast<T>(result);
        }
        return std::nullopt;
    }
};

auto rlib::make_ardyn(fs::path const &init) -> std::unique_ptr<ArDyn, ArDynDeleter> {
    if (init.empty() || !fs::exists(init) || !fs::is_regular_file(init)) {
        return nullptr;
    }

    auto ardyn = std::unique_ptr<ArDyn, ArDynDeleter>(new ArDyn{});

    // open everything, sanbdoxing is not a goal
    ardyn->lua.open_libraries(sol::lib::base,
                              sol::lib::package,
                              sol::lib::coroutine,
                              sol::lib::os,
                              sol::lib::math,
                              sol::lib::table,
                              sol::lib::debug,
                              sol::lib::bit32);

#define member(...) __VA_ARGS__ /* this macro makes clang-format nicer */
    ardyn->lua.new_usertype<Ar::Entry>("Entry",
                                       sol::constructors<Ar::Entry(size_t, size_t),
                                                         Ar::Entry(size_t, size_t, bool),
                                                         Ar::Entry(size_t, size_t, bool, bool)>(),
                                       member("offset", &Ar::Entry::offset),
                                       member("size", &Ar::Entry::size),
                                       member("high_entropy", &Ar::Entry::high_entropy),
                                       member("nest", &Ar::Entry::nest));

    ardyn->lua.new_usertype<IOReader>("IOReader",
                                      member("start", &IOReader::start),
                                      member("offset", &IOReader::offset),
                                      member("absolute", &IOReader::absolute),
                                      member("size", &IOReader::size),
                                      member("remains", &IOReader::remains),
                                      member("contains", &IOReader::contains),
                                      member("skip", &IOReader::skip),
                                      member("seek", &IOReader::seek),
                                      member("subreader", &IOReader::subreader),
                                      member("read_nstr", &IOReader::read_nstr<char>),
                                      member("read_zstr", &IOReader::read_zstr<char>),
                                      member("read_s8", &IOReader::read_le<int8_t>),
                                      member("read_u8", &IOReader::read_le<uint8_t>),
                                      member("read_s16", &IOReader::read_le<int16_t>),
                                      member("read_u16", &IOReader::read_le<uint16_t>),
                                      member("read_s32", &IOReader::read_le<int32_t>),
                                      member("read_u32", &IOReader::read_le<uint32_t>),
                                      member("read_s64", &IOReader::read_le<int64_t>),
                                      member("read_u64", &IOReader::read_le<uint64_t>),
                                      member("read_f32", &IOReader::read_le<float>),
                                      member("read_f64", &IOReader::read_le<double>),
                                      member("read_s8_be", &IOReader::read_be<int8_t>),
                                      member("read_u8_be", &IOReader::read_be<uint8_t>),
                                      member("read_s16_be", &IOReader::read_be<int16_t>),
                                      member("read_u16_be", &IOReader::read_be<uint16_t>),
                                      member("read_s32_be", &IOReader::read_be<int32_t>),
                                      member("read_u32_be", &IOReader::read_be<uint32_t>),
                                      member("read_s64_be", &IOReader::read_be<int64_t>),
                                      member("read_u64_be", &IOReader::read_be<uint64_t>),
                                      member("read_f32_be", &IOReader::read_be<float>),
                                      member("read_f64_be", &IOReader::read_be<double>));
#undef member

    ardyn->lua.safe_script_file(init.generic_string(), &sol::script_throw_on_error);

    ardyn->chunker = ardyn->lua["chunker"];

    if (!ardyn->chunker) {
        return nullptr;
    }

    // TODO: load lua here
    return ardyn;
}

auto Ar::process_try_dyn(IO const &io, offset_cb cb, Entry const &top_entry) const -> bool {
    if (!ardyn) {
        return false;
    }

    auto reader = IO::Reader(io, top_entry.offset, top_entry.size);
    auto entries = std::vector<Entry>();

    try {
        auto result = ardyn->chunker.call<std::optional<std::vector<Entry>>>(IOReader(reader), top_entry);
        if (!result) {
            return false;
        }
        entries = std::move(*result);
    } catch (std::runtime_error const &err) {
        this->push_error(top_entry, __PRETTY_FUNCTION__, err.what());
        return false;
    }

    std::erase_if(entries, [](auto const &entry) { return !entry.size; });

    if (entries.empty()) {
        return false;
    }

    for (auto &entry : entries) {
        rlib_ar_assert(reader.contains(entry.offset, entry.size));
        entry.offset += top_entry.offset;
    }

    rlib_ar_assert(this->process_iter(io, cb, top_entry, std::move(entries)));

    return true;
}
