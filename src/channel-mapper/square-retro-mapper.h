#pragma once

#include "channel-mapper.h"

#include <vector>

class SquareRetroMapper : public ChannelMapper {
public:
    explicit SquareRetroMapper(upse_module_instance_t* ins, uint32_t module_base_addr);

    static constexpr size_t SUPPORTED_CHANNELS = 16;

    std::any take_snapshot() const override;
    void restore_snapshot(std::any const&) override;
    int physical_to_logical(int phys_channel) const override;
    int logical_to_physical(int log_channel) const override;
    int supported_channels() const override { return SUPPORTED_CHANNELS; }
    bool sw_hook(upse_module_instance_t*, mem_access_size_t, uint32_t, uint32_t) override;
    bool jal_hook(upse_module_instance_t*) override { return false; };

private:
    std::vector<int> _phys_to_log;
    std::vector<int> _log_to_phys;
    std::vector<int> _next_log;
    std::vector<int> _log_channel_base;
};
