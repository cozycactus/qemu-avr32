/*
 * QEMU AVR CPU
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
#include "cpu.h"
#include "exec/exec-all.h"
#include "tcg/tcg.h"
#include "exec/helper-proto.h"
#include "hw/avr32/boot.h"
#include "qemu/qemu-print.h"
#include "qemu/timer.h"

#define SYSREG_EVBA_WORD (0x0004 / 4)
#define SYSREG_ECR_WORD (0x0010 / 4)
#define SYSREG_RSR_EX_WORD (0x0028 / 4)
#define SYSREG_RSR_INT0_WORD (0x0018 / 4)
#define SYSREG_RAR_EX_WORD (0x0048 / 4)
#define SYSREG_RAR_INT0_WORD (0x0038 / 4)
#define SYSREG_TLBEHI_WORD (0x0110 / 4)
#define SYSREG_TLBELO_WORD (0x0114 / 4)
#define SYSREG_TLBEAR_WORD (0x011c / 4)
#define SYSREG_MMUCR_WORD  (0x0120 / 4)
#define SYSREG_TLBARLO_WORD (0x0124 / 4)

#define TLBEHI_VALID (1u << 9)
#define TLBEHI_I     (1u << 8)
#define TLBELO_GLOBAL (1u << 8)
#define MMUCR_N      (1u << 3)
#define MMUCR_DRP_SHIFT 14
#define MMUCR_IRP_SHIFT 26
#define MMUCR_RP_MASK 0x3f
#define AVR32_PAGE_MASK 0xfffff000u
#define AVR32_PAGE_PRESENT (1u << 10)
#define AVR32_P1SEG 0x80000000u
#define AVR32_PGDIR_SHIFT 22
#define AVR32_PTRS_PER_PTE 1024
#define AVR32_ECR_TLB_MISS_X 20
#define AVR32_ECR_TLB_MISS_W 28
#define AVR32_MODE_INT0 2
#define AVR32_MODE_EXCEPTION 6

static inline void raise_exception(CPUAVR32AState *env, int index,
        uintptr_t retaddr);

static inline G_NORETURN void raise_exception(CPUAVR32AState *env, int index,
                                              uintptr_t retaddr)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = index;
    cpu_loop_exit_restore(cs, retaddr);
}

void helper_raise_illegal_instruction(CPUAVR32AState *env)
{
    static unsigned illegal_log_count;

    if (illegal_log_count < 16) {
        qemu_fprintf(stderr, "[%s] Illegal instruction @ %#010x\n",
                     __FUNCTION__, env->r[AVR32A_PC_REG]);
    } else if (illegal_log_count == 16) {
        qemu_fprintf(stderr, "[%s] further illegal-instruction logs suppressed\n",
                     __FUNCTION__);
    }
    illegal_log_count++;
    raise_exception(env, 23, 0);
}

static bool avr32_tlb_entry_matches(const AVR32TLBEntry *entry,
                                    uint32_t vaddr, uint32_t asid)
{
    if (!(entry->hi & TLBEHI_VALID)) {
        return false;
    }

    if ((entry->hi & AVR32_PAGE_MASK) != (vaddr & AVR32_PAGE_MASK)) {
        return false;
    }

    if (!(entry->lo & TLBELO_GLOBAL) && (entry->hi & 0xff) != asid) {
        return false;
    }

    return true;
}

static bool avr32_tlb_lookup(CPUAVR32AState *env, uint32_t vaddr,
                             hwaddr *paddr, unsigned *entry_index)
{
    uint32_t asid = env->sysr[SYSREG_TLBEHI_WORD] & 0xff;

    for (unsigned i = 0; i < AVR32_TLB_ENTRIES; i++) {
        if (avr32_tlb_entry_matches(&env->tlb[i], vaddr, asid)) {
            if (paddr) {
                *paddr = (env->tlb[i].lo & AVR32_PAGE_MASK)
                       | (vaddr & ~AVR32_PAGE_MASK);
            }
            if (entry_index) {
                *entry_index = i;
            }
            return true;
        }
    }

    return false;
}

static bool avr32_read_u32(CPUState *cs, hwaddr addr, uint32_t *value)
{
    MemTxResult result;

    *value = address_space_ldl_be(cs->as, addr, MEMTXATTRS_UNSPECIFIED,
                                  &result);
    return result == MEMTX_OK;
}

static bool avr32_page_table_lookup(CPUState *cs, CPUAVR32AState *env,
                                    uint32_t vaddr, hwaddr *paddr)
{
    uint32_t ptbr = env->sysr[0x0118 / 4];
    uint32_t pgd;
    uint32_t pte;
    uint32_t pgd_addr;
    uint32_t pte_addr;

    if (ptbr == 0) {
        return false;
    }

    pgd_addr = ptbr + ((vaddr >> AVR32_PGDIR_SHIFT) * sizeof(uint32_t));
    if (!avr32_read_u32(cs, pgd_addr, &pgd) || pgd == 0) {
        return false;
    }

    pte_addr = (pgd & AVR32_PAGE_MASK) | AVR32_P1SEG;
    pte_addr += ((vaddr >> 12) & (AVR32_PTRS_PER_PTE - 1)) * sizeof(uint32_t);
    if (!avr32_read_u32(cs, pte_addr, &pte)
        || !(pte & AVR32_PAGE_PRESENT)) {
        return false;
    }

    *paddr = (pte & AVR32_PAGE_MASK) | (vaddr & ~AVR32_PAGE_MASK);
    return true;
}

static unsigned avr32_tlb_replacement_index(CPUAVR32AState *env)
{
    uint32_t tlbehi = env->sysr[SYSREG_TLBEHI_WORD];
    uint32_t mmucr = env->sysr[SYSREG_MMUCR_WORD];
    unsigned shift = (tlbehi & TLBEHI_I) ? MMUCR_IRP_SHIFT : MMUCR_DRP_SHIFT;

    return ((mmucr >> shift) & MMUCR_RP_MASK) % AVR32_TLB_ENTRIES;
}

void helper_avr32_tlbs(CPUAVR32AState *env)
{
    uint32_t query_hi = env->sysr[SYSREG_TLBEHI_WORD];
    uint32_t query_asid = query_hi & 0xff;
    unsigned found = 0;

    for (unsigned i = 0; i < AVR32_TLB_ENTRIES; i++) {
        const AVR32TLBEntry *entry = &env->tlb[i];

        if (!(entry->hi & TLBEHI_VALID)) {
            continue;
        }
        if ((entry->hi & AVR32_PAGE_MASK) != (query_hi & AVR32_PAGE_MASK)) {
            continue;
        }
        if (!(entry->lo & TLBELO_GLOBAL) && (entry->hi & 0xff) != query_asid) {
            continue;
        }

        found = i;
        env->sysr[SYSREG_MMUCR_WORD] &= ~MMUCR_N;
        env->sysr[SYSREG_MMUCR_WORD] &= ~(MMUCR_RP_MASK << MMUCR_DRP_SHIFT);
        env->sysr[SYSREG_MMUCR_WORD] |= found << MMUCR_DRP_SHIFT;
        env->sysr[SYSREG_TLBEHI_WORD] = entry->hi;
        env->sysr[SYSREG_TLBELO_WORD] = entry->lo;
        if (found < 32) {
            env->sysr[SYSREG_TLBARLO_WORD] &= ~(0x80000000u >> found);
        }
        return;
    }

    env->sysr[SYSREG_MMUCR_WORD] |= MMUCR_N;
    env->sysr[SYSREG_TLBARLO_WORD] = 0xffffffffu;
}

void helper_avr32_tlbw(CPUAVR32AState *env)
{
    unsigned index = avr32_tlb_replacement_index(env);

    env->tlb[index].hi = env->sysr[SYSREG_TLBEHI_WORD];
    env->tlb[index].lo = env->sysr[SYSREG_TLBELO_WORD];

    if (index < 32) {
        env->sysr[SYSREG_TLBARLO_WORD] &= ~(0x80000000u >> index);
    }

    tlb_flush(env_cpu(env));
}

static uint32_t avr32_pack_sr(CPUAVR32AState *env)
{
    uint32_t sr = 0;

    for (unsigned i = 0; i < 32; i++) {
        sr |= (env->sflags[i] & 1) << i;
    }

    return sr;
}

static void avr32_set_mode(CPUAVR32AState *env, unsigned mode)
{
    env->sflags[22] = mode & 1;
    env->sflags[23] = (mode >> 1) & 1;
    env->sflags[24] = (mode >> 2) & 1;
}

static unsigned avr32_get_mode(CPUAVR32AState *env)
{
    return (env->sflags[22] & 1)
         | ((env->sflags[23] & 1) << 1)
         | ((env->sflags[24] & 1) << 2);
}

static void avr32_unpack_sr(CPUAVR32AState *env, uint32_t sr)
{
    for (unsigned i = 0; i < 32; i++) {
        env->sflags[i] = (sr >> i) & 1;
    }
}

void helper_avr32_tlb_miss(CPUAVR32AState *env, uint32_t address,
                           uint32_t ecr)
{
    CPUState *cs = env_cpu(env);
    uint32_t vector;

    if (ecr == AVR32_ECR_TLB_MISS_X) {
        vector = 0x50;
        env->sysr[SYSREG_TLBEHI_WORD] = (env->sysr[SYSREG_TLBEHI_WORD] & 0xff)
                                      | (address & AVR32_PAGE_MASK)
                                      | TLBEHI_I;
    } else {
        vector = ecr == AVR32_ECR_TLB_MISS_W ? 0x70 : 0x60;
        env->sysr[SYSREG_TLBEHI_WORD] = (env->sysr[SYSREG_TLBEHI_WORD] & 0xff)
                                      | (address & AVR32_PAGE_MASK);
    }

    env->sysr[SYSREG_ECR_WORD] = ecr;
    env->sysr[SYSREG_TLBEAR_WORD] = address;
    env->sysr[SYSREG_RAR_EX_WORD] = env->r[AVR32A_PC_REG];
    env->sysr[SYSREG_RSR_EX_WORD] = avr32_pack_sr(env);
    env->r[AVR32A_PC_REG] = env->sysr[SYSREG_EVBA_WORD] + vector;
    avr32_set_mode(env, AVR32_MODE_EXCEPTION);

    tlb_flush(cs);
    cpu_loop_exit_restore(cs, 0);
}

bool avr32_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                        MMUAccessType access_type, int mmu_idx,
                        bool probe, uintptr_t retaddr)
{
    AVR32ACPU *cpu = AVR32A_CPU(cs);
    CPUAVR32AState *env = &cpu->env;
    hwaddr paddr;
    int port = 0;

    port = PAGE_READ | PAGE_EXEC | PAGE_WRITE;
    if ((address < 0x80000000u)
        && avr32_tlb_lookup(env, address, &paddr, NULL)) {
        tlb_set_page(cs, address & TARGET_PAGE_MASK,
                     paddr & TARGET_PAGE_MASK, port,
                     mmu_idx, TARGET_PAGE_SIZE);
        return true;
    }

    if ((address < 0x80000000u)
        && avr32_page_table_lookup(cs, env, address, &paddr)) {
        tlb_set_page(cs, address & TARGET_PAGE_MASK,
                     paddr & TARGET_PAGE_MASK, port,
                     mmu_idx, TARGET_PAGE_SIZE);
        return true;
    }

    if (address < 0x80000000u && probe) {
        return false;
    }

    tlb_set_page(cs, address & TARGET_PAGE_MASK,
                 address & TARGET_PAGE_MASK, port,
                 mmu_idx, TARGET_PAGE_SIZE);
    return true;
}

void avr32_cpu_do_interrupt(CPUState *cs)
{
    //TODO: Processor specific
}

hwaddr avr32_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    AVR32ACPU *cpu = AVR32A_CPU(cs);
    hwaddr paddr;

    if ((addr < 0x80000000u)
        && avr32_tlb_lookup(&cpu->env, addr, &paddr, NULL)) {
        return paddr;
    }

    if ((addr < 0x80000000u)
        && avr32_page_table_lookup(cs, &cpu->env, addr, &paddr)) {
        return paddr;
    }

    return addr;
}


void helper_debug(CPUAVR32AState *env)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = EXCP_DEBUG;
    cpu_loop_exit(cs);
}

void helper_break(CPUAVR32AState *env)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = EXCP_DEBUG;
    cpu_loop_exit(cs);
}

uint32_t helper_avr32_count(void)
{
    return qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) / 50;
}

void helper_avr32_tlb_flush(CPUAVR32AState *env)
{
    tlb_flush(env_cpu(env));
}

void helper_avr32_rete(CPUAVR32AState *env)
{
    unsigned mode = avr32_get_mode(env);
    unsigned rar = SYSREG_RAR_EX_WORD;
    unsigned rsr = SYSREG_RSR_EX_WORD;

    if (mode >= AVR32_MODE_INT0 && mode < AVR32_MODE_INT0 + 4) {
        rar = SYSREG_RAR_INT0_WORD + mode - AVR32_MODE_INT0;
        rsr = SYSREG_RSR_INT0_WORD + mode - AVR32_MODE_INT0;
    }

    env->r[AVR32A_PC_REG] = env->sysr[rar];
    avr32_unpack_sr(env, env->sysr[rsr]);
    env->intsrc = -1;
    env->intlevel = 0;
    tlb_flush(env_cpu(env));
}

int avr32_cpu_memory_rw_debug(CPUState *cs, vaddr addr, uint8_t *buf,
                            int len, bool is_write)
{
    return cpu_memory_rw_debug(cs, addr, buf, len, is_write);
}

//TODO: The saturation might be not working as intended. Add more tests.
void helper_macsathhw(CPUAVR32AState *env, uint32_t rd, uint32_t op1,  uint32_t op2)
{
    uint32_t prod = 0;
    if(op1 == -1 && op2 == -1){
        prod = 0x7fffffff;
        env->sflags[sflagQ] = 1;
    }
    else{
        prod = (op1 * op2) << 1;
        if((op1 >> 31) && (op2 >> 31) && !(prod >>31)){
            prod = 0x80000000;
            env->sflags[sflagQ] = 1;
        }
        else if(!(op1 >>31) && !(op2 >> 31) && (prod >>31)){
            prod = 0x7fffffff;
            env->sflags[sflagQ] = 1;
        }
    }

    uint32_t res = prod + env->r[rd];
    if((prod >>31) && (env->r[rd] >>31) && !(res >>31)){
        res = 0x80000000;
        env->sflags[sflagQ] = 1;
    }
    else if(!(prod >>31) && !(env->r[rd] >>31) && (res >>31)){
        res = 0x7fffffff;
        env->sflags[sflagQ] = 1;
    }
    env->r[rd] = res;
}
