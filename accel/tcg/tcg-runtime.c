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
#include "exec/helper-proto-common.h"
#include "exec/cpu_ldst.h"
#include "exec/exec-all.h"
#include "disas/disas.h"
#include "exec/log.h"
#include "tcg/tcg.h"

#define HELPER_H  "accel/tcg/tcg-runtime.h"
#include "exec/helper-info.c.inc"
#undef  HELPER_H

//// --- Begin LibAFL code ---

#ifndef CONFIG_USER_ONLY

#include "sysemu/runstate.h"
#include "migration/snapshot.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "hw/core/cpu.h"
#include "sysemu/hw_accel.h"
#include <stdlib.h>
#include <string.h>

void libafl_save_qemu_snapshot(char *name, bool sync);
void libafl_load_qemu_snapshot(char *name, bool sync);

static void save_snapshot_cb(void* opaque)
{
    char* name = (char*)opaque;
    Error *err = NULL;
    if(!save_snapshot(name, true, NULL, false, NULL, &err)) {
        error_report_err(err);
        error_report("Could not save snapshot");
    }
    free(opaque);
}

void libafl_save_qemu_snapshot(char *name, bool sync)
{
    // use snapshots synchronously, use if main loop is not running
    if (sync) {
        //TODO: eliminate this code duplication
        //by passing a heap-allocated buffer from rust to c,
        //which c needs to free
        Error *err = NULL;
        if(!save_snapshot(name, true, NULL, false, NULL, &err)) {
            error_report_err(err);
            error_report("Could not save snapshot");
        }
        return;
    }
    char* name_buffer = malloc(strlen(name)+1);
    strcpy(name_buffer, name);
    aio_bh_schedule_oneshot_full(qemu_get_aio_context(), save_snapshot_cb, (void*)name_buffer, "save_snapshot");
}

static void load_snapshot_cb(void* opaque)
{
    char* name = (char*)opaque;
    Error *err = NULL;

    int saved_vm_running = runstate_is_running();
    vm_stop(RUN_STATE_RESTORE_VM);

    bool loaded = load_snapshot(name, NULL, false, NULL, &err);

    if(!loaded) {
        error_report_err(err);
        error_report("Could not load snapshot");
    }
    if (loaded && saved_vm_running) {
        vm_start();
    }
    free(opaque);
}

void libafl_load_qemu_snapshot(char *name, bool sync)
{
    // use snapshots synchronously, use if main loop is not running
    if (sync) {
        //TODO: see libafl_save_qemu_snapshot
        Error *err = NULL;

        int saved_vm_running = runstate_is_running();
        vm_stop(RUN_STATE_RESTORE_VM);

        bool loaded = load_snapshot(name, NULL, false, NULL, &err);

        if(!loaded) {
            error_report_err(err);
            error_report("Could not load snapshot");
        }
        if (loaded && saved_vm_running) {
            vm_start();
        }
        return;
    }
    char* name_buffer = malloc(strlen(name)+1);
    strcpy(name_buffer, name);
    aio_bh_schedule_oneshot_full(qemu_get_aio_context(), load_snapshot_cb, (void*)name_buffer, "load_snapshot");
}

#endif

#define EXCP_LIBAFL_BP 0xf4775747

int libafl_qemu_break_asap = 0;

CPUState* libafl_breakpoint_cpu;
vaddr libafl_breakpoint_pc;

#ifdef TARGET_ARM
#define THUMB_MASK(value) (value | libafl_breakpoint_cpu->env_ptr->thumb)
#else
#define THUMB_MASK(value) value
#endif

void libafl_qemu_trigger_breakpoint(CPUState* cpu);

void libafl_sync_breakpoint_cpu(void);

void libafl_sync_breakpoint_cpu(void)
{
    if (libafl_breakpoint_pc) {
        CPUClass* cc = CPU_GET_CLASS(libafl_breakpoint_cpu);
        cc->set_pc(libafl_breakpoint_cpu, THUMB_MASK(libafl_breakpoint_pc));
    }
    libafl_breakpoint_pc = 0;
}

void libafl_qemu_trigger_breakpoint(CPUState* cpu)
{
  libafl_breakpoint_cpu = cpu;
#ifndef CONFIG_USER_ONLY
    qemu_system_debug_request();
    cpu->stopped = true;
#endif
    if (cpu->running) {
        cpu->exception_index = EXCP_LIBAFL_BP;
        cpu_loop_exit(cpu);
    } else {
        libafl_qemu_break_asap = 1;
    }
}

void HELPER(libafl_qemu_handle_breakpoint)(CPUArchState *env, target_ulong pc)
{
    CPUState* cpu = env_cpu(env);
    libafl_breakpoint_pc = pc;
    libafl_qemu_trigger_breakpoint(cpu);
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
