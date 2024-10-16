#pragma once

// Rewritten COW cache for block devices, heavily inspired by kAFL/NYX
// implementation.

#include "qemu/osdep.h"

#include "qemu/iov.h"
#include "block/block.h"

#define INITIAL_NB_CHUNKS_PER_DEVICE (1024 * 64)

typedef struct SyxCowCacheDevice {
    GArray* data;          // [u8]
    GHashTable* positions; // blkdev offset (must be aligned on chunk_size) ->
                           // data offset
} SyxCowCacheDevice;

typedef struct SyxCowCacheLayer {
    GArray* blks; // [SyxCowCacheDevice]

    uint64_t chunk_size;
    uint64_t max_nb_chunks;

    QTAILQ_ENTRY(SyxCowCacheLayer) next;
} SyxCowCacheLayer;

typedef struct SyxCowCache {
    QTAILQ_HEAD(, SyxCowCacheLayer) layers;
} SyxCowCache;

SyxCowCache* syx_cow_cache_new(void);

// Returns a SyxCowCache with a new layer on top.
// Other layers from scc are still present.
SyxCowCache* syx_cow_cache_push(SyxCowCache* scc, uint64_t chunk_size,
                                uint64_t max_size);

void syx_cow_cache_pop(SyxCowCache* scc);

// void syx_cow_cache_pop_layer(SyxCowCache* scc);

void syx_cow_cache_flush_highest_layer(SyxCowCache* scc);

void syx_cow_cache_check_files_ro(void);