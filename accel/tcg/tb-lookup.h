/*
 * Copyright (C) 2017, Emilio G. Cota <cota@braap.org>
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 */
#ifndef EXEC_TB_LOOKUP_H
#define EXEC_TB_LOOKUP_H

#ifdef NEED_CPU_H
#include "cpu.h"
#else
#include "exec/poison.h"
#endif

#include "exec/exec-all.h"
#include "tb-hash.h"

/* Might cause an exception, so have a longjmp destination ready */
static inline TranslationBlock *tb_lookup(CPUState *cpu, target_ulong pc,
                                          target_ulong cs_base,
                                          uint32_t flags, uint32_t cflags)
{
    TranslationBlock *tb;
    uint32_t hash;

    /* we should never be trying to look up an INVALID tb */
    tcg_debug_assert(!(cflags & CF_INVALID));

    hash = tb_jmp_cache_hash_func(pc);
    tb = qatomic_rcu_read(&cpu->tb_jmp_cache[hash]);

    if (likely(tb &&
               tb->pc == pc &&
               tb->cs_base == cs_base &&
               tb->flags == flags &&
               tb->trace_vcpu_dstate == *cpu->trace_dstate &&
               tb_cflags(tb) == cflags)) {
        return tb;
    }
    tb = tb_htable_lookup(cpu, pc, cs_base, flags, cflags);
    if (tb == NULL) {
        return NULL;
    }
    qatomic_set(&cpu->tb_jmp_cache[hash], tb);
    return tb;
}

#endif /* EXEC_TB_LOOKUP_H */
