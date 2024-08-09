#pragma once

#include "qemu/osdep.h"

#include "qapi/error.h"

#include "exec/exec-all.h"
#include "exec/tb-flush.h"

#include "libafl/exit.h"
#include "libafl/hook.h"

struct libafl_new_thread_hook {
    // functions
    bool (*callback)(uint64_t data, CPUArchState* cpu, uint32_t tid);

    // data
    uint64_t data;
    size_t num;

    // next
    struct libafl_new_thread_hook* next;
};

size_t libafl_add_new_thread_hook(bool (*callback)(uint64_t data,
                                                   CPUArchState* env,
                                                   uint32_t tid),
                                  uint64_t data);
int libafl_qemu_remove_new_thread_hook(size_t num);

bool libafl_hook_new_thread_run(CPUArchState* env);
