/*
 *  HPPA memory access helper routines
 *
 *  Copyright (c) 2017 Helge Deller
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "hw/core/cpu.h"
#include "trace.h"

static hppa_tlb_entry *hppa_find_tlb(CPUHPPAState *env, vaddr addr)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(env->tlb); ++i) {
        hppa_tlb_entry *ent = &env->tlb[i];
        if (ent->va_b <= addr && addr <= ent->va_e) {
            trace_hppa_tlb_find_entry(env, ent + i, ent->entry_valid,
                                      ent->va_b, ent->va_e, ent->pa);
            return ent;
        }
    }
    trace_hppa_tlb_find_entry_not_found(env, addr);
    return NULL;
}

static void hppa_flush_tlb_ent(CPUHPPAState *env, hppa_tlb_entry *ent,
                               bool force_flush_btlb)
{
    CPUState *cs = env_cpu(env);

    if (!ent->entry_valid) {
        return;
    }

    trace_hppa_tlb_flush_ent(env, ent, ent->va_b, ent->va_e, ent->pa);

    tlb_flush_range_by_mmuidx(cs, ent->va_b,
                                ent->va_e - ent->va_b + 1,
                                HPPA_MMU_FLUSH_MASK, TARGET_LONG_BITS);

    /* never clear BTLBs, unless forced to do so. */
    if (ent < &env->tlb[HPPA_BTLB_ENTRIES] && !force_flush_btlb) {
        return;
    }

    memset(ent, 0, sizeof(*ent));
    ent->va_b = -1;
}

static hppa_tlb_entry *hppa_alloc_tlb_ent(CPUHPPAState *env)
{
    hppa_tlb_entry *ent;
    uint32_t i;

    if (env->tlb_last < HPPA_BTLB_ENTRIES || env->tlb_last >= ARRAY_SIZE(env->tlb)) {
        i = HPPA_BTLB_ENTRIES;
        env->tlb_last = HPPA_BTLB_ENTRIES + 1;
    } else {
        i = env->tlb_last;
        env->tlb_last++;
    }

    ent = &env->tlb[i];

    hppa_flush_tlb_ent(env, ent, false);
    return ent;
}

int hppa_get_physical_address(CPUHPPAState *env, vaddr addr, int mmu_idx,
                              int type, hwaddr *pphys, int *pprot,
                              hppa_tlb_entry **tlb_entry)
{
    hwaddr phys;
    int prot, r_prot, w_prot, x_prot, priv;
    hppa_tlb_entry *ent;
    int ret = -1;

    if (tlb_entry) {
        *tlb_entry = NULL;
    }

    /* Virtual translation disabled.  Direct map virtual to physical.  */
    if (mmu_idx == MMU_PHYS_IDX) {
        phys = addr;
        prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        goto egress;
    }

    /* Find a valid tlb entry that matches the virtual address.  */
    ent = hppa_find_tlb(env, addr);
    if (ent == NULL || !ent->entry_valid) {
        phys = 0;
        prot = 0;
        ret = (type == PAGE_EXEC) ? EXCP_ITLB_MISS : EXCP_DTLB_MISS;
        goto egress;
    }

    if (tlb_entry) {
        *tlb_entry = ent;
    }

    /* We now know the physical address.  */
    phys = ent->pa + (addr - ent->va_b);

    /* Map TLB access_rights field to QEMU protection.  */
    priv = MMU_IDX_TO_PRIV(mmu_idx);
    r_prot = (priv <= ent->ar_pl1) * PAGE_READ;
    w_prot = (priv <= ent->ar_pl2) * PAGE_WRITE;
    x_prot = (ent->ar_pl2 <= priv && priv <= ent->ar_pl1) * PAGE_EXEC;
    switch (ent->ar_type) {
    case 0: /* read-only: data page */
        prot = r_prot;
        break;
    case 1: /* read/write: dynamic data page */
        prot = r_prot | w_prot;
        break;
    case 2: /* read/execute: normal code page */
        prot = r_prot | x_prot;
        break;
    case 3: /* read/write/execute: dynamic code page */
        prot = r_prot | w_prot | x_prot;
        break;
    default: /* execute: promote to privilege level type & 3 */
        prot = x_prot;
        break;
    }

    /* access_id == 0 means public page and no check is performed */
    if ((env->psw & PSW_P) && ent->access_id) {
        /* If bits [31:1] match, and bit 0 is set, suppress write.  */
        int match = ent->access_id * 2 + 1;

        if (match == env->cr[CR_PID1] || match == env->cr[CR_PID2] ||
            match == env->cr[CR_PID3] || match == env->cr[CR_PID4]) {
            prot &= PAGE_READ | PAGE_EXEC;
            if (type == PAGE_WRITE) {
                ret = EXCP_DMPI;
                goto egress;
            }
        }
    }

    /* No guest access type indicates a non-architectural access from
       within QEMU.  Bypass checks for access, D, B and T bits.  */
    if (type == 0) {
        goto egress;
    }

    if (unlikely(!(prot & type))) {
        /* The access isn't allowed -- Inst/Data Memory Protection Fault.  */
        ret = (type & PAGE_EXEC) ? EXCP_IMP : EXCP_DMAR;
        goto egress;
    }

    /* In reverse priority order, check for conditions which raise faults.
       As we go, remove PROT bits that cover the condition we want to check.
       In this way, the resulting PROT will force a re-check of the
       architectural TLB entry for the next access.  */
    if (unlikely(!ent->d)) {
        if (type & PAGE_WRITE) {
            /* The D bit is not set -- TLB Dirty Bit Fault.  */
            ret = EXCP_TLB_DIRTY;
        }
        prot &= PAGE_READ | PAGE_EXEC;
    }
    if (unlikely(ent->b)) {
        if (type & PAGE_WRITE) {
            /* The B bit is set -- Data Memory Break Fault.  */
            ret = EXCP_DMB;
        }
        prot &= PAGE_READ | PAGE_EXEC;
    }
    if (unlikely(ent->t)) {
        if (!(type & PAGE_EXEC)) {
            /* The T bit is set -- Page Reference Fault.  */
            ret = EXCP_PAGE_REF;
        }
        prot &= PAGE_EXEC;
    }

 egress:
    *pphys = phys;
    *pprot = prot;
    trace_hppa_tlb_get_physical_address(env, ret, prot, addr, phys);
    return ret;
}

hwaddr hppa_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    HPPACPU *cpu = HPPA_CPU(cs);
    hwaddr phys;
    int prot, excp;

    /* If the (data) mmu is disabled, bypass translation.  */
    /* ??? We really ought to know if the code mmu is disabled too,
       in order to get the correct debugging dumps.  */
    if (!(cpu->env.psw & PSW_D)) {
        return addr;
    }

    excp = hppa_get_physical_address(&cpu->env, addr, MMU_KERNEL_IDX, 0,
                                     &phys, &prot, NULL);

    /* Since we're translating for debugging, the only error that is a
       hard error is no translation at all.  Otherwise, while a real cpu
       access might not have permission, the debugger does.  */
    return excp == EXCP_DTLB_MISS ? -1 : phys;
}

bool hppa_cpu_tlb_fill(CPUState *cs, vaddr addr, int size,
                       MMUAccessType type, int mmu_idx,
                       bool probe, uintptr_t retaddr)
{
    HPPACPU *cpu = HPPA_CPU(cs);
    CPUHPPAState *env = &cpu->env;
    hppa_tlb_entry *ent;
    int prot, excp, a_prot;
    hwaddr phys;

    switch (type) {
    case MMU_INST_FETCH:
        a_prot = PAGE_EXEC;
        break;
    case MMU_DATA_STORE:
        a_prot = PAGE_WRITE;
        break;
    default:
        a_prot = PAGE_READ;
        break;
    }

    excp = hppa_get_physical_address(env, addr, mmu_idx,
                                     a_prot, &phys, &prot, &ent);
    if (unlikely(excp >= 0)) {
        if (probe) {
            return false;
        }
        trace_hppa_tlb_fill_excp(env, addr, size, type, mmu_idx);
        /* Failure.  Raise the indicated exception.  */
        cs->exception_index = excp;
        if (cpu->env.psw & PSW_Q) {
            /* ??? Needs tweaking for hppa64.  */
            cpu->env.cr[CR_IOR] = addr;
            cpu->env.cr[CR_ISR] = addr >> 32;
        }
        cpu_loop_exit_restore(cs, retaddr);
    }

    trace_hppa_tlb_fill_success(env, addr & TARGET_PAGE_MASK,
                                phys & TARGET_PAGE_MASK, size, type, mmu_idx);
    /* Success!  Store the translation into the QEMU TLB.  */
    tlb_set_page(cs, addr & TARGET_PAGE_MASK, phys & TARGET_PAGE_MASK,
                 prot, mmu_idx, TARGET_PAGE_SIZE << (ent ? 2 * ent->page_size : 0));
    return true;
}

/* Insert (Insn/Data) TLB Address.  Note this is PA 1.1 only.  */
void HELPER(itlba)(CPUHPPAState *env, target_ulong addr, target_ureg reg)
{
    hppa_tlb_entry *empty = NULL;
    int i;

    /* Zap any old entries covering ADDR; notice empty entries on the way.  */
    for (i = HPPA_BTLB_ENTRIES; i < ARRAY_SIZE(env->tlb); ++i) {
        hppa_tlb_entry *ent = &env->tlb[i];
        if (ent->va_b <= addr && addr <= ent->va_e) {
            if (ent->entry_valid) {
                hppa_flush_tlb_ent(env, ent, false);
            }
            if (!empty) {
                empty = ent;
            }
        }
    }

    /* If we didn't see an empty entry, evict one.  */
    if (empty == NULL) {
        empty = hppa_alloc_tlb_ent(env);
    }

    /* Note that empty->entry_valid == 0 already.  */
    empty->va_b = addr & TARGET_PAGE_MASK;
    empty->va_e = empty->va_b + TARGET_PAGE_SIZE - 1;
    empty->pa = extract32(reg, 5, 20) << TARGET_PAGE_BITS;
    trace_hppa_tlb_itlba(env, empty, empty->va_b, empty->va_e, empty->pa);
}

static void set_access_bits(CPUHPPAState *env, hppa_tlb_entry *ent, target_ureg reg)
{
    ent->access_id = extract32(reg, 1, 18);
    ent->u = extract32(reg, 19, 1);
    ent->ar_pl2 = extract32(reg, 20, 2);
    ent->ar_pl1 = extract32(reg, 22, 2);
    ent->ar_type = extract32(reg, 24, 3);
    ent->b = extract32(reg, 27, 1);
    ent->d = extract32(reg, 28, 1);
    ent->t = extract32(reg, 29, 1);
    ent->entry_valid = 1;
    trace_hppa_tlb_itlbp(env, ent, ent->access_id, ent->u, ent->ar_pl2,
                         ent->ar_pl1, ent->ar_type, ent->b, ent->d, ent->t);
}

/* Insert (Insn/Data) TLB Protection.  Note this is PA 1.1 only.  */
void HELPER(itlbp)(CPUHPPAState *env, target_ulong addr, target_ureg reg)
{
    hppa_tlb_entry *ent = hppa_find_tlb(env, addr);

    if (unlikely(ent == NULL)) {
        qemu_log_mask(LOG_GUEST_ERROR, "ITLBP not following ITLBA\n");
        return;
    }

    set_access_bits(env, ent, reg);
}

/* Purge (Insn/Data) TLB.  This is explicitly page-based, and is
   synchronous across all processors.  */
static void ptlb_work(CPUState *cpu, run_on_cpu_data data)
{
    CPUHPPAState *env = cpu_env(cpu);
    target_ulong addr = (target_ulong) data.target_ptr;
    hppa_tlb_entry *ent = hppa_find_tlb(env, addr);

    if (ent && ent->entry_valid) {
        hppa_flush_tlb_ent(env, ent, false);
    }
}

void HELPER(ptlb)(CPUHPPAState *env, target_ulong addr)
{
    CPUState *src = env_cpu(env);
    CPUState *cpu;
    trace_hppa_tlb_ptlb(env);
    run_on_cpu_data data = RUN_ON_CPU_TARGET_PTR(addr);

    CPU_FOREACH(cpu) {
        if (cpu != src) {
            async_run_on_cpu(cpu, ptlb_work, data);
        }
    }
    async_safe_run_on_cpu(src, ptlb_work, data);
}

/* Purge (Insn/Data) TLB entry.  This affects an implementation-defined
   number of pages/entries (we choose all), and is local to the cpu.  */
void HELPER(ptlbe)(CPUHPPAState *env)
{
    trace_hppa_tlb_ptlbe(env);
    qemu_log_mask(CPU_LOG_MMU, "FLUSH ALL TLB ENTRIES\n");
    memset(&env->tlb[HPPA_BTLB_ENTRIES], 0,
        sizeof(env->tlb) - HPPA_BTLB_ENTRIES * sizeof(env->tlb[0]));
    env->tlb_last = HPPA_BTLB_ENTRIES;
    tlb_flush_by_mmuidx(env_cpu(env), HPPA_MMU_FLUSH_MASK);
}

void cpu_hppa_change_prot_id(CPUHPPAState *env)
{
    if (env->psw & PSW_P) {
        tlb_flush_by_mmuidx(env_cpu(env), HPPA_MMU_FLUSH_MASK);
    }
}

void HELPER(change_prot_id)(CPUHPPAState *env)
{
    cpu_hppa_change_prot_id(env);
}

target_ureg HELPER(lpa)(CPUHPPAState *env, target_ulong addr)
{
    hwaddr phys;
    int prot, excp;

    excp = hppa_get_physical_address(env, addr, MMU_KERNEL_IDX, 0,
                                     &phys, &prot, NULL);
    if (excp >= 0) {
        if (env->psw & PSW_Q) {
            /* ??? Needs tweaking for hppa64.  */
            env->cr[CR_IOR] = addr;
            env->cr[CR_ISR] = addr >> 32;
        }
        if (excp == EXCP_DTLB_MISS) {
            excp = EXCP_NA_DTLB_MISS;
        }
        trace_hppa_tlb_lpa_failed(env, addr);
        hppa_dynamic_excp(env, excp, GETPC());
    }
    trace_hppa_tlb_lpa_success(env, addr, phys);
    return phys;
}

/* Return the ar_type of the TLB at VADDR, or -1.  */
int hppa_artype_for_page(CPUHPPAState *env, target_ulong vaddr)
{
    hppa_tlb_entry *ent = hppa_find_tlb(env, vaddr);
    return ent ? ent->ar_type : -1;
}

/*
 * diag_btlb() emulates the PDC PDC_BLOCK_TLB firmware call to
 * allow operating systems to modify the Block TLB (BTLB) entries.
 * For implementation details see page 1-13 in
 * https://parisc.wiki.kernel.org/images-parisc/e/ef/Pdc11-v0.96-Ch1-procs.pdf
 */
void HELPER(diag_btlb)(CPUHPPAState *env)
{
    unsigned int phys_page, len, slot;
    int mmu_idx = cpu_mmu_index(env, 0);
    uintptr_t ra = GETPC();
    hppa_tlb_entry *btlb;
    uint64_t virt_page;
    uint32_t *vaddr;

#ifdef TARGET_HPPA64
    /* BTLBs are not supported on 64-bit CPUs */
    env->gr[28] = -1; /* nonexistent procedure */
    return;
#endif
    env->gr[28] = 0; /* PDC_OK */

    switch (env->gr[25]) {
    case 0:
        /* return BTLB parameters */
        qemu_log_mask(CPU_LOG_MMU, "PDC_BLOCK_TLB: PDC_BTLB_INFO\n");
        vaddr = probe_access(env, env->gr[24], 4 * sizeof(target_ulong),
                             MMU_DATA_STORE, mmu_idx, ra);
        if (vaddr == NULL) {
            env->gr[28] = -10; /* invalid argument */
        } else {
            vaddr[0] = cpu_to_be32(1);
            vaddr[1] = cpu_to_be32(16 * 1024);
            vaddr[2] = cpu_to_be32(HPPA_BTLB_FIXED);
            vaddr[3] = cpu_to_be32(HPPA_BTLB_VARIABLE);
        }
        break;
    case 1:
        /* insert BTLB entry */
        virt_page = env->gr[24];        /* upper 32 bits */
        virt_page <<= 32;
        virt_page |= env->gr[23];       /* lower 32 bits */
        phys_page = env->gr[22];
        len = env->gr[21];
        slot = env->gr[19];
        qemu_log_mask(CPU_LOG_MMU, "PDC_BLOCK_TLB: PDC_BTLB_INSERT "
                    "0x%08llx-0x%08llx: vpage 0x%llx for phys page 0x%04x len %d "
                    "into slot %d\n",
                    (long long) virt_page << TARGET_PAGE_BITS,
                    (long long) (virt_page + len) << TARGET_PAGE_BITS,
                    (long long) virt_page, phys_page, len, slot);
        if (slot < HPPA_BTLB_ENTRIES) {
            btlb = &env->tlb[slot];
            /* force flush of possibly existing BTLB entry */
            hppa_flush_tlb_ent(env, btlb, true);
            /* create new BTLB entry */
            btlb->va_b = virt_page << TARGET_PAGE_BITS;
            btlb->va_e = btlb->va_b + len * TARGET_PAGE_SIZE - 1;
            btlb->pa = phys_page << TARGET_PAGE_BITS;
            set_access_bits(env, btlb, env->gr[20]);
            btlb->t = 0;
            btlb->d = 1;
        } else {
            env->gr[28] = -10; /* invalid argument */
        }
        break;
    case 2:
        /* Purge BTLB entry */
        slot = env->gr[22];
        qemu_log_mask(CPU_LOG_MMU, "PDC_BLOCK_TLB: PDC_BTLB_PURGE slot %d\n",
                                    slot);
        if (slot < HPPA_BTLB_ENTRIES) {
            btlb = &env->tlb[slot];
            hppa_flush_tlb_ent(env, btlb, true);
        } else {
            env->gr[28] = -10; /* invalid argument */
        }
        break;
    case 3:
        /* Purge all BTLB entries */
        qemu_log_mask(CPU_LOG_MMU, "PDC_BLOCK_TLB: PDC_BTLB_PURGE_ALL\n");
        for (slot = 0; slot < HPPA_BTLB_ENTRIES; slot++) {
            btlb = &env->tlb[slot];
            hppa_flush_tlb_ent(env, btlb, true);
        }
        break;
    default:
        env->gr[28] = -2; /* nonexistent option */
        break;
    }
}
