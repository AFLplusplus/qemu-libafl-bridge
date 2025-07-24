/*
 * Generic ARMv7-M Machine
 * 
 * Minimal implementation for ARM Cortex-M series simulation
 * Supports custom memory layout and firmware loading
 * 
 * Copyright (c) 2023 Your Name
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-clock.h"
#include "qemu/error-report.h"
#include "hw/arm/armv7m.h"
#include "hw/arm/armv7m_soc.h"
#include "hw/arm/boot.h"
#include "qom/object.h"
#include "qapi/visitor.h"

#define SYSCLK_FRQ 168000000ULL

static void armv7m_machine_init(MachineState *machine)
{
    DeviceState *dev;
    Clock *sysclk;
    
    /* Create fixed-frequency system clock */
    sysclk = clock_new(OBJECT(machine), "SYSCLK");
    clock_set_hz(sysclk, SYSCLK_FRQ); 
    
    /* Initialize SoC device */
    dev = qdev_new(TYPE_ARMV7M_SOC);
    object_property_add_child(OBJECT(machine), "soc", OBJECT(dev));
    
    /* Connect system clock */
    qdev_connect_clock_in(dev, "sysclk", sysclk);
    
    /* Realize SoC device (triggers initialization chain) */
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    
    // /* Load firmware to flash memory */
    // ARMV7MSoCState *soc = ARMV7M_SOC(dev);
    armv7m_load_kernel(ARMV7M_SOC(dev)->armv7m.cpu, machine->kernel_filename,
                       0, FLASH_SIZE);
}

static void armv7m_machine_class_init(MachineClass *mc)
{

    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-m4"),
        NULL
    };
    mc->desc = "Generic ARMv7-M Machine";
    mc->init = armv7m_machine_init;
    mc->valid_cpu_types = valid_cpu_types;
}

DEFINE_MACHINE("armv7m", armv7m_machine_class_init)