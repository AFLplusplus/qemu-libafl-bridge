#pragma once
#include "qemu/osdep.h"
#include "qom/object.h"
#include "device-save.h"
#include "sysemu/sysemu.h"
#include "../syx-misc.h"

/**
 * SYX Snapshot parameters
 */
typedef struct syx_snapshot_init_params_s {
    uint64_t page_size;
} syx_snapshot_init_params_t;

/**
 * Saved ramblock
 */
typedef struct syx_snapshot_ramblock_s {
    uint8_t* ram; // RAM block
    uint64_t used_length; // Length of the ram block
    char idstr[256]; // Unique string identifier for the ramblock
} syx_snapshot_ramblock_t;

/**
 * A root snapshot representation. 
 */
typedef struct syx_snapshot_root_s {
    syx_snapshot_ramblock_t* ram_blocks;
    uint64_t nb_ram_blocks;

    device_save_state_t* dss;
} syx_snapshot_root_t;

/**
 * A snapshot's dirty list. It only stores dirty addresses
 * (without data). It is the developer's responsibility to
 * to effectively save dirty pages when it is necessary.
 */
typedef struct syx_snapshot_dirty_list_s {
    // Dirty pages since the last snapshot
    // Only physical addresses are stored at this point
    // Better if a few addresses are marked
    hwaddr* dirty_addr;
    uint64_t length;
    uint64_t capacity;
} syx_snapshot_dirty_list_t;

/**
 * A list of dirty pages with their old data.
 */
typedef struct syx_snapshot_dirty_page_s {
    hwaddr addr;
    uint8_t* data;
} syx_snapshot_dirty_page_t;

typedef struct syx_snapshot_dirty_page_list_s {
    syx_snapshot_dirty_page_t* dirty_pages;
    uint64_t length;
} syx_snapshot_dirty_page_list_t;

/**
 * A snapshot increment. It is used to quickly
 * save a VM state.
 */
typedef struct syx_snapshot_increment_s {
    // Back to root snapshot if NULL
    struct syx_snapshot_increment_s* parent;

    device_save_state_t* dss;

    syx_snapshot_dirty_page_list_t dirty_page_list;
} syx_snapshot_increment_t;

/**
 * A snapshot. It is the main object used in this API to
 * handle snapshoting.
 */
typedef struct syx_snapshot_s {
    syx_snapshot_root_t root_snapshot;
    syx_snapshot_increment_t* last_incremental_snapshot;

    syx_snapshot_dirty_list_t dirty_list;
} syx_snapshot_t;

typedef struct syx_snapshot_tracker_s {
    syx_snapshot_t** tracked_snapshots;
    uint64_t length;
    uint64_t capacity;
} syx_snapshot_tracker_t;

typedef struct syx_snapshot_state_s {
    bool is_enabled;

    uint64_t page_size;
    uint64_t page_mask;
 
    // Actively tracked snapshots. Their dirty lists will
    // be updated at each dirty access
    syx_snapshot_tracker_t tracked_snapshots;
} syx_snapshot_state_t;

//
// Namespace API's functions
//

//void syx_snapshot_init(void* opaque);
void syx_snapshot_init(void);
uint64_t syx_snapshot_handler(CPUState* cpu, uint32_t cmd, target_ulong target_opaque);


//
// Snapshot API
//

syx_snapshot_t* syx_snapshot_create(bool track);
void syx_snapshot_free(syx_snapshot_t* snapshot);
// void syx_snapshot_load(syx_snapshot_t* snapshot);


//
// Root snapshot API
//

syx_snapshot_root_t syx_snapshot_root_create(void);
void syx_snapshot_root_restore(syx_snapshot_t* snapshot);
void syx_snapshot_root_free(syx_snapshot_root_t* root);


//
// Snapshot tracker API
//

syx_snapshot_tracker_t syx_snapshot_tracker_init(void);
void syx_snapshot_track(syx_snapshot_tracker_t* tracker, syx_snapshot_t* snapshot);
void syx_snapshot_stop_track(syx_snapshot_tracker_t* tracker, syx_snapshot_t* snapshot);


//
// Snapshot increment API
//

void syx_snapshot_increment_push(syx_snapshot_t* snapshot, CPUState* cpu);
void syx_snapshot_increment_pop(syx_snapshot_t* snapshot);
void syx_snapshot_increment_restore_last(syx_snapshot_t* snapshot);
syx_snapshot_increment_t* syx_snapshot_increment_free(syx_snapshot_increment_t* increment);


//
// Misc functions
//

bool syx_snapshot_is_enabled(void);


//
// Dirty list API
//

syx_snapshot_dirty_list_t syx_snapshot_dirty_list_create(void);
void syx_snapshot_dirty_list_free(syx_snapshot_dirty_list_t* dirty_list);
syx_snapshot_dirty_page_list_t syx_snapshot_dirty_list_to_dirty_page_list(syx_snapshot_dirty_list_t* dirty_list);
void syx_snapshot_dirty_list_flush(syx_snapshot_dirty_list_t* dirty_list);

void syx_snapshot_dirty_list_add_hostaddr(void* host_addr);

/**
 * @brief Add a dirty physical address to the list
 * 
 * @param paddr The physical address to add
 */
void syx_snapshot_dirty_list_add(hwaddr paddr);

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
