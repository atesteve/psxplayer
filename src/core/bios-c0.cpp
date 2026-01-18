// SPDX-FileCopyrightText: 2026 Aitor Esteve Alvarado
// SPDX-License-Identifier: GPL-3.0-only

#include "bios.h"

void Bios::setIrqAutoAck(uint32_t irq, int value)
{
    if (irq > std::size(state.irq_auto_ack)) {
        throw AddressException{};
    }
    state.irq_auto_ack[irq] = value;
}
