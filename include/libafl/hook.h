#pragma once

#include "qemu/osdep.h"
#include "qapi/error.h"

#include "tcg/tcg-op.h"
#include "tcg/tcg-internal.h"
#include "tcg/tcg-temp-internal.h"

#define LIBAFL_MAX_INSNS 16

#define GEN_REMOVE_HOOK(name)                                                  \
    int libafl_qemu_remove_##name##_hook(size_t num, int invalidate)           \
    {                                                                          \
        CPUState* cpu;                                                         \
        struct libafl_##name##_hook** hk = &libafl_##name##_hooks;             \
                                                                               \
        while (*hk) {                                                          \
            if ((*hk)->num == num) {                                           \
                if (invalidate) {                                              \
                    CPU_FOREACH(cpu) { tb_flush(cpu); }                        \
                }                                                              \
                                                                               \
                void* tmp = *hk;                                               \
                *hk = (*hk)->next;                                             \
                free(tmp);                                                     \
                return 1;                                                      \
            } else {                                                           \
                hk = &(*hk)->next;                                             \
            }                                                                  \
        }                                                                      \
                                                                               \
        return 0;                                                              \
    }

#define GEN_REMOVE_HOOK1(name)                                                 \
    int libafl_qemu_remove_##name##_hook(size_t num)                           \
    {                                                                          \
        struct libafl_##name##_hook** hk = &libafl_##name##_hooks;             \
                                                                               \
        while (*hk) {                                                          \
            if ((*hk)->num == num) {                                           \
                void* tmp = *hk;                                               \
                *hk = (*hk)->next;                                             \
                free(tmp);                                                     \
                return 1;                                                      \
            } else {                                                           \
                hk = &(*hk)->next;                                             \
            }                                                                  \
        }                                                                      \
                                                                               \
        return 0;                                                              \
    }

// TODO: cleanup this
extern target_ulong libafl_gen_cur_pc;
extern size_t libafl_qemu_hooks_num;

void tcg_gen_callN(TCGHelperInfo* info, TCGTemp* ret, TCGTemp** args);

void libafl_tcg_gen_asan(TCGTemp* addr, size_t size);
