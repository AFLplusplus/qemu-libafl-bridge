#pragma once

#include "qemu/osdep.h"
#include "tcg/tcg.h"

#include "libafl/cpu.h"

#define LIBAFL_MAX_INSNS 16

#define GEN_REMOVE_HOOK(name)                                                  \
    int libafl_qemu_remove_##name##_hook(size_t num, int invalidate)           \
    {                                                                          \
        struct libafl_##name##_hook** hk = &libafl_##name##_hooks;             \
                                                                               \
        while (*hk) {                                                          \
            if ((*hk)->num == num) {                                           \
                if (invalidate) {                                              \
                    libafl_flush_jit();                                        \
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
extern vaddr libafl_gen_cur_pc;

void libafl_tcg_gen_asan(TCGTemp* addr, size_t size);
