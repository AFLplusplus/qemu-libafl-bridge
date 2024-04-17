#pragma once

#include "qemu/osdep.h"

uint8_t* libafl_paddr2host(CPUState* cpu, hwaddr addr, bool is_write);
uint64_t libafl_guest_page_size(void);
