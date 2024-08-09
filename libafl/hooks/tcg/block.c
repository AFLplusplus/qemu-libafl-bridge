#include "libafl/hooks/tcg/block.h"

struct libafl_block_hook* libafl_block_hooks;
size_t libafl_block_hooks_num = 0;

static TCGHelperInfo libafl_exec_block_hook_info = {
    .func = NULL,
    .name = "libafl_exec_block_hook",
    .flags = dh_callflag(void),
    .typemask =
        dh_typemask(void, 0) | dh_typemask(i64, 1) | dh_typemask(i64, 2)};

GEN_REMOVE_HOOK(block)

size_t libafl_add_block_hook(uint64_t (*gen)(uint64_t data, target_ulong pc),
                             void (*post_gen)(uint64_t data, target_ulong pc,
                                              target_ulong block_length),
                             void (*exec)(uint64_t data, uint64_t id),
                             uint64_t data)
{
    CPUState* cpu;
    CPU_FOREACH(cpu) { tb_flush(cpu); }

    struct libafl_block_hook* hook =
        calloc(sizeof(struct libafl_block_hook), 1);
    hook->gen = gen;
    hook->post_gen = post_gen;
    // hook->exec = exec;
    hook->data = data;
    hook->num = libafl_block_hooks_num++;
    hook->next = libafl_block_hooks;
    libafl_block_hooks = hook;

    if (exec) {
        memcpy(&hook->helper_info, &libafl_exec_block_hook_info,
               sizeof(TCGHelperInfo));
        hook->helper_info.func = exec;
    }

    return hook->num;
}

bool libafl_qemu_block_hook_set_jit(size_t num,
                                    size_t (*jit)(uint64_t data, uint64_t id))
{
    struct libafl_block_hook* hk = libafl_block_hooks;
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

void libafl_qemu_hook_block_post_gen(TranslationBlock* tb, vaddr pc)
{
    struct libafl_block_hook* hook = libafl_block_hooks;
    while (hook) {
        if (hook->post_gen)
            hook->post_gen(hook->data, pc, tb->size);
        hook = hook->next;
    }
}

void libafl_qemu_hook_block_run(target_ulong pc)
{
    struct libafl_block_hook* hook = libafl_block_hooks;

    while (hook) {
        uint64_t cur_id = 0;
        if (hook->gen)
            cur_id = hook->gen(hook->data, pc);
        if (cur_id != (uint64_t)-1 && hook->helper_info.func) {
            TCGv_i64 tmp0 = tcg_constant_i64(hook->data);
            TCGv_i64 tmp1 = tcg_constant_i64(cur_id);
            TCGTemp* tmp2[2] = {tcgv_i64_temp(tmp0), tcgv_i64_temp(tmp1)};
            tcg_gen_callN(&hook->helper_info, NULL, tmp2);
            tcg_temp_free_i64(tmp0);
            tcg_temp_free_i64(tmp1);
        }
        if (cur_id != (uint64_t)-1 && hook->jit) {
            hook->jit(hook->data, cur_id);
        }
        hook = hook->next;
    }
}
