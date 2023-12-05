#include "qemu/osdep.h"
#include "qapi/error.h"

#include "exec/exec-all.h"
#include "exec/tb-flush.h"

#include "hook.h"
#include "exit.h"

#ifndef TARGET_LONG_BITS
#error "TARGET_LONG_BITS not defined"
#endif

target_ulong libafl_gen_cur_pc;

struct libafl_hook* libafl_qemu_hooks[LIBAFL_TABLES_SIZE];
size_t libafl_qemu_hooks_num = 0;

size_t libafl_qemu_set_hook(target_ulong pc, void (*callback)(uint64_t data, target_ulong pc),
                            uint64_t data, int invalidate)
{
    CPUState *cpu;

    if (invalidate) {
        CPU_FOREACH(cpu) {
            libafl_breakpoint_invalidate(cpu, pc);
        }
    }

    size_t idx = LIBAFL_TABLES_HASH(pc);

    struct libafl_hook* hk = calloc(sizeof(struct libafl_hook), 1);
    hk->addr = pc;
    // hk->callback = callback;
    hk->data = data;
    hk->helper_info.func = callback;
    hk->helper_info.name = "libafl_hook";
    hk->helper_info.flags = dh_callflag(void);
    hk->helper_info.typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) | dh_typemask(tl, 2);
    // TODO check for overflow
    hk->num = libafl_qemu_hooks_num++;
    hk->next = libafl_qemu_hooks[idx];
    libafl_qemu_hooks[idx] = hk;
    return hk->num;
}

size_t libafl_qemu_remove_hooks_at(target_ulong addr, int invalidate)
{
    CPUState *cpu;
    size_t r = 0;
    
    size_t idx = LIBAFL_TABLES_HASH(addr);
    struct libafl_hook** hk = &libafl_qemu_hooks[idx];
    while (*hk) {
        if ((*hk)->addr == addr) {
            if (invalidate) {
                CPU_FOREACH(cpu) {
                    libafl_breakpoint_invalidate(cpu, addr);
                }
            }

            void *tmp = *hk;
            *hk = (*hk)->next;
            free(tmp);
            r++;
        } else {
            hk = &(*hk)->next;
        }
    }
    return r;
}

int libafl_qemu_remove_hook(size_t num, int invalidate)
{
    CPUState *cpu;
    size_t idx;
    
    for (idx = 0; idx < LIBAFL_TABLES_SIZE; ++idx) {
        struct libafl_hook** hk = &libafl_qemu_hooks[idx];
        while (*hk) {
            if ((*hk)->num == num) {
                if (invalidate) {
                    CPU_FOREACH(cpu) {
                        libafl_breakpoint_invalidate(cpu, (*hk)->addr);
                    }
                }

                void *tmp = *hk;
                *hk = (*hk)->next;
                free(tmp);
                return 1;
            } else {
                hk = &(*hk)->next;
            }
        }
    }
    return 0;
}

struct libafl_hook* libafl_search_hook(target_ulong addr)
{
    size_t idx = LIBAFL_TABLES_HASH(addr);

    struct libafl_hook* hk = libafl_qemu_hooks[idx];
    while (hk) {
        if (hk->addr == addr) {
            return hk;
        }
        hk = hk->next;
    }
    
    return NULL;
}

#define GEN_REMOVE_HOOK(name) \
int libafl_qemu_remove_##name##_hook(size_t num, int invalidate) \
{ \
    CPUState *cpu; \
    struct libafl_##name##_hook** hk = &libafl_##name##_hooks; \
     \
    while (*hk) { \
        if ((*hk)->num == num) { \
            if (invalidate) { \
                CPU_FOREACH(cpu) { \
                    tb_flush(cpu); \
                } \
            } \
             \
            void *tmp = *hk; \
            *hk = (*hk)->next; \
            free(tmp); \
            return 1; \
        } else { \
            hk = &(*hk)->next; \
        } \
    } \
     \
    return  0; \
}

#define GEN_REMOVE_HOOK1(name) \
int libafl_qemu_remove_##name##_hook(size_t num) \
{ \
    struct libafl_##name##_hook** hk = &libafl_##name##_hooks; \
     \
    while (*hk) { \
        if ((*hk)->num == num) { \
            void *tmp = *hk; \
            *hk = (*hk)->next; \
            free(tmp); \
            return 1; \
        } else { \
            hk = &(*hk)->next; \
        } \
    } \
     \
    return  0; \
}


static TCGHelperInfo libafl_exec_backdoor_hook_info = {
    .func = NULL, .name = "libafl_exec_backdoor_hook", \
    .flags = dh_callflag(void), \
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) | dh_typemask(tl, 2)
};

struct libafl_backdoor_hook* libafl_backdoor_hooks;
size_t libafl_backdoor_hooks_num = 0;

size_t libafl_add_backdoor_hook(void (*exec)(uint64_t data, target_ulong pc),
                                uint64_t data)
{
    struct libafl_backdoor_hook* hook = calloc(sizeof(struct libafl_backdoor_hook), 1);
    // hook->exec = exec;
    hook->data = data;
    hook->num = libafl_backdoor_hooks_num++;
    hook->next = libafl_backdoor_hooks;
    libafl_backdoor_hooks = hook;
    
    memcpy(&hook->helper_info, &libafl_exec_backdoor_hook_info, sizeof(TCGHelperInfo));
    hook->helper_info.func = exec;
    
    return hook->num;
}

GEN_REMOVE_HOOK(backdoor)

static TCGHelperInfo libafl_exec_edge_hook_info = {
    .func = NULL, .name = "libafl_exec_edge_hook", \
    .flags = dh_callflag(void), \
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) | dh_typemask(i64, 2)
};

struct libafl_edge_hook* libafl_edge_hooks;
size_t libafl_edge_hooks_num = 0;

size_t libafl_add_edge_hook(uint64_t (*gen)(uint64_t data, target_ulong src, target_ulong dst),
                          void (*exec)(uint64_t data, uint64_t id),
                          uint64_t data)
{
    CPUState *cpu;
    CPU_FOREACH(cpu) {
        tb_flush(cpu);
    }

    struct libafl_edge_hook* hook = calloc(sizeof(struct libafl_edge_hook), 1);
    hook->gen = gen;
    // hook->exec = exec;
    hook->data = data;
    hook->num = libafl_edge_hooks_num++;
    hook->next = libafl_edge_hooks;
    libafl_edge_hooks = hook;
    
    if (exec) {
        memcpy(&hook->helper_info, &libafl_exec_edge_hook_info, sizeof(TCGHelperInfo));
        hook->helper_info.func = exec;
    }
    
    return hook->num;
}

GEN_REMOVE_HOOK(edge)

bool libafl_qemu_edge_hook_set_jit(size_t num, size_t (*jit)(uint64_t data, uint64_t id)) {
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

static TCGHelperInfo libafl_exec_block_hook_info = {
    .func = NULL, .name = "libafl_exec_block_hook", \
    .flags = dh_callflag(void), \
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) | dh_typemask(i64, 2)
};

struct libafl_block_hook* libafl_block_hooks;
size_t libafl_block_hooks_num = 0;

size_t libafl_add_block_hook(uint64_t (*gen)(uint64_t data, target_ulong pc),
                             void (*post_gen)(uint64_t data, target_ulong pc, target_ulong block_length),
                             void (*exec)(uint64_t data, uint64_t id), uint64_t data)
{
    CPUState *cpu;
    CPU_FOREACH(cpu) {
        tb_flush(cpu);
    }

    struct libafl_block_hook* hook = calloc(sizeof(struct libafl_block_hook), 1);
    hook->gen = gen;
    hook->post_gen = post_gen;
    // hook->exec = exec;
    hook->data = data;
    hook->num = libafl_block_hooks_num++;
    hook->next = libafl_block_hooks;
    libafl_block_hooks = hook;
    
    if (exec) {
        memcpy(&hook->helper_info, &libafl_exec_block_hook_info, sizeof(TCGHelperInfo));
        hook->helper_info.func = exec;
    }
    
    return hook->num;
}

GEN_REMOVE_HOOK(block)

bool libafl_qemu_block_hook_set_jit(size_t num, size_t (*jit)(uint64_t data, uint64_t id)) {
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

static TCGHelperInfo libafl_exec_read_hook1_info = {
    .func = NULL, .name = "libafl_exec_read_hook1", \
    .flags = dh_callflag(void), \
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) | dh_typemask(i64, 2) | dh_typemask(tl, 3)
};
static TCGHelperInfo libafl_exec_read_hook2_info = {
    .func = NULL, .name = "libafl_exec_read_hook2", \
    .flags = dh_callflag(void), \
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) | dh_typemask(i64, 2) | dh_typemask(tl, 3)
};
static TCGHelperInfo libafl_exec_read_hook4_info = {
    .func = NULL, .name = "libafl_exec_read_hook4", \
    .flags = dh_callflag(void), \
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) | dh_typemask(i64, 2) | dh_typemask(tl, 3)
};
static TCGHelperInfo libafl_exec_read_hook8_info = {
    .func = NULL, .name = "libafl_exec_read_hook8", \
    .flags = dh_callflag(void), \
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) | dh_typemask(i64, 2) | dh_typemask(tl, 3)
};
static TCGHelperInfo libafl_exec_read_hookN_info = {
    .func = NULL, .name = "libafl_exec_read_hookN", \
    .flags = dh_callflag(void), \
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) | dh_typemask(i64, 2)
                | dh_typemask(tl, 3) | dh_typemask(i64, 4)
};
static TCGHelperInfo libafl_exec_write_hook1_info = {
    .func = NULL, .name = "libafl_exec_write_hook1", \
    .flags = dh_callflag(void), \
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) | dh_typemask(i64, 2) | dh_typemask(tl, 3)
};
static TCGHelperInfo libafl_exec_write_hook2_info = {
    .func = NULL, .name = "libafl_exec_write_hook2", \
    .flags = dh_callflag(void), \
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) | dh_typemask(i64, 2) | dh_typemask(tl, 3)
};
static TCGHelperInfo libafl_exec_write_hook4_info = {
    .func = NULL, .name = "libafl_exec_write_hook4", \
    .flags = dh_callflag(void), \
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) | dh_typemask(i64, 2) | dh_typemask(tl, 3)
};
static TCGHelperInfo libafl_exec_write_hook8_info = {
    .func = NULL, .name = "libafl_exec_write_hook8", \
    .flags = dh_callflag(void), \
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) | dh_typemask(i64, 2) | dh_typemask(tl, 3)
};
static TCGHelperInfo libafl_exec_write_hookN_info = {
    .func = NULL, .name = "libafl_exec_write_hookN", \
    .flags = dh_callflag(void), \
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1) | dh_typemask(i64, 2)
                | dh_typemask(tl, 3) | dh_typemask(i64, 4)
};

struct libafl_rw_hook* libafl_read_hooks;
size_t libafl_read_hooks_num = 0;

size_t libafl_add_read_hook(uint64_t (*gen)(uint64_t data, target_ulong pc, MemOpIdx oi),
                             void (*exec1)(uint64_t data, uint64_t id, target_ulong addr),
                             void (*exec2)(uint64_t data, uint64_t id, target_ulong addr),
                             void (*exec4)(uint64_t data, uint64_t id, target_ulong addr),
                             void (*exec8)(uint64_t data, uint64_t id, target_ulong addr),
                             void (*execN)(uint64_t data, uint64_t id, target_ulong addr, size_t size),
                             uint64_t data)
{
    CPUState *cpu;
    CPU_FOREACH(cpu) {
        tb_flush(cpu);
    }

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
        memcpy(&hook->helper_info1, &libafl_exec_read_hook1_info, sizeof(TCGHelperInfo));
        hook->helper_info1.func = exec1;
    }
    if (exec2) {
        memcpy(&hook->helper_info2, &libafl_exec_read_hook2_info, sizeof(TCGHelperInfo));
        hook->helper_info2.func = exec2;
    }
    if (exec4) {
        memcpy(&hook->helper_info4, &libafl_exec_read_hook4_info, sizeof(TCGHelperInfo));
        hook->helper_info4.func = exec4;
    }
    if (exec8) {
        memcpy(&hook->helper_info8, &libafl_exec_read_hook8_info, sizeof(TCGHelperInfo));
        hook->helper_info8.func = exec8;
    }
    if (execN) {
        memcpy(&hook->helper_infoN, &libafl_exec_read_hookN_info, sizeof(TCGHelperInfo));
        hook->helper_infoN.func = execN;
    }
    
    return hook->num;
}

GEN_REMOVE_HOOK(read)

struct libafl_rw_hook* libafl_write_hooks;
size_t libafl_write_hooks_num = 0;

size_t libafl_add_write_hook(uint64_t (*gen)(uint64_t data, target_ulong pc, MemOpIdx oi),
                             void (*exec1)(uint64_t data, uint64_t id, target_ulong addr),
                             void (*exec2)(uint64_t data, uint64_t id, target_ulong addr),
                             void (*exec4)(uint64_t data, uint64_t id, target_ulong addr),
                             void (*exec8)(uint64_t data, uint64_t id, target_ulong addr),
                             void (*execN)(uint64_t data, uint64_t id, target_ulong addr, size_t size),
                             uint64_t data)
{
    CPUState *cpu;
    CPU_FOREACH(cpu) {
        tb_flush(cpu);
    }

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
        memcpy(&hook->helper_info1, &libafl_exec_write_hook1_info, sizeof(TCGHelperInfo));
        hook->helper_info1.func = exec1;
    }
    if (exec2) {
        memcpy(&hook->helper_info2, &libafl_exec_write_hook2_info, sizeof(TCGHelperInfo));
        hook->helper_info2.func = exec2;
    }
    if (exec4) {
        memcpy(&hook->helper_info4, &libafl_exec_write_hook4_info, sizeof(TCGHelperInfo));
        hook->helper_info4.func = exec4;
    }
    if (exec8) {
        memcpy(&hook->helper_info8, &libafl_exec_write_hook8_info, sizeof(TCGHelperInfo));
        hook->helper_info8.func = exec8;
    }
    if (execN) {
        memcpy(&hook->helper_infoN, &libafl_exec_write_hookN_info, sizeof(TCGHelperInfo));
        hook->helper_infoN.func = execN;
    }
    
    return hook->num;
}

GEN_REMOVE_HOOK(write)

static void libafl_gen_rw(TCGTemp *addr, MemOpIdx oi, struct libafl_rw_hook* hook)
{
    size_t size = memop_size(get_memop(oi));

    while (hook) {
        uint64_t cur_id = 0;
        if (hook->gen)
            cur_id = hook->gen(hook->data, libafl_gen_cur_pc, oi);
        TCGHelperInfo* info = NULL;
        if (size == 1 && hook->helper_info1.func) info = &hook->helper_info1;
        else if (size == 2 && hook->helper_info2.func) info = &hook->helper_info2;
        else if (size == 4 && hook->helper_info4.func) info = &hook->helper_info4;
        else if (size == 8 && hook->helper_info8.func) info = &hook->helper_info8;
        if (cur_id != (uint64_t)-1) {
            if (info) {
                TCGv_i64 tmp0 = tcg_constant_i64(hook->data);
                TCGv_i64 tmp1 = tcg_constant_i64(cur_id);
                TCGTemp *tmp2[3] = { tcgv_i64_temp(tmp0), 
                                     tcgv_i64_temp(tmp1),
                                     addr };
                tcg_gen_callN(info, NULL, tmp2);
                tcg_temp_free_i64(tmp0);
                tcg_temp_free_i64(tmp1);
            } else if (hook->helper_infoN.func) {
                TCGv_i64 tmp0 = tcg_constant_i64(hook->data);
                TCGv_i64 tmp1 = tcg_constant_i64(cur_id);
                TCGv tmp2 = tcg_constant_tl(size);
                TCGTemp *tmp3[4] = { tcgv_i64_temp(tmp0),
                                     tcgv_i64_temp(tmp1),
                                     addr,
#if TARGET_LONG_BITS == 32
                                     tcgv_i32_temp(tmp2) };
#else
                                     tcgv_i64_temp(tmp2) };
#endif
                tcg_gen_callN(&hook->helper_infoN, NULL, tmp3);
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

void libafl_gen_read(TCGTemp *addr, MemOpIdx oi)
{
    libafl_gen_rw(addr, oi, libafl_read_hooks);
}

void libafl_gen_write(TCGTemp *addr, MemOpIdx oi)
{
    libafl_gen_rw(addr, oi, libafl_write_hooks);
}

static TCGHelperInfo libafl_exec_cmp_hook1_info = {
    .func = NULL, .name = "libafl_exec_cmp_hook1", \
    .flags = dh_callflag(void), \
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1)
    | dh_typemask(i64, 2) | dh_typemask(tl, 3) | dh_typemask(tl, 4)
};
static TCGHelperInfo libafl_exec_cmp_hook2_info = {
    .func = NULL, .name = "libafl_exec_cmp_hook2", \
    .flags = dh_callflag(void), \
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1)
    | dh_typemask(i64, 2) | dh_typemask(tl, 3) | dh_typemask(tl, 4)
};
static TCGHelperInfo libafl_exec_cmp_hook4_info = {
    .func = NULL, .name = "libafl_exec_cmp_hook4", \
    .flags = dh_callflag(void), \
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1)
    | dh_typemask(i64, 2) | dh_typemask(tl, 3) | dh_typemask(tl, 4)
};
static TCGHelperInfo libafl_exec_cmp_hook8_info = {
    .func = NULL, .name = "libafl_exec_cmp_hook8", \
    .flags = dh_callflag(void), \
    .typemask = dh_typemask(void, 0) | dh_typemask(i64, 1)
    | dh_typemask(i64, 2) | dh_typemask(i64, 3) | dh_typemask(i64, 4)
};

struct libafl_cmp_hook* libafl_cmp_hooks;
size_t libafl_cmp_hooks_num = 0;

size_t libafl_add_cmp_hook(uint64_t (*gen)(uint64_t data, target_ulong pc, size_t size),
                             void (*exec1)(uint64_t data, uint64_t id, uint8_t v0, uint8_t v1),
                             void (*exec2)(uint64_t data, uint64_t id, uint16_t v0, uint16_t v1),
                             void (*exec4)(uint64_t data, uint64_t id, uint32_t v0, uint32_t v1),
                             void (*exec8)(uint64_t data, uint64_t id, uint64_t v0, uint64_t v1),
                             uint64_t data)
{
    CPUState *cpu;
    CPU_FOREACH(cpu) {
        tb_flush(cpu);
    }

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
        memcpy(&hook->helper_info1, &libafl_exec_cmp_hook1_info, sizeof(TCGHelperInfo));
        hook->helper_info1.func = exec1;
    }
    if (exec2) {
        memcpy(&hook->helper_info2, &libafl_exec_cmp_hook2_info, sizeof(TCGHelperInfo));
        hook->helper_info2.func = exec2;
    }
    if (exec4) {
        memcpy(&hook->helper_info4, &libafl_exec_cmp_hook4_info, sizeof(TCGHelperInfo));
        hook->helper_info4.func = exec4;
    }
    if (exec8) {
        memcpy(&hook->helper_info8, &libafl_exec_cmp_hook8_info, sizeof(TCGHelperInfo));
        hook->helper_info8.func = exec8;
    }
    
    return hook->num;
}

GEN_REMOVE_HOOK(cmp)

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
        if (size == 1 && hook->helper_info1.func) info = &hook->helper_info1;
        else if (size == 2 && hook->helper_info2.func) info = &hook->helper_info2;
        else if (size == 4 && hook->helper_info4.func) info = &hook->helper_info4;
        else if (size == 8 && hook->helper_info8.func) info = &hook->helper_info8;
        if (cur_id != (uint64_t)-1 && info) {
            TCGv_i64 tmp0 = tcg_constant_i64(hook->data);
            TCGv_i64 tmp1 = tcg_constant_i64(cur_id);
            TCGTemp *tmp2[4] = { tcgv_i64_temp(tmp0), tcgv_i64_temp(tmp1),
#if TARGET_LONG_BITS == 32
                                 tcgv_i32_temp(op0), tcgv_i32_temp(op1) };
#else
                                 tcgv_i64_temp(op0), tcgv_i64_temp(op1) };
#endif
            tcg_gen_callN(info, NULL, tmp2);
            tcg_temp_free_i64(tmp0);
            tcg_temp_free_i64(tmp1);
        }
        hook = hook->next;
    }
}

struct libafl_pre_syscall_hook* libafl_pre_syscall_hooks;
struct libafl_post_syscall_hook* libafl_post_syscall_hooks;

size_t libafl_pre_syscall_hooks_num = 0;
size_t libafl_post_syscall_hooks_num = 0;

size_t libafl_add_pre_syscall_hook(struct syshook_ret (*callback)(
                                     uint64_t data, int sys_num, target_ulong arg0,
                                     target_ulong arg1, target_ulong arg2,
                                     target_ulong arg3, target_ulong arg4,
                                     target_ulong arg5, target_ulong arg6,
                                     target_ulong arg7),
                                   uint64_t data)
{
    struct libafl_pre_syscall_hook* hook = calloc(sizeof(struct libafl_pre_syscall_hook), 1);
    hook->callback = callback;
    hook->data = data;
    hook->num = libafl_pre_syscall_hooks_num++;
    hook->next = libafl_pre_syscall_hooks;
    libafl_pre_syscall_hooks = hook;
    
    return hook->num;
}

size_t libafl_add_post_syscall_hook(target_ulong (*callback)(
                                      uint64_t data, target_ulong ret, int sys_num,
                                      target_ulong arg0, target_ulong arg1,
                                      target_ulong arg2, target_ulong arg3,
                                      target_ulong arg4, target_ulong arg5,
                                      target_ulong arg6, target_ulong arg7),
                                    uint64_t data)
{
    struct libafl_post_syscall_hook* hook = calloc(sizeof(struct libafl_post_syscall_hook), 1);
    hook->callback = callback;
    hook->data = data;
    hook->num = libafl_post_syscall_hooks_num++;
    hook->next = libafl_post_syscall_hooks;
    libafl_post_syscall_hooks = hook;
    
    return hook->num;
}

GEN_REMOVE_HOOK1(pre_syscall)
GEN_REMOVE_HOOK1(post_syscall)

struct libafl_new_thread_hook* libafl_new_thread_hooks;
size_t libafl_new_thread_hooks_num = 0;

size_t libafl_add_new_thread_hook(bool (*callback)(uint64_t data, uint32_t tid),
                                  uint64_t data) {
    struct libafl_new_thread_hook* hook = calloc(sizeof(struct libafl_new_thread_hook), 1);
    hook->callback = callback;
    hook->data = data;
    hook->num = libafl_new_thread_hooks_num++;
    hook->next = libafl_new_thread_hooks;
    libafl_new_thread_hooks = hook;
    
    return hook->num;
}

GEN_REMOVE_HOOK1(new_thread)
