#pragma once

#include "qemu/osdep.h"

void libafl_save_qemu_snapshot(char* name, bool sync);
void libafl_load_qemu_snapshot(char* name, bool sync);
