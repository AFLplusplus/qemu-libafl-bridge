#include "libafl/hooks/thread.h"
#include "libafl/cpu.h"

#include <linux/unistd.h>

struct libafl_new_thread_hook* libafl_new_thread_hooks;
size_t libafl_new_thread_hooks_num = 0;

GEN_REMOVE_HOOK1(new_thread)

size_t libafl_add_new_thread_hook(bool (*callback)(uint64_t data,
                                                   CPUArchState* env,
                                                   uint32_t tid),
                                  uint64_t data)
{
    struct libafl_new_thread_hook* hook =
        calloc(sizeof(struct libafl_new_thread_hook), 1);
    hook->callback = callback;
    hook->data = data;
    hook->num = libafl_new_thread_hooks_num++;
    hook->next = libafl_new_thread_hooks;
    libafl_new_thread_hooks = hook;

    return hook->num;
}

bool libafl_hook_new_thread_run(CPUArchState* env, uint32_t tid)
{
#ifdef CONFIG_USER_ONLY
    libafl_set_qemu_env(env);
#endif

    if (libafl_new_thread_hooks) {
        bool continue_execution = true;

        struct libafl_new_thread_hook* h = libafl_new_thread_hooks;
        while (h) {
            continue_execution =
                h->callback(h->data, env, tid) && continue_execution;
            h = h->next;
        }

        return continue_execution;
    }

    return true;
}
