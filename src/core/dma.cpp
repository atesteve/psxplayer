// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "dma.h"
#include "spu/spu.h"
#include "events.h"

#include <fmt/format.h>

namespace {
// clang-format off

constexpr r3000_ptr_t DPCR = 0x1f8010f0;
constexpr r3000_ptr_t DICR = 0x1f8010f4;

constexpr size_t N_CHANNELS = 7;

enum : uint32_t {
    SYNC_MODE_BURST = 0,
    SYNC_MODE_SLICE,
    SYNC_MODE_LINKED_LIST,
};

union bcr_t {
    uint32_t raw;
    struct {
        uint32_t bs : 16;
        uint32_t ba : 16;
    } fields;
};

union dpcr_t {
    uint32_t raw = 0x07654321u;
    struct {
        uint32_t DMA0Pri : 3;
        uint32_t DMA0En  : 1;
        uint32_t DMA1Pri : 3;
        uint32_t DMA1En  : 1;
        uint32_t DMA2Pri : 3;
        uint32_t DMA2En  : 1;
        uint32_t DMA3Pri : 3;
        uint32_t DMA3En  : 1;
        uint32_t DMA4Pri : 3;
        uint32_t DMA4En  : 1;
        uint32_t DMA5Pri : 3;
        uint32_t DMA5En  : 1;
        uint32_t DMA6Pri : 3;
        uint32_t DMA6En  : 1;
        uint32_t CPUPri  : 3;
        uint32_t         : 1;
    } fields;
};

union dicr_t {
    uint32_t raw;
    struct {
        uint32_t intr_mode        : 7;
        uint32_t                  : 8;
        uint32_t bus_error        : 1;
        uint32_t intr_mask        : 7;
        uint32_t intr_en          : 1;
        uint32_t intr_flag        : 7;
        uint32_t intr_master_flag : 1;
    } fields;
};

union chcr_t {
    uint32_t raw;
    struct {
        uint32_t dir         : 1;
        uint32_t incr        : 1;
        uint32_t             : 6;
        uint32_t mod         : 1;
        uint32_t sync_mode   : 2;
        uint32_t             : 5;
        uint32_t chop_dma    : 3;
        uint32_t             : 1;
        uint32_t chop_cpu    : 3;
        uint32_t             : 1;
        uint32_t start       : 1;
        uint32_t             : 3;
        uint32_t force_start : 1;
        uint32_t pause       : 1;
        uint32_t bus_snoop   : 1;
        uint32_t             : 1;
    } fields;
};

static_assert(sizeof(bcr_t::fields) == sizeof(uint32_t));
static_assert(sizeof(chcr_t::fields) == sizeof(uint32_t));
static_assert(sizeof(dpcr_t::fields) == sizeof(uint32_t));
static_assert(sizeof(dicr_t::fields) == sizeof(uint32_t));

struct dma_channel_t {
    uint32_t madr;
    bcr_t bcr;
    chcr_t chcr;
    uint32_t : 32;
};

static_assert(sizeof(dma_channel_t) == 0x10);

struct dma_regs_t {
    dma_channel_t channel[N_CHANNELS];
    dpcr_t dpcr;
    dicr_t dicr;
    uint32_t _unused[2];
};

static_assert(sizeof(dma_regs_t) == 0x80);

// clang-format on

std::pair<uint32_t, uint32_t> dma_channel_reg(r3000_ptr_t addr)
{
    auto const n = (addr >> 4) & 0xff;
    if (n < 8 || n > 0xe) {
        return {-1, -1};
    }
    return {n - 8, (addr & 0xf) >> 2};
}

} // namespace

struct DMA::Private {
    void init(R3000* emu);

    void write_reg(r3000_ptr_t addr, uint32_t value);
    uint32_t read_reg(r3000_ptr_t addr);

    std::pair<uint32_t, uint32_t> get_dpcr_fields(uint32_t ch);
    void attempt_transfer(uint32_t channel);

    void update_dicr();

    R3000* emu;
    SPU* spu;
    Timing* timing;
    EmuBuffer<dma_regs_t> reg;
    EmuBuffer<uint8_t> ram;
    bool device_dma_request[N_CHANNELS]{};
};

void DMA::Private::init(R3000* emu)
{
    this->emu = emu;
    spu = emu->get_spu();
    timing = emu->get_timing();
    reg = emu->get_device_buffer<dma_regs_t>(HWReg::DMA_start, 1);
    ram = emu->get_buffer(0, 0x200000);
    reg->dpcr = dpcr_t{};
}

std::pair<uint32_t, uint32_t> DMA::Private::get_dpcr_fields(uint32_t ch)
{
    // clang-format off
    switch (ch) {
        case 0: return {(uint32_t)reg->dpcr.fields.DMA0Pri, (uint32_t)reg->dpcr.fields.DMA0En };
        case 1: return {(uint32_t)reg->dpcr.fields.DMA1Pri, (uint32_t)reg->dpcr.fields.DMA1En };
        case 2: return {(uint32_t)reg->dpcr.fields.DMA2Pri, (uint32_t)reg->dpcr.fields.DMA2En };
        case 3: return {(uint32_t)reg->dpcr.fields.DMA3Pri, (uint32_t)reg->dpcr.fields.DMA3En };
        case 4: return {(uint32_t)reg->dpcr.fields.DMA4Pri, (uint32_t)reg->dpcr.fields.DMA4En };
        case 5: return {(uint32_t)reg->dpcr.fields.DMA5Pri, (uint32_t)reg->dpcr.fields.DMA5En };
        case 6: return {(uint32_t)reg->dpcr.fields.DMA6Pri, (uint32_t)reg->dpcr.fields.DMA6En };
        default: return {};
    }
    // clang-format on
}

void DMA::Private::update_dicr()
{
    auto& fields = reg->dicr.fields;
    fields.intr_master_flag =
        fields.bus_error || (fields.intr_en && fields.intr_flag && fields.intr_mask);
}

void DMA::Private::attempt_transfer(uint32_t ch)
{
    auto& channel = reg->channel[ch];
    auto const [priority, enabled] = get_dpcr_fields(ch);
    if (!enabled) {
        return;
    }
    if (!channel.chcr.fields.start) {
        return;
    }
    if (!device_dma_request[ch]) {
        return;
    }

    bool const copy_to_device = channel.chcr.fields.dir;
    channel.chcr.fields.force_start = 0;

    switch (ch) {
    case 4: {
        switch (channel.chcr.fields.sync_mode) {
        case SYNC_MODE_BURST:
            break;

        case SYNC_MODE_SLICE: {
            if (channel.chcr.fields.incr) {
                // Don't support backwards transfers, at least for now.
                break;
            }
            auto [block_words, n_blocks] = channel.bcr.fields;
            auto address = channel.madr;
            uint64_t cycles = 0;
            while (n_blocks) {
                if (copy_to_device) {
                    cycles += spu->dma_write(address, block_words * sizeof(uint32_t));
                } else {
                    cycles += spu->dma_read(address, block_words * sizeof(uint32_t));
                }
                address += block_words * sizeof(uint32_t);
                n_blocks--;
            }
            timing->advance_clock(cycles);
            reg->dicr.fields.intr_flag |= reg->dicr.fields.intr_mask & (1 << 4);
            update_dicr();
            break;
        }

        case SYNC_MODE_LINKED_LIST:
            break;

        default:; // Reserved - do nothing
        }
        break;
    }
    default:; // Only SPU transfers are implemented.
    }
}

void DMA::Private::write_reg(r3000_ptr_t addr, uint32_t value)
{
    if (addr == DPCR) {
        reg->dpcr.raw = value;
        return;
    }

    if (addr == DICR) {
        dicr_t const input{value};
        auto& current_dicr = reg->dicr.fields;
        current_dicr.intr_mode = input.fields.intr_mode;
        current_dicr.bus_error = input.fields.bus_error;
        current_dicr.intr_mask = input.fields.intr_mask;
        current_dicr.intr_en = input.fields.intr_en;
        current_dicr.intr_flag &= ~input.fields.intr_mask;
        update_dicr();
    }

    auto const [channel, n] = dma_channel_reg(addr);
    // We only support DMA to SPU.
    if (channel != 4) {
        return;
    }

    switch (n) {
    case 0:
        // The top 8 bits and bottom 2 bits are always zero.
        reg->channel[channel].madr = value & 0x00fffffcu;
        break;
    case 1:
        reg->channel[channel].bcr.raw = value;
        break;
    case 2:
        reg->channel[channel].chcr.raw = value;
        attempt_transfer(channel);
        break;
    default:; // Do nothing.
    }
}

uint32_t DMA::Private::read_reg(r3000_ptr_t addr)
{
    if (addr == DPCR) {
        return reg->dpcr.raw;
    }

    if (addr == DICR) {
        return reg->dicr.raw;
    }

    auto const [channel, n] = dma_channel_reg(addr);
    // We only support DMA to SPU.
    if (channel != 4) {
        return 0;
    }

    switch (n) {
    case 0:
        return reg->channel[channel].madr;
    case 1:
        return reg->channel[channel].bcr.raw;
    case 2:
        return reg->channel[channel].chcr.raw;
    default:; // Do nothing.
    }

    return 0;
}

void DMA::write_reg(r3000_ptr_t addr, uint32_t value)
{
    _p->write_reg(addr, value);
}

uint32_t DMA::read_reg(r3000_ptr_t addr)
{
    return _p->read_reg(addr);
}

void DMA::request_transfer(uint32_t channel, bool request)
{
    _p->device_dma_request[channel] = request;
    if (request) {
        _p->attempt_transfer(channel);
    }
}

bool DMA::get_master_irq_flag()
{
    return _p->reg->dicr.fields.intr_master_flag;
}

void DMA::init(R3000* emu)
{
    _p->init(emu);
}

DMA::DMA()
    : _p{std::make_unique<Private>()}
{}

DMA::~DMA() = default;
