#include "libafl/hooks/syscall.h"

struct libafl_pre_syscall_hook* libafl_pre_syscall_hooks;
size_t libafl_pre_syscall_hooks_num = 0;

struct libafl_post_syscall_hook* libafl_post_syscall_hooks;
size_t libafl_post_syscall_hooks_num = 0;

GEN_REMOVE_HOOK1(pre_syscall)
GEN_REMOVE_HOOK1(post_syscall)

size_t libafl_add_pre_syscall_hook(libafl_pre_syscall_cb callback,
                                   uint64_t data)
{
    struct libafl_pre_syscall_hook* hook =
        calloc(sizeof(struct libafl_pre_syscall_hook), 1);
    hook->callback = callback;
    hook->data = data;
    hook->num = libafl_pre_syscall_hooks_num++;
    hook->next = libafl_pre_syscall_hooks;
    libafl_pre_syscall_hooks = hook;

    return hook->num;
}

size_t libafl_add_post_syscall_hook(
    target_ulong (*callback)(uint64_t data, target_ulong ret, int sys_num,
                             target_ulong arg0, target_ulong arg1,
                             target_ulong arg2, target_ulong arg3,
                             target_ulong arg4, target_ulong arg5,
                             target_ulong arg6, target_ulong arg7),
    uint64_t data)
{
    struct libafl_post_syscall_hook* hook =
        calloc(sizeof(struct libafl_post_syscall_hook), 1);
    hook->callback = callback;
    hook->data = data;
    hook->num = libafl_post_syscall_hooks_num++;
    hook->next = libafl_post_syscall_hooks;
    libafl_post_syscall_hooks = hook;

    return hook->num;
}

bool libafl_hook_syscall_pre_run(CPUArchState* env, int num, abi_long arg1,
                                 abi_long arg2, abi_long arg3, abi_long arg4,
                                 abi_long arg5, abi_long arg6, abi_long arg7,
                                 abi_long arg8, abi_long* ret)
{
    bool skip_syscall = false;

    struct libafl_pre_syscall_hook* h = libafl_pre_syscall_hooks;
    while (h) {
        // no null check
        struct libafl_syshook_ret hook_ret = h->callback(
            h->data, num, (target_ulong)arg1, (target_ulong)arg2,
            (target_ulong)arg3, (target_ulong)arg4, (target_ulong)arg5,
            (target_ulong)arg6, (target_ulong)arg7, (target_ulong)arg8);

        if (hook_ret.tag == LIBAFL_SYSHOOK_SKIP) {
            skip_syscall = true;
            *ret = (abi_ulong)hook_ret.syshook_skip_retval;
        }

        h = h->next;
    }

    return skip_syscall;
}

void libafl_hook_syscall_post_run(int num, abi_long arg1, abi_long arg2,
                                  abi_long arg3, abi_long arg4, abi_long arg5,
                                  abi_long arg6, abi_long arg7, abi_long arg8,
                                  abi_long* ret)
{
    struct libafl_post_syscall_hook* p = libafl_post_syscall_hooks;

    while (p) {
        // no null check
        *ret = (abi_ulong)p->callback(p->data, (target_ulong)*ret, num,
                                      (target_ulong)arg1, (target_ulong)arg2,
                                      (target_ulong)arg3, (target_ulong)arg4,
                                      (target_ulong)arg5, (target_ulong)arg6,
                                      (target_ulong)arg7, (target_ulong)arg8);
        p = p->next;
    }
}
