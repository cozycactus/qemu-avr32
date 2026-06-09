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
#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "avr32exp.h"
#include "boot.h"
#include "elf.h"
#include "qom/object.h"
#include "hw/boards.h"
#include "hw/core/cpu.h"
#include "hw/loader.h"
#include "exec/address-spaces.h"

struct AVR32ExampleBoardMachineState {
    /*< private >*/
    MachineState parent_obj;
    /*< public >*/
    AVR32EXPMcuState mcu;

};
typedef struct AVR32ExampleBoardMachineState AVR32ExampleBoardMachineState;

struct AVR32ExampleBoardMachineClass {
    /*< private >*/
    MachineClass parent_class;
    /*< public >*/
};
typedef struct AVR32ExampleBoardMachineClass AVR32ExampleBoardMachineClass;

#define TYPE_AVR32EXAMPLE_BOARD_BASE_MACHINE MACHINE_TYPE_NAME("avr32example-board-base")

#define TYPE_AVR32EXAMPLE_BOARD_MACHINE MACHINE_TYPE_NAME("avr32example-board")
DECLARE_OBJ_CHECKERS(AVR32ExampleBoardMachineState, AVR32ExampleBoardMachineClass,
        AVR32EXAMPLE_BOARD_MACHINE, TYPE_AVR32EXAMPLE_BOARD_MACHINE)

#define AVR32EXAMPLE_RAM_BASE           0x10000000
#define AVR32EXAMPLE_RAM_SIZE           (64 * MiB)
#define AVR32EXAMPLE_RAM_P1_ALIAS_BASE  0x90000000
#define AVR32EXAMPLE_TAGS_PHYS          (AVR32EXAMPLE_RAM_BASE + 0x03f00000)
#define AVR32EXAMPLE_TAGS_VIRT          (AVR32EXAMPLE_RAM_P1_ALIAS_BASE + 0x03f00000)

#define AVR32_ATAG_MAGIC    0xa2a25441
#define AVR32_ATAG_NONE     0x00000000
#define AVR32_ATAG_CORE     0x54410001
#define AVR32_ATAG_MEM      0x54410002
#define AVR32_ATAG_CMDLINE  0x54410003

static uint8_t *avr32example_put_be32(uint8_t *p, uint32_t value)
{
    stl_be_p(p, value);
    return p + sizeof(value);
}

static uint8_t *avr32example_put_tag_header(uint8_t *p, uint32_t tag,
                                            uint32_t size_words)
{
    p = avr32example_put_be32(p, size_words);
    return avr32example_put_be32(p, tag);
}

static void avr32example_setup_atags(AVR32ACPU *cpu)
{
    static const char cmdline[] =
        "console=ttyS0,115200n8 rdinit=/init lpj=100000";
    uint8_t tags[256] = { 0 };
    uint8_t *p = tags;
    uint32_t cmdline_size;
    size_t tags_size;
    MemTxResult res;

    p = avr32example_put_tag_header(p, AVR32_ATAG_CORE, 5);
    p = avr32example_put_be32(p, 1);
    p = avr32example_put_be32(p, 4096);
    p = avr32example_put_be32(p, 0);

    p = avr32example_put_tag_header(p, AVR32_ATAG_MEM, 5);
    p = avr32example_put_be32(p, AVR32EXAMPLE_RAM_BASE);
    p = avr32example_put_be32(p, AVR32EXAMPLE_RAM_SIZE);
    p = avr32example_put_be32(p, 0);

    cmdline_size = (sizeof(uint32_t) * 2 + sizeof(cmdline) + 3) / 4;
    p = avr32example_put_tag_header(p, AVR32_ATAG_CMDLINE, cmdline_size);
    memcpy(p, cmdline, sizeof(cmdline));
    p += cmdline_size * sizeof(uint32_t) - sizeof(uint32_t) * 2;

    p = avr32example_put_tag_header(p, AVR32_ATAG_NONE, 0);
    tags_size = p - tags;

    res = address_space_write(&address_space_memory, AVR32EXAMPLE_TAGS_PHYS,
                              MEMTXATTRS_UNSPECIFIED, tags, tags_size);
    if (res != MEMTX_OK) {
        error_report("Unable to write AVR32 Linux ATAGs at 0x%08x",
                     AVR32EXAMPLE_TAGS_PHYS);
        exit(1);
    }

    cpu->env.r[11] = AVR32EXAMPLE_TAGS_VIRT;
    cpu->env.r[12] = AVR32_ATAG_MAGIC;
}

static void avr32example_load_kernel(MachineState *machine,
                                     AVR32ACPU *cpu)
{
    uint64_t entry = 0;
    uint64_t low = 0;
    uint64_t high = 0;
    ssize_t kernel_size;

    if (!machine->kernel_filename) {
        return;
    }

    kernel_size = load_elf(machine->kernel_filename, NULL, NULL, NULL,
                           &entry, &low, &high, NULL,
                           1, EM_AVR32, 0, 0);
    if (kernel_size < 0) {
        error_report("Unable to load AVR32 kernel ELF '%s'",
                     machine->kernel_filename);
        exit(1);
    }

    avr32example_setup_atags(cpu);
    cpu_set_pc(CPU(cpu), (uint32_t)entry);
}

static void avr32example_board_init(MachineState *machine)
{
    AVR32ExampleBoardMachineState* m_state = AVR32EXAMPLE_BOARD_MACHINE(machine);

    object_initialize_child(OBJECT(machine), "mcu", &m_state->mcu, TYPE_AVR32EXPS_MCU);
    sysbus_realize(SYS_BUS_DEVICE(&m_state->mcu), &error_abort);

    if (machine->firmware) {
        if (!avr32_load_firmware(&m_state->mcu.cpu, machine,
                                 &m_state->mcu.flash, machine->firmware)) {
            exit(1);
        }
    }

    avr32example_load_kernel(machine, &m_state->mcu.cpu);
}

static void avr32example_board_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "AVR32 Example Board";
    mc->alias = "avr32example-board";
    mc->init = avr32example_board_init;
    mc->default_cpus = 1;
    mc->min_cpus = mc->default_cpus;
    mc->max_cpus = mc->default_cpus;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_parallel = 1;
}

static const TypeInfo avr32example_board_machine_types[] = {
        {
                .name           = TYPE_AVR32EXAMPLE_BOARD_MACHINE,
                .parent         = TYPE_MACHINE,
                .instance_size  = sizeof(AVR32ExampleBoardMachineState),
                .class_size     = sizeof(AVR32ExampleBoardMachineClass),
                .class_init     = avr32example_board_class_init,
        }
};

DEFINE_TYPES(avr32example_board_machine_types)
