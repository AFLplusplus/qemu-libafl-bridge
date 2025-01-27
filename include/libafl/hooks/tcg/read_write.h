#pragma once

#include "qemu/osdep.h"

#include "libafl/exit.h"
#include "libafl/hook.h"

#define libafl_read_hook libafl_rw_hook
#define libafl_write_hook libafl_rw_hook

#define LIBAFL_TABLES_SIZE 16384
#define LIBAFL_TABLES_HASH(p)                                                  \
    (((13 * ((size_t)(p))) ^ (((size_t)(p)) >> 15)) % LIBAFL_TABLES_SIZE)

typedef uint64_t (*libafl_rw_gen_cb)(uint64_t data, target_ulong pc,
                                     TCGTemp* addr, MemOpIdx oi);
typedef void (*libafl_rw_exec_cb)(uint64_t data, uint64_t id, target_ulong pc,
                                  target_ulong addr);
typedef void (*libafl_rw_execN_cb)(uint64_t data, uint64_t id, target_ulong pc,
                                   target_ulong addr, size_t size);

struct libafl_rw_hook {
    // functions
    libafl_rw_gen_cb gen;

    // data
    uint64_t data;
    size_t num;

    // helpers
    TCGHelperInfo helper_info1;
    TCGHelperInfo helper_info2;
    TCGHelperInfo helper_info4;
    TCGHelperInfo helper_info8;
    TCGHelperInfo helper_infoN;

    // next
    struct libafl_rw_hook* next;
};

void libafl_gen_read(TCGTemp* pc, TCGTemp* addr, MemOpIdx oi);
void libafl_gen_write(TCGTemp* pc, TCGTemp* addr, MemOpIdx oi);

size_t libafl_add_read_hook(libafl_rw_gen_cb gen, libafl_rw_exec_cb exec1,
                            libafl_rw_exec_cb exec2, libafl_rw_exec_cb exec4,
                            libafl_rw_exec_cb exec8, libafl_rw_execN_cb execN,
                            uint64_t data);

size_t libafl_add_write_hook(libafl_rw_gen_cb gen, libafl_rw_exec_cb exec1,
                             libafl_rw_exec_cb exec2, libafl_rw_exec_cb exec4,
                             libafl_rw_exec_cb exec8, libafl_rw_execN_cb execN,
                             uint64_t data);

int libafl_qemu_remove_read_hook(size_t num, int invalidate);
int libafl_qemu_remove_write_hook(size_t num, int invalidate);
