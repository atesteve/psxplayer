// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <fmt/format.h>
#include <fmt/compile.h>

#include <type_traits>
#include <istream>
#include <meta>
#include <iostream>
#include <string>
#include <optional>
#include <bit>

using namespace std::literals;

#define STATIC_ASSERT(cond, fmt, ...) \
    static_assert(cond, format(FMT_COMPILE(fmt), __VA_ARGS__))

struct StrDecodeMode {
    struct NullTerminated{};
    struct FixedSize{size_t size;};
    static constexpr auto Default = NullTerminated{};
};

struct Endianness {
    struct Native{};
    struct Big{};
    struct Little{};
    static constexpr auto Default = Native{};
};

struct Offset {
    static constexpr size_t UNSET = std::numeric_limits<size_t>::max();
    size_t offset{UNSET};
};

consteval auto class_members_of(std::meta::info const& i)
{
    static constexpr auto ctx = std::meta::access_context::current();
    std::vector<std::meta::info> ret;
    for (auto const& m : members_of(i, ctx)) {
        if (is_type(m) && is_class_type(m)) {
            ret.push_back(m);
        }
    }
    return ret;
}

template<typename T, typename DefaultAn = std::nullopt_t>
    requires (class_members_of(^^T).size() > 0)
consteval auto get_annotation(std::meta::info const& info, DefaultAn default_an = std::nullopt)
{
    static constexpr auto annotation_types = define_static_array(class_members_of(^^T));
    auto const annotations = annotations_of(info);

    template for (constexpr auto an_t : annotation_types) {
        auto const it = std::ranges::find_if(annotations, [&](auto const& elem) {
            return is_same_type(decay(type_of(elem)), an_t);
        });
        if (it != annotations.end()) {
            return constant_of(*it);
        }
    }
    // Return an object of the default type.
    if constexpr (!std::is_same_v<decltype(default_an), std::nullopt_t>) {
        return ^^default_an;
    } if constexpr (requires { T::Default; }) {
        return ^^T::Default;
    } else {
        return std::nullopt;
    }
}

template<typename T>
    requires (class_members_of(^^T).size() == 0)
consteval auto get_annotation(std::meta::info const& info)
{
    auto const annotations = annotations_of_with_type(info, ^^T);
    if (annotations.size() > 0) {
        return constant_of(annotations[0]);
    } else {
        return std::meta::reflect_constant(T{});
    }
}

template<typename StreamCharT, typename StringCharT>
auto& get_tok(std::basic_istream<StreamCharT>& f,
             std::basic_string<StringCharT>& data,
             char delim,
             auto&& whitespace_pred)
{
    data.clear();
    size_t trimmed_size = 0;
    while (f) {
        if (f.peek() == std::char_traits<StreamCharT>::eof()) {
            if (trimmed_size == 0) {
                f.get();
            }
            break;
        }
        StreamCharT c;
        f.get(c);
        if (c == delim) {
            if (trimmed_size == 0) {
                continue;
            } else {
                break;
            }
        }
        if (whitespace_pred(c)) {
            if (trimmed_size == 0) {
                continue;
            }
        } else {
            trimmed_size = data.size() + 1;
        }
        data += c;
    }
    data.resize(trimmed_size);
    return f;
}

template<typename StreamCharT, typename StringCharT>
auto& get_tok(std::basic_istream<StreamCharT>& f,
              std::basic_string<StringCharT>& data,
              char delim)
{
    return get_tok(f, data, delim, [](auto) { return false; });
}

template<typename CharT>
bool parse_string(std::basic_istream<CharT>& f,
                  std::string& data,
                  StrDecodeMode::NullTerminated const&)
{
    return bool(get_tok(f, data, '\0'));
}

template<typename CharT>
bool parse_string(std::basic_istream<CharT>& f,
                  std::string& data,
                  StrDecodeMode::FixedSize const& mode)
{
    data.resize(mode.size);
    f.read(reinterpret_cast<CharT*>(data.data()), mode.size);
    return f.gcount() == (std::streamsize)mode.size;
}

template<typename E, typename CharT>
bool parse_int(std::basic_istream<CharT>& f, auto& data)
{
    f.read(reinterpret_cast<CharT*>(&data), sizeof(data));
    if (f.gcount() != sizeof(data)) {
        return false;
    }
    if constexpr ((std::is_same_v<Endianness::Little, E>
                    && std::endian::native != std::endian::little) ||
                  (std::is_same_v<Endianness::Big, E>
                    && std::endian::native != std::endian::big)) {
        std::byteswap(data);
    }
    return true;
}

template<typename Strt, typename CharT>
    requires std::is_aggregate_v<Strt>
std::optional<Strt> parse(std::basic_istream<CharT>& f)
{
    static constexpr auto info = ^^Strt;
    static constexpr auto ctx = std::meta::access_context::current();
    static constexpr auto members = define_static_array(nonstatic_data_members_of(info, ctx));
    static constexpr auto default_endianness = [:get_annotation<Endianness>(info):];

    Strt r;

    template for (constexpr auto member : members) {
        auto& data = r.[:member:];
        constexpr auto offset = [:get_annotation<Offset>(member):];
        if constexpr (offset.offset != Offset::UNSET) {
            f.seekg(offset.offset);
        }
        if constexpr (is_integral_type(type_of(member))) {
            using annotation = [:type_of(get_annotation<Endianness>(member, default_endianness)):];
            if (!parse_int<annotation>(f, data)) {
                return std::nullopt;
            }
        } else if constexpr (is_same_type(type_of(member), ^^std::string)) {
            constexpr auto annotation = [:get_annotation<StrDecodeMode>(member):];
            if (!parse_string(f, data, annotation)) {
                return std::nullopt;
            }
        } else {
            STATIC_ASSERT(false, "Unsupported type: {}", display_string_of(type_of(member)));
        }
    }

    return r;
}
