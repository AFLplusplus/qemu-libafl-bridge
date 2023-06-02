#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "cpu.h"
#include "qemu/main-loop.h"
#include "migration/qemu-file.h"
#include "migration/vmstate.h"
#include "migration/savevm.h"
#include "memory.h"

#include "exec/ram_addr.h"
/// Physical to host memory in system memory.
#include "exec/ramlist.h"
#include "exec/address-spaces.h"
#include "exec/exec-all.h"

#include "sysemu/block-backend.h"
#include "migration/register.h"

// #include "target/i386/cpu.h"

#include "syx-snapshot.h"
#include "channel-buffer-writeback.h"
#include "device-save.h"

#define SYX_SNAPSHOT_LIST_INIT_SIZE      4096
#define SYX_SNAPSHOT_LIST_GROW_FACTOR    2

syx_snapshot_state_t syx_snapshot_state = {0};
static MemoryRegion* mr_to_enable = NULL;

void syx_snapshot_init(void) {
    //syx_snapshot_init_params_t* params = (syx_snapshot_init_params_t*) opaque;
    //uint64_t page_size = params->page_size;
    uint64_t page_size = TARGET_PAGE_SIZE;

    syx_snapshot_state.page_size = page_size;
    syx_snapshot_state.page_mask = ((uint64_t)-1) << __builtin_ctz(page_size);

    syx_snapshot_state.tracked_snapshots = syx_snapshot_tracker_init();

    syx_snapshot_state.is_enabled = false;
}

uint64_t syx_snapshot_handler(CPUState* cpu, uint32_t cmd, target_ulong target_opaque) {
    return (uint64_t) -1;
}

syx_snapshot_t* syx_snapshot_create(bool track) {
    syx_snapshot_t* snapshot = g_new0(syx_snapshot_t, 1);

    snapshot->root_snapshot = syx_snapshot_root_create();
    snapshot->last_incremental_snapshot = NULL;
    snapshot->dirty_list = syx_snapshot_dirty_list_create();

    if (track) {
        syx_snapshot_track(&syx_snapshot_state.tracked_snapshots, snapshot);
    }

    syx_snapshot_state.is_enabled = true;

    return snapshot;
}

void syx_snapshot_free(syx_snapshot_t* snapshot) {
    syx_snapshot_increment_t* increment = snapshot->last_incremental_snapshot;

    while (increment != NULL) {
        increment = syx_snapshot_increment_free(increment);
    }

    syx_snapshot_dirty_list_free(&snapshot->dirty_list);
    syx_snapshot_root_free(&snapshot->root_snapshot);

    g_free(snapshot);
}

syx_snapshot_root_t syx_snapshot_root_create(void) {
    syx_snapshot_root_t root = {0};

    RAMBlock* block;
    uint64_t nb_blocks = 0;
    device_save_state_t* dss = device_save_all();

    RAMBLOCK_FOREACH(block) {
        nb_blocks++;
    }

    root.ram_blocks = g_new0(syx_snapshot_ramblock_t, nb_blocks);
    root.nb_ram_blocks = nb_blocks;
    root.dss = dss;

    uint64_t ram_block_idx = 0;
    RAMBLOCK_FOREACH(block) {
        syx_snapshot_ramblock_t* snapshot_ram_block = &root.ram_blocks[ram_block_idx];
        strcpy(snapshot_ram_block->idstr, block->idstr);
        snapshot_ram_block->used_length = block->used_length;

        snapshot_ram_block->ram = g_new(uint8_t, block->used_length);
        memcpy(snapshot_ram_block->ram, block->host, block->used_length);

        ram_block_idx++;
    }
    assert(ram_block_idx == nb_blocks);

    return root;
}

void syx_snapshot_root_free(syx_snapshot_root_t* root) {
    for (uint64_t i = 0; i < root->nb_ram_blocks; ++i) {
        g_free(root->ram_blocks[i].ram);
    }

    g_free(root->ram_blocks);
}

syx_snapshot_tracker_t syx_snapshot_tracker_init(void) {
    syx_snapshot_tracker_t tracker = {
        .length = 0,
        .capacity = SYX_SNAPSHOT_LIST_INIT_SIZE,
        .tracked_snapshots = g_new(syx_snapshot_t*, SYX_SNAPSHOT_LIST_INIT_SIZE)
    };

    return tracker;
}

void syx_snapshot_track(syx_snapshot_tracker_t* tracker, syx_snapshot_t* snapshot) {
    if (tracker->length == tracker->capacity) {
        tracker->capacity *= SYX_SNAPSHOT_LIST_GROW_FACTOR;
        tracker->tracked_snapshots = g_realloc(tracker->tracked_snapshots, tracker->capacity * sizeof(syx_snapshot_t*));
    }

    assert(tracker->length < tracker->capacity);

    tracker->tracked_snapshots[tracker->length] = snapshot;
    tracker->length++;
}

void syx_snapshot_stop_track(syx_snapshot_tracker_t* tracker, syx_snapshot_t* snapshot) {
    for (uint64_t i = 0; i < tracker->length; ++i) {
        if (tracker->tracked_snapshots[i] == snapshot) {
            for (uint64_t j = i + i; j < tracker->length; ++j) {
                tracker->tracked_snapshots[j-1] = tracker->tracked_snapshots[j];
            }
            tracker->length--;
            return;
        }
    }

    SYX_PRINTF("ERROR: trying to remove an untracked snapshot\n");
    abort();
}

void syx_snapshot_increment_push(syx_snapshot_t* snapshot, CPUState* cpu) {
    syx_snapshot_increment_t* increment = g_new0(syx_snapshot_increment_t, 1);
    increment->parent = snapshot->last_incremental_snapshot;
    snapshot->last_incremental_snapshot = increment;

    increment->dirty_page_list = syx_snapshot_dirty_list_to_dirty_page_list(&snapshot->dirty_list);
    increment->dss = device_save_all();

    syx_snapshot_dirty_list_flush(&snapshot->dirty_list);
}

static syx_snapshot_dirty_page_t* get_dirty_page_from_addr_rec(syx_snapshot_increment_t* increment, hwaddr addr) {
    if (increment == NULL) {
        return NULL;
    }

    for (uint64_t i = 0; i < increment->dirty_page_list.length; ++i) {
        if (increment->dirty_page_list.dirty_pages[i].addr == addr) {
            return &increment->dirty_page_list.dirty_pages[i];
        }
    }

    return  get_dirty_page_from_addr_rec(increment->parent, addr);
}

static syx_snapshot_ramblock_t* find_ramblock(syx_snapshot_root_t* root, char* idstr) {
    for (size_t i = 0; i < root->nb_ram_blocks; i++) {
        if (!strcmp(idstr, root->ram_blocks[i].idstr)) {
            return &root->ram_blocks[i];
        }
    }

    return NULL;
}

static void restore_page_from_root(syx_snapshot_root_t* root, MemoryRegion* mr, hwaddr addr) {
    MemoryRegionSection mr_section = memory_region_find(mr, addr, syx_snapshot_state.page_size);

    if (mr_section.size == 0) {
        assert(mr_section.mr == NULL);

        SYX_PRINTF("Did not found a memory region while restoring the address %p from root snapshot.\n", (void*) addr);
        return;
    }

    if (mr_section.mr->ram) {
        syx_snapshot_ramblock_t* ram_block = find_ramblock(root, mr_section.mr->ram_block->idstr);
        assert(ram_block != NULL);
        assert(!strcmp(mr_section.mr->ram_block->idstr, ram_block->idstr));

        memcpy(mr_section.mr->ram_block->host + mr_section.offset_within_region,
                ram_block->ram + mr_section.offset_within_region, syx_snapshot_state.page_size);
    } else {
        if (!strcmp(mr_section.mr->name, "tseg-blackhole")) {
            assert(mr_to_enable == NULL);
            memory_region_set_enabled(mr_section.mr, false);
            mr_to_enable = mr_section.mr;
            restore_page_from_root(root, mr, addr);
        } else {
            // SYX_WARNING("[SYX SNAPSHOT RESTORE] Ram section not found for page @ 0x%lx (mr %s...)\n", addr, mr_section.mr->name);
        }
    }
}
//
static void restore_page(MemoryRegion* mr, syx_snapshot_dirty_page_t* page) {
    MemoryRegionSection mr_section = memory_region_find(mr, page->addr, syx_snapshot_state.page_size);
    assert(mr_section.size != 0 && mr_section.mr != NULL);
    assert(mr_section.mr->ram);
    
    memcpy(mr_section.mr->ram_block->host + mr_section.offset_within_region, page->data, syx_snapshot_state.page_size);
}

static void restore_to_last_increment(syx_snapshot_t* snapshot, MemoryRegion* mr) {
    syx_snapshot_increment_t* increment = snapshot->last_incremental_snapshot;
    syx_snapshot_dirty_list_t* dirty_list = &snapshot->dirty_list;

    for (uint64_t i = 0; i < dirty_list->length; ++i) {
        syx_snapshot_dirty_page_t* dirty_page = get_dirty_page_from_addr_rec(increment, dirty_list->dirty_addr[i]);
        if (dirty_page == NULL) {
            restore_page_from_root(&snapshot->root_snapshot, mr, dirty_list->dirty_addr[i]);
        } else {
            restore_page(mr, dirty_page);
        }
    }
}

void syx_snapshot_increment_pop(syx_snapshot_t* snapshot) {
    MemoryRegion* system_mr = get_system_memory();
    syx_snapshot_increment_t* last_increment = snapshot->last_incremental_snapshot;

    restore_to_last_increment(snapshot, system_mr);
    
    device_restore_all(last_increment->dss);

    syx_snapshot_dirty_list_flush(&snapshot->dirty_list);

    snapshot->last_incremental_snapshot = last_increment->parent;
    syx_snapshot_increment_free(last_increment);
}

void syx_snapshot_increment_restore_last(syx_snapshot_t* snapshot) {
    MemoryRegion* system_mr = get_system_memory();
    syx_snapshot_increment_t* last_increment = snapshot->last_incremental_snapshot;

    restore_to_last_increment(snapshot, system_mr);
    
    device_restore_all(last_increment->dss);

    syx_snapshot_dirty_list_flush(&snapshot->dirty_list);
}

syx_snapshot_increment_t* syx_snapshot_increment_free(syx_snapshot_increment_t* increment) {
    syx_snapshot_increment_t* parent = increment->parent;

    for (uint64_t i = 0; i < increment->dirty_page_list.length; ++i) {
        g_free(increment->dirty_page_list.dirty_pages[i].data);
    }

    device_free_all(increment->dss);
    g_free(increment);

    return parent;
}

syx_snapshot_dirty_list_t syx_snapshot_dirty_list_create(void) {
    syx_snapshot_dirty_list_t dirty_list = {
        .length = 0,
        .capacity = SYX_SNAPSHOT_LIST_INIT_SIZE,
        .dirty_addr = g_new(hwaddr, SYX_SNAPSHOT_LIST_INIT_SIZE)
    };

    return dirty_list;
}

void syx_snapshot_dirty_list_free(syx_snapshot_dirty_list_t* dirty_list) {
    g_free(dirty_list->dirty_addr);
}

static inline syx_snapshot_dirty_page_t* syx_snapshot_save_page_from_addr(MemoryRegion* mr, hwaddr addr) {
    syx_snapshot_dirty_page_t* dirty_page = g_new(syx_snapshot_dirty_page_t, 1);
    
    dirty_page->addr = addr;
    dirty_page->data = g_new(uint8_t, syx_snapshot_state.page_size);
    
    MemoryRegionSection mr_section = memory_region_find(mr, addr, syx_snapshot_state.page_size);

    assert(mr_section.size != 0 && mr_section.mr != NULL);

    if (!mr_section.mr->ram) {
        return NULL;
    }
    
    memcpy(dirty_page->data, mr_section.mr->ram_block->host + mr_section.offset_within_region, syx_snapshot_state.page_size);
    return dirty_page;
}

syx_snapshot_dirty_page_list_t syx_snapshot_dirty_list_to_dirty_page_list(syx_snapshot_dirty_list_t* dirty_list) {
    syx_snapshot_dirty_page_list_t dirty_page_list = {
        .length = dirty_list->length,
        .dirty_pages = g_new(syx_snapshot_dirty_page_t, dirty_list->length)
    };
    MemoryRegion* system_mr = get_system_memory();

    uint64_t real_len = 0;
    for (uint64_t i = 0; i < dirty_page_list.length; ++i) {
        syx_snapshot_dirty_page_t* page = syx_snapshot_save_page_from_addr(system_mr, dirty_list->dirty_addr[i]);
        if (page == NULL) {
            continue;
        }
        dirty_page_list.dirty_pages[real_len] = *page;
        real_len++;
        g_free(page);
    }

    dirty_page_list.length = real_len;

    return dirty_page_list;
}

static inline void syx_snapshot_dirty_list_add_internal(hwaddr paddr) {
    paddr &= syx_snapshot_state.page_mask;

    for (uint64_t i = 0; i < syx_snapshot_state.tracked_snapshots.length; ++i) {
        syx_snapshot_dirty_list_t* dirty_list = &syx_snapshot_state.tracked_snapshots.tracked_snapshots[i]->dirty_list;

        // Avoid adding already marked addresses
        for (uint64_t j = 0; j < dirty_list->length; ++j) {
            if (dirty_list->dirty_addr[j] == paddr) {
                goto next_snapshot_dirty_list;
            }
        }

        if (dirty_list->length == dirty_list->capacity) {
            //SYX_PRINTF("[DL %lu] Reallocation %lu ---> %lu\n", i, dirty_list->length, dirty_list->length * SYX_SNAPSHOT_LIST_GROW_FACTOR);
            dirty_list->capacity *= SYX_SNAPSHOT_LIST_GROW_FACTOR;
            dirty_list->dirty_addr = g_realloc(dirty_list->dirty_addr, dirty_list->capacity * sizeof(hwaddr));
        }

        dirty_list->dirty_addr[dirty_list->length] = paddr;
        dirty_list->length++;

        // SYX_PRINTF("[DL %lu] Added dirty page @addr 0x%lx. dirty list len: %lu\n", i, paddr, dirty_list->length);
    next_snapshot_dirty_list:;
    }
}

bool syx_snapshot_is_enabled(void) {
    return syx_snapshot_state.is_enabled;
}

/*
// The implementation is pretty bad, it would be nice to store host addr directly for
// the memcopy happening later on.
__attribute__((target("no-3dnow,no-sse,no-mmx"),no_caller_saved_registers)) void syx_snapshot_dirty_list_add_tcg_target(uint64_t dummy, void* host_addr) {
    // early check to know whether we should log the page access or not
    if (!syx_snapshot_is_enabled()) {
        return;
    }

    ram_addr_t offset;
    RAMBlock* rb = qemu_ram_block_from_host((void*) host_addr, true, &offset);

    assert(rb);
    
    hwaddr paddr = rb->mr->addr + offset;
    // If this assert is ever false, please understand why
    // qemu_ram_block_from_host result with true as second
    // param would not be host page aligned.
    assert(paddr == (paddr & syx_snapshot_state.page_mask));

    syx_snapshot_dirty_list_add_internal(paddr);
}
*/

void syx_snapshot_dirty_list_add_hostaddr(void* host_addr) {
    // early check to know whether we should log the page access or not
    if (!syx_snapshot_is_enabled()) {
        return;
    }

    ram_addr_t offset;
    RAMBlock* rb = qemu_ram_block_from_host((void*) host_addr, true, &offset);

    assert(rb);
    
    hwaddr paddr = rb->mr->addr + offset;
    // If this assert is ever false, please understand why
    // qemu_ram_block_from_host result with true as second
    // param would not be host page aligned.
    assert(paddr == (paddr & syx_snapshot_state.page_mask));

    syx_snapshot_dirty_list_add_internal(paddr);
}



void syx_snapshot_dirty_list_add(hwaddr paddr) {
    if (!syx_snapshot_is_enabled()) {
        return;
    }

    syx_snapshot_dirty_list_add_internal(paddr);
}

inline void syx_snapshot_dirty_list_flush(syx_snapshot_dirty_list_t* dirty_list) {
    dirty_list->length = 0;
}

static void syx_snapshot_restore_root_from_dirty_list(syx_snapshot_root_t* root, MemoryRegion* mr, syx_snapshot_dirty_list_t* dirty_list) {
    for (size_t i = 0; i < dirty_list->length; ++i) {
        restore_page_from_root(root, mr, dirty_list->dirty_addr[i]);
    }
}

void syx_snapshot_root_restore(syx_snapshot_t* snapshot) {
	bool must_unlock_iothread = false;
    MemoryRegion* system_mr = get_system_memory();

	if (!qemu_mutex_iothread_locked()) {
		qemu_mutex_lock_iothread();
		must_unlock_iothread = true;
	}

    syx_snapshot_restore_root_from_dirty_list(&snapshot->root_snapshot, system_mr, &snapshot->dirty_list);
    if (mr_to_enable) {
        memory_region_set_enabled(mr_to_enable, true);
        mr_to_enable = NULL;
    }
    device_restore_all(snapshot->root_snapshot.dss);
    syx_snapshot_dirty_list_flush(&snapshot->dirty_list);
 
	if (must_unlock_iothread) {
		qemu_mutex_unlock_iothread();
	}
}
