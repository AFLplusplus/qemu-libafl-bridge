#include "qemu/osdep.h"
#include "cpu.h"
#include "tcg/tcg.h"
#include "tcg/tcg-op-common.h"
#include "exec/cpu-common.h"

#include "libafl/exit.h"
#include "libafl/defs.h"
#include "libafl/cpu.h"

#if !defined(CONFIG_USER_ONLY) && defined(AS_LIB)
#include "system/runstate.h"
#endif
#include "linux-user/thread_cpu.h"

#ifdef CONFIG_USER_ONLY
#define THREAD_MODIFIER __thread
#else
#define THREAD_MODIFIER
#endif

struct libafl_breakpoint* libafl_qemu_breakpoints = NULL;

int libafl_qemu_set_breakpoint(vaddr pc)
{
    CPUState* cpu;

    CPU_FOREACH(cpu) { libafl_breakpoint_invalidate(cpu, pc); }

    struct libafl_breakpoint* bp = calloc(sizeof(struct libafl_breakpoint), 1);
    bp->addr = pc;
    bp->next = libafl_qemu_breakpoints;
    libafl_qemu_breakpoints = bp;
    return 1;
}

int libafl_qemu_remove_breakpoint(vaddr pc)
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

typedef struct LibAFLThreadInformation {
    int id;
    struct libafl_exit_reason *exit_reason;
    bool *expected_exit;
    QTAILQ_ENTRY(LibAFLThreadInformation) next;
} LibAFLThreadInformation;

QTAILQ_HEAD(LibAflThreadInformationList, LibAFLThreadInformation);
static union LibAflThreadInformationList libafl_thread_info_list_head;

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

static void prepare_qemu_exit(CPUState* cpu, vaddr next_pc)
{
    expected_exit = true;
    last_exit_reason.cpu = cpu;
    last_exit_reason.next_pc = next_pc;

#if !defined(CONFIG_USER_ONLY) && defined(AS_LIB)
    qemu_system_return_request();
#endif

    LibAFLThreadInformation *info;
    QTAILQ_FOREACH(info, &libafl_thread_info_list_head, next) {
        info->exit_reason->kind = last_exit_reason.kind;
        info->exit_reason->data.breakpoint.addr = next_pc;
        info->exit_reason->cpu = cpu;
        info->exit_reason->next_pc = next_pc;
        *(info->expected_exit) = true;
    }

    CPUState *c;
    CPU_FOREACH(c) {
        qemu_cpu_kick(c);
        cpu_exit(c);
        if (c->running) {
            c->exception_index = EXCP_LIBAFL_EXIT;
            cpu_exit(c);
        }
    }

    cpu_loop_exit(last_exit_reason.cpu);
}

CPUState* libafl_last_exit_cpu(void)
{
    if (expected_exit) {
        return last_exit_reason.cpu;
    }

    return NULL;
}

void libafl_exit_request_internal(CPUState* cpu, vaddr pc,
                                  ShutdownCause cause, int signal)
{
    last_exit_reason.kind = INTERNAL;
    last_exit_reason.data.internal.cause = cause;
    last_exit_reason.data.internal.signal = signal;

    last_exit_reason.cpu = cpu;
    last_exit_reason.next_pc = pc;
    expected_exit = true;
}

void libafl_exit_request_custom_insn(CPUState* cpu, vaddr pc,
                                     enum libafl_custom_insn_kind kind)
{
    last_exit_reason.kind = CUSTOM_INSN;

    prepare_qemu_exit(cpu, pc);
}

void libafl_exit_request_breakpoint(CPUState* cpu, vaddr pc)
{
    last_exit_reason.kind = BREAKPOINT;
    last_exit_reason.data.breakpoint.addr = pc;

    prepare_qemu_exit(cpu, pc);
}

void libafl_exit_request_crash(CPUState* cpu)
{
    CPUClass* cc = CPU_GET_CLASS(cpu);

    expected_exit = true;
    last_exit_reason.kind = CRASH;
    last_exit_reason.cpu = cpu;

    prepare_qemu_exit(current_cpu, cc->get_pc(cpu));
}

#ifndef CONFIG_USER_ONLY
void libafl_exit_request_timeout(void)
{
    expected_exit = true;
    last_exit_reason.kind = TIMEOUT;
    last_exit_reason.cpu = current_cpu;

#ifdef AS_LIB
    qemu_system_return_request();
#endif
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

    LibAFLThreadInformation *info;
    QTAILQ_FOREACH(info, &libafl_thread_info_list_head, next) {
        info->exit_reason->cpu = NULL;
        *(info->expected_exit) = false;
    }
}

struct libafl_exit_reason* libafl_get_exit_reason(void)
{
    if (expected_exit) {
        return &last_exit_reason;
    }

    return NULL;
}

void libafl_thread_info_list_init(void) {
    LibAFLThreadInformation *thread_info = g_new0(LibAFLThreadInformation, 1);
    thread_info->exit_reason = &last_exit_reason;
    thread_info->expected_exit = &expected_exit;
    thread_info->id = 0;

    QTAILQ_INIT(&libafl_thread_info_list_head);
    QTAILQ_INSERT_HEAD(&libafl_thread_info_list_head, thread_info, next);
}

void libafl_thread_info_list_add(void) {
    LibAFLThreadInformation *thread_info = g_new0(LibAFLThreadInformation, 1);
    thread_info->exit_reason = &last_exit_reason;
    thread_info->expected_exit = &expected_exit;
    thread_info->id = thread_cpu->cpu_index;

    QTAILQ_INSERT_TAIL(&libafl_thread_info_list_head, thread_info, next);
}

void libafl_thread_info_list_remove(void) {
    LibAFLThreadInformation *thread_info;

    int current_id = thread_cpu->cpu_index;

    QTAILQ_FOREACH(thread_info, &libafl_thread_info_list_head, next) {
        if (thread_info->id == current_id) {
            QTAILQ_REMOVE(&libafl_thread_info_list_head, thread_info, next);

            // avoid memory leak
            g_free(thread_info);

            break;
        }
    }
}

void libafl_qemu_breakpoint_run(vaddr pc_next)
{
    struct libafl_breakpoint* bp = libafl_qemu_breakpoints;
    while (bp) {
        if (bp->addr == pc_next) {
            TCGv_i64 tmp0 = tcg_constant_i64((uint64_t)pc_next);
            gen_helper_libafl_qemu_handle_breakpoint(tcg_env, tmp0);
        }
        bp = bp->next;
    }
}
