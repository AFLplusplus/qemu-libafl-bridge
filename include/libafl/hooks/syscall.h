#pragma once

#include "qemu/osdep.h"

#include "qapi/error.h"

#include "exec/exec-all.h"
#include "exec/tb-flush.h"

#include "libafl/exit.h"
#include "libafl/hook.h"

enum libafl_syshook_ret_tag {
    LIBAFL_SYSHOOK_RUN,
    LIBAFL_SYSHOOK_SKIP,
};

// Representation of a pre-syscall hook result.
// It is associated with the LibAFL enum `SyscallHookResult`.
// Any change made here should be also propagated to the Rust enum.
struct libafl_syshook_ret {
    enum libafl_syshook_ret_tag tag;
    union {
        target_ulong syshook_skip_retval;
    };
};

typedef struct libafl_syshook_ret (*libafl_pre_syscall_cb)(
    uint64_t data, int sys_num, target_ulong arg0, target_ulong arg1,
    target_ulong arg2, target_ulong arg3, target_ulong arg4, target_ulong arg5,
    target_ulong arg6, target_ulong arg7);

typedef target_ulong (*libafl_post_syscall_cb)(
    uint64_t data, target_ulong ret, int sys_num, target_ulong arg0,
    target_ulong arg1, target_ulong arg2, target_ulong arg3, target_ulong arg4,
    target_ulong arg5, target_ulong arg6, target_ulong arg7);

struct libafl_pre_syscall_hook {
    // functions
    libafl_pre_syscall_cb callback;

    // data
    uint64_t data;
    size_t num;

    // next
    struct libafl_pre_syscall_hook* next;
};

struct libafl_post_syscall_hook {
    // functions
    libafl_post_syscall_cb callback;

    // data
    uint64_t data;
    size_t num;

    // next
    struct libafl_post_syscall_hook* next;
};

size_t libafl_add_pre_syscall_hook(libafl_pre_syscall_cb callback,
                                   uint64_t data);
size_t libafl_add_post_syscall_hook(libafl_post_syscall_cb callback,
                                    uint64_t data);

int libafl_qemu_remove_pre_syscall_hook(size_t num);
int libafl_qemu_remove_post_syscall_hook(size_t num);

bool libafl_hook_syscall_pre_run(CPUArchState* env, int num, abi_long arg1,
                                 abi_long arg2, abi_long arg3, abi_long arg4,
                                 abi_long arg5, abi_long arg6, abi_long arg7,
                                 abi_long arg8, abi_long* ret);

void libafl_hook_syscall_post_run(int num, abi_long arg1, abi_long arg2,
                                  abi_long arg3, abi_long arg4, abi_long arg5,
                                  abi_long arg6, abi_long arg7, abi_long arg8,
                                  abi_long* ret);
