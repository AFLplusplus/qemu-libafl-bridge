#ifndef HW_ARM_ARMV7M_SOC_H
#define HW_ARM_ARMV7M_SOC_H

#include "hw/sysbus.h"
#include "hw/arm/armv7m.h"
#include "qom/object.h"

#define TYPE_ARMV7M_SOC "armv7m-soc"
OBJECT_DECLARE_SIMPLE_TYPE(ARMV7MSoCState, ARMV7M_SOC)

#define FLASH_BASE_ADDRESS 0x08000000
#define FLASH_SIZE (1024 * 1024)
#define SRAM_BASE_ADDRESS 0x20000000
#define SRAM_SIZE (128 * 1024)

/* ARMv7-M SoC state */
struct ARMV7MSoCState {
    SysBusDevice parent_obj;
    
    ARMv7MState armv7m;      /* ARMv7-M subsystem */
    
    /* Memory regions */
    MemoryRegion flash;      /* Flash memory */
    MemoryRegion sram;       /* SRAM memory */
    MemoryRegion flash_alias;
    
    /* Clocks */
    Clock *sysclk;          /* System clock input */
    Clock *refclk;          /* Reference clock input */
    
};

#endif