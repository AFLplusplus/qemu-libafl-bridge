#pragma once

#include "qemu/osdep.h"

#ifndef CONFIG_USER_ONLY
#include "exec/memory.h"
#include "qemu/rcu.h"
#include "cpu.h"
#endif

#ifndef CONFIG_USER_ONLY
uint8_t* libafl_paddr2host(CPUState* cpu, hwaddr addr, bool is_write);
hwaddr libafl_qemu_current_paging_id(CPUState* cpu);
#endif

target_ulong libafl_page_from_addr(target_ulong addr);
CPUState* libafl_qemu_get_cpu(int cpu_index);
int libafl_qemu_num_cpus(void);
CPUState* libafl_qemu_current_cpu(void);
int libafl_qemu_cpu_index(CPUState*);
int libafl_qemu_write_reg(CPUState* cpu, int reg, uint8_t* val);
int libafl_qemu_read_reg(CPUState* cpu, int reg, uint8_t* val);
int libafl_qemu_num_regs(CPUState* cpu);
void libafl_flush_jit(void);
void libafl_breakpoint_invalidate(CPUState *cpu, target_ulong pc);

int libafl_qemu_main(void);
int libafl_qemu_run(void);
void libafl_set_qemu_env(CPUArchState* env);
