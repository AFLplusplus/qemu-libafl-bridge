#include "libafl/tcg.h"
#include "libafl/hooks/tcg/instruction.h"

#include "libafl/cpu.h"

tcg_target_ulong libafl_gen_cur_pc;
struct libafl_instruction_hook*
    libafl_qemu_instruction_hooks[LIBAFL_TABLES_SIZE];
size_t libafl_qemu_hooks_num = 0;

size_t libafl_qemu_add_instruction_hooks(target_ulong pc,
                                         libafl_instruction_cb callback,
                                         uint64_t data, int invalidate)
{
    CPUState* cpu;

    if (invalidate) {
        CPU_FOREACH(cpu) { libafl_breakpoint_invalidate(cpu, pc); }
    }

    size_t idx = LIBAFL_TABLES_HASH(pc);

    struct libafl_instruction_hook* hk =
        calloc(sizeof(struct libafl_instruction_hook), 1);
    hk->addr = pc;
    // hk->callback = callback;
    hk->data = data;
    hk->helper_info.func = callback;
    hk->helper_info.name = "libafl_instruction_hook";
    hk->helper_info.flags = dh_callflag(void);
    hk->helper_info.typemask =
        dh_typemask(void, 0) | dh_typemask(i64, 1) | dh_typemask(tl, 2);
    // TODO check for overflow
    hk->num = libafl_qemu_hooks_num++;
    hk->next = libafl_qemu_instruction_hooks[idx];
    libafl_qemu_instruction_hooks[idx] = hk;
    return hk->num;
}

size_t libafl_qemu_remove_instruction_hooks_at(target_ulong addr,
                                               int invalidate)
{
    CPUState* cpu;
    size_t r = 0;

    size_t idx = LIBAFL_TABLES_HASH(addr);
    struct libafl_instruction_hook** hk = &libafl_qemu_instruction_hooks[idx];
    while (*hk) {
        if ((*hk)->addr == addr) {
            if (invalidate) {
                CPU_FOREACH(cpu) { libafl_breakpoint_invalidate(cpu, addr); }
            }

            void* tmp = *hk;
            *hk = (*hk)->next;
            free(tmp);
            r++;
        } else {
            hk = &(*hk)->next;
        }
    }
    return r;
}

int libafl_qemu_remove_instruction_hook(size_t num, int invalidate)
{
    CPUState* cpu;
    size_t idx;

    for (idx = 0; idx < LIBAFL_TABLES_SIZE; ++idx) {
        struct libafl_instruction_hook** hk =
            &libafl_qemu_instruction_hooks[idx];
        while (*hk) {
            if ((*hk)->num == num) {
                if (invalidate) {
                    CPU_FOREACH(cpu)
                    {
                        libafl_breakpoint_invalidate(cpu, (*hk)->addr);
                    }
                }

                void* tmp = *hk;
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

struct libafl_instruction_hook*
libafl_search_instruction_hook(target_ulong addr)
{
    size_t idx = LIBAFL_TABLES_HASH(addr);

    struct libafl_instruction_hook* hk = libafl_qemu_instruction_hooks[idx];
    while (hk) {
        if (hk->addr == addr) {
            return hk;
        }
        hk = hk->next;
    }

    return NULL;
}

void libafl_qemu_hook_instruction_run(vaddr pc_next)
{
    struct libafl_instruction_hook* hk =
        libafl_search_instruction_hook(pc_next);
    if (hk) {
        TCGv_i64 tmp0 = tcg_constant_i64(hk->data);
#if TARGET_LONG_BITS == 32
        TCGv_i32 tmp1 = tcg_constant_i32(pc_next);
        TCGTemp* tmp2[2] = {tcgv_i64_temp(tmp0), tcgv_i32_temp(tmp1)};
#else
        TCGv_i64 tmp1 = tcg_constant_i64(pc_next);
        TCGTemp* tmp2[2] = {tcgv_i64_temp(tmp0), tcgv_i64_temp(tmp1)};
#endif
        tcg_gen_callN(hk->helper_info.func, &hk->helper_info, NULL, tmp2);
    }
}
