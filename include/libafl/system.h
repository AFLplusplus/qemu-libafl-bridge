#pragma once

#include "hw/core/cpu.h"
#include "gdbstub/enums.h"
#include "sysemu/accel-ops.h"
#include "sysemu/cpus.h"
#include "sysemu/block-backend.h"

int libafl_qemu_set_hw_breakpoint(vaddr addr);
int libafl_qemu_remove_hw_breakpoint(vaddr addr);

void libafl_qemu_init(int argc, char** argv);

/** Write to a block device with aio API
 * The same way the guest would, 
 * thus this writes to the Syx COW cache (if it is initialized)
 */
void libafl_blk_write(BlockBackend *blk, void *buf, int64_t offset, int64_t sz);