#pragma once

#include "qemu/osdep.h"

#include "qapi/error.h"

#include "exec/exec-all.h"
#include "exec/tb-flush.h"

#include "libafl/exit.h"
#include "libafl/hook.h"

typedef void (*libafl_cpu_run_fn)(uint64_t data, CPUState* cpu);

struct libafl_cpu_run_hook {
    // functions
    libafl_cpu_run_fn pre_cpu_run;
    libafl_cpu_run_fn post_cpu_run;

    // data
    uint64_t data;
    size_t num;

    // next
    struct libafl_cpu_run_hook* next;
};

size_t libafl_hook_cpu_run_add(libafl_cpu_run_fn pre_cpu_run,
                               libafl_cpu_run_fn post_cpu_run, uint64_t data);

int libafl_hook_cpu_run_remove(size_t num);

int libafl_qemu_remove_cpu_run_hook(size_t num);

void libafl_hook_cpu_run_pre_exec(CPUState* cpu);
void libafl_hook_cpu_run_post_exec(CPUState* cpu);
