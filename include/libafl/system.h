#pragma once

#include "hw/core/cpu.h"

int libafl_qemu_set_hw_breakpoint(vaddr addr);
int libafl_qemu_remove_hw_breakpoint(vaddr addr);

void libafl_qemu_init(int argc, char** argv);
int libafl_qemu_run(void);

size_t libafl_target_page_size(void);
int libafl_target_page_mask(void);
int libafl_target_page_offset_mask(void);
