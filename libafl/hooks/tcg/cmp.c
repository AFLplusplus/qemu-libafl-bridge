#include "libafl/tcg.h"
#include "libafl/hooks/tcg/cmp.h"

static struct libafl_cmp_hook* libafl_cmp_hooks;
static size_t libafl_cmp_hooks_num = 0;

static TCGHelperInfo libafl_exec_cmp_hook1_info = {
    .func = NULL,
    .name = "libafl_exec_cmp_hook1",
    .flags = dh_callflag(void),
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) |
                dh_typemask(i64, 2) | dh_typemask(tl, 3) | dh_typemask(tl, 4)};
static TCGHelperInfo libafl_exec_cmp_hook2_info = {
    .func = NULL,
    .name = "libafl_exec_cmp_hook2",
    .flags = dh_callflag(void),
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) |
                dh_typemask(i64, 2) | dh_typemask(tl, 3) | dh_typemask(tl, 4)};
static TCGHelperInfo libafl_exec_cmp_hook4_info = {
    .func = NULL,
    .name = "libafl_exec_cmp_hook4",
    .flags = dh_callflag(void),
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) |
                dh_typemask(i64, 2) | dh_typemask(tl, 3) | dh_typemask(tl, 4)};
static TCGHelperInfo libafl_exec_cmp_hook8_info = {
    .func = NULL,
    .name = "libafl_exec_cmp_hook8",
    .flags = dh_callflag(void),
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) |
                dh_typemask(i64, 2) | dh_typemask(i64, 3) |
                dh_typemask(i64, 4)};

GEN_REMOVE_HOOK(cmp)

size_t libafl_add_cmp_hook(libafl_cmp_gen_cb gen_cb,
                           libafl_cmp_exec1_cb exec1_cb,
                           libafl_cmp_exec2_cb exec2_cb,
                           libafl_cmp_exec4_cb exec4_cb,
                           libafl_cmp_exec8_cb exec8_cb, uint64_t data)
{
    CPUState* cpu;
    CPU_FOREACH(cpu) { tb_flush(cpu); }

    struct libafl_cmp_hook* hook = calloc(sizeof(struct libafl_cmp_hook), 1);
    hook->gen_cb = gen_cb;
    hook->data = data;
    hook->num = libafl_cmp_hooks_num++;
    hook->next = libafl_cmp_hooks;
    libafl_cmp_hooks = hook;

    if (exec1_cb) {
        memcpy(&hook->helper_info1, &libafl_exec_cmp_hook1_info,
               sizeof(TCGHelperInfo));
        hook->helper_info1.func = exec1_cb;
    }
    if (exec2_cb) {
        memcpy(&hook->helper_info2, &libafl_exec_cmp_hook2_info,
               sizeof(TCGHelperInfo));
        hook->helper_info2.func = exec2_cb;
    }
    if (exec4_cb) {
        memcpy(&hook->helper_info4, &libafl_exec_cmp_hook4_info,
               sizeof(TCGHelperInfo));
        hook->helper_info4.func = exec4_cb;
    }
    if (exec8_cb) {
        memcpy(&hook->helper_info8, &libafl_exec_cmp_hook8_info,
               sizeof(TCGHelperInfo));
        hook->helper_info8.func = exec8_cb;
    }

    return hook->num;
}

void libafl_gen_cmp(target_ulong pc, TCGv op0, TCGv op1, MemOp ot)
{
    size_t size = 0;
    switch (ot & MO_SIZE) {
    case MO_64:
        size = 8;
        break;
    case MO_32:
        size = 4;
        break;
    case MO_16:
        size = 2;
        break;
    case MO_8:
        size = 1;
        break;
    default:
        return;
    }

    struct libafl_cmp_hook* hook = libafl_cmp_hooks;
    while (hook) {
        uint64_t cur_id = 0;
        if (hook->gen_cb)
            cur_id = hook->gen_cb(hook->data, pc, size);
        TCGHelperInfo* info = NULL;
        if (size == 1 && hook->helper_info1.func)
            info = &hook->helper_info1;
        else if (size == 2 && hook->helper_info2.func)
            info = &hook->helper_info2;
        else if (size == 4 && hook->helper_info4.func)
            info = &hook->helper_info4;
        else if (size == 8 && hook->helper_info8.func)
            info = &hook->helper_info8;
        if (cur_id != (uint64_t)-1 && info) {
            TCGv_i64 tmp0 = tcg_constant_i64(hook->data);
            TCGv_i64 tmp1 = tcg_constant_i64(cur_id);
            TCGTemp* tmp2[4] = {tcgv_i64_temp(tmp0), tcgv_i64_temp(tmp1),
                                tcgv_tl_temp(op0), tcgv_tl_temp(op1)};
            tcg_gen_callN(info->func, info, NULL, tmp2);
        }
        hook = hook->next;
    }
}
