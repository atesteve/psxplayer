// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "psf.h"

#include "parse-bin/parse-bin.h"

#include <fmt/format.h>
#include <zlib.h>

#include <fstream>
#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>

namespace {

constexpr size_t MAX_SIZE = 4 * 1024 * 1024; // 4MB
constexpr size_t MAX_UNCOMPRESSED_SIZE = 2033664;

// clang-format doesn't know about annotations yet, so exclude these for now.
// clang-format off
struct [[=Endianness::Little{}]] PSFHeader {
    [[=StrDecodeMode::FixedSize(3)]]
    std::string magic;
    uint8_t version;
    uint32_t reserved_size;
    uint32_t payload_size;
    uint32_t payload_crc;
};

struct [[=Endianness::Little{}]] PSXExeHeader {
    [[=StrDecodeMode::FixedSize(8)]]
    std::string magic;
    [[=Offset(0x10)]]
    uint32_t entry_point;
    [[=Offset(0x18)]]
    uint32_t text_addr;
    [[=Offset(0x1c)]]
    uint32_t text_size;
    [[=Offset(0x30)]]
    uint32_t sp;
    [[=Offset(0x4c), =StrDecodeMode::NullTerminated{}]]
    std::string marker;
};

struct TagHeader {
    [[=StrDecodeMode::FixedSize(5)]]
    std::string magic;
};

// clang-format on
} // namespace

void load_psf(std::filesystem::path path)
{
    std::fstream f{};
    f.open(path, std::ios::in | std::ios::binary);

    if (!f) {
        fmt::println("Can't open the file: {}", path.string());
        return;
    }

    auto const header = parse<PSFHeader>(f);

    if (!header || header->magic != "PSF") {
        fmt::println("Not a PSF file: {}", path.string());
        return;
    }

    // Only version 1 PSF files are supported.
    if (header->version != 1) {
        fmt::println("Unsupported PSF version {}: {}", header->version, path.string());
        return;
    }

    if (header->reserved_size > MAX_SIZE || header->payload_size > MAX_SIZE) {
        fmt::println("PSF sections are too big: {}", path.string());
        return;
    }

    // Skip reserved area.
    f.seekg(header->reserved_size, std::ios::cur);

    std::vector<uint8_t> buffer;
    buffer.resize(header->payload_size);
    f.read(reinterpret_cast<char*>(buffer.data()), header->payload_size);

    if (f.gcount() != header->payload_size) {
        fmt::println("PSF is truncated: {}", path.string());
        return;
    }

    auto const computed_crc = crc32(0, buffer.data(), buffer.size());

    if (header->payload_crc != computed_crc) {
        fmt::println("CRC mismatch (expected: {:#010x}, actual: {:#010x}): {}",
                     header->payload_crc,
                     computed_crc,
                     path.string());
        return;
    }

    {
        std::basic_string<uint8_t> payload;
        payload.resize(MAX_UNCOMPRESSED_SIZE);
        unsigned long destLen = payload.size();
        auto const result = uncompress(payload.data(), &destLen, buffer.data(), buffer.size());

        if (result != Z_OK) {
            fmt::println("Could not decompress payload: {}", path.string());
            return;
        }

        payload.resize(destLen);

        std::basic_istringstream iss{payload};
        auto const exe_header = parse<PSXExeHeader>(iss);

        if (!exe_header || exe_header->magic != "PS-X EXE") {
            fmt::println("Invalid PSX EXE format: {}", path.string());
            return;
        }

        auto const tag_header = parse<TagHeader>(f);

        if (!tag_header || tag_header->magic != "[TAG]") {
            fmt::println("There is no tag section: {}", path.string());
            return;
        }
    }

    {
        std::string line;
        std::unordered_map<std::string, std::string> tags;

        // PSF specifies that anything less or equal to 0x20 (i.e. ' ') is considered "whitespace".
        auto const is_whitespace = [](uint8_t c) { return c <= ' '; };

        while (get_tok(f, line, '\n')) {
            std::istringstream line_iss{line};
            std::string key, value;
            get_tok(line_iss, key, '=', is_whitespace);
            get_tok(line_iss, value, '=', is_whitespace);
            auto& entry = tags[std::move(key)];
            if (entry.empty()) {
                entry = std::move(value);
            } else {
                entry += value;
            }
        }

        if (auto const it = tags.find("_lib"); it != tags.end()) {
            auto& lib = it->second;
            std::ranges::replace(lib, '\\', '/');
            auto folder = path;
            folder.remove_filename();
            load_psf(folder / lib);
        }
    }
}
