#pragma once

#include "qemu/osdep.h"

#include "qapi/error.h"

#include "exec/exec-all.h"
#include "exec/tb-flush.h"

#include "libafl/exit.h"
#include "libafl/hook.h"

typedef void (*libafl_backdoor_exec_cb)(uint64_t data, CPUArchState* cpu,
                                        target_ulong pc);

struct libafl_backdoor_hook {
    // data
    uint64_t data;
    size_t num;

    // helpers
    TCGHelperInfo helper_info;

    // next
    struct libafl_backdoor_hook* next;
};

size_t libafl_add_backdoor_hook(libafl_backdoor_exec_cb exec_cb, uint64_t data);

int libafl_qemu_remove_backdoor_hook(size_t num, int invalidate);

void libafl_qemu_hook_backdoor_run(vaddr pc_next);
