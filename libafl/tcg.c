#include "qemu/osdep.h"
#include "qemu/log.h"

#include "exec/cpu-common.h"

#include "accel/tcg/internal-common.h"
#include "accel/tcg/internal-target.h"
#include "accel/tcg/trace.h"

#include "tcg/insn-start-words.h"
#include "tcg/perf.h"

#include "libafl/tcg.h"
#include "libafl/hooks/tcg/edge.h"

static target_ulong reverse_bits(target_ulong num)
{
    unsigned int count = sizeof(num) * 8 - 1;
    target_ulong reverse_num = num;

    num >>= 1;
    while(num)
    {
       reverse_num <<= 1;
       reverse_num |= num & 1;
       num >>= 1;
       count--;
    }
    reverse_num <<= count;
    return reverse_num;
}

/*
 * Isolate the portion of code gen which can setjmp/longjmp.
 * Return the size of the generated code, or negative on error.
 */
static int libafl_setjmp_gen_code(CPUArchState *env, TranslationBlock *tb,
                           vaddr pc, void *host_pc,
                           int *max_insns, int64_t *ti)
{
    int ret = sigsetjmp(tcg_ctx->jmp_trans, 0);
    if (unlikely(ret != 0)) {
        return ret;
    }

    tcg_func_start(tcg_ctx);

    tcg_ctx->cpu = env_cpu(env);

    // -- start gen_intermediate_code
    const int num_insns = 1; // do "as-if" we were translating a single target instruction

#ifndef TARGET_INSN_START_EXTRA_WORDS
    tcg_gen_insn_start(pc);
#elif TARGET_INSN_START_EXTRA_WORDS == 1
    tcg_gen_insn_start(pc, 0);
#elif TARGET_INSN_START_EXTRA_WORDS == 2
    tcg_gen_insn_start(pc, 0, 0);
#else
#error Unhandled TARGET_INSN_START_EXTRA_WORDS value
#endif

    // run edge hooks
    libafl_qemu_hook_edge_run();

    tcg_gen_goto_tb(0);
    tcg_gen_exit_tb(tb, 0);

    // This is obviously wrong, but it is required that the number / size of target instruction translated
    // is at least 1. For now, we make it so that no problem occurs later on.
    tb->icount = num_insns; // number of target instructions translated in the TB.
    tb->size = num_insns; // size (in target bytes) of target instructions translated in the TB.
    // -- end gen_intermediate_code

    assert(tb->size != 0);
    tcg_ctx->cpu = NULL;
    *max_insns = tb->icount;

    return tcg_gen_code(tcg_ctx, tb, pc);
}

/* Called with mmap_lock held for user mode emulation.  */
TranslationBlock *libafl_gen_edge(CPUState *cpu, target_ulong src_block,
                                  target_ulong dst_block, int exit_n,
                                  target_ulong cs_base, uint32_t flags,
                                  int cflags)
{
    CPUArchState *env = cpu_env(cpu);
    TranslationBlock *tb;
    tb_page_addr_t phys_pc;
    tcg_insn_unit *gen_code_buf;
    int gen_code_size, search_size, max_insns;
    int64_t ti;
    void *host_pc;

    // edge hooks generation callbacks
    // early check if it should be skipped or not
    bool no_exec_hook = libafl_qemu_hook_edge_gen(src_block, dst_block);
    if (no_exec_hook) {
        // no exec hooks to run for edges, not point in generating a TB
        return NULL;
    }

    target_ulong pc = src_block ^ reverse_bits((target_ulong)exit_n);

    assert_memory_lock();
    qemu_thread_jit_write();

    // TODO: this (get_page_addr_code_hostp) is a bottleneck in systemmode, investigate why
    phys_pc = get_page_addr_code_hostp(env, src_block, &host_pc);
    phys_pc ^= reverse_bits((tb_page_addr_t)exit_n);

    // if (phys_pc == -1) {
    //     /* Generate a one-shot TB with 1 insn in it */
    //     cflags = (cflags & ~CF_COUNT_MASK) | 1;
    // }

    /* Generate a one-shot TB with max 16 insn in it */
    cflags = (cflags & ~CF_COUNT_MASK) | LIBAFL_MAX_INSNS;
    QEMU_BUILD_BUG_ON(LIBAFL_MAX_INSNS > TCG_MAX_INSNS);

    max_insns = cflags & CF_COUNT_MASK;
    if (max_insns == 0) {
        max_insns = TCG_MAX_INSNS;
    }
    QEMU_BUILD_BUG_ON(CF_COUNT_MASK + 1 != TCG_MAX_INSNS);

 buffer_overflow:
    assert_no_pages_locked();
    tb = tcg_tb_alloc(tcg_ctx);
    if (unlikely(!tb)) {
        /* flush must be done */
        tb_flush(cpu);
        mmap_unlock();
        /* Make the execution loop process the flush as soon as possible.  */
        cpu->exception_index = EXCP_INTERRUPT;
        cpu_loop_exit(cpu);
    }

    gen_code_buf = tcg_ctx->code_gen_ptr;
    tb->tc.ptr = tcg_splitwx_to_rx(gen_code_buf);

    if (!(cflags & CF_PCREL)) {
        tb->pc = pc;
    }

    tb->cs_base = cs_base;
    tb->flags = flags;
    tb->cflags = cflags | CF_IS_EDGE;
    tb_set_page_addr0(tb, phys_pc);
    tb_set_page_addr1(tb, -1);
    // if (phys_pc != -1) {
    //     tb_lock_page0(phys_pc);
    // }

    tcg_ctx->gen_tb = tb;
    tcg_ctx->addr_type = TARGET_LONG_BITS == 32 ? TCG_TYPE_I32 : TCG_TYPE_I64;
#ifdef CONFIG_SOFTMMU
    tcg_ctx->page_bits = TARGET_PAGE_BITS;
    tcg_ctx->page_mask = TARGET_PAGE_MASK;
    tcg_ctx->tlb_dyn_max_bits = CPU_TLB_DYN_MAX_BITS;
#endif
    tcg_ctx->insn_start_words = TARGET_INSN_START_WORDS;
#ifdef TCG_GUEST_DEFAULT_MO
    tcg_ctx->guest_mo = TCG_GUEST_DEFAULT_MO;
#else
    tcg_ctx->guest_mo = TCG_MO_ALL;
#endif

 restart_translate:
    trace_translate_block(tb, pc, tb->tc.ptr);

    gen_code_size = libafl_setjmp_gen_code(env, tb, pc, host_pc, &max_insns, &ti);
    if (unlikely(gen_code_size < 0)) {
        switch (gen_code_size) {
            case -1:
                /*
                 * Overflow of code_gen_buffer, or the current slice of it.
                 *
                 * TODO: We don't need to re-do gen_intermediate_code, nor
                 * should we re-do the tcg optimization currently hidden
                 * inside tcg_gen_code.  All that should be required is to
                 * flush the TBs, allocate a new TB, re-initialize it per
                 * above, and re-do the actual code generation.
                 */
                qemu_log_mask(CPU_LOG_TB_OP | CPU_LOG_TB_OP_OPT,
                              "Restarting code generation for "
                              "code_gen_buffer overflow\n");
                tb_unlock_pages(tb);
                tcg_ctx->gen_tb = NULL;
                goto buffer_overflow;

            case -2:
                assert(false && "This should never happen for edge code. There must be a bug.");
                /*
                 * The code generated for the TranslationBlock is too large.
                 * The maximum size allowed by the unwind info is 64k.
                 * There may be stricter constraints from relocations
                 * in the tcg backend.
                 *
                 * Try again with half as many insns as we attempted this time.
                 * If a single insn overflows, there's a bug somewhere...
                 */
                assert(max_insns > 1);
                max_insns /= 2;
                qemu_log_mask(CPU_LOG_TB_OP | CPU_LOG_TB_OP_OPT,
                              "Restarting code generation with "
                              "smaller translation block (max %d insns)\n",
                              max_insns);

                /*
                 * The half-sized TB may not cross pages.
                 * TODO: Fix all targets that cross pages except with
                 * the first insn, at which point this can't be reached.
                 */
                // phys_p2 = tb_page_addr1(tb);
                // if (unlikely(phys_p2 != -1)) {
                //     tb_unlock_page1(phys_pc, phys_p2);
                //     tb_set_page_addr1(tb, -1);
                // }
                goto restart_translate;

            case -3:
                /*
                 * We had a page lock ordering problem.  In order to avoid
                 * deadlock we had to drop the lock on page0, which means
                 * that everything we translated so far is compromised.
                 * Restart with locks held on both pages.
                 */
                qemu_log_mask(CPU_LOG_TB_OP | CPU_LOG_TB_OP_OPT,
                              "Restarting code generation with re-locked pages");
                goto restart_translate;

            default:
                g_assert_not_reached();
        }
    }
    tcg_ctx->gen_tb = NULL;

    search_size = encode_search(tb, (void *)gen_code_buf + gen_code_size);
    if (unlikely(search_size < 0)) {
        tb_unlock_pages(tb);
        goto buffer_overflow;
    }
    tb->tc.size = gen_code_size;

    /*
     * For CF_PCREL, attribute all executions of the generated code
     * to its first mapping.
     */
    perf_report_code(pc, tb, tcg_splitwx_to_rx(gen_code_buf));

    qatomic_set(&tcg_ctx->code_gen_ptr, (void *)
        ROUND_UP((uintptr_t)gen_code_buf + gen_code_size + search_size,
                 CODE_GEN_ALIGN));

    /* init jump list */
    qemu_spin_init(&tb->jmp_lock);
    tb->jmp_list_head = (uintptr_t)NULL;
    tb->jmp_list_next[0] = (uintptr_t)NULL;
    tb->jmp_list_next[1] = (uintptr_t)NULL;
    tb->jmp_dest[0] = (uintptr_t)NULL;
    tb->jmp_dest[1] = (uintptr_t)NULL;

    /* init original jump addresses which have been set during tcg_gen_code() */
    if (tb->jmp_reset_offset[0] != TB_JMP_OFFSET_INVALID) {
        tb_reset_jump(tb, 0);
    }
    if (tb->jmp_reset_offset[1] != TB_JMP_OFFSET_INVALID) {
        tb_reset_jump(tb, 1);
    }

    assert_no_pages_locked();

#ifndef CONFIG_USER_ONLY
    tb->page_addr[0] = tb->page_addr[1] = -1;
#endif
    return tb;
}
