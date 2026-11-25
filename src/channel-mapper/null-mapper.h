// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "channel-mapper.h"

class NullMapper : public ChannelMapper {
public:
    explicit NullMapper(int supported_channels)
        : _supported_channels{supported_channels}
    {}

    void take_snapshot(SnapshotSerializer&) const override {}
    void restore_snapshot(SnapshotDeserializer&) override {}
    int physical_to_logical(int phys_channel) const override
    {
        if (phys_channel < 0 || phys_channel >= _supported_channels) {
            return -1;
        }
        return phys_channel;
    }
    int logical_to_physical(int log_channel) const override
    {
        if (log_channel < 0 || log_channel >= _supported_channels) {
            return -1;
        }
        return log_channel;
    }
    int supported_channels() const override { return _supported_channels; }
    bool sw_hook(R3000*, uint32_t, uint32_t, uint32_t) override { return false; }
    bool jal_hook(R3000*) override { return false; }

private:
    int _supported_channels;
};
