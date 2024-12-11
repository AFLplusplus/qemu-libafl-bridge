#include "qemu/osdep.h"
#include "sysemu/sysemu.h"

#include "libafl/system.h"

int libafl_qemu_toggle_hw_breakpoint(vaddr addr, bool set);

void libafl_qemu_init(int argc, char** argv) { qemu_init(argc, argv); }

int libafl_qemu_set_hw_breakpoint(vaddr addr)
{
    return libafl_qemu_toggle_hw_breakpoint(addr, true);
}

int libafl_qemu_remove_hw_breakpoint(vaddr addr)
{
    return libafl_qemu_toggle_hw_breakpoint(addr, false);
}

int libafl_qemu_toggle_hw_breakpoint(vaddr addr, bool set) {
    const int type = GDB_BREAKPOINT_HW;
    const vaddr len = 1;
    const AccelOpsClass *ops = cpus_get_accel();

    CPUState* cs;
//    int i = 0;
    int ret = 0;
//    vaddr old_addr, new_addr;

    if (!ops->insert_breakpoint) {
        return -ENOSYS;
    }

    // let's add/remove it on every CPU
    CPU_FOREACH(cs) {
        if (set) {
            ret = ops->insert_breakpoint(cs, type, addr, len);
        } else {
            ret = ops->remove_breakpoint(cs, type, addr, len);
        }
        if (ret != 0) {
            return ret;
        }
    }

//    // update libafl hw breakpoint state libafl_qemu_hw_breakpoints
//    if (set) {
//		old_addr = 0;
//        new_addr = addr;
//    } else {
//    	old_addr = addr;
//        new_addr = 0;
//    }
//    while (libafl_qemu_hw_breakpoints[i] != old_addr && i < 4) {
//        ++i;
//    }
//    if (i==4) {
//    	return -EINVAL;
//    }
//    libafl_qemu_hw_breakpoints[i] = new_addr;

    return 0;
}
