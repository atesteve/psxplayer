#include "ff6-mapper.h"

namespace {
struct SaveState {
    std::vector<int> phys_to_log;
    std::vector<int> log_to_phys;
};

template<typename Int>
auto read_psx_memory(upse_module_instance_t* ins, uint32_t addr)
{
    auto const* ptr = ins->upse_ps1_memory_LUT[addr >> 16];
    if (!ptr) {
        return Int{};
    }
    return *((Int const*)(&ptr[addr & 0xffff]));
}

} // namespace

FF6Mapper::FF6Mapper()
    : _phys_to_log(SUPPORTED_CHANNELS, -1)
    , _log_to_phys(SUPPORTED_CHANNELS, -1)
{}

std::any FF6Mapper::take_snapshot() const
{
    return SaveState{
        _phys_to_log,
        _log_to_phys,
    };
}

void FF6Mapper::restore_snapshot(std::any const& snapshot)
{
    auto const& state = std::any_cast<SaveState>(snapshot);
    _phys_to_log = state.phys_to_log;
    _log_to_phys = state.log_to_phys;
}

int FF6Mapper::physical_to_logical(int phys_channel) const
{
    if (size_t(phys_channel) >= _phys_to_log.size()) {
        return -1;
    }
    return _phys_to_log[phys_channel];
}

int FF6Mapper::logical_to_physical(int log_channel) const
{
    if (size_t(log_channel) >= _log_to_phys.size()) {
        return -1;
    }
    return _log_to_phys[log_channel];
}

bool FF6Mapper::sw_hook(upse_module_instance_t* ins,
                        mem_access_size_t,
                        uint32_t addr,
                        uint32_t value)
{
    static constexpr uint32_t BASE_ADDR = 0x8009ba10;
    static constexpr uint32_t HOOK_PC_1 = 0x800433cc;
    static constexpr uint32_t HOOK_PC_2 = 0x80046764;

    if (addr < BASE_ADDR || addr >= BASE_ADDR + 24 * sizeof(uint16_t)) {
        return false;
    }

    if (ins->cpustate.pc != HOOK_PC_1 && ins->cpustate.pc != HOOK_PC_2) {
        return false;
    }

    uint32_t const phys = (addr - BASE_ADDR) / sizeof(uint16_t);
    uint32_t const log = value - 1;

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

bool FF6Mapper::jal_hook(upse_module_instance_t*)
{
    return false;
}
