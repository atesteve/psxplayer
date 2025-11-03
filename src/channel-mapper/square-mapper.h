#pragma once

#include "channel-mapper.h"

#include <vector>

class SquareMapper : public ChannelMapper {
public:
    explicit SquareMapper(uint32_t jal_address,
                          uint32_t base_ptr_offset,
                          uint32_t channel_ptr_pitch);

    std::any take_snapshot() const override;
    void restore_snapshot(std::any const&) override;
    int physical_to_logical(int phys_channel) const override;
    int logical_to_physical(int log_channel) const override;
    int supported_channels() const override { return 32; }
    bool sw_hook(upse_module_instance_t*, mem_access_size_t, uint32_t, uint32_t) override;
    bool jal_hook(upse_module_instance_t*) override;

private:
    uint32_t _jal_address;
    uint32_t _base_ptr_offset;
    uint32_t _channel_ptr_pitch;

    std::vector<int> _pys_to_log;
    std::vector<int> _log_to_pys;
    uint32_t _channel_base_addr;
};
