#pragma once

#include "qemu/osdep.h"

#include "qapi/error.h"

#include "exec/exec-all.h"
#include "exec/tb-flush.h"

#include "libafl/exit.h"
#include "libafl/hook.h"

typedef uint64_t (*libafl_cmp_gen_cb)(uint64_t data, target_ulong pc,
                                      size_t size);
typedef void (*libafl_cmp_exec1_cb)(uint64_t data, uint64_t id, uint8_t v0,
                                    uint8_t v1);
typedef void (*libafl_cmp_exec2_cb)(uint64_t data, uint64_t id, uint16_t v0,
                                    uint16_t v1);
typedef void (*libafl_cmp_exec4_cb)(uint64_t data, uint64_t id, uint32_t v0,
                                    uint32_t v1);
typedef void (*libafl_cmp_exec8_cb)(uint64_t data, uint64_t id, uint64_t v0,
                                    uint64_t v1);

struct libafl_cmp_hook {
    // functions
    libafl_cmp_gen_cb gen_cb;

    // data
    uint64_t data;
    size_t num;

    // helpers
    TCGHelperInfo helper_info1;
    TCGHelperInfo helper_info2;
    TCGHelperInfo helper_info4;
    TCGHelperInfo helper_info8;

    // next
    struct libafl_cmp_hook* next;
};

void libafl_gen_cmp(target_ulong pc, TCGv op0, TCGv op1, MemOp ot);
size_t libafl_add_cmp_hook(libafl_cmp_gen_cb gen_cb,
                           libafl_cmp_exec1_cb exec1_cb,
                           libafl_cmp_exec2_cb exec2_cb,
                           libafl_cmp_exec4_cb exec4_cb,
                           libafl_cmp_exec8_cb exec8_cb, uint64_t data);

int libafl_qemu_remove_cmp_hook(size_t num, int invalidate);
