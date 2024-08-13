#include "qemu/osdep.h"
#include "libafl/utils.h"

uintptr_t libafl_qemu_host_page_size(void)
{
    return qemu_real_host_page_size();
}
