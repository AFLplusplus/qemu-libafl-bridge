/*
 * SYX Snapshot
 *
 * A speed-oriented snapshot mechanism.
 *
 * TODO: complete documentation.
 */

#pragma once
#include "qemu/osdep.h"
#include "qom/object.h"
#include "sysemu/sysemu.h"

#include "device-save.h"
#include "../syx-misc.h"

/**
 * Saved ramblock
 */
typedef struct SyxSnapshotRAMBlock {
    uint8_t* ram; // RAM block
    uint64_t used_length; // Length of the ram block
} SyxSnapshotRAMBlock;

/**
 * A root snapshot representation. 
 */
typedef struct SyxSnapshotRoot {
    GHashTable* rbs_snapshot; // hash map: H(rb) -> SyxSnapshotRAMBlock
    DeviceSaveState* dss;
} SyxSnapshotRoot;

/**
 * A list of dirty pages with their old data.
 */
typedef struct SyxSnapshotDirtyPage {
    ram_addr_t offset_within_rb;
    uint8_t* data;
} SyxSnapshotDirtyPage;

typedef struct SyxSnapshotDirtyPageList {
    SyxSnapshotDirtyPage* dirty_pages;
    uint64_t length;
} SyxSnapshotDirtyPageList;

/**
 * A snapshot increment. It is used to quickly
 * save a VM state.
 */
typedef struct SyxSnapshotIncrement {
    // Back to root snapshot if NULL
    struct SyxSnapshotIncrement* parent;

    DeviceSaveState* dss;

    GHashTable* rbs_dirty_pages; // hash map: H(rb) -> SyxSnapshotDirtyPageList
} SyxSnapshotIncrement;

/**
 * A snapshot. It is the main object used in this API to
 * handle snapshotting.
 */
typedef struct SyxSnapshot {
    SyxSnapshotRoot root_snapshot;
    SyxSnapshotIncrement* last_incremental_snapshot;

    GHashTable* rbs_dirty_list; // hash map: H(rb) -> GHashTable(offset_within_ramblock). Filled lazily.
} SyxSnapshot;

typedef struct SyxSnapshotTracker {
    SyxSnapshot** tracked_snapshots;
    uint64_t length;
    uint64_t capacity;
} SyxSnapshotTracker;

typedef struct SyxSnapshotState {
    bool is_enabled;

    uint64_t page_size;
    uint64_t page_mask;
 
    // Actively tracked snapshots. Their dirty lists will
    // be updated at each dirty access
    SyxSnapshotTracker tracked_snapshots;
} SyxSnapshotState;


void syx_snapshot_init(void);


//
// Snapshot API
//

SyxSnapshot* syx_snapshot_new(bool track, DeviceSnapshotKind kind, char** devices);
void syx_snapshot_free(SyxSnapshot* snapshot);
void syx_snapshot_root_restore(SyxSnapshot* snapshot);
uint64_t syx_snapshot_check_memory_consistency(SyxSnapshot* snapshot);

// Push the current RAM state and saves it
void syx_snapshot_increment_push(SyxSnapshot* snapshot, DeviceSnapshotKind kind, char** devices);

// Restores the last push. Restores the root snapshot if no incremental snapshot is present.
void syx_snapshot_increment_pop(SyxSnapshot* snapshot);

void syx_snapshot_increment_restore_last(SyxSnapshot* snapshot);


//
// Snapshot tracker API
//

SyxSnapshotTracker syx_snapshot_tracker_init(void);
void syx_snapshot_track(SyxSnapshotTracker* tracker, SyxSnapshot* snapshot);
void syx_snapshot_stop_track(SyxSnapshotTracker* tracker, SyxSnapshot* snapshot);


//
// Misc functions
//

bool syx_snapshot_is_enabled(void);


//
// Dirty list API
//

void syx_snapshot_dirty_list_add_hostaddr(void* host_addr);
void syx_snapshot_dirty_list_add_hostaddr_range(void* host_addr, uint64_t len);

/**
 * @brief Add a dirty physical address to the list
 * 
 * @param paddr The physical address to add
 */
void syx_snapshot_dirty_list_add_paddr(hwaddr paddr);

/**
 * @brief Same as syx_snapshot_dirty_list_add. The difference
 * being that it has been specially compiled for full context
 * saving so that it can be called from anywhere, even in
 * extreme environments where SystemV ABI is not respected.
 * It was created with tcg-target.inc.c environment in
 * mind.
 * 
 * @param dummy A dummy argument. it is to comply with
 *              tcg-target.inc.c specific environment.
 * @param host_addr The host address where the dirty page is located.
 */
void syx_snapshot_dirty_list_add_tcg_target(uint64_t dummy, void* host_addr);
