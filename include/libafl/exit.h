#pragma once

#include "qemu/osdep.h"
#include "exec/cpu-defs.h"

#define EXCP_LIBAFL_EXIT 0xf4775747

struct libafl_breakpoint {
    target_ulong addr;
    struct libafl_breakpoint* next;
};

int libafl_qemu_set_breakpoint(target_ulong pc);
int libafl_qemu_remove_breakpoint(target_ulong pc);
void libafl_qemu_trigger_breakpoint(CPUState* cpu);
void libafl_qemu_breakpoint_run(vaddr pc_next);

enum libafl_exit_reason_kind {
    INTERNAL = 0,
    BREAKPOINT = 1,
    SYNC_EXIT = 2,
    TIMEOUT = 3,
};

// QEMU exited on its own for some reason.
struct libafl_exit_reason_internal {
    ShutdownCause cause;
    int signal; // valid if cause == SHUTDOWN_CAUSE_HOST_SIGNAL
};

// A breakpoint has been triggered.
struct libafl_exit_reason_breakpoint {
    target_ulong addr;
};

// A synchronous exit has been triggered.
struct libafl_exit_reason_sync_exit {};

// A timeout occured and we were asked to exit on timeout
struct libafl_exit_reason_timeout {};

struct libafl_exit_reason {
    enum libafl_exit_reason_kind kind;
    CPUState* cpu; // CPU that triggered an exit.
    vaddr next_pc; // The PC that should be stored in the CPU when re-entering.
    union {
        struct libafl_exit_reason_internal internal;        // kind == INTERNAL
        struct libafl_exit_reason_breakpoint breakpoint;    // kind == BREAKPOINT
        struct libafl_exit_reason_sync_exit sync_exit;      // kind == SYNC_EXIT
        struct libafl_exit_reason_timeout timeout;          // kind == TIMEOUT
    } data;
};

// Only makes sense to call if an exit was expected
// Will return NULL if there was no exit expected.
CPUState* libafl_last_exit_cpu(void);

void libafl_exit_signal_vm_start(void);
bool libafl_exit_asap(void);
void libafl_sync_exit_cpu(void);

void libafl_exit_request_internal(CPUState* cpu, uint64_t pc,
                                  ShutdownCause cause, int signal);
void libafl_exit_request_breakpoint(CPUState* cpu, target_ulong pc);
void libafl_exit_request_sync_backdoor(CPUState* cpu, target_ulong pc);

#ifndef CONFIG_USER_ONLY
void libafl_exit_request_timeout(void);
#endif

struct libafl_exit_reason* libafl_get_exit_reason(void);
