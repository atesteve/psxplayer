#include "bios.h"

namespace {

// clang-format off

enum event_class {
    EVENT_VBLANK = 0xf0000001,  // IRQ0
    EVENT_GPU    = 0xf0000002,  // IRQ1
    EVENT_CDROM  = 0xf0000003,  // IRQ2
    EVENT_DMA    = 0xf0000004,  // IRQ3
    EVENT_RTC0   = 0xf0000005,  // IRQ4 - Timer 0
    EVENT_RTC1   = 0xf0000006,  // IRQ5 - Timer 1 or 2
    //  0xf0000007 - unused, should be Timer 2
    EVENT_JOY    = 0xf0000008,  // IRQ7
    EVENT_SPU    = 0xf0000009,  // IRQ9
    EVENT_PIO    = 0xf000000a,  // IRQ10
    EVENT_SIO    = 0xf000000b,  // IRQ8
    EVENT_CARD   = 0xf0000011,
    EVENT_BU     = 0xf4000001,
};

enum event_mode {
    EVENT_MODE_CALLBACK    = 0x1000,
    EVENT_MODE_NO_CALLBACK = 0x2000,
};

enum event_flag {
    EVENT_FLAG_FREE     = 0x0000,
    EVENT_FLAG_DISABLED = 0x1000,
    EVENT_FLAG_ENABLED  = 0x2000,
    EVENT_FLAG_PENDING  = 0x4000,
};

enum event_spec {
  SPEC_ZERO    = 0x0001, // counter becomes zero
  SPEC_INT     = 0x0002, // interrupted
  SPEC_EOF     = 0x0004, // end of i/o
  SPEC_CLOSED  = 0x0008, // file was closed
  SPEC_ACK     = 0x0010, // command acknowledged
  SPEC_COMP    = 0x0020, // command completed
  SPEC_READY   = 0x0040, // data ready
  SPEC_END     = 0x0080, // data end
  SPEC_TIMEOUT = 0x0100, // time out
  SPEC_UNK     = 0x0200, // unknown command
  SPEC_REND    = 0x0400, // end of read buffer
  SPEC_WEND    = 0x0800, // end of write buffer
  SPEC_IRQ     = 0x1000, // general interrupt
  SPEC_NEW     = 0x2000, // new device
  SPEC_SYSCALL = 0x4000, // system call instruction ;SYS(04h..FFFFFFFFh)
  SPEC_ERR     = 0x8000, // error happened
  SPEC_WERR    = 0x8001, // previous write error happened
  SPEC_MDOM    = 0x0301, // domain error in libmath
  SPEC_MRANGE  = 0x0302, // range error in libmath
};

// clang-format on

constexpr size_t MAX_EVENTS = 256; // Plenty

} // namespace

uint32_t Bios::openEvent(uint32_t clazz, uint32_t spec, uint32_t mode, uint32_t handler)
{
    if (state.events.size() >= MAX_EVENTS) {
        return -1;
    }

    auto const id = state.next_event_id++;
    state.events.emplace(id,
                         Event{
                             .clazz = clazz,
                             .spec = spec,
                             .mode = mode,
                             .handler = handler,
                             .flags = EVENT_FLAG_DISABLED,
                         });
    return id;
}

int32_t Bios::closeEvent(uint32_t event)
{
    state.events.erase(event);
    return 1;
}

int32_t Bios::waitEvent(uint32_t event)
{
    return 1;
}

int32_t Bios::enableEvent(uint32_t event)
{
    auto const it = state.events.find(event);
    if (it != state.events.cend()) {
        it->second.flags = EVENT_FLAG_ENABLED;
    }
    return 1;
}

int32_t Bios::testEvent(uint32_t event) {
    auto const it = state.events.find(event);
    if (it != state.events.cend() && it->second.flags == EVENT_FLAG_PENDING) {
        it->second.flags = EVENT_FLAG_ENABLED;
        return 1;
    }
    return 0;
}

void Bios::HookEntryInt(EmuBuffer<psx_jmp_buf> buf)
{
    state.unhanled_irq_farjmp = buf;
}
