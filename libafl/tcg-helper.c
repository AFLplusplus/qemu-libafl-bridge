#include "qemu/osdep.h"
#include "qemu/host-utils.h"
#include "exec/cpu-common.h"
#include "exec/helper-proto-common.h"

#include "libafl/exit.h"

#define HELPER_H  "libafl/tcg-helper.h"
#include "exec/helper-info.c.inc"
#undef  HELPER_H

void HELPER(libafl_qemu_handle_breakpoint)(CPUArchState *env, uint64_t pc)
{
    CPUState* cpu = env_cpu(env);
    libafl_exit_request_breakpoint(cpu, (target_ulong) pc);
}

void HELPER(libafl_qemu_handle_custom_insn)(CPUArchState *env, uint64_t pc, uint32_t kind)
{
    CPUState* cpu = env_cpu(env);
    libafl_exit_request_custom_insn(cpu, (target_ulong) pc, (enum libafl_custom_insn_kind) kind);
}
