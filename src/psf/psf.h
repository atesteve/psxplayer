// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <expected>

struct PSF {
    uint32_t entry_point;
    uint32_t sp;
    std::vector<uint8_t> psx_ram;
    std::unordered_map<std::string, std::string> tags;
};

std::expected<PSF, std::string> load_psf(std::filesystem::path path);
