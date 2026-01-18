// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "channel-mapper.h"

class NullMapper : public ChannelMapper {
public:
    explicit NullMapper(int supported_channels)
        : _supported_channels{supported_channels}
    {}

    std::any take_snapshot() const override { return {}; }
    void restore_snapshot(std::any const&) override {}
    int physical_to_logical(int phys_channel) const override { return phys_channel; }
    int logical_to_physical(int log_channel) const override { return log_channel; }
    int supported_channels() const override { return _supported_channels; }
    bool sw_hook(upse_module_instance_t*, mem_access_size_t, uint32_t, uint32_t) override
    {
        return false;
    }
    bool jal_hook(upse_module_instance_t*) override { return false; }

private:
    int _supported_channels;
};
