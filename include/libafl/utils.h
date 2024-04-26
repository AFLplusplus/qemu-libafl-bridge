#pragma once

#include "qemu/osdep.h"

#ifndef CONFIG_USER_ONLY
uint8_t* libafl_paddr2host(CPUState* cpu, hwaddr addr, bool is_write);
#endif
