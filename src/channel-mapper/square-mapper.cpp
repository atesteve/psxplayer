// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "square-mapper.h"

namespace {
struct SaveState {
    std::vector<int> phys_to_log;
    std::vector<int> log_to_phys;
    uint32_t channel_base_addr;
};

} // namespace

SquareMapper::SquareMapper(uint32_t jal_address,
                           uint32_t base_ptr_offset,
                           uint32_t channel_ptr_pitch)
    : _jal_address{jal_address}
    , _base_ptr_offset{base_ptr_offset}
    , _channel_ptr_pitch{channel_ptr_pitch}
    , _phys_to_log(24, -1)
    , _log_to_phys(32, -1)
    , _channel_base_addr{0}
{}

std::any SquareMapper::take_snapshot() const
{
    return SaveState{
        _phys_to_log,
        _log_to_phys,
        _channel_base_addr,
    };
}

void SquareMapper::restore_snapshot(std::any const& snapshot)
{
    auto const& state = std::any_cast<SaveState const&>(snapshot);
    _phys_to_log = state.phys_to_log;
    _log_to_phys = state.log_to_phys;
    _channel_base_addr = state.channel_base_addr;
}

int SquareMapper::physical_to_logical(int phys_channel) const
{
    if (size_t(phys_channel) >= _phys_to_log.size()) {
        return -1;
    }
    return _phys_to_log[phys_channel];
}

int SquareMapper::logical_to_physical(int log_channel) const
{
    if (size_t(log_channel) >= _log_to_phys.size()) {
        return -1;
    }
    return _log_to_phys[log_channel];
}

bool SquareMapper::sw_hook(upse_module_instance_t*, mem_access_size_t, uint32_t addr, uint32_t data)
{
    if (_channel_base_addr == 0) {
        return false;
    }

    // Out of the region of interest.
    if (addr < _channel_base_addr || addr >= _channel_base_addr + 32 * _channel_ptr_pitch) {
        return false;
    }

    auto const offset = addr - _channel_base_addr;

    // We are interested on offsets that are a multiple of _channel_ptr_pitch.
    if (offset % _channel_ptr_pitch != 0) {
        return false;
    }

    int const log = offset / _channel_ptr_pitch;
    int const current_pys = _log_to_phys[log];
    int const pys = data < 24 ? data : -1;

    _log_to_phys[log] = pys;
    if (current_pys != -1) {
        _phys_to_log[current_pys] = -1;
    }
    if (pys != -1) {
        _phys_to_log[pys] = log;
    }

    return true;
}

bool SquareMapper::jal_hook(upse_module_instance_t* ins)
{
    // If the information is already captured, no need to repeat the process.
    if (_channel_base_addr) {
        return false;
    }

    // Not an interesting function address.
    if (ins->cpustate.pc != _jal_address) {
        return false;
    }

    // a0 constains the interesting pointer.
    auto const base_ptr = ins->cpustate.GPR.n.a0;
    // First channel at offset `_base_ptr_offset`
    auto const channel_base_ptr = base_ptr + _base_ptr_offset;

    _channel_base_addr = channel_base_ptr;

    // There is no change to the map, it has only been set up.
    return false;
}
