#include "libafl/hooks/tcg/backdoor.h"

struct libafl_backdoor_hook* libafl_backdoor_hooks;
size_t libafl_backdoor_hooks_num = 0;

static TCGHelperInfo libafl_exec_backdoor_hook_info = {
    .func = NULL,
    .name = "libafl_exec_backdoor_hook",
    .flags = dh_callflag(void),
    .typemask = dh_typemask(void, 0) | dh_typemask(env, 1) |
                dh_typemask(i64, 2) | dh_typemask(tl, 3)};

GEN_REMOVE_HOOK(backdoor)

size_t libafl_add_backdoor_hook(void (*exec)(uint64_t data, CPUArchState* cpu,
                                             target_ulong pc),
                                uint64_t data)
{
    struct libafl_backdoor_hook* hook =
        calloc(sizeof(struct libafl_backdoor_hook), 1);
    // hook->exec = exec;
    hook->data = data;
    hook->num = libafl_backdoor_hooks_num++;
    hook->next = libafl_backdoor_hooks;
    libafl_backdoor_hooks = hook;

    memcpy(&hook->helper_info, &libafl_exec_backdoor_hook_info,
           sizeof(TCGHelperInfo));
    hook->helper_info.func = exec;

    return hook->num;
}

void libafl_qemu_hook_backdoor_run(vaddr pc_next)
{
    struct libafl_backdoor_hook* bhk = libafl_backdoor_hooks;
    while (bhk) {
        TCGv_i64 tmp0 = tcg_constant_i64(bhk->data);
        TCGv tmp2 = tcg_constant_tl(pc_next);
        TCGTemp* args[3] = {tcgv_i64_temp(tmp0), tcgv_ptr_temp(tcg_env),
                            tcgv_tl_temp(tmp2)};

        tcg_gen_callN(&bhk->helper_info, NULL, args);

        bhk = bhk->next;
    }
}
