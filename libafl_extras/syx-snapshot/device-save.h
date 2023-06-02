#pragma once

#include "qemu/osdep.h"

#define DEVICE_SAVE_KIND_FULL   0

typedef struct device_save_state_s {
    uint8_t kind;
    uint8_t* save_buffer;
    size_t save_buffer_size;
} device_save_state_t;

device_save_state_t* device_save_all(void);
void device_restore_all(device_save_state_t* device_save_state);
void device_free_all(device_save_state_t* dss);
