#include "qemu/osdep.h"

#ifdef CONFIG_USER_ONLY
#include "qemu.h"
#include "user-internals.h"
#endif

#include "exec/gdbstub.h"
#include "exec/cpu-defs.h"
#include "exec/tb-flush.h"
#include "exec/exec-all.h"
#include "hw/core/sysemu-cpu-ops.h"

#include "libafl/cpu.h"

#include "libafl/exit.h"
#include "libafl/hook.h"

int gdb_write_register(CPUState* cpu, uint8_t* mem_buf, int reg);

static __thread GByteArray* libafl_qemu_mem_buf = NULL;
static __thread int num_regs = 0;

#ifdef CONFIG_USER_ONLY
static __thread CPUArchState* libafl_qemu_env;
#endif

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

hwaddr libafl_qemu_current_paging_id(CPUState* cpu)
{
    CPUClass* cc = CPU_GET_CLASS(cpu);
    if (cc->sysemu_ops && cc->sysemu_ops->get_paging_id) {
        return cc->sysemu_ops->get_paging_id(cpu);
    } else {
        return 0;
    }
}

void libafl_breakpoint_invalidate(CPUState* cpu, target_ulong pc)
{
    // TODO invalidate only the virtual pages related to the TB
    tb_flush(cpu);
}
#else
void libafl_breakpoint_invalidate(CPUState* cpu, target_ulong pc)
{
    mmap_lock();
    tb_invalidate_phys_range(pc, pc + 1);
    mmap_unlock();
}
#endif

target_ulong libafl_page_from_addr(target_ulong addr)
{
    return addr & TARGET_PAGE_MASK;
}

CPUState* libafl_qemu_get_cpu(int cpu_index)
{
    CPUState* cpu;
    CPU_FOREACH(cpu)
    {
        if (cpu->cpu_index == cpu_index)
            return cpu;
    }
    return NULL;
}

int libafl_qemu_num_cpus(void)
{
    CPUState* cpu;
    int num = 0;
    CPU_FOREACH(cpu) { num++; }
    return num;
}

CPUState* libafl_qemu_current_cpu(void)
{
#ifndef CONFIG_USER_ONLY
    if (current_cpu == NULL) {
        return libafl_last_exit_cpu();
    }
#endif
    return current_cpu;
}

int libafl_qemu_cpu_index(CPUState* cpu)
{
    if (cpu)
        return cpu->cpu_index;
    return -1;
}

int libafl_qemu_write_reg(CPUState* cpu, int reg, uint8_t* val)
{
    return gdb_write_register(cpu, val, reg);
}

int libafl_qemu_read_reg(CPUState* cpu, int reg, uint8_t* val)
{
    int len;

    if (libafl_qemu_mem_buf == NULL) {
        libafl_qemu_mem_buf = g_byte_array_sized_new(64);
    }

    g_byte_array_set_size(libafl_qemu_mem_buf, 0);

    len = gdb_read_register(cpu, libafl_qemu_mem_buf, reg);

    if (len > 0) {
        memcpy(val, libafl_qemu_mem_buf->data, len);
    }

    return len;
}

int libafl_qemu_num_regs(CPUState* cpu)
{
    if (!num_regs) {
        CPUClass* cc = CPU_GET_CLASS(cpu);

        if (cc->gdb_num_core_regs) {
            num_regs = cc->gdb_num_core_regs;
        } else {
            const GDBFeature *feature = gdb_find_static_feature(cc->gdb_core_xml_file);

            g_assert(feature);
            g_assert(feature->num_regs > 0);

            num_regs = feature->num_regs;
        }
    }

    return num_regs;
}

void libafl_flush_jit(void)
{
    CPUState* cpu;
    CPU_FOREACH(cpu) { tb_flush(cpu); }
}

#ifdef CONFIG_USER_ONLY
__attribute__((weak)) int libafl_qemu_main(void)
{
    libafl_qemu_run();
    return 0;
}

int libafl_qemu_run(void)
{
    cpu_loop(libafl_qemu_env);
    return 1;
}

void libafl_set_qemu_env(CPUArchState* env) { libafl_qemu_env = env; }
#endif
