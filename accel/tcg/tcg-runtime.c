/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2008 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "cpu.h"
#include "exec/helper-proto.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"
#include "disas/disas.h"
#include "exec/log.h"
#include "tcg/tcg.h"
#include "sysemu/cpus.h"
#include "sysemu/runstate.h"
#include "exec/cpu_ldst.h"

//// --- Begin LibAFL code ---

void libafl_init_fuzzer(void);
void libafl_getwork(void *input_map_qemu, uint64_t input_map_qemu_sz);
void libafl_finishwork(void);
void libafl_crash(void);

static uint64_t input_map_qemu_addr;
static uint64_t input_map_qemu_size;

//#define AFL_DEBUG 1

#define EXCP_LIBAFL_BP 0xf4775747

void HELPER(libafl_qemu_handle_breakpoint)(CPUArchState *env)
{
    CPUState* cpu = env_cpu(env);
    cpu->exception_index = EXCP_LIBAFL_BP;
    cpu_loop_exit(cpu);
}

target_ulong HELPER(libafl_qemu_hypercall)(CPUArchState *env, target_ulong r0, target_ulong r1, target_ulong r2)
{
    CPUState* cpu = env_cpu(env);
    #ifdef AFL_DEBUG
    printf("got Hypercall!\n");
    printf("pause cpus\n");
    //vm_stop(RUN_STATE_PAUSED);
    printf("r0: %ld\n", r0);
    printf("r1: %ld\n", r1);
    printf("r2: %ld\n", r2);
    #endif
    char buf[4096];
    switch(r0) {
        case 1:
        // get work
        libafl_getwork(&buf, input_map_qemu_size);
        cpu_memory_rw_debug(cpu, input_map_qemu_addr, &buf, input_map_qemu_size, true);
        break;
        case 2:
        // finish work
        libafl_finishwork();
        break;
        case 3:
        libafl_crash();
        break;
        case 0: // fallthrough
        default:
        input_map_qemu_addr = r1;
        input_map_qemu_size = r2;
        libafl_init_fuzzer();
        break;    
    }
    //cpu_loop_exit_atomic(env_cpu(env), GETPC());
    //cpu_loop_exit(cpu);
#ifdef AFL_DEBUG
    printf("return from hypercall\n");
#endif
    return 0;
}

//// --- End LibAFL code ---

/* 32-bit helpers */

int32_t HELPER(div_i32)(int32_t arg1, int32_t arg2)
{
    return arg1 / arg2;
}

int32_t HELPER(rem_i32)(int32_t arg1, int32_t arg2)
{
    return arg1 % arg2;
}

uint32_t HELPER(divu_i32)(uint32_t arg1, uint32_t arg2)
{
    return arg1 / arg2;
}

uint32_t HELPER(remu_i32)(uint32_t arg1, uint32_t arg2)
{
    return arg1 % arg2;
}

/* 64-bit helpers */

uint64_t HELPER(shl_i64)(uint64_t arg1, uint64_t arg2)
{
    return arg1 << arg2;
}

uint64_t HELPER(shr_i64)(uint64_t arg1, uint64_t arg2)
{
    return arg1 >> arg2;
}

int64_t HELPER(sar_i64)(int64_t arg1, int64_t arg2)
{
    return arg1 >> arg2;
}

int64_t HELPER(div_i64)(int64_t arg1, int64_t arg2)
{
    return arg1 / arg2;
}

int64_t HELPER(rem_i64)(int64_t arg1, int64_t arg2)
{
    return arg1 % arg2;
}

uint64_t HELPER(divu_i64)(uint64_t arg1, uint64_t arg2)
{
    return arg1 / arg2;
}

uint64_t HELPER(remu_i64)(uint64_t arg1, uint64_t arg2)
{
    return arg1 % arg2;
}

uint64_t HELPER(muluh_i64)(uint64_t arg1, uint64_t arg2)
{
    uint64_t l, h;
    mulu64(&l, &h, arg1, arg2);
    return h;
}

int64_t HELPER(mulsh_i64)(int64_t arg1, int64_t arg2)
{
    uint64_t l, h;
    muls64(&l, &h, arg1, arg2);
    return h;
}

uint32_t HELPER(clz_i32)(uint32_t arg, uint32_t zero_val)
{
    return arg ? clz32(arg) : zero_val;
}

uint32_t HELPER(ctz_i32)(uint32_t arg, uint32_t zero_val)
{
    return arg ? ctz32(arg) : zero_val;
}

uint64_t HELPER(clz_i64)(uint64_t arg, uint64_t zero_val)
{
    return arg ? clz64(arg) : zero_val;
}

uint64_t HELPER(ctz_i64)(uint64_t arg, uint64_t zero_val)
{
    return arg ? ctz64(arg) : zero_val;
}

uint32_t HELPER(clrsb_i32)(uint32_t arg)
{
    return clrsb32(arg);
}

uint64_t HELPER(clrsb_i64)(uint64_t arg)
{
    return clrsb64(arg);
}

uint32_t HELPER(ctpop_i32)(uint32_t arg)
{
    return ctpop32(arg);
}

uint64_t HELPER(ctpop_i64)(uint64_t arg)
{
    return ctpop64(arg);
}

void HELPER(exit_atomic)(CPUArchState *env)
{
    cpu_loop_exit_atomic(env_cpu(env), GETPC());
}
