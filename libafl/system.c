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

int libafl_qemu_toggle_hw_breakpoint(vaddr addr, bool set)
{
    const int type = GDB_BREAKPOINT_HW;
    const vaddr len = 1;
    const AccelOpsClass* ops = cpus_get_accel();

    CPUState* cs = first_cpu;
    int ret = 0;

    if (!ops->insert_breakpoint) {
        return -ENOSYS;
    }

    // let's add/remove the breakpoint on the first CPU.
    // Both TCG and KVM propagate it to all CPUs internally.
    if (set) {
        ret = ops->insert_breakpoint(cs, type, addr, len);
    } else {
        ret = ops->remove_breakpoint(cs, type, addr, len);
    }
    if (ret != 0) {
        return ret;
    }

    return 0;
}
