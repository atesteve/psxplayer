#pragma once

#include <any>
#include <memory>
#include <string_view>

#include "libupse/upse.h"

class ChannelMapper {
public:
    virtual ~ChannelMapper() = default;

    virtual std::any take_snapshot() const = 0;
    virtual void restore_snapshot(std::any const& snapshot) = 0;
    virtual int physical_to_logical(int phys_channel) const = 0;
    virtual int logical_to_physical(int log_channel) const = 0;
    virtual int supported_channels() const = 0;
    virtual bool sw_hook(upse_module_instance_t* ins,
                         mem_access_size_t size,
                         uint32_t addr,
                         uint32_t data) = 0;
    virtual bool jal_hook(upse_module_instance_t* ins) = 0;

    static std::unique_ptr<ChannelMapper> build(upse_module_instance_t* ins,
                                                std::string_view game_name);
};
