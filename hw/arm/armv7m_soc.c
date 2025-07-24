#include "qemu/osdep.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "hw/arm/armv7m_soc.h"
#include "hw/qdev-clock.h"
#include "hw/qdev-properties.h"
#include "hw/misc/unimp.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "system/system.h"
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
}

static void armv7m_soc_realize(DeviceState *dev_soc, Error **errp)
{
    ARMV7MSoCState *s = ARMV7M_SOC(dev_soc);
    MemoryRegion *system_memory = get_system_memory();
    Error *err = NULL;

    load_config_from_env();

    if (!clock_has_source(s->sysclk)) {
        error_setg(errp, "sysclk clock must be wired up by the board code");
        return;
    }
    
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
    if (err) {
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

    object_property_set_link(OBJECT(&s->armv7m), "memory",
                             OBJECT(system_memory), &error_abort);
    
    // 启用ARMv7M子系统
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->armv7m), errp)) {
        return;
    }

    // 所有外设都用未实现设备占位
    create_unimplemented_device("RCC",         0x40023800, 0x400);
    create_unimplemented_device("SYSCFG",      0x40013800, 0x400);
    create_unimplemented_device("USART1",      0x40011000, 0x400);
    create_unimplemented_device("USART2",      0x40004400, 0x400);
    create_unimplemented_device("USART3",      0x40004800, 0x400);
    create_unimplemented_device("UART4",       0x40004C00, 0x400);
    create_unimplemented_device("UART5",       0x40005000, 0x400);
    create_unimplemented_device("USART6",      0x40011400, 0x400);
    create_unimplemented_device("UART7",       0x40007800, 0x400);
    create_unimplemented_device("UART8",       0x40007C00, 0x400);
    create_unimplemented_device("TIMER2",      0x40000000, 0x400);
    create_unimplemented_device("TIMER3",      0x40000400, 0x400);
    create_unimplemented_device("TIMER4",      0x40000800, 0x400);
    create_unimplemented_device("TIMER5",      0x40000C00, 0x400);
    create_unimplemented_device("ADC1",        0x40012000, 0x400);
    create_unimplemented_device("ADC2",        0x40012100, 0x400);
    create_unimplemented_device("ADC3",        0x40012200, 0x400);
    create_unimplemented_device("ADC4",        0x40012300, 0x400);
    create_unimplemented_device("ADC5",        0x40012400, 0x400);
    create_unimplemented_device("ADC6",        0x40012500, 0x400);
    create_unimplemented_device("SPI1",        0x40013000, 0x400);
    create_unimplemented_device("SPI2",        0x40003800, 0x400);
    create_unimplemented_device("SPI3",        0x40003C00, 0x400);
    create_unimplemented_device("SPI4",        0x40013400, 0x400);
    create_unimplemented_device("SPI5",        0x40015000, 0x400);
    create_unimplemented_device("SPI6",        0x40015400, 0x400);
    create_unimplemented_device("EXTI",        0x40013C00, 0x400);
    create_unimplemented_device("timer[7]",    0x40001400, 0x400);
    create_unimplemented_device("timer[12]",   0x40001800, 0x400);
    create_unimplemented_device("timer[6]",    0x40001000, 0x400);
    create_unimplemented_device("timer[13]",   0x40001C00, 0x400);
    create_unimplemented_device("timer[14]",   0x40002000, 0x400);
    create_unimplemented_device("RTC and BKP", 0x40002800, 0x400);
    create_unimplemented_device("WWDG",        0x40002C00, 0x400);
    create_unimplemented_device("IWDG",        0x40003000, 0x400);
    create_unimplemented_device("I2S2ext",     0x40003000, 0x400);
    create_unimplemented_device("I2S3ext",     0x40004000, 0x400);
    create_unimplemented_device("I2C1",        0x40005400, 0x400);
    create_unimplemented_device("I2C2",        0x40005800, 0x400);
    create_unimplemented_device("I2C3",        0x40005C00, 0x400);
    create_unimplemented_device("CAN1",        0x40006400, 0x400);
    create_unimplemented_device("CAN2",        0x40006800, 0x400);
    create_unimplemented_device("PWR",         0x40007000, 0x400);
    create_unimplemented_device("DAC",         0x40007400, 0x400);
    create_unimplemented_device("timer[1]",    0x40010000, 0x400);
    create_unimplemented_device("timer[8]",    0x40010400, 0x400);
    create_unimplemented_device("SDIO",        0x40012C00, 0x400);
    create_unimplemented_device("timer[9]",    0x40014000, 0x400);
    create_unimplemented_device("timer[10]",   0x40014400, 0x400);
    create_unimplemented_device("timer[11]",   0x40014800, 0x400);
    create_unimplemented_device("GPIOA",       0x40020000, 0x400);
    create_unimplemented_device("GPIOB",       0x40020400, 0x400);
    create_unimplemented_device("GPIOC",       0x40020800, 0x400);
    create_unimplemented_device("GPIOD",       0x40020C00, 0x400);
    create_unimplemented_device("GPIOE",       0x40021000, 0x400);
    create_unimplemented_device("GPIOF",       0x40021400, 0x400);
    create_unimplemented_device("GPIOG",       0x40021800, 0x400);
    create_unimplemented_device("GPIOH",       0x40021C00, 0x400);
    create_unimplemented_device("GPIOI",       0x40022000, 0x400);
    create_unimplemented_device("CRC",         0x40023000, 0x400);
    create_unimplemented_device("Flash Int",   0x40023C00, 0x400);
    create_unimplemented_device("BKPSRAM",     0x40024000, 0x400);
    create_unimplemented_device("DMA1",        0x40026000, 0x400);
    create_unimplemented_device("DMA2",        0x40026400, 0x400);
    create_unimplemented_device("Ethernet",    0x40028000, 0x1400);
    create_unimplemented_device("USB OTG HS",  0x40040000, 0x30000);
    create_unimplemented_device("USB OTG FS",  0x50000000, 0x31000);
    create_unimplemented_device("DCMI",        0x50050000, 0x400);
    create_unimplemented_device("RNG",         0x50060800, 0x400);
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

type_init(armv7m_soc_register_types);