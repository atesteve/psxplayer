#include "bios.h"

void Bios::HookEntryInt(uint32_t entry_point)
{
    state.int_entry_point = entry_point;
}
