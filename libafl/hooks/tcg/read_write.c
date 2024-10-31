#include "libafl/tcg.h"
#include "libafl/hooks/tcg/read_write.h"

struct libafl_rw_hook* libafl_read_hooks;
size_t libafl_read_hooks_num = 0;

struct libafl_rw_hook* libafl_write_hooks;
size_t libafl_write_hooks_num = 0;

static TCGHelperInfo libafl_exec_read_hook1_info = {
    .func = NULL,
    .name = "libafl_exec_read_hook1",
    .flags = dh_callflag(void),
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) |
                dh_typemask(i64, 2) | dh_typemask(tl, 3)};
static TCGHelperInfo libafl_exec_read_hook2_info = {
    .func = NULL,
    .name = "libafl_exec_read_hook2",
    .flags = dh_callflag(void),
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) |
                dh_typemask(i64, 2) | dh_typemask(tl, 3)};
static TCGHelperInfo libafl_exec_read_hook4_info = {
    .func = NULL,
    .name = "libafl_exec_read_hook4",
    .flags = dh_callflag(void),
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) |
                dh_typemask(i64, 2) | dh_typemask(tl, 3)};
static TCGHelperInfo libafl_exec_read_hook8_info = {
    .func = NULL,
    .name = "libafl_exec_read_hook8",
    .flags = dh_callflag(void),
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) |
                dh_typemask(i64, 2) | dh_typemask(tl, 3)};
static TCGHelperInfo libafl_exec_read_hookN_info = {
    .func = NULL,
    .name = "libafl_exec_read_hookN",
    .flags = dh_callflag(void),
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) |
                dh_typemask(i64, 2) | dh_typemask(tl, 3) | dh_typemask(i64, 4)};
static TCGHelperInfo libafl_exec_write_hook1_info = {
    .func = NULL,
    .name = "libafl_exec_write_hook1",
    .flags = dh_callflag(void),
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) |
                dh_typemask(i64, 2) | dh_typemask(tl, 3)};
static TCGHelperInfo libafl_exec_write_hook2_info = {
    .func = NULL,
    .name = "libafl_exec_write_hook2",
    .flags = dh_callflag(void),
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) |
                dh_typemask(i64, 2) | dh_typemask(tl, 3)};
static TCGHelperInfo libafl_exec_write_hook4_info = {
    .func = NULL,
    .name = "libafl_exec_write_hook4",
    .flags = dh_callflag(void),
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) |
                dh_typemask(i64, 2) | dh_typemask(tl, 3)};
static TCGHelperInfo libafl_exec_write_hook8_info = {
    .func = NULL,
    .name = "libafl_exec_write_hook8",
    .flags = dh_callflag(void),
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) |
                dh_typemask(i64, 2) | dh_typemask(tl, 3)};
static TCGHelperInfo libafl_exec_write_hookN_info = {
    .func = NULL,
    .name = "libafl_exec_write_hookN",
    .flags = dh_callflag(void),
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) |
                dh_typemask(i64, 2) | dh_typemask(tl, 3) | dh_typemask(i64, 4)};

GEN_REMOVE_HOOK(read)

GEN_REMOVE_HOOK(write)

size_t libafl_add_read_hook(
    uint64_t (*gen)(uint64_t data, target_ulong pc, TCGTemp* addr, MemOpIdx oi),
    void (*exec1)(uint64_t data, uint64_t id, target_ulong addr),
    void (*exec2)(uint64_t data, uint64_t id, target_ulong addr),
    void (*exec4)(uint64_t data, uint64_t id, target_ulong addr),
    void (*exec8)(uint64_t data, uint64_t id, target_ulong addr),
    void (*execN)(uint64_t data, uint64_t id, target_ulong addr, size_t size),
    uint64_t data)
{
    CPUState* cpu;
    CPU_FOREACH(cpu) { tb_flush(cpu); }

    struct libafl_rw_hook* hook = calloc(sizeof(struct libafl_rw_hook), 1);
    hook->gen = gen;
    /*hook->exec1 = exec1;
    hook->exec2 = exec2;
    hook->exec4 = exec4;
    hook->exec8 = exec8;
    hook->execN = execN;*/
    hook->data = data;
    hook->num = libafl_read_hooks_num++;
    hook->next = libafl_read_hooks;
    libafl_read_hooks = hook;

    if (exec1) {
        memcpy(&hook->helper_info1, &libafl_exec_read_hook1_info,
               sizeof(TCGHelperInfo));
        hook->helper_info1.func = exec1;
    }
    if (exec2) {
        memcpy(&hook->helper_info2, &libafl_exec_read_hook2_info,
               sizeof(TCGHelperInfo));
        hook->helper_info2.func = exec2;
    }
    if (exec4) {
        memcpy(&hook->helper_info4, &libafl_exec_read_hook4_info,
               sizeof(TCGHelperInfo));
        hook->helper_info4.func = exec4;
    }
    if (exec8) {
        memcpy(&hook->helper_info8, &libafl_exec_read_hook8_info,
               sizeof(TCGHelperInfo));
        hook->helper_info8.func = exec8;
    }
    if (execN) {
        memcpy(&hook->helper_infoN, &libafl_exec_read_hookN_info,
               sizeof(TCGHelperInfo));
        hook->helper_infoN.func = execN;
    }

    return hook->num;
}

size_t libafl_add_write_hook(
    uint64_t (*gen)(uint64_t data, target_ulong pc, TCGTemp* addr, MemOpIdx oi),
    void (*exec1)(uint64_t data, uint64_t id, target_ulong addr),
    void (*exec2)(uint64_t data, uint64_t id, target_ulong addr),
    void (*exec4)(uint64_t data, uint64_t id, target_ulong addr),
    void (*exec8)(uint64_t data, uint64_t id, target_ulong addr),
    void (*execN)(uint64_t data, uint64_t id, target_ulong addr, size_t size),
    uint64_t data)
{
    CPUState* cpu;
    CPU_FOREACH(cpu) { tb_flush(cpu); }

    struct libafl_rw_hook* hook = calloc(sizeof(struct libafl_rw_hook), 1);
    hook->gen = gen;
    /*hook->exec1 = exec1;
    hook->exec2 = exec2;
    hook->exec4 = exec4;
    hook->exec8 = exec8;
    hook->execN = execN;*/
    hook->data = data;
    hook->num = libafl_write_hooks_num++;
    hook->next = libafl_write_hooks;
    libafl_write_hooks = hook;

    if (exec1) {
        memcpy(&hook->helper_info1, &libafl_exec_write_hook1_info,
               sizeof(TCGHelperInfo));
        hook->helper_info1.func = exec1;
    }
    if (exec2) {
        memcpy(&hook->helper_info2, &libafl_exec_write_hook2_info,
               sizeof(TCGHelperInfo));
        hook->helper_info2.func = exec2;
    }
    if (exec4) {
        memcpy(&hook->helper_info4, &libafl_exec_write_hook4_info,
               sizeof(TCGHelperInfo));
        hook->helper_info4.func = exec4;
    }
    if (exec8) {
        memcpy(&hook->helper_info8, &libafl_exec_write_hook8_info,
               sizeof(TCGHelperInfo));
        hook->helper_info8.func = exec8;
    }
    if (execN) {
        memcpy(&hook->helper_infoN, &libafl_exec_write_hookN_info,
               sizeof(TCGHelperInfo));
        hook->helper_infoN.func = execN;
    }

    return hook->num;
}

static void libafl_gen_rw(TCGTemp* addr, MemOpIdx oi,
                          struct libafl_rw_hook* hook)
{
    size_t size = memop_size(get_memop(oi));

    while (hook) {
        uint64_t cur_id = 0;
        if (hook->gen)
            cur_id = hook->gen(hook->data, libafl_gen_cur_pc, addr, oi);
        TCGHelperInfo* info = NULL;
        if (size == 1 && hook->helper_info1.func)
            info = &hook->helper_info1;
        else if (size == 2 && hook->helper_info2.func)
            info = &hook->helper_info2;
        else if (size == 4 && hook->helper_info4.func)
            info = &hook->helper_info4;
        else if (size == 8 && hook->helper_info8.func)
            info = &hook->helper_info8;
        if (cur_id != (uint64_t)-1) {
            if (info) {
                TCGv_i64 tmp0 = tcg_constant_i64(hook->data);
                TCGv_i64 tmp1 = tcg_constant_i64(cur_id);
                TCGTemp* tmp2[3] = {tcgv_i64_temp(tmp0), tcgv_i64_temp(tmp1),
                                    addr};
                tcg_gen_callN(info->func, info, NULL, tmp2);
                tcg_temp_free_i64(tmp0);
                tcg_temp_free_i64(tmp1);
            } else if (hook->helper_infoN.func) {
                TCGv_i64 tmp0 = tcg_constant_i64(hook->data);
                TCGv_i64 tmp1 = tcg_constant_i64(cur_id);
                TCGv tmp2 = tcg_constant_tl(size);
                TCGTemp* tmp3[4] = {tcgv_i64_temp(tmp0), tcgv_i64_temp(tmp1),
                                    addr,
#if TARGET_LONG_BITS == 32
                                    tcgv_i32_temp(tmp2)};
#else
                                    tcgv_i64_temp(tmp2)};
#endif
                tcg_gen_callN(hook->helper_infoN.func, &hook->helper_infoN, NULL, tmp3);
                tcg_temp_free_i64(tmp0);
                tcg_temp_free_i64(tmp1);
#if TARGET_LONG_BITS == 32
                tcg_temp_free_i32(tmp2);
#else
                tcg_temp_free_i64(tmp2);
#endif
            }
        }
        hook = hook->next;
    }
}

void libafl_gen_read(TCGTemp* addr, MemOpIdx oi)
{
    libafl_gen_rw(addr, oi, libafl_read_hooks);
}

void libafl_gen_write(TCGTemp* addr, MemOpIdx oi)
{
    libafl_gen_rw(addr, oi, libafl_write_hooks);
}
