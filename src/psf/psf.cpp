// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "psf.h"

#include "parse-bin/parse-bin.h"

#include <fmt/format.h>
#include <zlib.h>

#include <fstream>
#include <algorithm>

namespace {

constexpr size_t MAX_SIZE = 4 * 1024 * 1024; // 4MB
constexpr size_t MAX_UNCOMPRESSED_SIZE = 2033664;
constexpr size_t PSX_RAM_SIZE = 2 * 1024 * 1024; // 2MB;
constexpr size_t PSX_EXE_HEADER_SIZE = 0x800;

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

std::expected<PSF, std::string> load_psf_lib(std::filesystem::path path,
                                             std::string lib_name,
                                             std::vector<uint8_t>& psx_ram,
                                             int recursion_level);

void copy_payload(PSXExeHeader const& exe_header,
                  std::basic_string<uint8_t> const& payload,
                  std::vector<uint8_t>& psx_ram)
{
    // Copy the text section to the PSX RAM. Use (PSX_RAM_SIZE - 1) to mask off the top bits of
    // the destination address.
    std::copy_n(payload.begin() + PSX_EXE_HEADER_SIZE,
                exe_header.text_size,
                psx_ram.begin() + (exe_header.text_addr & (PSX_RAM_SIZE - 1)));
}

std::expected<PSF, std::string> load_psf_internal(std::filesystem::path path,
                                                  std::vector<uint8_t>& psx_ram,
                                                  int recursion_level)
{
    if (recursion_level >= 4) {
        return std::unexpected{"Too many recursive libraries"};
    }

    std::fstream f{};
    f.open(path, std::ios::in | std::ios::binary);

    if (!f) {
        return std::unexpected{fmt::format("Can't open the file: {}", path.string())};
    }

    auto const header = parse<PSFHeader>(f);

    if (!header || header->magic != "PSF") {
        return std::unexpected{fmt::format("Not a PSF file: {}", path.string())};
    }

    // Only version 1 PSF files are supported.
    if (header->version != 1) {
        return std::unexpected{
            fmt::format("Unsupported PSF version {}: {}", header->version, path.string())};
    }

    if (header->reserved_size > MAX_SIZE || header->payload_size > MAX_SIZE) {
        return std::unexpected{fmt::format("PSF sections are too big: {}", path.string())};
    }

    // Skip reserved area.
    f.seekg(header->reserved_size, std::ios::cur);

    std::vector<uint8_t> buffer;
    buffer.resize(header->payload_size);
    f.read(reinterpret_cast<char*>(buffer.data()), header->payload_size);

    if (f.gcount() != header->payload_size) {
        return std::unexpected{fmt::format("PSF is truncated: {}", path.string())};
    }

    auto const computed_crc = crc32(0, buffer.data(), buffer.size());

    if (header->payload_crc != computed_crc) {
        return std::unexpected{
            fmt::format("CRC mismatch (expected: {:#010x}, actual: {:#010x}): {}",
                        header->payload_crc,
                        computed_crc,
                        path.string())};
    }

    // The module to return.
    PSF psf{};
    std::basic_string<uint8_t> payload;
    payload.resize(MAX_UNCOMPRESSED_SIZE);

    unsigned long destLen = payload.size();
    auto const result = uncompress(payload.data(), &destLen, buffer.data(), buffer.size());

    if (result != Z_OK) {
        return std::unexpected{fmt::format("Could not decompress payload: {}", path.string())};
    }

    payload.resize(destLen);

    std::basic_istringstream iss{payload};
    auto const exe_header = parse<PSXExeHeader>(iss);

    if (!exe_header || exe_header->magic != "PS-X EXE") {
        return std::unexpected{fmt::format("Invalid PSX EXE format: {}", path.string())};
    }

    if (payload.size() < exe_header->text_size + PSX_EXE_HEADER_SIZE) {
        return std::unexpected{
            fmt::format("Text size is larger than the payload: {}", path.string())};
    }

    psf.entry_point = exe_header->entry_point;
    psf.sp = exe_header->sp;

    auto const tag_header = parse<TagHeader>(f);

    if (!tag_header || tag_header->magic != "[TAG]") {
        // There are no tags, so just copy the payload and return.
        copy_payload(*exe_header, payload, psx_ram);
        return psf;
    }

    std::string line;
    auto& tags = psf.tags;

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

    // Load "_lib" if any.
    if (auto const it = tags.find("_lib"); it != tags.end()) {
        auto ret = load_psf_lib(path, it->second, psx_ram, recursion_level + 1);
        if (!ret) {
            return ret;
        }
        // _lib is loaded "on top" of the minipsf, so we need to use its entry point and SP.
        psf.entry_point = ret->entry_point;
        psf.sp = ret->sp;
    }

    // After _lib has been loaded, but before _libN, copy the payload onto the PSX ram.
    copy_payload(*exe_header, payload, psx_ram);

    // Now load "_libN" libraries.
    for (auto i = 2u; i < 10; i++) {
        if (auto const it = tags.find(fmt::format("_lib{}", i)); it != tags.end()) {
            auto ret = load_psf_lib(path, it->second, psx_ram, recursion_level + 1);
            if (!ret) {
                return ret;
            }
        } else {
            break;
        }
    }

    return psf;
}

std::expected<PSF, std::string> load_psf_lib(std::filesystem::path path,
                                             std::string lib_name,
                                             std::vector<uint8_t>& psx_ram,
                                             int recursion_level)
{
    std::ranges::replace(lib_name, '\\', '/');
    path.remove_filename();
    return load_psf_internal(path / lib_name, psx_ram, recursion_level);
}

} // namespace

std::expected<PSF, std::string> load_psf(std::filesystem::path path)
{
    std::vector<uint8_t> psx_ram;
    psx_ram.resize(PSX_RAM_SIZE);

    auto ret = load_psf_internal(std::move(path), psx_ram, 0);

    if (ret) {
        ret->psx_ram = std::move(psx_ram);
    }

    return ret;
}
