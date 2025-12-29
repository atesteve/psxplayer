#include "dma.h"

namespace {
// clang-format off

constexpr r3000_ptr_t MADR = 0x1f801000;
constexpr r3000_ptr_t BCR  = 0x1f801004;
constexpr r3000_ptr_t CHCR = 0x1f801008;
constexpr r3000_ptr_t DPCR = 0x1f8010f0;
constexpr r3000_ptr_t DICR = 0x1f8010f4;

struct dpcr_t {
    uint32_t DMA0Pri : 3 = 1;
    uint32_t DMA0En  : 1 = 0;
    uint32_t DMA1Pri : 3 = 2;
    uint32_t DMA1En  : 1 = 0;
    uint32_t DMA2Pri : 3 = 3;
    uint32_t DMA2En  : 1 = 0;
    uint32_t DMA3Pri : 3 = 4;
    uint32_t DMA3En  : 1 = 0;
    uint32_t DMA4Pri : 3 = 5;
    uint32_t DMA4En  : 1 = 0;
    uint32_t DMA5Pri : 3 = 6;
    uint32_t DMA5En  : 1 = 0;
    uint32_t DMA6Pri : 3 = 7;
    uint32_t DMA6En  : 1 = 0;
    uint32_t CPUPri  : 3 = 0;
    uint32_t _       : 1 = 0;
};

struct dicr_t {
    uint32_t intr_mode        : 7 = 0;
    uint32_t _                : 8 = 0;
    uint32_t bus_error        : 1 = 0;
    uint32_t intr_mask        : 7 = 0;
    uint32_t intr_en          : 1 = 0;
    uint32_t intr_flag        : 7 = 0;
    uint32_t intr_master_flag : 1 = 0;
};

struct chcr_t {
    uint32_t dir         : 1 = 0;
    uint32_t incr        : 1 = 0;
    uint32_t _           : 6 = 0;
    uint32_t mod         : 1 = 0;
    uint32_t sync_mode   : 2 = 0;
    uint32_t _           : 5 = 0;
    uint32_t chop_dma    : 3 = 0;
    uint32_t _           : 1 = 0;
    uint32_t chop_cpu    : 3 = 0;
    uint32_t _           : 1 = 0;
    uint32_t tr          : 1 = 0;
    uint32_t _           : 3 = 0;
    uint32_t force_start : 1 = 0;
    uint32_t pause       : 1 = 0;
    uint32_t bus_snoop   : 1 = 0;
    uint32_t _           : 1 = 0;
};

static_assert(sizeof(dpcr_t) == sizeof(uint32_t));
static_assert(sizeof(dicr_t) == sizeof(uint32_t));
static_assert(sizeof(chcr_t) == sizeof(uint32_t));

// clang-format on

uint32_t dma_n(r3000_ptr_t addr)
{
    auto const n = (addr >> 4) & 0xff;
    if (n < 8 || n > 0xe) {
        return -1;
    }
    return n - 8;
}

} // namespace

struct DMA::Private {
    template<std::integral Int = uint32_t>
    void write_reg(uint32_t addr, Int value)
    {
        *(Int*)(&device_memory[addr - HWReg::DEVICE_BASE]) = value;
    }

    template<std::integral Int = uint32_t>
    Int read_reg(uint32_t addr)
    {
        return *(Int*)(&device_memory[addr - HWReg::DEVICE_BASE]);
    }

    void init(R3000* emu);

    void write_dma_reg(r3000_ptr_t addr, uint32_t value);
    uint32_t read_dma_reg(r3000_ptr_t addr);

    R3000* emu;
    EmuBuffer<uint8_t> device_memory;
    EmuBuffer<uint8_t> ram;
};

void DMA::Private::init(R3000* emu)
{
    this->emu = emu;
    device_memory = emu->get_device_buffer(HWReg::DEVICE_BASE, 0x2000);
    ram = emu->get_buffer(0, 0x200000);

    write_reg(DPCR, std::bit_cast<uint32_t>(dpcr_t{}));
}

void DMA::Private::write_dma_reg(r3000_ptr_t addr, uint32_t value)
{
    if (addr == DPCR) {
        write_reg(addr, value);
        return;
    }

    if (addr == DICR) {
        auto const bits = std::bit_cast<dicr_t>(value);
        auto current_dicr = std::bit_cast<dicr_t>(read_reg(addr));
        current_dicr.intr_mode = bits.intr_mode;
        current_dicr.bus_error = bits.bus_error;
        current_dicr.intr_mask = bits.intr_mask;
        current_dicr.intr_en = bits.intr_en;
        current_dicr.intr_flag &= ~bits.intr_mask;
        write_reg(addr, std::bit_cast<uint32_t>(current_dicr));
    }

    auto const dma = dma_n(addr);
    if (dma > 0xff) {
        return;
    }

    switch (addr & 0xfffff00f) {
    case MADR:
        // The top 8 bits and bottom 2 bits are always zero.
        write_reg(addr, value & 0x00fffffcu);
        break;
    case BCR:
        break;
    case CHCR:
        break;
    default:; // Do nothing.
    }
}

uint32_t DMA::Private::read_dma_reg(r3000_ptr_t addr)
{
    if (addr == DPCR) {
        return read_reg(addr);
    }

    if (addr == DICR) {
        auto bits = std::bit_cast<dicr_t>(read_reg(addr));
        bits.intr_master_flag =
            bits.bus_error || (bits.intr_en && bits.intr_flag && bits.intr_mask);
        return std::bit_cast<uint32_t>(bits);
    }

    auto const dma = dma_n(addr);
    if (dma > 0xff) {
        return 0;
    }

    switch (addr & 0xfffff00f) {
    case MADR:
        return read_reg(addr);
    case BCR:
        return read_reg(addr);
    case CHCR:
        return read_reg(addr);
    default:; // Do nothing.
    }

    return 0;
}

void DMA::write_dma_reg(r3000_ptr_t addr, uint32_t value)
{
    _p->write_dma_reg(addr, value);
}

uint32_t DMA::read_dma_reg(r3000_ptr_t addr)
{
    return _p->read_dma_reg(addr);
}

void DMA::init(R3000* emu)
{
    _p->init(emu);
}

DMA::DMA()
    : _p{std::make_unique<Private>()}
{}

DMA::~DMA() = default;
