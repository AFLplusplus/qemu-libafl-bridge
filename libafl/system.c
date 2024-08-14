#include "qemu/osdep.h"
#include "sysemu/sysemu.h"

#include "libafl/system.h"

void libafl_qemu_init(int argc, char** argv) { qemu_init(argc, argv); }
