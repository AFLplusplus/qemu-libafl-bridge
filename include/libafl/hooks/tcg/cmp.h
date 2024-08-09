#pragma once

#include "qemu/osdep.h"

#include "qapi/error.h"

#include "exec/exec-all.h"
#include "exec/tb-flush.h"

#include "libafl/exit.h"
#include "libafl/hook.h"

struct libafl_cmp_hook {
    // functions
    uint64_t (*gen)(uint64_t data, target_ulong pc, size_t size);

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
size_t libafl_add_cmp_hook(
    uint64_t (*gen)(uint64_t data, target_ulong pc, size_t size),
    void (*exec1)(uint64_t data, uint64_t id, uint8_t v0, uint8_t v1),
    void (*exec2)(uint64_t data, uint64_t id, uint16_t v0, uint16_t v1),
    void (*exec4)(uint64_t data, uint64_t id, uint32_t v0, uint32_t v1),
    void (*exec8)(uint64_t data, uint64_t id, uint64_t v0, uint64_t v1),
    uint64_t data);
int libafl_qemu_remove_cmp_hook(size_t num, int invalidate);
