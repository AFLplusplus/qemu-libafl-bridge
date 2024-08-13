#include "qemu/osdep.h"

#include "qemu/main-loop.h"
#include "sysemu/sysemu.h"
#include "migration/vmstate.h"
#include "cpu.h"

#include "exec/ramlist.h"
#include "exec/ram_addr.h"
#include "exec/exec-all.h"

#include "libafl/syx-snapshot/syx-snapshot.h"
#include "libafl/syx-snapshot/device-save.h"

#define SYX_SNAPSHOT_LIST_INIT_SIZE 4096
#define SYX_SNAPSHOT_LIST_GROW_FACTOR 2
#define TARGET_NEXT_PAGE_ADDR(p)                                               \
    ((typeof(p))(((uintptr_t)p + TARGET_PAGE_SIZE) & TARGET_PAGE_MASK))

/**
 * Saved ramblock
 */
typedef struct SyxSnapshotRAMBlock {
    uint8_t* ram;         // RAM block
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

SyxSnapshotState syx_snapshot_state = {0};
static MemoryRegion* mr_to_enable = NULL;

static void destroy_ramblock_snapshot(gpointer root_snapshot);

static void syx_snapshot_dirty_list_flush(SyxSnapshot* snapshot);

static void
rb_save_dirty_addr_to_table(gpointer offset_within_rb, gpointer unused,
                            gpointer rb_dirty_list_to_page_args_ptr);

static void rb_dirty_list_to_dirty_pages(gpointer rb_idstr_hash,
                                         gpointer rb_dirty_list_hash_table_ptr,
                                         gpointer rbs_dirty_pages_ptr);

static inline void syx_snapshot_dirty_list_add_internal(RAMBlock* rb,
                                                        ram_addr_t offset);

static void empty_rb_dirty_list(gpointer rb_idstr_hash,
                                gpointer rb_dirty_list_hash_table_ptr,
                                gpointer user_data);

static void
destroy_snapshot_dirty_page_list(gpointer snapshot_dirty_page_list_ptr);

static void root_restore_rb_page(gpointer offset_within_rb, gpointer unused,
                                 gpointer root_restore_args_ptr);

static void root_restore_rb(gpointer rb_idstr_hash,
                            gpointer rb_dirty_pages_hash_table_ptr,
                            gpointer snapshot_ptr);

static void root_restore_check_memory_rb(gpointer rb_idstr_hash,
                                         gpointer rb_dirty_pages_hash_table_ptr,
                                         gpointer snapshot_ptr);

static SyxSnapshotIncrement*
syx_snapshot_increment_free(SyxSnapshotIncrement* increment);

static RAMBlock* ramblock_lookup(gpointer rb_idstr_hash)
{
    RAMBlock* block;
    RAMBLOCK_FOREACH(block)
    {
        if (rb_idstr_hash == GINT_TO_POINTER(block->idstr_hash)) {
            return block;
        }
    }

    return NULL;
}

// Root snapshot API
static SyxSnapshotRoot* syx_snapshot_root_new(DeviceSnapshotKind kind,
                                              char** devices);

static void syx_snapshot_root_free(SyxSnapshotRoot* root);

struct rb_dirty_list_to_page_args {
    RAMBlock* rb;
    SyxSnapshotDirtyPageList* dirty_page_list;
    uint64_t* table_idx;
};

struct rb_page_root_restore_args {
    RAMBlock* rb;
    SyxSnapshotRAMBlock* snapshot_rb;
};

struct rb_increment_restore_args {
    SyxSnapshot* snapshot;
    SyxSnapshotIncrement* increment;
};

struct rb_page_increment_restore_args {
    RAMBlock* rb;
    SyxSnapshot* snapshot;
    SyxSnapshotIncrement* increment;
};

struct rb_check_memory_args {
    SyxSnapshot* snapshot;          // IN
    uint64_t nb_inconsistent_pages; // OUT
};

void syx_snapshot_init(bool cached_bdrvs)
{
    uint64_t page_size = TARGET_PAGE_SIZE;

    syx_snapshot_state.page_size = page_size;
    syx_snapshot_state.page_mask = ((uint64_t)-1) << __builtin_ctz(page_size);

    syx_snapshot_state.tracked_snapshots = syx_snapshot_tracker_init();

    if (cached_bdrvs) {
        syx_snapshot_state.before_fuzz_cache = syx_cow_cache_new();
        syx_cow_cache_push_layer(syx_snapshot_state.before_fuzz_cache,
                                 SYX_SNAPSHOT_COW_CACHE_DEFAULT_CHUNK_SIZE,
                                 SYX_SNAPSHOT_COW_CACHE_DEFAULT_MAX_BLOCKS);
    }

    syx_snapshot_state.is_enabled = false;
}

SyxSnapshot* syx_snapshot_new(bool track, bool is_active_bdrv_cache,
                              DeviceSnapshotKind kind, char** devices)
{
    SyxSnapshot* snapshot = g_new0(SyxSnapshot, 1);

    snapshot->root_snapshot = syx_snapshot_root_new(kind, devices);
    snapshot->last_incremental_snapshot = NULL;
    snapshot->rbs_dirty_list =
        g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL,
                              (GDestroyNotify)g_hash_table_remove_all);
    snapshot->bdrvs_cow_cache = syx_cow_cache_new();

    if (is_active_bdrv_cache) {
        syx_cow_cache_move(snapshot->bdrvs_cow_cache,
                           &syx_snapshot_state.before_fuzz_cache);
        syx_snapshot_state.active_bdrv_cache_snapshot = snapshot;
    } else {
        syx_cow_cache_push_layer(snapshot->bdrvs_cow_cache,
                                 SYX_SNAPSHOT_COW_CACHE_DEFAULT_CHUNK_SIZE,
                                 SYX_SNAPSHOT_COW_CACHE_DEFAULT_MAX_BLOCKS);
    }

    if (track) {
        syx_snapshot_track(&syx_snapshot_state.tracked_snapshots, snapshot);
    }

    syx_snapshot_state.is_enabled = true;

    return snapshot;
}

void syx_snapshot_free(SyxSnapshot* snapshot)
{
    SyxSnapshotIncrement* increment = snapshot->last_incremental_snapshot;

    while (increment != NULL) {
        increment = syx_snapshot_increment_free(increment);
    }

    g_hash_table_remove_all(snapshot->rbs_dirty_list);

    syx_snapshot_root_free(snapshot->root_snapshot);

    g_free(snapshot);
}

static void destroy_ramblock_snapshot(gpointer root_snapshot)
{
    SyxSnapshotRAMBlock* snapshot_rb = root_snapshot;

    g_free(snapshot_rb->ram);
    g_free(snapshot_rb);
}

static SyxSnapshotRoot* syx_snapshot_root_new(DeviceSnapshotKind kind,
                                              char** devices)
{
    SyxSnapshotRoot* root = g_new0(SyxSnapshotRoot, 1);

    RAMBlock* block;
    RAMBlock* inner_block;
    DeviceSaveState* dss = device_save_kind(kind, devices);

    root->rbs_snapshot = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                               NULL, destroy_ramblock_snapshot);
    root->dss = dss;

    RAMBLOCK_FOREACH(block)
    {
        RAMBLOCK_FOREACH(inner_block)
        {
            if (block != inner_block &&
                inner_block->idstr_hash == block->idstr_hash) {
                SYX_ERROR("Hash collision detected on RAMBlocks %s and %s, "
                          "snapshotting will not work correctly.",
                          inner_block->idstr, block->idstr);
                exit(1);
            }
        }

        SyxSnapshotRAMBlock* snapshot_rb = g_new(SyxSnapshotRAMBlock, 1);
        snapshot_rb->used_length = block->used_length;
        snapshot_rb->ram = g_new(uint8_t, block->used_length);
        memcpy(snapshot_rb->ram, block->host, block->used_length);

        g_hash_table_insert(root->rbs_snapshot,
                            GINT_TO_POINTER(block->idstr_hash), snapshot_rb);
    }

    return root;
}

static void syx_snapshot_root_free(SyxSnapshotRoot* root)
{
    g_hash_table_destroy(root->rbs_snapshot);
    g_free(root);
}

SyxSnapshotTracker syx_snapshot_tracker_init(void)
{
    SyxSnapshotTracker tracker = {
        .length = 0,
        .capacity = SYX_SNAPSHOT_LIST_INIT_SIZE,
        .tracked_snapshots = g_new(SyxSnapshot*, SYX_SNAPSHOT_LIST_INIT_SIZE)};

    return tracker;
}

void syx_snapshot_track(SyxSnapshotTracker* tracker, SyxSnapshot* snapshot)
{
    if (tracker->length == tracker->capacity) {
        tracker->capacity *= SYX_SNAPSHOT_LIST_GROW_FACTOR;
        tracker->tracked_snapshots =
            g_realloc(tracker->tracked_snapshots,
                      tracker->capacity * sizeof(SyxSnapshot*));
    }

    assert(tracker->length < tracker->capacity);

    tracker->tracked_snapshots[tracker->length] = snapshot;
    tracker->length++;
}

void syx_snapshot_stop_track(SyxSnapshotTracker* tracker, SyxSnapshot* snapshot)
{
    for (uint64_t i = 0; i < tracker->length; ++i) {
        if (tracker->tracked_snapshots[i] == snapshot) {
            for (uint64_t j = i + i; j < tracker->length; ++j) {
                tracker->tracked_snapshots[j - 1] =
                    tracker->tracked_snapshots[j];
            }
            tracker->length--;
            return;
        }
    }

    SYX_PRINTF("ERROR: trying to remove an untracked snapshot\n");
    abort();
}

static void rb_save_dirty_addr_to_table(gpointer offset_within_rb,
                                        gpointer unused,
                                        gpointer rb_dirty_list_to_page_args_ptr)
{
    struct rb_dirty_list_to_page_args* args = rb_dirty_list_to_page_args_ptr;
    RAMBlock* rb = args->rb;
    SyxSnapshotDirtyPage* dirty_page =
        &args->dirty_page_list->dirty_pages[*args->table_idx];
    dirty_page->offset_within_rb = (ram_addr_t)offset_within_rb;

    memcpy((gpointer)dirty_page->data, rb->host + (ram_addr_t)offset_within_rb,
           syx_snapshot_state.page_size);

    *args->table_idx += 1;
}

static void rb_dirty_list_to_dirty_pages(gpointer rb_idstr_hash,
                                         gpointer rb_dirty_list_hash_table_ptr,
                                         gpointer rbs_dirty_pages_ptr)
{
    GHashTable* rbs_dirty_pages = rbs_dirty_pages_ptr;
    GHashTable* rb_dirty_list = rb_dirty_list_hash_table_ptr;

    RAMBlock* rb = ramblock_lookup(rb_idstr_hash);

    if (rb) {
        SyxSnapshotDirtyPageList* dirty_page_list =
            g_new(SyxSnapshotDirtyPageList, 1);
        dirty_page_list->length = g_hash_table_size(rb_dirty_list);
        dirty_page_list->dirty_pages =
            g_new(SyxSnapshotDirtyPage, dirty_page_list->length);

        uint64_t* ctr = g_new0(uint64_t, 1);

        struct rb_dirty_list_to_page_args dirty_list_to_page_args = {
            .rb = rb, .table_idx = ctr, .dirty_page_list = dirty_page_list};

        g_hash_table_foreach(rbs_dirty_pages, rb_save_dirty_addr_to_table,
                             &dirty_list_to_page_args);

        g_free(dirty_list_to_page_args.table_idx);
    } else {
        SYX_ERROR("Impossible to find RAMBlock with pages marked as dirty.");
    }
}

static void
destroy_snapshot_dirty_page_list(gpointer snapshot_dirty_page_list_ptr)
{
    SyxSnapshotDirtyPageList* snapshot_dirty_page_list =
        snapshot_dirty_page_list_ptr;

    for (uint64_t i = 0; i < snapshot_dirty_page_list->length; ++i) {
        g_free(snapshot_dirty_page_list->dirty_pages[i].data);
    }

    g_free(snapshot_dirty_page_list->dirty_pages);
    g_free(snapshot_dirty_page_list);
}

void syx_snapshot_increment_push(SyxSnapshot* snapshot, DeviceSnapshotKind kind,
                                 char** devices)
{
    SyxSnapshotIncrement* increment = g_new0(SyxSnapshotIncrement, 1);
    increment->parent = snapshot->last_incremental_snapshot;
    snapshot->last_incremental_snapshot = increment;

    increment->rbs_dirty_pages = g_hash_table_new_full(
        g_direct_hash, g_direct_equal, NULL, destroy_snapshot_dirty_page_list);
    g_hash_table_foreach(snapshot->rbs_dirty_list, rb_dirty_list_to_dirty_pages,
                         increment->rbs_dirty_pages);
    increment->dss = device_save_kind(kind, devices);

    g_hash_table_remove_all(snapshot->rbs_dirty_list);
}

static SyxSnapshotDirtyPage*
get_dirty_page_from_addr_rec(SyxSnapshotIncrement* increment, RAMBlock* rb,
                             ram_addr_t offset_within_rb)
{
    if (increment == NULL) {
        return NULL;
    }

    SyxSnapshotDirtyPageList* dpl = g_hash_table_lookup(
        increment->rbs_dirty_pages, GINT_TO_POINTER(rb->idstr_hash));

    if (dpl) {
        for (uint64_t i = 0; i < dpl->length; ++i) {
            if (dpl->dirty_pages[i].offset_within_rb == offset_within_rb) {
                return &dpl->dirty_pages[i];
            }
        }
    }

    return get_dirty_page_from_addr_rec(increment->parent, rb,
                                        offset_within_rb);
}

static void restore_dirty_page_to_increment(gpointer offset_within_rb,
                                            gpointer _unused, gpointer args_ptr)
{
    struct rb_page_increment_restore_args* args = args_ptr;
    RAMBlock* rb = args->rb;
    SyxSnapshot* snapshot = args->snapshot;
    SyxSnapshotIncrement* increment = args->increment;
    ram_addr_t offset = (ram_addr_t)offset_within_rb;

    SyxSnapshotDirtyPage* dp =
        get_dirty_page_from_addr_rec(increment, rb, offset);

    if (dp) {
        memcpy(rb->host + offset, dp->data, syx_snapshot_state.page_size);
    } else {
        SyxSnapshotRAMBlock* rrb =
            g_hash_table_lookup(snapshot->root_snapshot->rbs_snapshot,
                                GINT_TO_POINTER(rb->idstr_hash));
        assert(rrb);

        memcpy(rb->host + offset, rrb->ram, syx_snapshot_state.page_size);
    }
}

static void restore_rb_to_increment(gpointer rb_idstr_hash,
                                    gpointer rb_dirty_pages_hash_table_ptr,
                                    gpointer args_ptr)
{
    struct rb_increment_restore_args* args = args_ptr;
    GHashTable* rb_dirty_pages_hash_table = rb_dirty_pages_hash_table_ptr;

    RAMBlock* rb = ramblock_lookup(rb_idstr_hash);
    struct rb_page_increment_restore_args page_args = {
        .snapshot = args->snapshot, .increment = args->increment, .rb = rb};

    g_hash_table_foreach(rb_dirty_pages_hash_table,
                         restore_dirty_page_to_increment, &page_args);
}

static void restore_to_increment(SyxSnapshot* snapshot,
                                 SyxSnapshotIncrement* increment)
{
    struct rb_increment_restore_args args = {.snapshot = snapshot,
                                             .increment = increment};

    g_hash_table_foreach(snapshot->rbs_dirty_list, restore_rb_to_increment,
                         &args);
}

void syx_snapshot_increment_pop(SyxSnapshot* snapshot)
{
    SyxSnapshotIncrement* last_increment = snapshot->last_incremental_snapshot;

    device_restore_all(last_increment->dss);
    restore_to_increment(snapshot, last_increment);

    snapshot->last_incremental_snapshot = last_increment->parent;
    syx_snapshot_increment_free(last_increment);

    syx_snapshot_dirty_list_flush(snapshot);
}

void syx_snapshot_increment_restore_last(SyxSnapshot* snapshot)
{
    SyxSnapshotIncrement* last_increment = snapshot->last_incremental_snapshot;

    device_restore_all(last_increment->dss);
    restore_to_increment(snapshot, last_increment);

    syx_snapshot_dirty_list_flush(snapshot);
}

static SyxSnapshotIncrement*
syx_snapshot_increment_free(SyxSnapshotIncrement* increment)
{
    SyxSnapshotIncrement* parent_increment = increment->parent;
    g_hash_table_destroy(increment->rbs_dirty_pages);
    device_free_all(increment->dss);
    g_free(increment);
    return parent_increment;
}

static void syx_snapshot_dirty_list_flush(SyxSnapshot* snapshot)
{
    g_hash_table_foreach(snapshot->rbs_dirty_list, empty_rb_dirty_list,
                         (gpointer)snapshot);
}

static inline void syx_snapshot_dirty_list_add_internal(RAMBlock* rb,
                                                        ram_addr_t offset)
{
    assert((offset & syx_snapshot_state.page_mask) ==
           offset); // offsets should always be page-aligned.

    for (uint64_t i = 0; i < syx_snapshot_state.tracked_snapshots.length; ++i) {
        SyxSnapshot* snapshot =
            syx_snapshot_state.tracked_snapshots.tracked_snapshots[i];

        GHashTable* rb_dirty_list = g_hash_table_lookup(
            snapshot->rbs_dirty_list, GINT_TO_POINTER(rb->idstr_hash));

        if (unlikely(!rb_dirty_list)) {
#ifdef SYX_SNAPSHOT_DEBUG
            printf("rb_dirty_list did not exit, creating...\n");
#endif
            rb_dirty_list = g_hash_table_new(g_direct_hash, g_direct_equal);
            g_hash_table_insert(snapshot->rbs_dirty_list,
                                GINT_TO_POINTER(rb->idstr_hash), rb_dirty_list);
        }

        if (g_hash_table_add(rb_dirty_list, GINT_TO_POINTER(offset))) {
#ifdef SYX_SNAPSHOT_DEBUG
            SYX_PRINTF("[%s] Marking offset 0x%lx as dirty\n", rb->idstr,
                       offset);
#endif
        }
    }
}

bool syx_snapshot_is_enabled(void) { return syx_snapshot_state.is_enabled; }

/*
// TODO: Check if using this method is better for performances.
// The implementation is pretty bad, it would be nice to store host addr
directly for
// the memcopy happening later on.
__attribute__((target("no-3dnow,no-sse,no-mmx"),no_caller_saved_registers)) void
syx_snapshot_dirty_list_add_tcg_target(uint64_t dummy, void* host_addr) {
    // early check to know whether we should log the page access or not
    if (!syx_snapshot_is_enabled()) {
        return;
    }

    ram_addr_t offset;
    RAMBlock* rb = qemu_ram_block_from_host((void*) host_addr, true, &offset);

    if (!rb) {
        return;
    }

    syx_snapshot_dirty_list_add_internal(rb, offset);
}
*/

// host_addr should be page-aligned.
void syx_snapshot_dirty_list_add_hostaddr(void* host_addr)
{
    // early check to know whether we should log the page access or not
    if (!syx_snapshot_is_enabled()) {
        return;
    }

    ram_addr_t offset;
    RAMBlock* rb = qemu_ram_block_from_host((void*)host_addr, true, &offset);

#ifdef SYX_SNAPSHOT_DEBUG
    SYX_PRINTF("Should mark offset 0x%lx as dirty\n", offset);
#endif

    if (!rb) {
        return;
    }

    syx_snapshot_dirty_list_add_internal(rb, offset);
}

void syx_snapshot_dirty_list_add_hostaddr_range(void* host_addr, uint64_t len)
{
    // early check to know whether we should log the page access or not
    if (!syx_snapshot_is_enabled()) {
        return;
    }

    assert(len < INT64_MAX);
    int64_t len_signed = (int64_t)len;

    syx_snapshot_dirty_list_add_hostaddr(
        QEMU_ALIGN_PTR_DOWN(host_addr, syx_snapshot_state.page_size));
    void* next_page_addr = TARGET_NEXT_PAGE_ADDR(host_addr);
    assert(next_page_addr > host_addr);
    assert(QEMU_PTR_IS_ALIGNED(next_page_addr, TARGET_PAGE_SIZE));

    int64_t len_to_next_page = next_page_addr - host_addr;

    host_addr += len_to_next_page;
    len_signed -= len_to_next_page;

    while (len_signed > 0) {
        assert(QEMU_PTR_IS_ALIGNED(host_addr, TARGET_PAGE_SIZE));

        syx_snapshot_dirty_list_add_hostaddr(host_addr);
        len_signed -= TARGET_PAGE_SIZE;
    }
}

static void empty_rb_dirty_list(gpointer _rb_idstr_hash,
                                gpointer rb_dirty_list_hash_table_ptr,
                                gpointer _user_data)
{
    GHashTable* rb_dirty_hash_table = rb_dirty_list_hash_table_ptr;
    g_hash_table_remove_all(rb_dirty_hash_table);
}

static void root_restore_rb_page(gpointer offset_within_rb, gpointer _unused,
                                 gpointer root_restore_args_ptr)
{
    struct rb_page_root_restore_args* args = root_restore_args_ptr;
    RAMBlock* rb = args->rb;
    SyxSnapshotRAMBlock* snapshot_rb = args->snapshot_rb;

    // safe cast because ram_addr_t is also an alias to void*
    void* host_rb_restore = rb->host + (ram_addr_t)offset_within_rb;
    void* host_snapshot_rb_restore =
        (gpointer)snapshot_rb->ram + (ram_addr_t)offset_within_rb;

#ifdef SYX_SNAPSHOT_DEBUG
    SYX_PRINTF("\t[%s] Restore at offset 0x%lx of size %lu...\n", rb->idstr,
               (uint64_t)offset_within_rb, syx_snapshot_state.page_size);
#endif

    memcpy(host_rb_restore, host_snapshot_rb_restore,
           syx_snapshot_state.page_size);
    // TODO: manage special case of TSEG.
}

static void root_restore_rb(gpointer rb_idstr_hash,
                            gpointer rb_dirty_pages_hash_table_ptr,
                            gpointer snapshot_ptr)
{
    SyxSnapshot* snapshot = snapshot_ptr;
    GHashTable* rb_dirty_pages_hash_table = rb_dirty_pages_hash_table_ptr;
    RAMBlock* rb = ramblock_lookup(rb_idstr_hash);

    if (rb) {
        SyxSnapshotRAMBlock* snapshot_ramblock = g_hash_table_lookup(
            snapshot->root_snapshot->rbs_snapshot, rb_idstr_hash);

        struct rb_page_root_restore_args root_restore_args = {
            .rb = rb, .snapshot_rb = snapshot_ramblock};

        g_hash_table_foreach(rb_dirty_pages_hash_table, root_restore_rb_page,
                             &root_restore_args);
    } else {
        SYX_ERROR("Saved RAMBlock not found.");
        exit(1);
    }
}

static void root_restore_check_memory_rb(gpointer rb_idstr_hash,
                                         gpointer rb_dirty_pages_hash_table_ptr,
                                         gpointer check_memory_args_ptr)
{
    struct rb_check_memory_args* args = check_memory_args_ptr;
    SyxSnapshot* snapshot = args->snapshot;
    RAMBlock* rb = ramblock_lookup(rb_idstr_hash);

    if (rb) {
        SYX_PRINTF("Checking memory consistency of %s... ", rb->idstr);
        SyxSnapshotRAMBlock* rb_snapshot = g_hash_table_lookup(
            snapshot->root_snapshot->rbs_snapshot, rb_idstr_hash);
        assert(rb_snapshot);

        assert(rb->used_length == rb_snapshot->used_length);

        for (uint64_t i = 0; i < rb->used_length;
             i += syx_snapshot_state.page_size) {
            if (memcmp(rb->host + i, rb_snapshot->ram + i,
                       syx_snapshot_state.page_size) != 0) {
                SYX_ERROR("\nFound incorrect page at offset 0x%lx\n", i);
                for (uint64_t j = 0; j < syx_snapshot_state.page_size; j++) {
                    if (*(rb->host + i + j) != *(rb_snapshot->ram + i + j)) {
                        SYX_ERROR("\t- byte at address 0x%lx differs\n", i + j);
                    }
                }
                args->nb_inconsistent_pages++;
            }
        }

        if (args->nb_inconsistent_pages > 0) {
            SYX_ERROR("[%s] Found %lu page %s.\n", rb->idstr,
                      args->nb_inconsistent_pages,
                      args->nb_inconsistent_pages > 1 ? "inconsistencies"
                                                      : "inconsistency");
        } else {
            SYX_PRINTF("OK.\n");
        }
    } else {
        SYX_ERROR("RB not found...\n");
        exit(1);
    }
}

SyxSnapshotCheckResult syx_snapshot_check(SyxSnapshot* ref_snapshot)
{
    struct rb_check_memory_args args = {
        .snapshot = ref_snapshot,
        .nb_inconsistent_pages = 0,
    };

    g_hash_table_foreach(ref_snapshot->rbs_dirty_list,
                         root_restore_check_memory_rb, &args);

    struct SyxSnapshotCheckResult res = {.nb_inconsistencies =
                                             args.nb_inconsistent_pages};

    return res;
}

void syx_snapshot_root_restore(SyxSnapshot* snapshot)
{
    // health check.
    CPUState* cpu;
    CPU_FOREACH(cpu) { assert(cpu->stopped); }

    bool must_unlock_bql = false;

    if (!bql_locked()) {
        bql_lock();
        must_unlock_bql = true;
    }

    // In case, we first restore devices if there is a modification of memory
    // layout
    device_restore_all(snapshot->root_snapshot->dss);

    g_hash_table_foreach(snapshot->rbs_dirty_list, root_restore_rb, snapshot);

    syx_cow_cache_flush_highest_layer(snapshot->bdrvs_cow_cache);

    if (mr_to_enable) {
        memory_region_set_enabled(mr_to_enable, true);
        mr_to_enable = NULL;
    }

    syx_snapshot_dirty_list_flush(snapshot);

    if (must_unlock_bql) {
        bql_unlock();
    }
}

bool syx_snapshot_cow_cache_read_entry(BlockBackend* blk, int64_t offset,
                                       int64_t bytes, QEMUIOVector* qiov,
                                       size_t qiov_offset,
                                       BdrvRequestFlags flags)
{
    if (!syx_snapshot_state.active_bdrv_cache_snapshot) {
        if (syx_snapshot_state.before_fuzz_cache) {
            syx_cow_cache_read_entry(syx_snapshot_state.before_fuzz_cache, blk,
                                     offset, bytes, qiov, qiov_offset, flags);
            return true;
        }

        return false;
    } else {
        syx_cow_cache_read_entry(
            syx_snapshot_state.active_bdrv_cache_snapshot->bdrvs_cow_cache, blk,
            offset, bytes, qiov, qiov_offset, flags);
        return true;
    }
}

bool syx_snapshot_cow_cache_write_entry(BlockBackend* blk, int64_t offset,
                                        int64_t bytes, QEMUIOVector* qiov,
                                        size_t qiov_offset,
                                        BdrvRequestFlags flags)
{
    if (!syx_snapshot_state.active_bdrv_cache_snapshot) {
        if (syx_snapshot_state.before_fuzz_cache) {
            assert(syx_cow_cache_write_entry(
                syx_snapshot_state.before_fuzz_cache, blk, offset, bytes, qiov,
                qiov_offset, flags));
            return true;
        }

        return false;
    } else {
        assert(syx_cow_cache_write_entry(
            syx_snapshot_state.active_bdrv_cache_snapshot->bdrvs_cow_cache, blk,
            offset, bytes, qiov, qiov_offset, flags));
        return true;
    }
}
