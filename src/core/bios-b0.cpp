#include "bios.h"

enum event_class {
    EVENT_VBLANK = 0xf0000001, // IRQ0
    EVENT_GPU = 0xf0000002,    // IRQ1
    EVENT_CDROM = 0xf0000003,  // IRQ2
    EVENT_DMA = 0xf0000004,    // IRQ3
    EVENT_RTC0 = 0xf0000005,   // IRQ4 - Timer 0
    EVENT_RTC1 = 0xf0000006,   // IRQ5 - Timer 1 or 2
    //  0xf0000007 - unused, should be Timer 2
    EVENT_CONTROLLER = 0xf0000008, // IRQ7
    EVENT_SPU = 0xf0000009,        // IRQ9
    EVENT_PIO = 0xf000000a,        // IRQ10
    EVENT_SIO = 0xf000000b,        // IRQ8
    EVENT_CARD = 0xf0000011,
    EVENT_BU = 0xf4000001,
};

enum event_mode {
    EVENT_MODE_CALLBACK = 0x1000,
    EVENT_MODE_NO_CALLBACK = 0x2000,
};

enum event_flag {
    EVENT_FLAG_FREE = 0x0000,
    EVENT_FLAG_DISABLED = 0x1000,
    EVENT_FLAG_ENABLED = 0x2000,
    EVENT_FLAG_PENDING = 0x4000,
};

uint32_t Bios::openEvent(uint32_t clazz, uint32_t spec, uint32_t mode, uint32_t handler) {
    return 0x12345678;
}

int32_t Bios::waitEvent(uint32_t event)
{
    return 1;
}

int32_t Bios::enableEvent(uint32_t event) {
    return 1;
}

void Bios::HookEntryInt(uint32_t entry_point)
{
    state.int_entry_point = entry_point;
}
