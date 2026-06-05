/*
 * QEMU AVR32 Example board
 *
 * Copyright (c) 2022-2023 Florian Göhler
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */
#ifndef HW_AVR32_AVR32EXPC_H
#define HW_AVR32_AVR32EXPC_H

#include "target/avr32/cpu.h"
#include "chardev/char-fe.h"
#include "qom/object.h"
#include "hw/sysbus.h"

#define TYPE_AVR32EXP_MCU "AVR32EXP"
#define TYPE_AVR32EXPS_MCU "AVR32EXPS"

typedef struct AVR32EXPMcuState AVR32EXPMcuState;
DECLARE_INSTANCE_CHECKER(AVR32EXPMcuState, AVR32EXP_MCU, TYPE_AVR32EXP_MCU)

typedef struct AVR32EXPUSARTState {
    MemoryRegion iomem;
    CharBackend chr;
    struct AVR32EXPMcuState *mcu;
    uint32_t imr;
    uint32_t rpr;
    uint32_t rcr;
    uint32_t tpr;
    uint32_t tcr;
    uint32_t rnpr;
    uint32_t rncr;
    uint32_t tnpr;
    uint32_t tncr;
    uint32_t ptsr;
    uint32_t rx_status;
    uint8_t rx_byte;
    bool rx_ready;
    unsigned irq;
} AVR32EXPUSARTState;

struct AVR32EXPMcuState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    AVR32ACPU cpu;
    MemoryRegion flash;
    MemoryRegion sram;
    MemoryRegion ram;
    MemoryRegion ram_p1_alias;
    MemoryRegion ram_p2_alias;
    MemoryRegion intc_iomem;
    uint32_t intc_intpr[33];
    uint32_t intc_intreq[33];
    int intc_pending_irq;
    AVR32EXPUSARTState usart[4];
    MemoryRegion test_exit;
    uint32_t test_result;
    uint32_t test_expected;
};

#endif // HW_AVR32_AVR32EXPC_H
