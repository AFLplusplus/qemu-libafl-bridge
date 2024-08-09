#include "libafl/hooks/cpu_run.h"

static struct libafl_cpu_run_hook* libafl_cpu_run_hooks = NULL;
static size_t libafl_cpu_run_hooks_num = 0;

GEN_REMOVE_HOOK1(cpu_run)

size_t libafl_hook_cpu_run_add(libafl_cpu_run_fn pre_cpu_run,
                               libafl_cpu_run_fn post_cpu_run, uint64_t data)
{
    struct libafl_cpu_run_hook* hook =
        calloc(sizeof(struct libafl_cpu_run_hook), 1);

    hook->pre_cpu_run = pre_cpu_run;
    hook->post_cpu_run = post_cpu_run;

    hook->data = data;
    hook->num = libafl_cpu_run_hooks_num++;
    hook->next = libafl_cpu_run_hooks;
    libafl_cpu_run_hooks = hook;

    return hook->num;
}

void libafl_hook_cpu_run_pre_exec(CPUState* cpu)
{
    struct libafl_cpu_run_hook* h = libafl_cpu_run_hooks;
    while (h) {
        h->pre_cpu_run(h->data, cpu);
        h = h->next;
    }
}

void libafl_hook_cpu_run_post_exec(CPUState* cpu)
{
    struct libafl_cpu_run_hook* h = libafl_cpu_run_hooks;
    while (h) {
        h->post_cpu_run(h->data, cpu);
        h = h->next;
    }
}
