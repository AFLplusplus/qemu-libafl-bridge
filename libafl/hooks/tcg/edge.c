#include "libafl/hooks/tcg/edge.h"

struct libafl_edge_hook* libafl_edge_hooks;
size_t libafl_edge_hooks_num = 0;

static TCGHelperInfo libafl_exec_edge_hook_info = {
    .func = NULL,
    .name = "libafl_exec_edge_hook",
    .flags = dh_callflag(void),
    .typemask =
        dh_typemask(void, 0) | dh_typemask(i64, 1) | dh_typemask(i64, 2)};

GEN_REMOVE_HOOK(edge)

size_t libafl_add_edge_hook(uint64_t (*gen)(uint64_t data, target_ulong src,
                                            target_ulong dst),
                            void (*exec)(uint64_t data, uint64_t id),
                            uint64_t data)
{
    CPUState* cpu;
    CPU_FOREACH(cpu) { tb_flush(cpu); }

    struct libafl_edge_hook* hook = calloc(sizeof(struct libafl_edge_hook), 1);
    hook->gen = gen;
    // hook->exec = exec;
    hook->data = data;
    hook->num = libafl_edge_hooks_num++;
    hook->next = libafl_edge_hooks;
    libafl_edge_hooks = hook;

    if (exec) {
        memcpy(&hook->helper_info, &libafl_exec_edge_hook_info,
               sizeof(TCGHelperInfo));
        hook->helper_info.func = exec;
    }

    return hook->num;
}

bool libafl_qemu_edge_hook_set_jit(size_t num,
                                   size_t (*jit)(uint64_t data, uint64_t id))
{
    struct libafl_edge_hook* hk = libafl_edge_hooks;
    while (hk) {
        if (hk->num == num) {
            hk->jit = jit;
            return true;
        } else {
            hk = hk->next;
        }
    }
    return false;
}

bool libafl_qemu_hook_edge_gen(target_ulong src_block, target_ulong dst_block)
{
    struct libafl_edge_hook* hook = libafl_edge_hooks;
    bool no_exec_hook = true;

    while (hook) {
        hook->cur_id = 0;

        if (hook->gen) {
            hook->cur_id = hook->gen(hook->data, src_block, dst_block);
        }

        if (hook->cur_id != (uint64_t)-1 &&
            (hook->helper_info.func || hook->jit)) {
            no_exec_hook = false;
        }

        hook = hook->next;
    }

    return no_exec_hook;
}

void libafl_qemu_hook_edge_run(void)
{
    struct libafl_edge_hook* hook = libafl_edge_hooks;

    while (hook) {
        if (hook->cur_id != (uint64_t)-1 && hook->helper_info.func) {
            TCGv_i64 tmp0 = tcg_constant_i64(hook->data);
            TCGv_i64 tmp1 = tcg_constant_i64(hook->cur_id);
            TCGTemp* tmp2[2] = {tcgv_i64_temp(tmp0), tcgv_i64_temp(tmp1)};
            tcg_gen_callN(&hook->helper_info, NULL, tmp2);
            tcg_temp_free_i64(tmp0);
            tcg_temp_free_i64(tmp1);
        }
        if (hook->cur_id != (uint64_t)-1 && hook->jit) {
            hook->jit(hook->data, hook->cur_id);
        }
        hook = hook->next;
    }
}
