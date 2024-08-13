#pragma once

// Rewritten COW cache for block devices, heavily inspired by kAFL/NYX
// implementation.

#include "qemu/osdep.h"

#include "qemu/iov.h"
#include "block/block.h"

#define INITIAL_NB_CHUNKS_PER_DEVICE (1024 * 64)

typedef struct SyxCowCacheDevice {
    GArray* data;
    GHashTable* positions; // blk_offset -> data_position
} SyxCowCacheDevice;

typedef struct SyxCowCacheLayer SyxCowCacheLayer;

typedef struct SyxCowCacheLayer {
    GHashTable* cow_cache_devices; // H(device) -> SyxCowCacheDevice
    uint64_t chunk_size;
    uint64_t max_nb_chunks;

    QTAILQ_ENTRY(SyxCowCacheLayer) next;
} SyxCowCacheLayer;

typedef struct SyxCowCache {
    QTAILQ_HEAD(, SyxCowCacheLayer) layers;
} SyxCowCache;

SyxCowCache* syx_cow_cache_new(void);

// lhs <- rhs
// rhs is freed and nulled.
void syx_cow_cache_move(SyxCowCache* lhs, SyxCowCache** rhs);

void syx_cow_cache_push_layer(SyxCowCache* scc, uint64_t chunk_size,
                              uint64_t max_size);
void syx_cow_cache_pop_layer(SyxCowCache* scc);

void syx_cow_cache_flush_highest_layer(SyxCowCache* scc);

void syx_cow_cache_read_entry(SyxCowCache* scc, BlockBackend* blk,
                              int64_t offset, int64_t bytes, QEMUIOVector* qiov,
                              size_t qiov_offset, BdrvRequestFlags flags);

bool syx_cow_cache_write_entry(SyxCowCache* scc, BlockBackend* blk,
                               int64_t offset, int64_t bytes,
                               QEMUIOVector* qiov, size_t qiov_offset,
                               BdrvRequestFlags flags);
