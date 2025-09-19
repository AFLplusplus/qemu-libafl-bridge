#include "qemu/osdep.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "hw/arm/armv7m_soc.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties.h"
#include "hw/misc/unimp.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include <stdlib.h>
#include <string.h>

// 定义全局变量保存参数
static hwaddr flash_base = FLASH_BASE_ADDRESS;
static uint64_t flash_size = FLASH_SIZE;
static uint32_t sram_base = SRAM_BASE_ADDRESS;
static hwaddr sram_size = SRAM_SIZE;

static void load_config_from_env(void) {
    char *env;
    env = getenv("FLASH_BASE");
    if (env) {
        flash_base = strtoul(env, NULL, 0);
        printf("FLASH_BASE = 0x%lx (%lu)\n", (unsigned long)flash_base, (unsigned long)flash_base);
    }
    env = getenv("FLASH_SIZE");
    if (env) {
        flash_size = strtoul(env, NULL, 0);
        printf("FLASH_SIZE = 0x%lx (%lu)\n", (unsigned long)flash_size, (unsigned long)flash_size);
    }
    env = getenv("SRAM_BASE");
    if (env) {
        sram_base = strtoul(env, NULL, 0);
        printf("SRAM_BASE = 0x%lx (%lu)\n", (unsigned long)sram_base, (unsigned long)sram_base);
    }
    env = getenv("SRAM_SIZE");
    if (env) {
        sram_size = strtoul(env, NULL, 0);
        printf("SRAM_SIZE = 0x%lx (%lu)\n", (unsigned long)sram_size, (unsigned long)sram_size);
    }
}

static void armv7m_soc_initfn(Object *obj)
{
    ARMV7MSoCState *s = ARMV7M_SOC(obj);
    
    // 初始化ARMv7M子对象
    object_initialize_child(obj, "armv7m", &s->armv7m, TYPE_ARMV7M);
    
    // 创建系统时钟输入
    s->sysclk = qdev_init_clock_in(DEVICE(s), "sysclk", NULL, NULL, 0);
    s->refclk = qdev_init_clock_in(DEVICE(s), "refclk", NULL, NULL, 0);
}

static void armv7m_soc_realize(DeviceState *dev_soc, Error **errp)
{
    ARMV7MSoCState *s = ARMV7M_SOC(dev_soc);
    MemoryRegion *system_memory = get_system_memory();
    Error *err = NULL;

    load_config_from_env();

    if (clock_has_source(s->refclk)) {
        error_setg(errp, "refclk clock must not be wired up by the board code");
        return;
    }
    
    if (!clock_has_source(s->sysclk)) {
        error_setg(errp, "sysclk clock must be wired up by the board code");
        return;
    }

    /* The refclk always runs at frequency HCLK / 8 */
    clock_set_mul_div(s->refclk, 8, 1);
    clock_set_source(s->refclk, s->sysclk);
    
    // 初始化Flash内存
    memory_region_init_rom(&s->flash, OBJECT(dev_soc), "flash", flash_size, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    memory_region_init_alias(&s->flash_alias, OBJECT(dev_soc),
                             "flash.alias", &s->flash, 0,
                             flash_size);
    memory_region_add_subregion(system_memory, flash_base, &s->flash);
    memory_region_add_subregion(system_memory, 0, &s->flash_alias);
    
    // 初始化SRAM内存
    memory_region_init_ram(&s->sram, NULL, "sram", sram_size, &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    memory_region_add_subregion(system_memory, sram_base, &s->sram);
    
    // 初始化ARMv7M子系统
    DeviceState *armv7m_dev = DEVICE(&s->armv7m);
    
    // 设置中断数量（默认32个）
    qdev_prop_set_uint32(armv7m_dev, "num-irq", 96);
    qdev_prop_set_uint8(armv7m_dev, "num-prio-bits", 4);
    qdev_prop_set_string(armv7m_dev, "cpu-type", ARM_CPU_TYPE_NAME("cortex-m4"));
    qdev_prop_set_bit(armv7m_dev, "enable-bitband", true);
    
    // 连接时钟
    qdev_connect_clock_in(armv7m_dev, "cpuclk", s->sysclk);
    qdev_connect_clock_in(armv7m_dev, "refclk", s->refclk);

    object_property_set_link(OBJECT(&s->armv7m), "memory",
                             OBJECT(system_memory), &error_abort);
    
    // 启用ARMv7M子系统
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->armv7m), errp)) {
        return;
    }

    // 所有外设都用未实现设备占位
    create_unimplemented_device("empty",       0x40000000, 0x20000000);
}

static void armv7m_soc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = armv7m_soc_realize;

}

static const TypeInfo armv7m_soc_info = {
    .name = TYPE_ARMV7M_SOC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ARMV7MSoCState),
    .instance_init = armv7m_soc_initfn,
    .class_init = armv7m_soc_class_init,
};

static void armv7m_soc_register_types(void)
{
    type_register_static(&armv7m_soc_info);
}

type_init(armv7m_soc_register_types)