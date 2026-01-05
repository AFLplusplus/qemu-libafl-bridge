#pragma once

#include "qemu/osdep.h"
#include "tcg/helper-info.h"

#include "libafl/exit.h"

#define LIBAFL_TABLES_SIZE 16384
#define LIBAFL_TABLES_HASH(p)                                                  \
    (((13 * ((size_t)(p))) ^ (((size_t)(p)) >> 15)) % LIBAFL_TABLES_SIZE)

typedef void (*libafl_instruction_cb)(uint64_t data, vaddr pc);

struct libafl_instruction_hook {
    // data
    uint64_t data;
    size_t num;
    vaddr addr;

    // helpers
    TCGHelperInfo helper_info;

    // next
    struct libafl_instruction_hook* next;
};

size_t libafl_qemu_add_instruction_hooks(vaddr pc,
                                         libafl_instruction_cb callback,
                                         uint64_t data, int invalidate);

int libafl_qemu_remove_instruction_hook(size_t num, int invalidate);

size_t libafl_qemu_remove_instruction_hooks_at(vaddr addr,
                                               int invalidate);

struct libafl_instruction_hook*
libafl_search_instruction_hook(vaddr addr);

void libafl_qemu_hook_instruction_run(vaddr pc_next);
