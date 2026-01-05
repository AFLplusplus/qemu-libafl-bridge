#pragma once

#include "accel/tcg/tb-cpu-state.h"
#include "qemu/osdep.h"
#include "tcg/helper-info.h"

typedef uint64_t (*libafl_edge_gen_cb)(uint64_t data, vaddr src,
                                       vaddr dst);
typedef void (*libafl_edge_exec_cb)(uint64_t data, uint64_t id);
typedef size_t (*libafl_edge_jit_cb)(uint64_t data, uint64_t id);

struct libafl_edge_hook {
    // functions
    libafl_edge_gen_cb gen_cb;
    libafl_edge_jit_cb jit_cb; // optional opt

    // data
    uint64_t data;
    size_t num;
    uint64_t cur_id;

    // helpers
    TCGHelperInfo helper_info;

    // next
    struct libafl_edge_hook* next;
};

TranslationBlock* libafl_gen_edge(CPUState* cpu, vaddr src_block,
                                  vaddr dst_block, int exit_n,
                                  TCGTBCPUState s);

size_t libafl_add_edge_hook(libafl_edge_gen_cb gen_cb,
                            libafl_edge_exec_cb exec_cb, uint64_t data);

bool libafl_qemu_edge_hook_set_jit(
    size_t num,
    libafl_edge_jit_cb jit_cb); // no param names to avoid to be marked as safe

int libafl_qemu_remove_edge_hook(size_t num, int invalidate);

bool libafl_qemu_hook_edge_gen(vaddr src_block, vaddr dst_block);
void libafl_qemu_hook_edge_run(void);
