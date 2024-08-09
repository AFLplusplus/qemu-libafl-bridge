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
#include "syx-cow-cache.h"

#include "libafl/syx-misc.h"

#define SYX_SNAPSHOT_COW_CACHE_DEFAULT_CHUNK_SIZE 64
#define SYX_SNAPSHOT_COW_CACHE_DEFAULT_MAX_BLOCKS (1024 * 1024)

typedef struct SyxSnapshotRoot SyxSnapshotRoot;
typedef struct SyxSnapshotIncrement SyxSnapshotIncrement;

/**
 * A snapshot. It is the main object used in this API to
 * handle snapshotting.
 */
typedef struct SyxSnapshot {
    SyxSnapshotRoot* root_snapshot;
    SyxSnapshotIncrement* last_incremental_snapshot;

    SyxCowCache* bdrvs_cow_cache;
    GHashTable*
        rbs_dirty_list; // hash map: H(rb) ->
                        // GHashTable(offset_within_ramblock). Filled lazily.
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

    // In use iif syx is initialized with cached_bdrvs flag on.
    // It is not updated anymore when an active bdrv cache snapshto is set.
    SyxCowCache* before_fuzz_cache;
    // snapshot used to restore bdrv cache if enabled.
    SyxSnapshot* active_bdrv_cache_snapshot;

    // Root
} SyxSnapshotState;

typedef struct SyxSnapshotCheckResult {
    uint64_t nb_inconsistencies;
} SyxSnapshotCheckResult;

void syx_snapshot_init(bool cached_bdrvs);

//
// Snapshot API
//

SyxSnapshot* syx_snapshot_new(bool track, bool is_active_bdrv_cache,
                              DeviceSnapshotKind kind, char** devices);

void syx_snapshot_free(SyxSnapshot* snapshot);

void syx_snapshot_root_restore(SyxSnapshot* snapshot);

SyxSnapshotCheckResult syx_snapshot_check(SyxSnapshot* ref_snapshot);

// Push the current RAM state and saves it
void syx_snapshot_increment_push(SyxSnapshot* snapshot, DeviceSnapshotKind kind,
                                 char** devices);

// Restores the last push. Restores the root snapshot if no incremental snapshot
// is present.
void syx_snapshot_increment_pop(SyxSnapshot* snapshot);

void syx_snapshot_increment_restore_last(SyxSnapshot* snapshot);

//
// Snapshot tracker API
//

SyxSnapshotTracker syx_snapshot_tracker_init(void);

void syx_snapshot_track(SyxSnapshotTracker* tracker, SyxSnapshot* snapshot);

void syx_snapshot_stop_track(SyxSnapshotTracker* tracker,
                             SyxSnapshot* snapshot);

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

bool syx_snapshot_cow_cache_read_entry(BlockBackend* blk, int64_t offset,
                                       int64_t bytes, QEMUIOVector* qiov,
                                       size_t qiov_offset,
                                       BdrvRequestFlags flags);

bool syx_snapshot_cow_cache_write_entry(BlockBackend* blk, int64_t offset,
                                        int64_t bytes, QEMUIOVector* qiov,
                                        size_t qiov_offset,
                                        BdrvRequestFlags flags);
