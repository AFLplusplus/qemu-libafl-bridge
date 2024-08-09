#pragma once

#include "qemu/osdep.h"

#include "qapi/error.h"

#include "exec/exec-all.h"
#include "exec/tb-flush.h"

#include "libafl/exit.h"
#include "libafl/hook.h"

#define LIBAFL_TABLES_SIZE 16384
#define LIBAFL_TABLES_HASH(p)                                                  \
    (((13 * ((size_t)(p))) ^ (((size_t)(p)) >> 15)) % LIBAFL_TABLES_SIZE)

struct libafl_instruction_hook {
    // data
    uint64_t data;
    size_t num;
    target_ulong addr;

    // helpers
    TCGHelperInfo helper_info;

    // next
    struct libafl_instruction_hook* next;
};

size_t libafl_qemu_add_instruction_hooks(target_ulong pc,
                                         void (*callback)(uint64_t data,
                                                          target_ulong pc),
                                         uint64_t data, int invalidate);

int libafl_qemu_remove_instruction_hook(size_t num, int invalidate);

size_t libafl_qemu_remove_instruction_hooks_at(target_ulong addr,
                                               int invalidate);

struct libafl_instruction_hook*
libafl_search_instruction_hook(target_ulong addr);

void libafl_qemu_hook_instruction_run(vaddr pc_next);
