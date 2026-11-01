// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <expected>
#include <chrono>
#include <string_view>

struct PSF {
    uint32_t entry_point;
    uint32_t sp;
    std::vector<uint8_t> psx_ram;
    std::unordered_map<std::string, std::string> tags;

    std::chrono::milliseconds get_length() const;
    std::chrono::milliseconds get_fadeout_length() const;

private:
    std::chrono::milliseconds decode_length(std::string_view key) const;
};

std::expected<PSF, std::string> load_psf(std::filesystem::path path);
