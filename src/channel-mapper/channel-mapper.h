// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "psxemu/core/snapshot.h"

#include <any>
#include <memory>
#include <string_view>

class R3000;

class ChannelMapper {
public:
    virtual ~ChannelMapper() = default;

    virtual void take_snapshot(SnapshotSerializer& serializer) const = 0;
    virtual void restore_snapshot(SnapshotDeserializer& deserializer) = 0;
    virtual int physical_to_logical(int phys_channel) const = 0;
    virtual int logical_to_physical(int log_channel) const = 0;
    virtual int supported_channels() const = 0;
    virtual bool sw_hook(R3000* emu,
                         uint32_t size,
                         uint32_t addr,
                         uint32_t data) = 0;
    virtual bool jal_hook(R3000* emu) = 0;
    static std::unique_ptr<ChannelMapper>
        build(R3000* emu, std::string_view game_name, std::string_view title);
};
