#pragma once

#include "hw/core/cpu.h"

int libafl_qemu_set_hw_breakpoint(vaddr addr);
int libafl_qemu_remove_hw_breakpoint(vaddr addr);

void libafl_qemu_init(int argc, char** argv);
