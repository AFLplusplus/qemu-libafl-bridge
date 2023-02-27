/*
 * Generic intermediate code generation.
 *
 * Copyright (C) 2016-2017 Lluís Vilanova <vilanova@ac.upc.edu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "tcg/tcg.h"
#include "tcg/tcg-op.h"
#include "exec/exec-all.h"
#include "exec/gen-icount.h"
#include "exec/log.h"
#include "exec/translator.h"
#include "exec/plugin-gen.h"
#include "sysemu/replay.h"

//// --- Begin LibAFL code ---

#include "tcg/tcg-internal.h"

extern target_ulong libafl_gen_cur_pc;

struct libafl_breakpoint {
    target_ulong addr;
    struct libafl_breakpoint* next;
};

extern struct libafl_breakpoint* libafl_qemu_breakpoints;

struct libafl_hook {
    target_ulong addr;
    void (*callback)(uint64_t);
    uint64_t data;
    TCGHelperInfo helper_info;
    struct libafl_hook* next;
};

extern struct libafl_hook* libafl_qemu_hooks;

struct libafl_hook* libafl_search_hook(target_ulong addr);

struct libafl_backdoor_hook {
    void (*exec)(target_ulong pc, uint64_t data);
    uint64_t data;
    TCGHelperInfo helper_info;
    struct libafl_backdoor_hook* next;
};

extern struct libafl_backdoor_hook* libafl_backdoor_hooks;

//// --- End LibAFL code ---

/* Pairs with tcg_clear_temp_count.
   To be called by #TranslatorOps.{translate_insn,tb_stop} if
   (1) the target is sufficiently clean to support reporting,
   (2) as and when all temporaries are known to be consumed.
   For most targets, (2) is at the end of translate_insn.  */
void translator_loop_temp_check(DisasContextBase *db)
{
    if (tcg_check_temp_count()) {
        qemu_log("warning: TCG temporary leaks before "
                 TARGET_FMT_lx "\n", db->pc_next);
    }
}

bool translator_use_goto_tb(DisasContextBase *db, target_ulong dest)
{
    /* Suppress goto_tb if requested. */
    if (tb_cflags(db->tb) & CF_NO_GOTO_TB) {
        return false;
    }

    /* Check for the dest on the same page as the start of the TB.  */
    return ((db->pc_first ^ dest) & TARGET_PAGE_MASK) == 0;
}

void translator_loop(CPUState *cpu, TranslationBlock *tb, int max_insns,
                     target_ulong pc, void *host_pc,
                     const TranslatorOps *ops, DisasContextBase *db)
{
    uint32_t cflags = tb_cflags(tb);
    bool plugin_enabled;

    /* Initialize DisasContext */
    db->tb = tb;
    db->pc_first = pc;
    db->pc_next = pc;
    db->is_jmp = DISAS_NEXT;
    db->num_insns = 0;
    db->max_insns = max_insns;
    db->singlestep_enabled = cflags & CF_SINGLE_STEP;
    db->host_addr[0] = host_pc;
    db->host_addr[1] = NULL;

#ifdef CONFIG_USER_ONLY
    page_protect(pc);
#endif

    ops->init_disas_context(db, cpu);
    tcg_debug_assert(db->is_jmp == DISAS_NEXT);  /* no early exit */

    /* Reset the temp count so that we can identify leaks */
    tcg_clear_temp_count();

    /* Start translating.  */
    gen_tb_start(db->tb);
    ops->tb_start(db, cpu);
    tcg_debug_assert(db->is_jmp == DISAS_NEXT);  /* no early exit */

    plugin_enabled = plugin_gen_tb_start(cpu, db, cflags & CF_MEMI_ONLY);

    while (true) {
        db->num_insns++;
        ops->insn_start(db, cpu);
        tcg_debug_assert(db->is_jmp == DISAS_NEXT);  /* no early exit */

        if (plugin_enabled) {
            plugin_gen_insn_start(cpu, db);
        }

        //// --- Begin LibAFL code ---

        struct libafl_hook* hk = libafl_search_hook(db->pc_next);
        if (hk) {
            TCGv tmp0 = tcg_const_tl(db->pc_next);
            TCGv_i64 tmp1 = tcg_const_i64(hk->data);
#if TARGET_LONG_BITS == 32
            TCGTemp *tmp2[2] = { tcgv_i32_temp(tmp0), tcgv_i64_temp(tmp1) };
#else
            TCGTemp *tmp2[2] = { tcgv_i64_temp(tmp0), tcgv_i64_temp(tmp1) };
#endif
            tcg_gen_callN(hk->callback, NULL, 2, tmp2);
#if TARGET_LONG_BITS == 32
            tcg_temp_free_i32(tmp0);
#else
            tcg_temp_free_i64(tmp0);
#endif
            tcg_temp_free_i64(tmp1);
        }

        struct libafl_breakpoint* bp = libafl_qemu_breakpoints;
        while (bp) {
            if (bp->addr == db->pc_next) {
                TCGv tmp0 = tcg_const_tl(db->pc_next);
                gen_helper_libafl_qemu_handle_breakpoint(cpu_env, tmp0);
#if TARGET_LONG_BITS == 32
                tcg_temp_free_i32(tmp0);
#else
                tcg_temp_free_i64(tmp0);
#endif
            }
            bp = bp->next;
        }

        libafl_gen_cur_pc = db->pc_next;

        // 0x0f, 0x3a, 0xf2, 0x44
        uint8_t backdoor = translator_ldub(cpu->env_ptr, db, db->pc_next);
        if (backdoor == 0x0f) {
            backdoor = translator_ldub(cpu->env_ptr, db, db->pc_next +1);
            if (backdoor == 0x3a) {
                backdoor = translator_ldub(cpu->env_ptr, db, db->pc_next +2);
                if (backdoor == 0xf2) {
                    backdoor = translator_ldub(cpu->env_ptr, db, db->pc_next +3);
                    if (backdoor == 0x44) {
                        struct libafl_backdoor_hook* hk = libafl_backdoor_hooks;
                        while (hk) {
                            TCGv tmp0 = tcg_const_tl(db->pc_next);
                            TCGv_i64 tmp1 = tcg_const_i64(hk->data);
#if TARGET_LONG_BITS == 32
                            TCGTemp *tmp2[2] = { tcgv_i32_temp(tmp0), tcgv_i64_temp(tmp1) };
#else
                            TCGTemp *tmp2[2] = { tcgv_i64_temp(tmp0), tcgv_i64_temp(tmp1) };
#endif
                            tcg_gen_callN(hk->exec, NULL, 2, tmp2);
#if TARGET_LONG_BITS == 32
                            tcg_temp_free_i32(tmp0);
#else
                            tcg_temp_free_i64(tmp0);
#endif
                            tcg_temp_free_i64(tmp1);
                            hk = hk->next;
                        }

                        db->pc_next += 4;
                        goto post_translate_insn;
                    }
                }
            }
        }

        //// --- End LibAFL code ---

        /* Disassemble one instruction.  The translate_insn hook should
           update db->pc_next and db->is_jmp to indicate what should be
           done next -- either exiting this loop or locate the start of
           the next instruction.  */
        if (db->num_insns == db->max_insns && (cflags & CF_LAST_IO)) {
            /* Accept I/O on the last instruction.  */
            gen_io_start();
            ops->translate_insn(db, cpu);
        } else {
            /* we should only see CF_MEMI_ONLY for io_recompile */
            tcg_debug_assert(!(cflags & CF_MEMI_ONLY));
            ops->translate_insn(db, cpu);
        }

post_translate_insn:
        /*
         * We can't instrument after instructions that change control
         * flow although this only really affects post-load operations.
         *
         * Calling plugin_gen_insn_end() before we possibly stop translation
         * is important. Even if this ends up as dead code, plugin generation
         * needs to see a matching plugin_gen_insn_{start,end}() pair in order
         * to accurately track instrumented helpers that might access memory.
         */
        if (plugin_enabled) {
            plugin_gen_insn_end();
        }

        /* Stop translation if translate_insn so indicated.  */
        if (db->is_jmp != DISAS_NEXT) {
            break;
        }

        /* Stop translation if the output buffer is full,
           or we have executed all of the allowed instructions.  */
        if (tcg_op_buf_full() || db->num_insns >= db->max_insns) {
            db->is_jmp = DISAS_TOO_MANY;
            break;
        }
    }

    /* Emit code to exit the TB, as indicated by db->is_jmp.  */
    ops->tb_stop(db, cpu);
    gen_tb_end(db->tb, db->num_insns);

    if (plugin_enabled) {
        plugin_gen_tb_end(cpu);
    }

    /* The disas_log hook may use these values rather than recompute.  */
    tb->size = db->pc_next - db->pc_first;
    tb->icount = db->num_insns;

#ifdef DEBUG_DISAS
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)
        && qemu_log_in_addr_range(db->pc_first)) {
        FILE *logfile = qemu_log_trylock();
        if (logfile) {
            fprintf(logfile, "----------------\n");
            ops->disas_log(db, cpu, logfile);
            fprintf(logfile, "\n");
            qemu_log_unlock(logfile);
        }
    }
#endif
}

static void *translator_access(CPUArchState *env, DisasContextBase *db,
                               target_ulong pc, size_t len)
{
    void *host;
    target_ulong base, end;
    TranslationBlock *tb;

    tb = db->tb;

    /* Use slow path if first page is MMIO. */
    if (unlikely(tb_page_addr0(tb) == -1)) {
        return NULL;
    }

    end = pc + len - 1;
    if (likely(is_same_page(db, end))) {
        host = db->host_addr[0];
        base = db->pc_first;
    } else {
        host = db->host_addr[1];
        base = TARGET_PAGE_ALIGN(db->pc_first);
        if (host == NULL) {
            tb_page_addr_t phys_page =
                get_page_addr_code_hostp(env, base, &db->host_addr[1]);

            /*
             * If the second page is MMIO, treat as if the first page
             * was MMIO as well, so that we do not cache the TB.
             */
            if (unlikely(phys_page == -1)) {
                tb_set_page_addr0(tb, -1);
                return NULL;
            }

            tb_set_page_addr1(tb, phys_page);
#ifdef CONFIG_USER_ONLY
            page_protect(end);
#endif
            host = db->host_addr[1];
        }

        /* Use slow path when crossing pages. */
        if (is_same_page(db, pc)) {
            return NULL;
        }
    }

    tcg_debug_assert(pc >= base);
    return host + (pc - base);
}

uint8_t translator_ldub(CPUArchState *env, DisasContextBase *db, abi_ptr pc)
{
    uint8_t ret;
    void *p = translator_access(env, db, pc, sizeof(ret));

    if (p) {
        plugin_insn_append(pc, p, sizeof(ret));
        return ldub_p(p);
    }
    ret = cpu_ldub_code(env, pc);
    plugin_insn_append(pc, &ret, sizeof(ret));
    return ret;
}

uint16_t translator_lduw(CPUArchState *env, DisasContextBase *db, abi_ptr pc)
{
    uint16_t ret, plug;
    void *p = translator_access(env, db, pc, sizeof(ret));

    if (p) {
        plugin_insn_append(pc, p, sizeof(ret));
        return lduw_p(p);
    }
    ret = cpu_lduw_code(env, pc);
    plug = tswap16(ret);
    plugin_insn_append(pc, &plug, sizeof(ret));
    return ret;
}

uint32_t translator_ldl(CPUArchState *env, DisasContextBase *db, abi_ptr pc)
{
    uint32_t ret, plug;
    void *p = translator_access(env, db, pc, sizeof(ret));

    if (p) {
        plugin_insn_append(pc, p, sizeof(ret));
        return ldl_p(p);
    }
    ret = cpu_ldl_code(env, pc);
    plug = tswap32(ret);
    plugin_insn_append(pc, &plug, sizeof(ret));
    return ret;
}

uint64_t translator_ldq(CPUArchState *env, DisasContextBase *db, abi_ptr pc)
{
    uint64_t ret, plug;
    void *p = translator_access(env, db, pc, sizeof(ret));

    if (p) {
        plugin_insn_append(pc, p, sizeof(ret));
        return ldq_p(p);
    }
    ret = cpu_ldq_code(env, pc);
    plug = tswap64(ret);
    plugin_insn_append(pc, &plug, sizeof(ret));
    return ret;
}
