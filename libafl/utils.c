#include "qemu/osdep.h"
#include "libafl/utils.h"

uintptr_t libafl_qemu_host_page_size(void)
{
    return qemu_real_host_page_size();
}

#ifndef CONFIG_USER_ONLY

uint8_t* libafl_paddr2host(CPUState* cpu, hwaddr addr, bool is_write)
{
    if (addr == -1) {
        return NULL;
    }

    hwaddr xlat;
    MemoryRegion* mr;
    WITH_RCU_READ_LOCK_GUARD()
    {
        mr = address_space_translate(cpu->as, addr, &xlat, NULL, is_write,
                                     MEMTXATTRS_UNSPECIFIED);
    }

    return qemu_map_ram_ptr(mr->ram_block, xlat);
}

#endif
