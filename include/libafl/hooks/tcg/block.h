#pragma once

#include "qemu/osdep.h"

#include "qapi/error.h"

#include "exec/exec-all.h"
#include "exec/tb-flush.h"

#include "libafl/exit.h"
#include "libafl/hook.h"

struct libafl_block_hook {
    // functions
    uint64_t (*gen)(uint64_t data, target_ulong pc);
    void (*post_gen)(uint64_t data, target_ulong pc, target_ulong block_length);

    size_t (*jit)(uint64_t data, uint64_t id); // optional opt

    // data
    uint64_t data;
    size_t num;

    // helpers
    TCGHelperInfo helper_info;

    // next
    struct libafl_block_hook* next;
};

void libafl_qemu_hook_block_post_gen(TranslationBlock* tb, vaddr pc);
void libafl_qemu_hook_block_run(target_ulong pc);

bool libafl_qemu_block_hook_set_jit(
    size_t num,
    size_t (*jit)(uint64_t,
                  uint64_t)); // no param names to avoid to be marked as safe
int libafl_qemu_remove_block_hook(size_t num, int invalidate);
size_t libafl_add_block_hook(uint64_t (*gen)(uint64_t data, target_ulong pc),
                             void (*post_gen)(uint64_t data, target_ulong pc,
                                              target_ulong block_length),
                             void (*exec)(uint64_t data, uint64_t id),
                             uint64_t data);
