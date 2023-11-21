#include "exit.h"

#include "sysemu/runstate.h"

// TODO: merge with definition in tcg-runtime.c
#define EXCP_LIBAFL_EXIT 0xf4775747

#ifdef CONFIG_USER_ONLY
__thread int libafl_qemu_break_asap = 0;
__thread CPUState* libafl_breakpoint_cpu;
__thread vaddr libafl_breakpoint_pc;
static __thread struct libafl_exit_reason last_exit_reason;
#else
static struct libafl_exit_reason last_exit_reason;
#endif

#if defined(TARGET_ARM) && !defined(TARGET_AARCH64)
#define THUMB_MASK(value) (value | cpu_env(libafl_breakpoint_cpu)->thumb)
#else
#define THUMB_MASK(value) value
#endif

static bool expected_exit = false;

void libafl_sync_exit_cpu(void)
{
    if (last_exit_reason.next_pc) {
        CPUClass* cc = CPU_GET_CLASS(last_exit_reason.cpu);
        cc->set_pc(last_exit_reason.cpu, THUMB_MASK(last_exit_reason.next_pc));
    }
    last_exit_reason.next_pc = 0;
}

bool libafl_exit_asap(void){
    return last_exit_reason.exit_asap;
}

static void prepare_qemu_exit(CPUState* cpu, ulong next_pc)
{
    expected_exit = true;
    last_exit_reason.cpu = cpu;
    last_exit_reason.next_pc = next_pc;

#ifndef CONFIG_USER_ONLY
    qemu_system_debug_request();
    cpu->stopped = true;
#endif
    if (cpu->running) {
        cpu->exception_index = EXCP_LIBAFL_EXIT;
        cpu_loop_exit(cpu);
    } else {
        last_exit_reason.exit_asap = 1;
    }
}

CPUState* libafl_last_exit_cpu(void)
{
    if (expected_exit) {
        return last_exit_reason.cpu;
    }

    return NULL;
}

void libafl_exit_request_sync_backdoor(CPUState* cpu, target_ulong pc)
{
    last_exit_reason.kind = SYNC_BACKDOOR;

    prepare_qemu_exit(cpu, pc);
}

void libafl_exit_request_breakpoint(CPUState* cpu, target_ulong pc)
{
    last_exit_reason.kind = BREAKPOINT;
    last_exit_reason.data.breakpoint.addr = pc;

    prepare_qemu_exit(cpu, pc);
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
