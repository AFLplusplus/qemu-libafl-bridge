#include "libafl/tcg.h"
#include "libafl/hooks/tcg/cmp.h"

struct libafl_cmp_hook* libafl_cmp_hooks;
size_t libafl_cmp_hooks_num = 0;

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

size_t libafl_add_cmp_hook(
    uint64_t (*gen)(uint64_t data, target_ulong pc, size_t size),
    void (*exec1)(uint64_t data, uint64_t id, uint8_t v0, uint8_t v1),
    void (*exec2)(uint64_t data, uint64_t id, uint16_t v0, uint16_t v1),
    void (*exec4)(uint64_t data, uint64_t id, uint32_t v0, uint32_t v1),
    void (*exec8)(uint64_t data, uint64_t id, uint64_t v0, uint64_t v1),
    uint64_t data)
{
    CPUState* cpu;
    CPU_FOREACH(cpu) { tb_flush(cpu); }

    struct libafl_cmp_hook* hook = calloc(sizeof(struct libafl_cmp_hook), 1);
    hook->gen = gen;
    /*hook->exec1 = exec1;
    hook->exec2 = exec2;
    hook->exec4 = exec4;
    hook->exec8 = exec8;*/
    hook->data = data;
    hook->num = libafl_cmp_hooks_num++;
    hook->next = libafl_cmp_hooks;
    libafl_cmp_hooks = hook;

    if (exec1) {
        memcpy(&hook->helper_info1, &libafl_exec_cmp_hook1_info,
               sizeof(TCGHelperInfo));
        hook->helper_info1.func = exec1;
    }
    if (exec2) {
        memcpy(&hook->helper_info2, &libafl_exec_cmp_hook2_info,
               sizeof(TCGHelperInfo));
        hook->helper_info2.func = exec2;
    }
    if (exec4) {
        memcpy(&hook->helper_info4, &libafl_exec_cmp_hook4_info,
               sizeof(TCGHelperInfo));
        hook->helper_info4.func = exec4;
    }
    if (exec8) {
        memcpy(&hook->helper_info8, &libafl_exec_cmp_hook8_info,
               sizeof(TCGHelperInfo));
        hook->helper_info8.func = exec8;
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
        if (hook->gen)
            cur_id = hook->gen(hook->data, pc, size);
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
#if TARGET_LONG_BITS == 32
                                tcgv_i32_temp(op0), tcgv_i32_temp(op1)};
#else
                                tcgv_i64_temp(op0), tcgv_i64_temp(op1)};
#endif
            tcg_gen_callN(info->func, info, NULL, tmp2);
            tcg_temp_free_i64(tmp0);
            tcg_temp_free_i64(tmp1);
        }
        hook = hook->next;
    }
}
