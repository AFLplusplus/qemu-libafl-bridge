#pragma once

#include "qemu/osdep.h"

#ifndef CONFIG_USER_ONLY
#include "exec/memory.h"
#include "qemu/rcu.h"
#include "cpu.h"
#endif

uintptr_t libafl_qemu_host_page_size(void);

#ifndef CONFIG_USER_ONLY
uint8_t* libafl_paddr2host(CPUState* cpu, hwaddr addr, bool is_write);
#endif
