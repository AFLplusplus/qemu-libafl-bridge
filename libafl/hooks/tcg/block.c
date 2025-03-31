#include "libafl/tcg.h"
#include "libafl/hooks/tcg/block.h"

static struct libafl_block_hook* libafl_block_hooks;
static size_t libafl_block_hooks_num = 0;

static TCGHelperInfo libafl_exec_block_hook_info = {
    .func = NULL,
    .name = "libafl_exec_block_hook",
    .flags = dh_callflag(void),
    .typemask =
        dh_typemask(void, 0) | dh_typemask(i64, 1) | dh_typemask(i64, 2)};

GEN_REMOVE_HOOK(block)

size_t libafl_add_block_hook(libafl_block_pre_gen_cb pre_gen_cb,
                             libafl_block_post_gen_cb post_gen_cb,
                             libafl_block_exec_cb exec_cb, uint64_t data)
{
    CPUState* cpu;
    CPU_FOREACH(cpu) { tb_flush(cpu); }

    struct libafl_block_hook* hook =
        calloc(sizeof(struct libafl_block_hook), 1);
    hook->pre_gen_cb = pre_gen_cb;
    hook->post_gen_cb = post_gen_cb;
    hook->data = data;
    hook->num = libafl_block_hooks_num++;
    hook->next = libafl_block_hooks;
    libafl_block_hooks = hook;

    if (exec_cb) {
        memcpy(&hook->helper_info, &libafl_exec_block_hook_info,
               sizeof(TCGHelperInfo));
        hook->helper_info.func = exec_cb;
    }

    return hook->num;
}

bool libafl_qemu_block_hook_set_jit(size_t num, libafl_block_jit_cb jit_cb)
{
    struct libafl_block_hook* hk = libafl_block_hooks;
    while (hk) {
        if (hk->num == num) {
            hk->jit_cb = jit_cb;
            return true;
        }

        hk = hk->next;
    }
    return false;
}

void libafl_qemu_hook_block_post_run(TranslationBlock* tb, TranslationBlock* last_tb, vaddr pc, int tb_exit)
{
    struct libafl_block_hook* hook = libafl_block_hooks;
    while (hook) {
        if (hook->post_gen_cb)
            hook->post_gen_cb(hook->data, pc, tb->size, tb, last_tb, tb_exit);
        hook = hook->next;
    }
}

void libafl_qemu_hook_block_pre_run(target_ulong pc)
{
    struct libafl_block_hook* hook = libafl_block_hooks;

    while (hook) {
        uint64_t cur_id = 0;

        if (hook->pre_gen_cb) {
            cur_id = hook->pre_gen_cb(hook->data, pc);
        }

        if (cur_id != (uint64_t)-1 && hook->helper_info.func) {
            TCGv_i64 tmp0 = tcg_constant_i64(hook->data);
            TCGv_i64 tmp1 = tcg_constant_i64(cur_id);
            TCGTemp* tmp2[2] = {tcgv_i64_temp(tmp0), tcgv_i64_temp(tmp1)};
            tcg_gen_callN(hook->helper_info.func, &hook->helper_info, NULL,
                          tmp2);
            tcg_temp_free_i64(tmp0);
            tcg_temp_free_i64(tmp1);
        }

        if (cur_id != (uint64_t)-1 && hook->jit_cb) {
            hook->jit_cb(hook->data, cur_id);
        }

        hook = hook->next;
    }
}
