#pragma once

#include <cstdint>
#include <span>
#include <vector>

std::pair<std::vector<int16_t>, int>
    decode_adpcm_sample(std::span<uint8_t const> ram, uint32_t addr, uint32_t loop_addr);
