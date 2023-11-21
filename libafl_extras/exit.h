#pragma once

#include "qemu/osdep.h"
#include "exec/cpu-defs.h"

enum libafl_exit_reason_kind {
    BREAKPOINT = 0,
    SYNC_BACKDOOR = 1
};

struct libafl_exit_reason_breakpoint {
    target_ulong addr;
};

struct libafl_exit_reason_sync_backdoor { };

struct libafl_exit_reason {
    enum libafl_exit_reason_kind kind;
    CPUState* cpu; // CPU that triggered an exit.
    vaddr next_pc; // The PC that should be stored in the CPU when re-entering.
    int exit_asap; // TODO: add a field to CPU
    union {
        struct libafl_exit_reason_breakpoint breakpoint; // kind == BREAKPOINT
        struct libafl_exit_reason_sync_backdoor backdoor; // kind == SYNC_BACKDOOR
    } data;
};

// Only makes sense to call if an exit was expected
// Will return NULL if there was no exit expected.
CPUState* libafl_last_exit_cpu(void);

void libafl_exit_signal_vm_start(void);
bool libafl_exit_asap(void);
void libafl_sync_exit_cpu(void);

void libafl_exit_request_sync_backdoor(CPUState* cpu, target_ulong pc);
void libafl_exit_request_breakpoint(CPUState* cpu, target_ulong pc);
struct libafl_exit_reason* libafl_get_exit_reason(void);
