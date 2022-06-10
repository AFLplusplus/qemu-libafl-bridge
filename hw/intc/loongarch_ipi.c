/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * LoongArch ipi interrupt support
 *
 * Copyright (C) 2021 Loongson Technology Corporation Limited
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/intc/loongarch_ipi.h"
#include "hw/irq.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "exec/address-spaces.h"
#include "hw/loongarch/virt.h"
#include "migration/vmstate.h"
#include "target/loongarch/internals.h"
#include "trace.h"

static uint64_t loongarch_ipi_readl(void *opaque, hwaddr addr, unsigned size)
{
    IPICore *s = opaque;
    uint64_t ret = 0;
    int index = 0;

    addr &= 0xff;
    switch (addr) {
    case CORE_STATUS_OFF:
        ret = s->status;
        break;
    case CORE_EN_OFF:
        ret = s->en;
        break;
    case CORE_SET_OFF:
        ret = 0;
        break;
    case CORE_CLEAR_OFF:
        ret = 0;
        break;
    case CORE_BUF_20 ... CORE_BUF_38 + 4:
        index = (addr - CORE_BUF_20) >> 2;
        ret = s->buf[index];
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "invalid read: %x", (uint32_t)addr);
        break;
    }

    trace_loongarch_ipi_read(size, (uint64_t)addr, ret);
    return ret;
}

static int get_ipi_data(target_ulong val)
{
    int i, mask, data;

    data = val >> 32;
    mask = (val >> 27) & 0xf;

    for (i = 0; i < 4; i++) {
        if ((mask >> i) & 1) {
            data &= ~(0xff << (i * 8));
        }
    }
    return data;
}

static void ipi_send(uint64_t val)
{
    int cpuid, data;
    CPULoongArchState *env;

    cpuid = (val >> 16) & 0x3ff;
    /* IPI status vector */
    data = 1 << (val & 0x1f);
    qemu_mutex_lock_iothread();
    CPUState *cs = qemu_get_cpu(cpuid);
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    env = &cpu->env;
    loongarch_cpu_set_irq(cpu, IRQ_IPI, 1);
    qemu_mutex_unlock_iothread();
    address_space_stl(&env->address_space_iocsr, 0x1008,
                      data, MEMTXATTRS_UNSPECIFIED, NULL);

}

static void mail_send(uint64_t val)
{
    int cpuid, data;
    hwaddr addr;
    CPULoongArchState *env;

    cpuid = (val >> 16) & 0x3ff;
    addr = 0x1020 + (val & 0x1c);
    CPUState *cs = qemu_get_cpu(cpuid);
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    env = &cpu->env;
    data = get_ipi_data(val);
    address_space_stl(&env->address_space_iocsr, addr,
                      data, MEMTXATTRS_UNSPECIFIED, NULL);
}

static void any_send(uint64_t val)
{
    int cpuid, data;
    hwaddr addr;
    CPULoongArchState *env;

    cpuid = (val >> 16) & 0x3ff;
    addr = val & 0xffff;
    CPUState *cs = qemu_get_cpu(cpuid);
    LoongArchCPU *cpu = LOONGARCH_CPU(cs);
    env = &cpu->env;
    data = get_ipi_data(val);
    address_space_stl(&env->address_space_iocsr, addr,
                      data, MEMTXATTRS_UNSPECIFIED, NULL);
}

static void loongarch_ipi_writel(void *opaque, hwaddr addr, uint64_t val,
                                 unsigned size)
{
    IPICore *s = opaque;
    int index = 0;

    addr &= 0xff;
    trace_loongarch_ipi_write(size, (uint64_t)addr, val);
    switch (addr) {
    case CORE_STATUS_OFF:
        qemu_log_mask(LOG_GUEST_ERROR, "can not be written");
        break;
    case CORE_EN_OFF:
        s->en = val;
        break;
    case CORE_SET_OFF:
        s->status |= val;
        if (s->status != 0 && (s->status & s->en) != 0) {
            qemu_irq_raise(s->irq);
        }
        break;
    case CORE_CLEAR_OFF:
        s->status &= ~val;
        if (s->status == 0 && s->en != 0) {
            qemu_irq_lower(s->irq);
        }
        break;
    case CORE_BUF_20 ... CORE_BUF_38 + 4:
        index = (addr - CORE_BUF_20) >> 2;
        s->buf[index] = val;
        break;
    case IOCSR_IPI_SEND:
        ipi_send(val);
        break;
    case IOCSR_MAIL_SEND:
        mail_send(val);
        break;
    case IOCSR_ANY_SEND:
        any_send(val);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "invalid write: %x", (uint32_t)addr);
        break;
    }
}

static const MemoryRegionOps loongarch_ipi_ops = {
    .read = loongarch_ipi_readl,
    .write = loongarch_ipi_writel,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 8,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void loongarch_ipi_init(Object *obj)
{
    int cpu;
    LoongArchMachineState *lams;
    LoongArchIPI *s = LOONGARCH_IPI(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    Object *machine = qdev_get_machine();
    ObjectClass *mc = object_get_class(machine);
    /* 'lams' should be initialized */
    if (!strcmp(MACHINE_CLASS(mc)->name, "none")) {
        return;
    }
    lams = LOONGARCH_MACHINE(machine);
    for (cpu = 0; cpu < MAX_IPI_CORE_NUM; cpu++) {
        memory_region_init_io(&s->ipi_iocsr_mem[cpu], obj, &loongarch_ipi_ops,
                            &lams->ipi_core[cpu], "loongarch_ipi_iocsr", 0x100);
        sysbus_init_mmio(sbd, &s->ipi_iocsr_mem[cpu]);
        qdev_init_gpio_out(DEVICE(obj), &lams->ipi_core[cpu].irq, 1);
    }
}

static const VMStateDescription vmstate_ipi_core = {
    .name = "ipi-single",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(status, IPICore),
        VMSTATE_UINT32(en, IPICore),
        VMSTATE_UINT32(set, IPICore),
        VMSTATE_UINT32(clear, IPICore),
        VMSTATE_UINT32_ARRAY(buf, IPICore, MAX_IPI_MBX_NUM * 2),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_loongarch_ipi = {
    .name = TYPE_LOONGARCH_IPI,
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(ipi_core, LoongArchMachineState,
                             MAX_IPI_CORE_NUM, 0,
                             vmstate_ipi_core, IPICore),
        VMSTATE_END_OF_LIST()
    }
};

static void loongarch_ipi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->vmsd = &vmstate_loongarch_ipi;
}

static const TypeInfo loongarch_ipi_info = {
    .name          = TYPE_LOONGARCH_IPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(LoongArchIPI),
    .instance_init = loongarch_ipi_init,
    .class_init    = loongarch_ipi_class_init,
};

static void loongarch_ipi_register_types(void)
{
    type_register_static(&loongarch_ipi_info);
}

type_init(loongarch_ipi_register_types)
