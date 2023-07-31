#pragma once

#include "qemu/osdep.h"

#define DEVICE_SAVE_KIND_FULL   0

typedef struct device_save_state_s {
    uint8_t kind;
    uint8_t* save_buffer;
    size_t save_buffer_size;
} device_save_state_t;

// Type of device snapshot
typedef enum device_snapshot_kind_e {
    DEVICE_SNAPSHOT_ALL,
    DEVICE_SNAPSHOT_ALLOWLIST,
    DEVICE_SNAPSHOT_DENYLIST
} device_snapshot_kind_t;

device_save_state_t* device_save_all(void);
device_save_state_t* device_save_kind(device_snapshot_kind_t kind, char** names);
void device_restore_all(device_save_state_t* device_save_state);
void device_free_all(device_save_state_t* dss);
