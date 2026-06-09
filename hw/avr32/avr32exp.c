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
#include "qemu/module.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "exec/address-spaces.h"
#include "exec/exec-all.h"
#include "chardev/char-fe.h"
#include "sysemu/sysemu.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "qom/object.h"
#include "hw/misc/unimp.h"
#include "avr32exp.h"

struct AVR32EXPMcuClass {
    /*< private >*/
    SysBusDeviceClass parent_class;

    /*< public >*/
    const char *cpu_type;

    size_t flash_size;
    size_t sram_size;
    size_t ram_size;
};

typedef struct AVR32EXPMcuClass AVR32EXPMcuClass;

#define AVR32EXP_FLASH_BASE 0x80000000
#define AVR32EXP_SRAM_BASE 0x00000000
#define AVR32EXP_RAM_BASE 0x10000000
#define AVR32EXP_RAM_P1_ALIAS_BASE 0x90000000
#define AVR32EXP_RAM_P2_ALIAS_BASE 0xa0000000
#define AVR32EXP_INTC_BASE 0xfff00400
#define AVR32EXP_INTC_SIZE 0x400
#define AVR32EXP_INTC_NR_IRQS 33
#define AVR32EXP_USART0_BASE 0xffe00c00
#define AVR32EXP_USART_STRIDE 0x400
#define AVR32EXP_USART_SIZE 0x400

#define AVR32EXP_INTC_INTPR_BASE 0x000
#define AVR32EXP_INTC_INTREQ_BASE 0x100
#define AVR32EXP_INTC_INTCAUSE3 0x200
#define AVR32EXP_INTC_INTCAUSE2 0x204
#define AVR32EXP_INTC_INTCAUSE1 0x208
#define AVR32EXP_INTC_INTCAUSE0 0x20c
#define AVR32EXP_INTC_INTLEV_MASK (3u << 30)
#define AVR32EXP_INTC_OFFSET_MASK 0x00ffffffu
#define AVR32EXP_USART1_IRQ 7

#define ATMEL_US_CR 0x00
#define ATMEL_US_IER 0x08
#define ATMEL_US_IDR 0x0c
#define ATMEL_US_IMR 0x10
#define ATMEL_US_CSR 0x14
#define ATMEL_US_RHR 0x18
#define ATMEL_US_THR 0x1c
#define ATMEL_US_NAME 0xf0
#define ATMEL_US_VERSION 0xfc
#define ATMEL_PDC_RPR 0x100
#define ATMEL_PDC_RCR 0x104
#define ATMEL_PDC_TPR 0x108
#define ATMEL_PDC_TCR 0x10c
#define ATMEL_PDC_RNPR 0x110
#define ATMEL_PDC_RNCR 0x114
#define ATMEL_PDC_TNPR 0x118
#define ATMEL_PDC_TNCR 0x11c
#define ATMEL_PDC_PTCR 0x120
#define ATMEL_PDC_PTSR 0x124

#define ATMEL_US_RSTRX BIT(2)
#define ATMEL_US_RXEN BIT(4)
#define ATMEL_US_RXDIS BIT(5)
#define ATMEL_US_STTTO BIT(11)
#define ATMEL_US_RXRDY BIT(0)
#define ATMEL_US_TXRDY BIT(1)
#define ATMEL_US_ENDRX BIT(3)
#define ATMEL_US_ENDTX BIT(4)
#define ATMEL_US_TIMEOUT BIT(8)
#define ATMEL_US_TXEMPTY BIT(9)
#define ATMEL_US_TXBUFE BIT(11)
#define ATMEL_PDC_RXTEN BIT(0)
#define ATMEL_PDC_RXTDIS BIT(1)
#define ATMEL_PDC_TXTEN BIT(8)
#define ATMEL_PDC_TXTDIS BIT(9)

DECLARE_CLASS_CHECKERS(AVR32EXPMcuClass, AVR32EXP_MCU,
        TYPE_AVR32EXP_MCU)

static int avr32exp_intc_irq_level(AVR32EXPMcuState *mcu, unsigned irq)
{
    return (mcu->intc_intpr[irq] & AVR32EXP_INTC_INTLEV_MASK) >> 30;
}

static uint32_t avr32exp_intc_irq_offset(AVR32EXPMcuState *mcu, unsigned irq)
{
    return mcu->intc_intpr[irq] & AVR32EXP_INTC_OFFSET_MASK;
}

static void avr32exp_intc_update_cpu(AVR32EXPMcuState *mcu)
{
    CPUState *cs = CPU(&mcu->cpu);
    int best_irq = -1;
    int best_level = -1;

    for (unsigned i = 0; i < AVR32EXP_INTC_NR_IRQS; i++) {
        if (i != AVR32EXP_USART1_IRQ) {
            continue;
        }
        if (!mcu->intc_intreq[i]) {
            continue;
        }
        if (best_irq < 0 || avr32exp_intc_irq_level(mcu, i) > best_level) {
            best_irq = i;
            best_level = avr32exp_intc_irq_level(mcu, i);
        }
    }

    if (best_irq < 0) {
        mcu->intc_pending_irq = -1;
        mcu->cpu.env.intsrc = -1;
        mcu->cpu.env.intlevel = 0;
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
        return;
    }

    mcu->intc_pending_irq = best_irq;
    mcu->cpu.env.intsrc = best_irq;
    mcu->cpu.env.intlevel = best_level;
    mcu->cpu.env.autovector = avr32exp_intc_irq_offset(mcu, best_irq);
    cpu_interrupt(cs, CPU_INTERRUPT_HARD);
}

static void avr32exp_intc_set_irq(AVR32EXPMcuState *mcu, unsigned irq,
                                  bool level)
{
    if (irq >= AVR32EXP_INTC_NR_IRQS) {
        return;
    }

    mcu->intc_intreq[irq] = level ? 1 : 0;
    avr32exp_intc_update_cpu(mcu);
}

static uint64_t avr32exp_intc_read(void *opaque, hwaddr offset, unsigned size)
{
    AVR32EXPMcuState *mcu = opaque;

    if (offset >= AVR32EXP_INTC_INTPR_BASE
        && offset < AVR32EXP_INTC_INTPR_BASE + AVR32EXP_INTC_NR_IRQS * 4
        && (offset & 3) == 0) {
        unsigned irq = offset / 4;

        return irq == AVR32EXP_USART1_IRQ ? mcu->intc_intpr[irq] : 0;
    }

    if (offset >= AVR32EXP_INTC_INTREQ_BASE
        && offset < AVR32EXP_INTC_INTREQ_BASE + AVR32EXP_INTC_NR_IRQS * 4
        && (offset & 3) == 0) {
        return mcu->intc_intreq[(offset - AVR32EXP_INTC_INTREQ_BASE) / 4];
    }

    switch (offset) {
    case AVR32EXP_INTC_INTCAUSE0:
    case AVR32EXP_INTC_INTCAUSE1:
    case AVR32EXP_INTC_INTCAUSE2:
    case AVR32EXP_INTC_INTCAUSE3: {
        int level = (AVR32EXP_INTC_INTCAUSE0 - offset) / 4;

        if (mcu->intc_pending_irq >= 0
            && avr32exp_intc_irq_level(mcu, mcu->intc_pending_irq) == level) {
            return mcu->intc_pending_irq;
        }
        return 0;
    }
    default:
        return 0;
    }
}

static void avr32exp_intc_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size)
{
    AVR32EXPMcuState *mcu = opaque;

    if (offset >= AVR32EXP_INTC_INTPR_BASE
        && offset < AVR32EXP_INTC_INTPR_BASE + AVR32EXP_INTC_NR_IRQS * 4
        && (offset & 3) == 0) {
        mcu->intc_intpr[offset / 4] = value;
        avr32exp_intc_update_cpu(mcu);
    }
}

static const MemoryRegionOps avr32exp_intc_ops = {
    .read = avr32exp_intc_read,
    .write = avr32exp_intc_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint32_t avr32exp_usart_status(AVR32EXPUSARTState *usart)
{
    uint32_t status = ATMEL_US_TXRDY | ATMEL_US_TXEMPTY;

    if (usart->rx_ready) {
        status |= ATMEL_US_RXRDY;
    }
    status |= usart->rx_status;
    if (usart->tcr == 0) {
        status |= ATMEL_US_ENDTX;
    }
    if (usart->tcr == 0 && usart->tncr == 0) {
        status |= ATMEL_US_TXBUFE;
    }

    return status;
}

static void avr32exp_usart_update_irq(AVR32EXPUSARTState *usart)
{
    avr32exp_intc_set_irq(usart->mcu, usart->irq,
                          (usart->imr & avr32exp_usart_status(usart)) != 0);
}

static void avr32exp_usart_drain_tx_pdc(AVR32EXPUSARTState *usart)
{
    while ((usart->ptsr & ATMEL_PDC_TXTEN) && usart->tcr != 0) {
        uint32_t count = usart->tcr;

        for (uint32_t i = 0; i < count; i++) {
            uint8_t ch = 0;
            MemTxResult result;

            ch = address_space_ldub(&address_space_memory, usart->tpr + i,
                                    MEMTXATTRS_UNSPECIFIED, &result);
            if (result != MEMTX_OK) {
                continue;
            }
            if (qemu_chr_fe_backend_connected(&usart->chr)) {
                qemu_chr_fe_write_all(&usart->chr, &ch, 1);
            }
        }

        usart->tpr += count;
        usart->tcr = 0;

        if (usart->tncr != 0) {
            usart->tpr = usart->tnpr;
            usart->tcr = usart->tncr;
            usart->tnpr = 0;
            usart->tncr = 0;
        }
    }

    avr32exp_usart_update_irq(usart);
}

static int avr32exp_usart_can_receive(void *opaque)
{
    AVR32EXPUSARTState *usart = opaque;

    if ((usart->ptsr & ATMEL_PDC_RXTEN) && usart->rcr != 0) {
        return usart->rcr;
    }
    return !usart->rx_ready;
}

static void avr32exp_usart_receive(void *opaque, const uint8_t *buf, int size)
{
    AVR32EXPUSARTState *usart = opaque;
    int i;

    for (i = 0; i < size; ++i) {
        if ((usart->ptsr & ATMEL_PDC_RXTEN) && usart->rcr != 0) {
            MemTxResult result;

            address_space_stb(&address_space_memory, usart->rpr, buf[i],
                              MEMTXATTRS_UNSPECIFIED, &result);
            if (result != MEMTX_OK) {
                continue;
            }
            usart->rpr++;
            usart->rcr--;
            usart->rx_status |= ATMEL_US_TIMEOUT;
            if (usart->rcr == 0) {
                usart->rx_status |= ATMEL_US_ENDRX;
            }
            continue;
        }

        if (usart->rx_ready) {
            break;
        }
        usart->rx_byte = buf[i];
        usart->rx_ready = true;
    }

    avr32exp_usart_update_irq(usart);
    qemu_chr_fe_accept_input(&usart->chr);
}

static uint64_t avr32exp_usart_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    AVR32EXPUSARTState *usart = opaque;

    switch (offset) {
    case ATMEL_US_IMR:
        return usart->imr;
    case ATMEL_US_CSR:
        return avr32exp_usart_status(usart);
    case ATMEL_US_RHR:
        if (usart->rx_ready) {
            uint8_t ch = usart->rx_byte;

            usart->rx_ready = false;
            avr32exp_usart_update_irq(usart);
            qemu_chr_fe_accept_input(&usart->chr);
            return ch;
        }
        return 0;
    case ATMEL_US_NAME:
        return 0x55534152; /* USAR(T) */
    case ATMEL_US_VERSION:
        return 0x302;
    case ATMEL_PDC_RPR:
        return usart->rpr;
    case ATMEL_PDC_RCR:
        return usart->rcr;
    case ATMEL_PDC_TPR:
        return usart->tpr;
    case ATMEL_PDC_TCR:
        return usart->tcr;
    case ATMEL_PDC_RNPR:
        return usart->rnpr;
    case ATMEL_PDC_RNCR:
        return usart->rncr;
    case ATMEL_PDC_TNPR:
        return usart->tnpr;
    case ATMEL_PDC_TNCR:
        return usart->tncr;
    case ATMEL_PDC_PTSR:
        return usart->ptsr;
    default:
        return 0;
    }
}

static void avr32exp_usart_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size)
{
    AVR32EXPUSARTState *usart = opaque;
    uint8_t ch;

    switch (offset) {
    case ATMEL_US_CR:
        if (value & (ATMEL_US_RSTRX | ATMEL_US_RXDIS)) {
            usart->rx_ready = false;
            usart->rx_status = 0;
            qemu_chr_fe_accept_input(&usart->chr);
        }
        if (value & ATMEL_US_STTTO) {
            usart->rx_status &= ~ATMEL_US_TIMEOUT;
            avr32exp_usart_update_irq(usart);
        }
        if (value & ATMEL_US_RXEN) {
            qemu_chr_fe_accept_input(&usart->chr);
        }
        break;
    case ATMEL_US_IER:
        usart->imr |= value;
        avr32exp_usart_update_irq(usart);
        break;
    case ATMEL_US_IDR:
        usart->imr &= ~value;
        avr32exp_usart_update_irq(usart);
        break;
    case ATMEL_US_THR:
        ch = value;
        if (qemu_chr_fe_backend_connected(&usart->chr)) {
            qemu_chr_fe_write_all(&usart->chr, &ch, 1);
        }
        avr32exp_usart_update_irq(usart);
        break;
    case ATMEL_PDC_RPR:
        usart->rpr = value;
        break;
    case ATMEL_PDC_RCR:
        usart->rcr = value;
        if (value != 0) {
            usart->rx_status &= ~ATMEL_US_ENDRX;
            qemu_chr_fe_accept_input(&usart->chr);
        }
        break;
    case ATMEL_PDC_TPR:
        usart->tpr = value;
        break;
    case ATMEL_PDC_TCR:
        usart->tcr = value;
        avr32exp_usart_drain_tx_pdc(usart);
        break;
    case ATMEL_PDC_RNPR:
        usart->rnpr = value;
        break;
    case ATMEL_PDC_RNCR:
        usart->rncr = value;
        break;
    case ATMEL_PDC_TNPR:
        usart->tnpr = value;
        break;
    case ATMEL_PDC_TNCR:
        usart->tncr = value;
        break;
    case ATMEL_PDC_PTCR:
        if (value & ATMEL_PDC_TXTDIS) {
            usart->ptsr &= ~ATMEL_PDC_TXTEN;
        }
        if (value & ATMEL_PDC_RXTDIS) {
            usart->ptsr &= ~ATMEL_PDC_RXTEN;
        }
        if (value & ATMEL_PDC_TXTEN) {
            usart->ptsr |= ATMEL_PDC_TXTEN;
            avr32exp_usart_drain_tx_pdc(usart);
        }
        if (value & ATMEL_PDC_RXTEN) {
            usart->ptsr |= ATMEL_PDC_RXTEN;
            qemu_chr_fe_accept_input(&usart->chr);
        }
        avr32exp_usart_update_irq(usart);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps avr32exp_usart_ops = {
    .read = avr32exp_usart_read,
    .write = avr32exp_usart_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

// This functions sets up the device
static void avr32exp_realize(DeviceState *dev, Error **errp)
{
    AVR32EXPMcuState *s = AVR32EXP_MCU(dev);
    const AVR32EXPMcuClass *mc = AVR32EXP_MCU_GET_CLASS(dev);
    static const char * const usart_names[] = {
        "atmel-usart0", "atmel-usart1", "atmel-usart2", "atmel-usart3",
    };
    int i;

    /* CPU */
    object_initialize_child(OBJECT(dev), "cpu", &s->cpu, mc->cpu_type);
    object_property_set_bool(OBJECT(&s->cpu), "realized", true, &error_abort);

    /* Flash */
    memory_region_init_rom(&s->flash, OBJECT(dev),
                           "flash", mc->flash_size, &error_fatal);
    memory_region_add_subregion(get_system_memory(),
                                AVR32EXP_FLASH_BASE, &s->flash);

    /* SRAM */
    memory_region_init_ram(&s->sram, OBJECT(dev),
                           "sram", mc->sram_size, &error_fatal);
    memory_region_add_subregion(get_system_memory(),
                                AVR32EXP_SRAM_BASE, &s->sram);

    /* Linux for AT32AP700x expects SDRAM at 0x10000000 and executes
     * through AVR32's P1/P2 virtual aliases while this target still uses
     * identity TLB translation.
     */
    memory_region_init_ram(&s->ram, OBJECT(dev),
                           "sdram", mc->ram_size, &error_fatal);
    memory_region_add_subregion(get_system_memory(),
                                AVR32EXP_RAM_BASE, &s->ram);

    memory_region_init_alias(&s->ram_p1_alias, OBJECT(dev),
                             "sdram-p1-alias", &s->ram, 0, mc->ram_size);
    memory_region_add_subregion(get_system_memory(),
                                AVR32EXP_RAM_P1_ALIAS_BASE,
                                &s->ram_p1_alias);

    memory_region_init_alias(&s->ram_p2_alias, OBJECT(dev),
                             "sdram-p2-alias", &s->ram, 0, mc->ram_size);
    memory_region_add_subregion(get_system_memory(),
                                AVR32EXP_RAM_P2_ALIAS_BASE,
                                &s->ram_p2_alias);

    s->intc_pending_irq = -1;
    memory_region_init_io(&s->intc_iomem, OBJECT(dev), &avr32exp_intc_ops, s,
                          "at32ap-intc", AVR32EXP_INTC_SIZE);
    memory_region_add_subregion(get_system_memory(), AVR32EXP_INTC_BASE,
                                &s->intc_iomem);

    for (i = 0; i < ARRAY_SIZE(s->usart); i++) {
        s->usart[i].mcu = s;
        s->usart[i].irq = 6 + i;
        s->usart[i].rx_status = 0;
        s->usart[i].rx_ready = false;
        memory_region_init_io(&s->usart[i].iomem, OBJECT(dev),
                              &avr32exp_usart_ops, &s->usart[i],
                              usart_names[i], AVR32EXP_USART_SIZE);
        memory_region_add_subregion(get_system_memory(),
                                    AVR32EXP_USART0_BASE +
                                    i * AVR32EXP_USART_STRIDE,
                                    &s->usart[i].iomem);
    }

    /*
     * ATSTK1002 maps hardware USART1 to Linux ttyS0, so the board console
     * should consume QEMU's first serial backend.
     */
    qemu_chr_fe_init(&s->usart[1].chr,
                     serial_hd(0) ?: qemu_chr_new("avr32-usart1-null",
                                                  "null", NULL),
                     &error_abort);
    qemu_chr_fe_set_handlers(&s->usart[1].chr, avr32exp_usart_can_receive,
                             avr32exp_usart_receive, NULL, NULL,
                             &s->usart[1], NULL, true);
}

static void avr32exp_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = avr32exp_realize;
    dc->user_creatable = false;
}

static void avr32exps_class_init(ObjectClass *oc, void *data){

    AVR32EXPMcuClass* avr32exp = AVR32EXP_MCU_CLASS(oc);

    avr32exp->cpu_type = AVR32A_CPU_TYPE_NAME("AVR32EXPC");
    avr32exp->flash_size = 1024 * KiB;
    avr32exp->sram_size = 64 * KiB;
    avr32exp->ram_size = 64 * MiB;
}

static const TypeInfo avr32exp_mcu_types[] = {
        {
                .name           = TYPE_AVR32EXPS_MCU,
                .parent         = TYPE_AVR32EXP_MCU,
                .class_init     = avr32exps_class_init,
        }, {
                .name           = TYPE_AVR32EXP_MCU,
                .parent         = TYPE_SYS_BUS_DEVICE,
                .instance_size  = sizeof(AVR32EXPMcuState),
                .class_size     = sizeof(AVR32EXPMcuClass),
                .class_init     = avr32exp_class_init,
                .abstract       = true,
        }
};

DEFINE_TYPES(avr32exp_mcu_types)
