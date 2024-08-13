#pragma once

#include "qemu/osdep.h"

#define DEVICE_SAVE_KIND_FULL 0

typedef struct DeviceSaveState {
    uint8_t kind;
    uint8_t* save_buffer;
    size_t save_buffer_size;
} DeviceSaveState;

// Type of device snapshot
typedef enum DeviceSnapshotKind {
    DEVICE_SNAPSHOT_ALL,
    DEVICE_SNAPSHOT_ALLOWLIST,
    DEVICE_SNAPSHOT_DENYLIST
} DeviceSnapshotKind;

DeviceSaveState* device_save_all(void);
DeviceSaveState* device_save_kind(DeviceSnapshotKind kind, char** names);

void device_restore_all(DeviceSaveState* device_save_state);
void device_free_all(DeviceSaveState* dss);

char** device_list_all(void);
