#include "adpcm.h"
#include "iop/util.h"

#include <algorithm>
#include <limits>
#include <vector>
#include <ranges>

namespace {

struct ADPCM_block {
    uint8_t shift : 4;
    uint8_t filter : 4;
    uint8_t loop_end : 1;
    uint8_t loop_repeat : 1;
    uint8_t loop_start : 1;
    uint8_t : 5;
    struct Data {
        int8_t lo_sample : 4;
        int8_t hi_sample : 4;
    } data[14];
};

static_assert(sizeof(ADPCM_block) == 16);

constexpr auto SAMPLES_PER_BLOCK = sizeof(ADPCM_block::data) * 2;

struct Flags {
    bool start;
    bool end;
};

Flags decode_adpcm_block(std::span<uint8_t const> ram,
                         uint32_t addr,
                         std::span<int16_t> out,
                         std::pair<int16_t, int16_t> prev_samples)
{
    if (addr > ram.size() - 16) {
        return {true, true};
    }

    auto [a, b] = prev_samples;
    auto const* block = stdx::start_lifetime_as<ADPCM_block const>(&ram[addr]);

    for (auto const& [i, data] : std::ranges::enumerate_view{block->data}) {
        auto const process_sample = [&](int32_t sample) {
            auto const shifted = sample << (12 - block->shift);
            auto ret = [&] {
                // clang-format off
                switch (block->filter) {
                case 0: return shifted;
                case 1: return shifted + (60  * a          + 32) / 64;
                case 2: return shifted + (115 * a - 52 * b + 32) / 64;
                case 3: return shifted + (98  * a - 55 * b + 32) / 64;
                case 4: return shifted + (122 * a - 60 * b + 32) / 64;
                default:
                    // Invalid, just return the raw sample.
                    return shifted;
                }
                // clang-format on
            }();

            ret = std::clamp<int32_t>(
                ret, std::numeric_limits<int16_t>::min(), std::numeric_limits<int16_t>::max());
            b = std::exchange(a, ret);
            return ret;
        };

        out[i * 2] = process_sample(data.lo_sample);
        out[i * 2 + 1] = process_sample(data.hi_sample);
    }

    return {block->loop_start, block->loop_end};
}

} // namespace

std::pair<std::vector<int16_t>, int>
    decode_adpcm_sample(std::span<uint8_t const> ram, uint32_t addr, uint32_t loop_addr)
{
    std::vector<int16_t> out;
    out.resize(SAMPLES_PER_BLOCK);
    std::pair<int16_t, int16_t> prev_samples{0, 0};
    int start = ((loop_addr - addr) / sizeof(ADPCM_block)) * SAMPLES_PER_BLOCK;

    Flags flags;
    while (!(flags = decode_adpcm_block(
                 ram, addr, std::span{out}.last(SAMPLES_PER_BLOCK), prev_samples))
                .end) {
        if (flags.start) {
            start = out.size() - SAMPLES_PER_BLOCK;
        }
        prev_samples.first = out[out.size() - 1];
        prev_samples.second = out[out.size() - 2];
        out.resize(out.size() + SAMPLES_PER_BLOCK);
        addr += sizeof(ADPCM_block);
    }

    return {std::move(out), start};
}
