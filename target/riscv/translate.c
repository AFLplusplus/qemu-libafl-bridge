/*
 * RISC-V emulation for qemu: main translation routines.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "tcg/tcg-op.h"
#include "disas/disas.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

#include "exec/translator.h"
#include "exec/log.h"

#include "instmap.h"

/* global register indices */
static TCGv cpu_gpr[32], cpu_pc, cpu_vl;
static TCGv_i64 cpu_fpr[32]; /* assume F and D extensions */
static TCGv load_res;
static TCGv load_val;

#include "exec/gen-icount.h"

typedef struct DisasContext {
    DisasContextBase base;
    /* pc_succ_insn points to the instruction following base.pc_next */
    target_ulong pc_succ_insn;
    target_ulong priv_ver;
    bool virt_enabled;
    uint32_t opcode;
    uint32_t mstatus_fs;
    target_ulong misa;
    uint32_t mem_idx;
    /* Remember the rounding mode encoded in the previous fp instruction,
       which we have already installed into env->fp_status.  Or -1 for
       no previous fp instruction.  Note that we exit the TB when writing
       to any system register, which includes CSR_FRM, so we do not have
       to reset this known value.  */
    int frm;
    bool ext_ifencei;
    bool hlsx;
    /* vector extension */
    bool vill;
    uint8_t lmul;
    uint8_t sew;
    uint16_t vlen;
    uint16_t mlen;
    bool vl_eq_vlmax;
    CPUState *cs;
} DisasContext;

static inline bool has_ext(DisasContext *ctx, uint32_t ext)
{
    return ctx->misa & ext;
}

#ifdef TARGET_RISCV32
# define is_32bit(ctx)  true
#elif defined(CONFIG_USER_ONLY)
# define is_32bit(ctx)  false
#else
static inline bool is_32bit(DisasContext *ctx)
{
    return (ctx->misa & RV32) == RV32;
}
#endif

/*
 * RISC-V requires NaN-boxing of narrower width floating point values.
 * This applies when a 32-bit value is assigned to a 64-bit FP register.
 * For consistency and simplicity, we nanbox results even when the RVD
 * extension is not present.
 */
static void gen_nanbox_s(TCGv_i64 out, TCGv_i64 in)
{
    tcg_gen_ori_i64(out, in, MAKE_64BIT_MASK(32, 32));
}

/*
 * A narrow n-bit operation, where n < FLEN, checks that input operands
 * are correctly Nan-boxed, i.e., all upper FLEN - n bits are 1.
 * If so, the least-significant bits of the input are used, otherwise the
 * input value is treated as an n-bit canonical NaN (v2.2 section 9.2).
 *
 * Here, the result is always nan-boxed, even the canonical nan.
 */
static void gen_check_nanbox_s(TCGv_i64 out, TCGv_i64 in)
{
    TCGv_i64 t_max = tcg_const_i64(0xffffffff00000000ull);
    TCGv_i64 t_nan = tcg_const_i64(0xffffffff7fc00000ull);

    tcg_gen_movcond_i64(TCG_COND_GEU, out, in, t_max, in, t_nan);
    tcg_temp_free_i64(t_max);
    tcg_temp_free_i64(t_nan);
}

static void generate_exception(DisasContext *ctx, int excp)
{
    tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next);
    TCGv_i32 helper_tmp = tcg_const_i32(excp);
    gen_helper_raise_exception(cpu_env, helper_tmp);
    tcg_temp_free_i32(helper_tmp);
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void generate_exception_mtval(DisasContext *ctx, int excp)
{
    tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next);
    tcg_gen_st_tl(cpu_pc, cpu_env, offsetof(CPURISCVState, badaddr));
    TCGv_i32 helper_tmp = tcg_const_i32(excp);
    gen_helper_raise_exception(cpu_env, helper_tmp);
    tcg_temp_free_i32(helper_tmp);
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void gen_exception_debug(void)
{
    TCGv_i32 helper_tmp = tcg_const_i32(EXCP_DEBUG);
    gen_helper_raise_exception(cpu_env, helper_tmp);
    tcg_temp_free_i32(helper_tmp);
}

/* Wrapper around tcg_gen_exit_tb that handles single stepping */
static void exit_tb(DisasContext *ctx)
{
    if (ctx->base.singlestep_enabled) {
        gen_exception_debug();
    } else {
        tcg_gen_exit_tb(NULL, 0);
    }
}

/* Wrapper around tcg_gen_lookup_and_goto_ptr that handles single stepping */
static void lookup_and_goto_ptr(DisasContext *ctx)
{
    if (ctx->base.singlestep_enabled) {
        gen_exception_debug();
    } else {
        tcg_gen_lookup_and_goto_ptr();
    }
}

static void gen_exception_illegal(DisasContext *ctx)
{
    generate_exception(ctx, RISCV_EXCP_ILLEGAL_INST);
}

static void gen_exception_inst_addr_mis(DisasContext *ctx)
{
    generate_exception_mtval(ctx, RISCV_EXCP_INST_ADDR_MIS);
}

static void gen_goto_tb(DisasContext *ctx, int n, target_ulong dest)
{
    if (translator_use_goto_tb(&ctx->base, dest)) {
        tcg_gen_goto_tb(n);
        tcg_gen_movi_tl(cpu_pc, dest);
        tcg_gen_exit_tb(ctx->base.tb, n);
    } else {
        tcg_gen_movi_tl(cpu_pc, dest);
        lookup_and_goto_ptr(ctx);
    }
}

/* Wrapper for getting reg values - need to check of reg is zero since
 * cpu_gpr[0] is not actually allocated
 */
static inline void gen_get_gpr(TCGv t, int reg_num)
{
    if (reg_num == 0) {
        tcg_gen_movi_tl(t, 0);
    } else {
        tcg_gen_mov_tl(t, cpu_gpr[reg_num]);
    }
}

/* Wrapper for setting reg values - need to check of reg is zero since
 * cpu_gpr[0] is not actually allocated. this is more for safety purposes,
 * since we usually avoid calling the OP_TYPE_gen function if we see a write to
 * $zero
 */
static inline void gen_set_gpr(int reg_num_dst, TCGv t)
{
    if (reg_num_dst != 0) {
        tcg_gen_mov_tl(cpu_gpr[reg_num_dst], t);
    }
}

static void gen_mulhsu(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv rl = tcg_temp_new();
    TCGv rh = tcg_temp_new();

    tcg_gen_mulu2_tl(rl, rh, arg1, arg2);
    /* fix up for one negative */
    tcg_gen_sari_tl(rl, arg1, TARGET_LONG_BITS - 1);
    tcg_gen_and_tl(rl, rl, arg2);
    tcg_gen_sub_tl(ret, rh, rl);

    tcg_temp_free(rl);
    tcg_temp_free(rh);
}

static void gen_div(TCGv ret, TCGv source1, TCGv source2)
{
    TCGv cond1, cond2, zeroreg, resultopt1;
    /*
     * Handle by altering args to tcg_gen_div to produce req'd results:
     * For overflow: want source1 in source1 and 1 in source2
     * For div by zero: want -1 in source1 and 1 in source2 -> -1 result
     */
    cond1 = tcg_temp_new();
    cond2 = tcg_temp_new();
    zeroreg = tcg_const_tl(0);
    resultopt1 = tcg_temp_new();

    tcg_gen_movi_tl(resultopt1, (target_ulong)-1);
    tcg_gen_setcondi_tl(TCG_COND_EQ, cond2, source2, (target_ulong)(~0L));
    tcg_gen_setcondi_tl(TCG_COND_EQ, cond1, source1,
                        ((target_ulong)1) << (TARGET_LONG_BITS - 1));
    tcg_gen_and_tl(cond1, cond1, cond2); /* cond1 = overflow */
    tcg_gen_setcondi_tl(TCG_COND_EQ, cond2, source2, 0); /* cond2 = div 0 */
    /* if div by zero, set source1 to -1, otherwise don't change */
    tcg_gen_movcond_tl(TCG_COND_EQ, source1, cond2, zeroreg, source1,
            resultopt1);
    /* if overflow or div by zero, set source2 to 1, else don't change */
    tcg_gen_or_tl(cond1, cond1, cond2);
    tcg_gen_movi_tl(resultopt1, (target_ulong)1);
    tcg_gen_movcond_tl(TCG_COND_EQ, source2, cond1, zeroreg, source2,
            resultopt1);
    tcg_gen_div_tl(ret, source1, source2);

    tcg_temp_free(cond1);
    tcg_temp_free(cond2);
    tcg_temp_free(zeroreg);
    tcg_temp_free(resultopt1);
}

static void gen_divu(TCGv ret, TCGv source1, TCGv source2)
{
    TCGv cond1, zeroreg, resultopt1;
    cond1 = tcg_temp_new();

    zeroreg = tcg_const_tl(0);
    resultopt1 = tcg_temp_new();

    tcg_gen_setcondi_tl(TCG_COND_EQ, cond1, source2, 0);
    tcg_gen_movi_tl(resultopt1, (target_ulong)-1);
    tcg_gen_movcond_tl(TCG_COND_EQ, source1, cond1, zeroreg, source1,
            resultopt1);
    tcg_gen_movi_tl(resultopt1, (target_ulong)1);
    tcg_gen_movcond_tl(TCG_COND_EQ, source2, cond1, zeroreg, source2,
            resultopt1);
    tcg_gen_divu_tl(ret, source1, source2);

    tcg_temp_free(cond1);
    tcg_temp_free(zeroreg);
    tcg_temp_free(resultopt1);
}

static void gen_rem(TCGv ret, TCGv source1, TCGv source2)
{
    TCGv cond1, cond2, zeroreg, resultopt1;

    cond1 = tcg_temp_new();
    cond2 = tcg_temp_new();
    zeroreg = tcg_const_tl(0);
    resultopt1 = tcg_temp_new();

    tcg_gen_movi_tl(resultopt1, 1L);
    tcg_gen_setcondi_tl(TCG_COND_EQ, cond2, source2, (target_ulong)-1);
    tcg_gen_setcondi_tl(TCG_COND_EQ, cond1, source1,
                        (target_ulong)1 << (TARGET_LONG_BITS - 1));
    tcg_gen_and_tl(cond2, cond1, cond2); /* cond1 = overflow */
    tcg_gen_setcondi_tl(TCG_COND_EQ, cond1, source2, 0); /* cond2 = div 0 */
    /* if overflow or div by zero, set source2 to 1, else don't change */
    tcg_gen_or_tl(cond2, cond1, cond2);
    tcg_gen_movcond_tl(TCG_COND_EQ, source2, cond2, zeroreg, source2,
            resultopt1);
    tcg_gen_rem_tl(resultopt1, source1, source2);
    /* if div by zero, just return the original dividend */
    tcg_gen_movcond_tl(TCG_COND_EQ, ret, cond1, zeroreg, resultopt1,
            source1);

    tcg_temp_free(cond1);
    tcg_temp_free(cond2);
    tcg_temp_free(zeroreg);
    tcg_temp_free(resultopt1);
}

static void gen_remu(TCGv ret, TCGv source1, TCGv source2)
{
    TCGv cond1, zeroreg, resultopt1;
    cond1 = tcg_temp_new();
    zeroreg = tcg_const_tl(0);
    resultopt1 = tcg_temp_new();

    tcg_gen_movi_tl(resultopt1, (target_ulong)1);
    tcg_gen_setcondi_tl(TCG_COND_EQ, cond1, source2, 0);
    tcg_gen_movcond_tl(TCG_COND_EQ, source2, cond1, zeroreg, source2,
            resultopt1);
    tcg_gen_remu_tl(resultopt1, source1, source2);
    /* if div by zero, just return the original dividend */
    tcg_gen_movcond_tl(TCG_COND_EQ, ret, cond1, zeroreg, resultopt1,
            source1);

    tcg_temp_free(cond1);
    tcg_temp_free(zeroreg);
    tcg_temp_free(resultopt1);
}

static void gen_jal(DisasContext *ctx, int rd, target_ulong imm)
{
    target_ulong next_pc;

    /* check misaligned: */
    next_pc = ctx->base.pc_next + imm;
    if (!has_ext(ctx, RVC)) {
        if ((next_pc & 0x3) != 0) {
            gen_exception_inst_addr_mis(ctx);
            return;
        }
    }
    if (rd != 0) {
        tcg_gen_movi_tl(cpu_gpr[rd], ctx->pc_succ_insn);
    }

    gen_goto_tb(ctx, 0, ctx->base.pc_next + imm); /* must use this for safety */
    ctx->base.is_jmp = DISAS_NORETURN;
}

#ifndef CONFIG_USER_ONLY
/* The states of mstatus_fs are:
 * 0 = disabled, 1 = initial, 2 = clean, 3 = dirty
 * We will have already diagnosed disabled state,
 * and need to turn initial/clean into dirty.
 */
static void mark_fs_dirty(DisasContext *ctx)
{
    TCGv tmp;
    target_ulong sd;

    if (ctx->mstatus_fs == MSTATUS_FS) {
        return;
    }
    /* Remember the state change for the rest of the TB.  */
    ctx->mstatus_fs = MSTATUS_FS;

    tmp = tcg_temp_new();
    sd = is_32bit(ctx) ? MSTATUS32_SD : MSTATUS64_SD;

    tcg_gen_ld_tl(tmp, cpu_env, offsetof(CPURISCVState, mstatus));
    tcg_gen_ori_tl(tmp, tmp, MSTATUS_FS | sd);
    tcg_gen_st_tl(tmp, cpu_env, offsetof(CPURISCVState, mstatus));

    if (ctx->virt_enabled) {
        tcg_gen_ld_tl(tmp, cpu_env, offsetof(CPURISCVState, mstatus_hs));
        tcg_gen_ori_tl(tmp, tmp, MSTATUS_FS | sd);
        tcg_gen_st_tl(tmp, cpu_env, offsetof(CPURISCVState, mstatus_hs));
    }
    tcg_temp_free(tmp);
}
#else
static inline void mark_fs_dirty(DisasContext *ctx) { }
#endif

static void gen_set_rm(DisasContext *ctx, int rm)
{
    TCGv_i32 t0;

    if (ctx->frm == rm) {
        return;
    }
    ctx->frm = rm;
    t0 = tcg_const_i32(rm);
    gen_helper_set_rounding_mode(cpu_env, t0);
    tcg_temp_free_i32(t0);
}

static int ex_plus_1(DisasContext *ctx, int nf)
{
    return nf + 1;
}

#define EX_SH(amount) \
    static int ex_shift_##amount(DisasContext *ctx, int imm) \
    {                                         \
        return imm << amount;                 \
    }
EX_SH(1)
EX_SH(2)
EX_SH(3)
EX_SH(4)
EX_SH(12)

#define REQUIRE_EXT(ctx, ext) do { \
    if (!has_ext(ctx, ext)) {      \
        return false;              \
    }                              \
} while (0)

#define REQUIRE_64BIT(ctx) do { \
    if (is_32bit(ctx)) {        \
        return false;           \
    }                           \
} while (0)

static int ex_rvc_register(DisasContext *ctx, int reg)
{
    return 8 + reg;
}

static int ex_rvc_shifti(DisasContext *ctx, int imm)
{
    /* For RV128 a shamt of 0 means a shift by 64. */
    return imm ? imm : 64;
}

/* Include the auto-generated decoder for 32 bit insn */
#include "decode-insn32.c.inc"

static bool gen_arith_imm_fn(DisasContext *ctx, arg_i *a,
                             void (*func)(TCGv, TCGv, target_long))
{
    TCGv source1;
    source1 = tcg_temp_new();

    gen_get_gpr(source1, a->rs1);

    (*func)(source1, source1, a->imm);

    gen_set_gpr(a->rd, source1);
    tcg_temp_free(source1);
    return true;
}

static bool gen_arith_imm_tl(DisasContext *ctx, arg_i *a,
                             void (*func)(TCGv, TCGv, TCGv))
{
    TCGv source1, source2;
    source1 = tcg_temp_new();
    source2 = tcg_temp_new();

    gen_get_gpr(source1, a->rs1);
    tcg_gen_movi_tl(source2, a->imm);

    (*func)(source1, source1, source2);

    gen_set_gpr(a->rd, source1);
    tcg_temp_free(source1);
    tcg_temp_free(source2);
    return true;
}

static void gen_addw(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_add_tl(ret, arg1, arg2);
    tcg_gen_ext32s_tl(ret, ret);
}

static void gen_subw(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_sub_tl(ret, arg1, arg2);
    tcg_gen_ext32s_tl(ret, ret);
}

static void gen_mulw(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_mul_tl(ret, arg1, arg2);
    tcg_gen_ext32s_tl(ret, ret);
}

static bool gen_arith_div_w(DisasContext *ctx, arg_r *a,
                            void(*func)(TCGv, TCGv, TCGv))
{
    TCGv source1, source2;
    source1 = tcg_temp_new();
    source2 = tcg_temp_new();

    gen_get_gpr(source1, a->rs1);
    gen_get_gpr(source2, a->rs2);
    tcg_gen_ext32s_tl(source1, source1);
    tcg_gen_ext32s_tl(source2, source2);

    (*func)(source1, source1, source2);

    tcg_gen_ext32s_tl(source1, source1);
    gen_set_gpr(a->rd, source1);
    tcg_temp_free(source1);
    tcg_temp_free(source2);
    return true;
}

static bool gen_arith_div_uw(DisasContext *ctx, arg_r *a,
                            void(*func)(TCGv, TCGv, TCGv))
{
    TCGv source1, source2;
    source1 = tcg_temp_new();
    source2 = tcg_temp_new();

    gen_get_gpr(source1, a->rs1);
    gen_get_gpr(source2, a->rs2);
    tcg_gen_ext32u_tl(source1, source1);
    tcg_gen_ext32u_tl(source2, source2);

    (*func)(source1, source1, source2);

    tcg_gen_ext32s_tl(source1, source1);
    gen_set_gpr(a->rd, source1);
    tcg_temp_free(source1);
    tcg_temp_free(source2);
    return true;
}

static void gen_pack(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_deposit_tl(ret, arg1, arg2,
                       TARGET_LONG_BITS / 2,
                       TARGET_LONG_BITS / 2);
}

static void gen_packu(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t = tcg_temp_new();
    tcg_gen_shri_tl(t, arg1, TARGET_LONG_BITS / 2);
    tcg_gen_deposit_tl(ret, arg2, t, 0, TARGET_LONG_BITS / 2);
    tcg_temp_free(t);
}

static void gen_packh(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t = tcg_temp_new();
    tcg_gen_ext8u_tl(t, arg2);
    tcg_gen_deposit_tl(ret, arg1, t, 8, TARGET_LONG_BITS - 8);
    tcg_temp_free(t);
}

static void gen_sbop_mask(TCGv ret, TCGv shamt)
{
    tcg_gen_movi_tl(ret, 1);
    tcg_gen_shl_tl(ret, ret, shamt);
}

static void gen_bset(TCGv ret, TCGv arg1, TCGv shamt)
{
    TCGv t = tcg_temp_new();

    gen_sbop_mask(t, shamt);
    tcg_gen_or_tl(ret, arg1, t);

    tcg_temp_free(t);
}

static void gen_bclr(TCGv ret, TCGv arg1, TCGv shamt)
{
    TCGv t = tcg_temp_new();

    gen_sbop_mask(t, shamt);
    tcg_gen_andc_tl(ret, arg1, t);

    tcg_temp_free(t);
}

static void gen_binv(TCGv ret, TCGv arg1, TCGv shamt)
{
    TCGv t = tcg_temp_new();

    gen_sbop_mask(t, shamt);
    tcg_gen_xor_tl(ret, arg1, t);

    tcg_temp_free(t);
}

static void gen_bext(TCGv ret, TCGv arg1, TCGv shamt)
{
    tcg_gen_shr_tl(ret, arg1, shamt);
    tcg_gen_andi_tl(ret, ret, 1);
}

static void gen_slo(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_not_tl(ret, arg1);
    tcg_gen_shl_tl(ret, ret, arg2);
    tcg_gen_not_tl(ret, ret);
}

static void gen_sro(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_not_tl(ret, arg1);
    tcg_gen_shr_tl(ret, ret, arg2);
    tcg_gen_not_tl(ret, ret);
}

static bool gen_grevi(DisasContext *ctx, arg_grevi *a)
{
    TCGv source1 = tcg_temp_new();
    TCGv source2;

    gen_get_gpr(source1, a->rs1);

    if (a->shamt == (TARGET_LONG_BITS - 8)) {
        /* rev8, byte swaps */
        tcg_gen_bswap_tl(source1, source1);
    } else {
        source2 = tcg_temp_new();
        tcg_gen_movi_tl(source2, a->shamt);
        gen_helper_grev(source1, source1, source2);
        tcg_temp_free(source2);
    }

    gen_set_gpr(a->rd, source1);
    tcg_temp_free(source1);
    return true;
}

#define GEN_SHADD(SHAMT)                                       \
static void gen_sh##SHAMT##add(TCGv ret, TCGv arg1, TCGv arg2) \
{                                                              \
    TCGv t = tcg_temp_new();                                   \
                                                               \
    tcg_gen_shli_tl(t, arg1, SHAMT);                           \
    tcg_gen_add_tl(ret, t, arg2);                              \
                                                               \
    tcg_temp_free(t);                                          \
}

GEN_SHADD(1)
GEN_SHADD(2)
GEN_SHADD(3)

static void gen_ctzw(TCGv ret, TCGv arg1)
{
    tcg_gen_ori_tl(ret, arg1, (target_ulong)MAKE_64BIT_MASK(32, 32));
    tcg_gen_ctzi_tl(ret, ret, 64);
}

static void gen_clzw(TCGv ret, TCGv arg1)
{
    tcg_gen_ext32u_tl(ret, arg1);
    tcg_gen_clzi_tl(ret, ret, 64);
    tcg_gen_subi_tl(ret, ret, 32);
}

static void gen_cpopw(TCGv ret, TCGv arg1)
{
    tcg_gen_ext32u_tl(arg1, arg1);
    tcg_gen_ctpop_tl(ret, arg1);
}

static void gen_packw(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t = tcg_temp_new();
    tcg_gen_ext16s_tl(t, arg2);
    tcg_gen_deposit_tl(ret, arg1, t, 16, 48);
    tcg_temp_free(t);
}

static void gen_packuw(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv t = tcg_temp_new();
    tcg_gen_shri_tl(t, arg1, 16);
    tcg_gen_deposit_tl(ret, arg2, t, 0, 16);
    tcg_gen_ext32s_tl(ret, ret);
    tcg_temp_free(t);
}

static void gen_rorw(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();

    /* truncate to 32-bits */
    tcg_gen_trunc_tl_i32(t1, arg1);
    tcg_gen_trunc_tl_i32(t2, arg2);

    tcg_gen_rotr_i32(t1, t1, t2);

    /* sign-extend 64-bits */
    tcg_gen_ext_i32_tl(ret, t1);

    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
}

static void gen_rolw(TCGv ret, TCGv arg1, TCGv arg2)
{
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();

    /* truncate to 32-bits */
    tcg_gen_trunc_tl_i32(t1, arg1);
    tcg_gen_trunc_tl_i32(t2, arg2);

    tcg_gen_rotl_i32(t1, t1, t2);

    /* sign-extend 64-bits */
    tcg_gen_ext_i32_tl(ret, t1);

    tcg_temp_free_i32(t1);
    tcg_temp_free_i32(t2);
}

static void gen_grevw(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_ext32u_tl(arg1, arg1);
    gen_helper_grev(ret, arg1, arg2);
}

static void gen_gorcw(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_ext32u_tl(arg1, arg1);
    gen_helper_gorcw(ret, arg1, arg2);
}

#define GEN_SHADD_UW(SHAMT)                                       \
static void gen_sh##SHAMT##add_uw(TCGv ret, TCGv arg1, TCGv arg2) \
{                                                                 \
    TCGv t = tcg_temp_new();                                      \
                                                                  \
    tcg_gen_ext32u_tl(t, arg1);                                   \
                                                                  \
    tcg_gen_shli_tl(t, t, SHAMT);                                 \
    tcg_gen_add_tl(ret, t, arg2);                                 \
                                                                  \
    tcg_temp_free(t);                                             \
}

GEN_SHADD_UW(1)
GEN_SHADD_UW(2)
GEN_SHADD_UW(3)

static void gen_add_uw(TCGv ret, TCGv arg1, TCGv arg2)
{
    tcg_gen_ext32u_tl(arg1, arg1);
    tcg_gen_add_tl(ret, arg1, arg2);
}

static bool gen_arith(DisasContext *ctx, arg_r *a,
                      void(*func)(TCGv, TCGv, TCGv))
{
    TCGv source1, source2;
    source1 = tcg_temp_new();
    source2 = tcg_temp_new();

    gen_get_gpr(source1, a->rs1);
    gen_get_gpr(source2, a->rs2);

    (*func)(source1, source1, source2);

    gen_set_gpr(a->rd, source1);
    tcg_temp_free(source1);
    tcg_temp_free(source2);
    return true;
}

static bool gen_shift(DisasContext *ctx, arg_r *a,
                        void(*func)(TCGv, TCGv, TCGv))
{
    TCGv source1 = tcg_temp_new();
    TCGv source2 = tcg_temp_new();

    gen_get_gpr(source1, a->rs1);
    gen_get_gpr(source2, a->rs2);

    tcg_gen_andi_tl(source2, source2, TARGET_LONG_BITS - 1);
    (*func)(source1, source1, source2);

    gen_set_gpr(a->rd, source1);
    tcg_temp_free(source1);
    tcg_temp_free(source2);
    return true;
}

static uint32_t opcode_at(DisasContextBase *dcbase, target_ulong pc)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPUState *cpu = ctx->cs;
    CPURISCVState *env = cpu->env_ptr;

    return cpu_ldl_code(env, pc);
}

static bool gen_shifti(DisasContext *ctx, arg_shift *a,
                       void(*func)(TCGv, TCGv, TCGv))
{
    if (a->shamt >= TARGET_LONG_BITS) {
        return false;
    }

    TCGv source1 = tcg_temp_new();
    TCGv source2 = tcg_temp_new();

    gen_get_gpr(source1, a->rs1);

    tcg_gen_movi_tl(source2, a->shamt);
    (*func)(source1, source1, source2);

    gen_set_gpr(a->rd, source1);
    tcg_temp_free(source1);
    tcg_temp_free(source2);
    return true;
}

static bool gen_shiftw(DisasContext *ctx, arg_r *a,
                       void(*func)(TCGv, TCGv, TCGv))
{
    TCGv source1 = tcg_temp_new();
    TCGv source2 = tcg_temp_new();

    gen_get_gpr(source1, a->rs1);
    gen_get_gpr(source2, a->rs2);

    tcg_gen_andi_tl(source2, source2, 31);
    (*func)(source1, source1, source2);
    tcg_gen_ext32s_tl(source1, source1);

    gen_set_gpr(a->rd, source1);
    tcg_temp_free(source1);
    tcg_temp_free(source2);
    return true;
}

static bool gen_shiftiw(DisasContext *ctx, arg_shift *a,
                        void(*func)(TCGv, TCGv, TCGv))
{
    TCGv source1 = tcg_temp_new();
    TCGv source2 = tcg_temp_new();

    gen_get_gpr(source1, a->rs1);
    tcg_gen_movi_tl(source2, a->shamt);

    (*func)(source1, source1, source2);
    tcg_gen_ext32s_tl(source1, source1);

    gen_set_gpr(a->rd, source1);
    tcg_temp_free(source1);
    tcg_temp_free(source2);
    return true;
}

static void gen_ctz(TCGv ret, TCGv arg1)
{
    tcg_gen_ctzi_tl(ret, arg1, TARGET_LONG_BITS);
}

static void gen_clz(TCGv ret, TCGv arg1)
{
    tcg_gen_clzi_tl(ret, arg1, TARGET_LONG_BITS);
}

static bool gen_unary(DisasContext *ctx, arg_r2 *a,
                      void(*func)(TCGv, TCGv))
{
    TCGv source = tcg_temp_new();

    gen_get_gpr(source, a->rs1);

    (*func)(source, source);

    gen_set_gpr(a->rd, source);
    tcg_temp_free(source);
    return true;
}

/* Include insn module translation function */
#include "insn_trans/trans_rvi.c.inc"
#include "insn_trans/trans_rvm.c.inc"
#include "insn_trans/trans_rva.c.inc"
#include "insn_trans/trans_rvf.c.inc"
#include "insn_trans/trans_rvd.c.inc"
#include "insn_trans/trans_rvh.c.inc"
#include "insn_trans/trans_rvv.c.inc"
#include "insn_trans/trans_rvb.c.inc"
#include "insn_trans/trans_privileged.c.inc"

/* Include the auto-generated decoder for 16 bit insn */
#include "decode-insn16.c.inc"

static void decode_opc(CPURISCVState *env, DisasContext *ctx, uint16_t opcode)
{
    /* check for compressed insn */
    if (extract16(opcode, 0, 2) != 3) {
        if (!has_ext(ctx, RVC)) {
            gen_exception_illegal(ctx);
        } else {
            ctx->pc_succ_insn = ctx->base.pc_next + 2;
            if (!decode_insn16(ctx, opcode)) {
                gen_exception_illegal(ctx);
            }
        }
    } else {
        uint32_t opcode32 = opcode;
        opcode32 = deposit32(opcode32, 16, 16,
                             translator_lduw(env, ctx->base.pc_next + 2));
        ctx->pc_succ_insn = ctx->base.pc_next + 4;
        if (!decode_insn32(ctx, opcode32)) {
            gen_exception_illegal(ctx);
        }
    }
}

static void riscv_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPURISCVState *env = cs->env_ptr;
    RISCVCPU *cpu = RISCV_CPU(cs);
    uint32_t tb_flags = ctx->base.tb->flags;

    ctx->pc_succ_insn = ctx->base.pc_first;
    ctx->mem_idx = tb_flags & TB_FLAGS_MMU_MASK;
    ctx->mstatus_fs = tb_flags & TB_FLAGS_MSTATUS_FS;
    ctx->priv_ver = env->priv_ver;
#if !defined(CONFIG_USER_ONLY)
    if (riscv_has_ext(env, RVH)) {
        ctx->virt_enabled = riscv_cpu_virt_enabled(env);
    } else {
        ctx->virt_enabled = false;
    }
#else
    ctx->virt_enabled = false;
#endif
    ctx->misa = env->misa;
    ctx->frm = -1;  /* unknown rounding mode */
    ctx->ext_ifencei = cpu->cfg.ext_ifencei;
    ctx->vlen = cpu->cfg.vlen;
    ctx->hlsx = FIELD_EX32(tb_flags, TB_FLAGS, HLSX);
    ctx->vill = FIELD_EX32(tb_flags, TB_FLAGS, VILL);
    ctx->sew = FIELD_EX32(tb_flags, TB_FLAGS, SEW);
    ctx->lmul = FIELD_EX32(tb_flags, TB_FLAGS, LMUL);
    ctx->mlen = 1 << (ctx->sew  + 3 - ctx->lmul);
    ctx->vl_eq_vlmax = FIELD_EX32(tb_flags, TB_FLAGS, VL_EQ_VLMAX);
    ctx->cs = cs;
}

static void riscv_tr_tb_start(DisasContextBase *db, CPUState *cpu)
{
}

static void riscv_tr_insn_start(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(ctx->base.pc_next);
}

static void riscv_tr_translate_insn(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    CPURISCVState *env = cpu->env_ptr;
    uint16_t opcode16 = translator_lduw(env, ctx->base.pc_next);

    decode_opc(env, ctx, opcode16);
    ctx->base.pc_next = ctx->pc_succ_insn;

    if (ctx->base.is_jmp == DISAS_NEXT) {
        target_ulong page_start;

        page_start = ctx->base.pc_first & TARGET_PAGE_MASK;
        if (ctx->base.pc_next - page_start >= TARGET_PAGE_SIZE) {
            ctx->base.is_jmp = DISAS_TOO_MANY;
        }
    }
}

static void riscv_tr_tb_stop(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    switch (ctx->base.is_jmp) {
    case DISAS_TOO_MANY:
        gen_goto_tb(ctx, 0, ctx->base.pc_next);
        break;
    case DISAS_NORETURN:
        break;
    default:
        g_assert_not_reached();
    }
}

static void riscv_tr_disas_log(const DisasContextBase *dcbase, CPUState *cpu)
{
#ifndef CONFIG_USER_ONLY
    RISCVCPU *rvcpu = RISCV_CPU(cpu);
    CPURISCVState *env = &rvcpu->env;
#endif

    qemu_log("IN: %s\n", lookup_symbol(dcbase->pc_first));
#ifndef CONFIG_USER_ONLY
    qemu_log("Priv: "TARGET_FMT_ld"; Virt: "TARGET_FMT_ld"\n", env->priv, env->virt);
#endif
    log_target_disas(cpu, dcbase->pc_first, dcbase->tb->size);
}

static const TranslatorOps riscv_tr_ops = {
    .init_disas_context = riscv_tr_init_disas_context,
    .tb_start           = riscv_tr_tb_start,
    .insn_start         = riscv_tr_insn_start,
    .translate_insn     = riscv_tr_translate_insn,
    .tb_stop            = riscv_tr_tb_stop,
    .disas_log          = riscv_tr_disas_log,
};

void gen_intermediate_code(CPUState *cs, TranslationBlock *tb, int max_insns)
{
    DisasContext ctx;

    translator_loop(&riscv_tr_ops, &ctx.base, cs, tb, max_insns);
}

void riscv_translate_init(void)
{
    int i;

    /* cpu_gpr[0] is a placeholder for the zero register. Do not use it. */
    /* Use the gen_set_gpr and gen_get_gpr helper functions when accessing */
    /* registers, unless you specifically block reads/writes to reg 0 */
    cpu_gpr[0] = NULL;

    for (i = 1; i < 32; i++) {
        cpu_gpr[i] = tcg_global_mem_new(cpu_env,
            offsetof(CPURISCVState, gpr[i]), riscv_int_regnames[i]);
    }

    for (i = 0; i < 32; i++) {
        cpu_fpr[i] = tcg_global_mem_new_i64(cpu_env,
            offsetof(CPURISCVState, fpr[i]), riscv_fpr_regnames[i]);
    }

    cpu_pc = tcg_global_mem_new(cpu_env, offsetof(CPURISCVState, pc), "pc");
    cpu_vl = tcg_global_mem_new(cpu_env, offsetof(CPURISCVState, vl), "vl");
    load_res = tcg_global_mem_new(cpu_env, offsetof(CPURISCVState, load_res),
                             "load_res");
    load_val = tcg_global_mem_new(cpu_env, offsetof(CPURISCVState, load_val),
                             "load_val");
}
