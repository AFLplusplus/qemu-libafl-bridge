#include "libafl/exit.h"

#include "tcg/tcg.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-temp-internal.h"
#include "sysemu/runstate.h"

#include "cpu.h"
#include "libafl/cpu.h"

#ifdef CONFIG_USER_ONLY
#define THREAD_MODIFIER __thread
#else
#define THREAD_MODIFIER
#endif

struct libafl_breakpoint* libafl_qemu_breakpoints = NULL;

int libafl_qemu_set_breakpoint(target_ulong pc)
{
    CPUState* cpu;

    CPU_FOREACH(cpu) { libafl_breakpoint_invalidate(cpu, pc); }

    struct libafl_breakpoint* bp = calloc(sizeof(struct libafl_breakpoint), 1);
    bp->addr = pc;
    bp->next = libafl_qemu_breakpoints;
    libafl_qemu_breakpoints = bp;
    return 1;
}

int libafl_qemu_remove_breakpoint(target_ulong pc)
{
    CPUState* cpu;
    int r = 0;

    struct libafl_breakpoint** bp = &libafl_qemu_breakpoints;
    while (*bp) {
        if ((*bp)->addr == pc) {
            CPU_FOREACH(cpu) { libafl_breakpoint_invalidate(cpu, pc); }

            *bp = (*bp)->next;
            r = 1;
        } else {
            bp = &(*bp)->next;
        }
    }
    return r;
}

static THREAD_MODIFIER struct libafl_exit_reason last_exit_reason;
static THREAD_MODIFIER bool expected_exit = false;

#if defined(TARGET_ARM)
#define THUMB_MASK(cpu, value) (value | cpu_env(cpu)->thumb)
#else
#define THUMB_MASK(cpu, value) value
#endif

// called before exiting the cpu exec with the custom exception
void libafl_sync_exit_cpu(void)
{
    if (last_exit_reason.next_pc) {
        CPUClass* cc = CPU_GET_CLASS(last_exit_reason.cpu);
        cc->set_pc(last_exit_reason.cpu,
                   THUMB_MASK(last_exit_reason.cpu, last_exit_reason.next_pc));
    }
    last_exit_reason.next_pc = 0;
}

bool libafl_exit_asap(void) { return expected_exit; }

static void prepare_qemu_exit(CPUState* cpu, target_ulong next_pc)
{
    expected_exit = true;
    last_exit_reason.cpu = cpu;
    last_exit_reason.next_pc = next_pc;

#ifndef CONFIG_USER_ONLY
    qemu_system_debug_request();
    cpu->stopped = true; // TODO check if still needed
#endif

    // in usermode, this may be called from the syscall hook, thus already out
    // of the cpu_exec but still in the cpu_loop
    if (cpu->running) {
        cpu->exception_index = EXCP_LIBAFL_EXIT;
        cpu_loop_exit(cpu);
    }
}

CPUState* libafl_last_exit_cpu(void)
{
    if (expected_exit) {
        return last_exit_reason.cpu;
    }

    return NULL;
}

void libafl_exit_request_internal(CPUState* cpu, uint64_t pc,
                                  ShutdownCause cause, int signal)
{
    last_exit_reason.kind = INTERNAL;
    last_exit_reason.data.internal.cause = cause;
    last_exit_reason.data.internal.signal = signal;

    last_exit_reason.cpu = cpu;
    last_exit_reason.next_pc = pc;
    expected_exit = true;
}

void libafl_exit_request_custom_insn(CPUState* cpu, target_ulong pc,
                                     enum libafl_custom_insn_kind kind)
{
    last_exit_reason.kind = CUSTOM_INSN;

    prepare_qemu_exit(cpu, pc);
}

void libafl_exit_request_breakpoint(CPUState* cpu, target_ulong pc)
{
    last_exit_reason.kind = BREAKPOINT;
    last_exit_reason.data.breakpoint.addr = pc;

    prepare_qemu_exit(cpu, pc);
}

#ifndef CONFIG_USER_ONLY
void libafl_exit_request_timeout(void)
{
    expected_exit = true;
    last_exit_reason.kind = TIMEOUT;
    last_exit_reason.cpu = current_cpu;

    qemu_system_debug_request();
}
#endif

void libafl_qemu_trigger_breakpoint(CPUState* cpu)
{
    CPUClass* cc = CPU_GET_CLASS(cpu);
    libafl_exit_request_breakpoint(cpu, cc->get_pc(cpu));
}

void libafl_exit_signal_vm_start(void)
{
    last_exit_reason.cpu = NULL;
    expected_exit = false;
}

struct libafl_exit_reason* libafl_get_exit_reason(void)
{
    if (expected_exit) {
        return &last_exit_reason;
    }

    return NULL;
}

void libafl_qemu_breakpoint_run(vaddr pc_next)
{
    struct libafl_breakpoint* bp = libafl_qemu_breakpoints;
    while (bp) {
        if (bp->addr == pc_next) {
            TCGv_i64 tmp0 = tcg_constant_i64((uint64_t)pc_next);
            gen_helper_libafl_qemu_handle_breakpoint(tcg_env, tmp0);
            tcg_temp_free_i64(tmp0);
        }
        bp = bp->next;
    }
}
