#pragma once

#include "qemu/osdep.h"
#include "tcg/helper-info.h"

#include "libafl/exit.h"

typedef uint64_t (*libafl_block_pre_gen_cb)(uint64_t data, vaddr pc);
typedef void (*libafl_block_post_gen_cb)(uint64_t data, vaddr pc,
                                         vaddr block_length);

typedef void (*libafl_block_exec_cb)(uint64_t data, uint64_t id);

typedef size_t (*libafl_block_jit_cb)(uint64_t data, uint64_t id);

struct libafl_block_hook {
    // functions
    libafl_block_pre_gen_cb pre_gen_cb;
    libafl_block_post_gen_cb post_gen_cb;

    libafl_block_jit_cb jit_cb; // optional opt

    // data
    uint64_t data;
    size_t num;

    // helpers
    TCGHelperInfo helper_info;

    // next
    struct libafl_block_hook* next;
};

size_t libafl_add_block_hook(libafl_block_pre_gen_cb pre_gen_cb,
                             libafl_block_post_gen_cb post_gen_cb,
                             libafl_block_exec_cb exec_cb, uint64_t data);

bool libafl_qemu_block_hook_set_jit(
    size_t num,
    libafl_block_jit_cb jit_cb); // no param names to avoid to be marked as safe

int libafl_qemu_remove_block_hook(size_t num, int invalidate);

void libafl_qemu_hook_block_pre_run(vaddr pc);
void libafl_qemu_hook_block_post_run(TranslationBlock* tb, vaddr pc);
