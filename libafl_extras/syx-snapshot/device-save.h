#pragma once

#include "qemu/osdep.h"
#include "io/channel-buffer.h"

#define DEVICE_SAVE_KIND_FULL   0

typedef struct device_save_state_s {
    uint8_t kind;
    QIOChannelBuffer* save_buffer;
    QEMUFile* save_file;
} device_save_state_t;

device_save_state_t* device_save_all(void);
void device_restore_all(device_save_state_t* device_save_state);
void device_free_all(device_save_state_t* dss);
