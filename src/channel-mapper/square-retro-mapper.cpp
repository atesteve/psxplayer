#include "square-retro-mapper.h"

#include <fmt/format.h>

namespace {
struct SaveState {
    std::vector<int> phys_to_log;
    std::vector<int> log_to_phys;
    std::vector<int> next_log;
};

template<typename Int = uint32_t>
auto read_psx_memory(upse_module_instance_t* ins, uint32_t addr)
{
    auto const* ptr = ins->upse_ps1_memory_LUT[addr >> 16];
    if (!ptr) {
        return Int{};
    }
    return *((Int const*)(&ptr[addr & 0xffff]));
}

} // namespace

SquareRetroMapper::SquareRetroMapper(upse_module_instance_t* ins, uint32_t module_base_addr)
    : _phys_to_log(24, -1)
    , _log_to_phys(SUPPORTED_CHANNELS, -1)
{
    if (read_psx_memory(ins, module_base_addr) != 2
        || read_psx_memory(ins, module_base_addr + 4) != 12) {
        fmt::println("Warning: unrecognized module format");
        return;
    }

    auto const channels_offsets = [&] -> uint32_t {
        int found_nulls = 0;
        for (uint32_t i = 0; i < 0x10000; i += sizeof(uint32_t)) {
            if (read_psx_memory(ins, module_base_addr + i) == 0) {
                found_nulls += 1;
            }
            if (found_nulls == 2) {
                return module_base_addr + i + sizeof(uint32_t);
            }
        }
        return 0;
    }();

    if (!channels_offsets) {
        fmt::println("Warning: couldn't find channel offsets");
        return;
    }

    static constexpr uint32_t SECTION_END = 0x00200000;

    auto const channel_addrs = [&] {
        std::vector<uint32_t> channel_addrs;
        for (int i = 0; i < 16; i++) {
            auto const entry = read_psx_memory(ins, channels_offsets + i * sizeof(uint32_t));
            if (entry == SECTION_END) {
                break;
            }
            channel_addrs.push_back(entry);
        }

        for (auto& addr : channel_addrs) {
            addr += channels_offsets + channel_addrs.size() * sizeof(uint32_t);
        }
        return channel_addrs;
    }();

    uint32_t base_channel = 0;

    for (auto const channel_base : channel_addrs) {
        int simultaneous_notes = 1;
        int max_sim_notes = 1;
        for (uint32_t i = 0; i < 0x10000; i += 4) {
            auto const entry = read_psx_memory(ins, channel_base + i);

            if (entry == SECTION_END) {
                max_sim_notes = std::max(max_sim_notes, simultaneous_notes);
                _log_channel_base.push_back(base_channel);
                base_channel += max_sim_notes;
                break;
            }

            auto const code = (entry >> 16) & 0x3f;
            if (code == 0 || code >= 0x20) {
                continue;
            }

            auto const duration = entry >> 24;
            if (duration == 0) {
                simultaneous_notes++;
            } else {
                max_sim_notes = std::max(max_sim_notes, simultaneous_notes);
                simultaneous_notes = 1;
            }
        }
    }

    if (base_channel >= SUPPORTED_CHANNELS) {
        // If we have too many channels as a consequence of polyphony, just don't attempt to
        // separate them.
        _log_channel_base.clear();
    } else {
        _next_log.resize(_log_channel_base.size());
    }
}

std::any SquareRetroMapper::take_snapshot() const
{
    return SaveState{
        _phys_to_log,
        _log_to_phys,
        _next_log,
    };
}

void SquareRetroMapper::restore_snapshot(std::any const& snapshot)
{
    auto const& state = std::any_cast<SaveState const&>(snapshot);
    _phys_to_log = state.phys_to_log;
    _log_to_phys = state.log_to_phys;
    _next_log = state.next_log;
}

int SquareRetroMapper::physical_to_logical(int phys_channel) const
{
    if (size_t(phys_channel) >= _phys_to_log.size()) {
        return -1;
    }
    return _phys_to_log[phys_channel];
}

int SquareRetroMapper::logical_to_physical(int log_channel) const
{
    if (size_t(log_channel) >= _log_to_phys.size()) {
        return -1;
    }
    return _log_to_phys[log_channel];
}

bool SquareRetroMapper::sw_hook(upse_module_instance_t* ins,
                        mem_access_size_t,
                        uint32_t addr,
                        uint32_t value)
{
    static constexpr uint32_t BASE_ADDR = 0x8009ba10;
    static constexpr uint32_t HOOK_PC_1 = 0x800433cc;
    static constexpr uint32_t HOOK_PC_2 = 0x80046764;
    static constexpr uint32_t INSTRUMENT_BASE = 0x801f508c;
    static constexpr uint32_t INSTRUMENT_PITCH = 0x64;

    if (addr < BASE_ADDR || addr >= BASE_ADDR + 24 * sizeof(uint16_t)) {
        return false;
    }

    if (ins->cpustate.pc != HOOK_PC_1 && ins->cpustate.pc != HOOK_PC_2) {
        return false;
    }

    uint32_t const phys = (addr - BASE_ADDR) / sizeof(uint16_t);
    uint32_t const log = [&] {
        auto const internal_log = value - 1;
        if (internal_log >= _log_channel_base.size()) {
            return internal_log;
        }

        uint32_t const ret = _log_channel_base[internal_log] + _next_log[internal_log];

        auto const entry_ptr =
            read_psx_memory(ins, INSTRUMENT_BASE + internal_log * INSTRUMENT_PITCH);
        auto const entry = read_psx_memory(ins, entry_ptr);
        auto const duration = entry >> 24;

        if (duration == 0) {
            _next_log[internal_log]++;
        } else {
            _next_log[internal_log] = 0;
        }

        return ret;
    }();

    auto const current_log = _phys_to_log[phys];
    _phys_to_log[phys] = log < SUPPORTED_CHANNELS ? log : -1;
    if (current_log >= 0) {
        _log_to_phys[current_log] = -1;
    }
    if (log < SUPPORTED_CHANNELS) {
        auto const current_phys = _log_to_phys[log];
        if (current_phys >= 0) {
            _phys_to_log[current_phys] = -1;
        }
        _log_to_phys[log] = phys;
    }

    return true;
}
