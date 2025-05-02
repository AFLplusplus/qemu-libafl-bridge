#include "qemu/osdep.h"

#include "libafl/syx-snapshot/syx-cow-cache.h"
#include "system/block-backend-io.h"

#define IS_POWER_OF_TWO(x) ((x != 0) && ((x & (x - 1)) == 0))

SyxCowCache* syx_cow_cache_new(void)
{
    SyxCowCache* cache = g_new0(SyxCowCache, 2);

    QTAILQ_INIT(&cache->layers);

    return cache;
}

static gchar* g_array_element_ptr(GArray* array, guint position)
{
    assert(position < array->len);
    return array->data + position * g_array_get_element_size(array);
}

void syx_cow_cache_push_layer(SyxCowCache* scc, uint64_t chunk_size,
                              uint64_t max_size)
{
    SyxCowCacheLayer* new_layer = g_new0(SyxCowCacheLayer, 1);

    new_layer->cow_cache_devices =
        g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);
    new_layer->chunk_size = chunk_size;
    new_layer->max_nb_chunks = max_size;

    assert(IS_POWER_OF_TWO(chunk_size));
    assert(!(max_size % chunk_size));

    QTAILQ_INSERT_HEAD(&scc->layers, new_layer, next);
}

void syx_cow_cache_pop_layer(SyxCowCache* scc)
{
    // TODO
}

static void flush_device_layer(gpointer _blk_name_hash, gpointer cache_device,
                               gpointer _user_data)
{
    SyxCowCacheDevice* sccd = (SyxCowCacheDevice*)cache_device;

    g_hash_table_remove_all(sccd->positions);
    g_array_set_size(sccd->data, 0);
}

void syx_cow_cache_flush_highest_layer(SyxCowCache* scc)
{
    SyxCowCacheLayer* highest_layer = QTAILQ_FIRST(&scc->layers);

    // highest_layer->cow_cache_devices
    g_hash_table_foreach(highest_layer->cow_cache_devices, flush_device_layer,
                         NULL);
}

void syx_cow_cache_move(SyxCowCache* lhs, SyxCowCache** rhs)
{
    lhs->layers = (*rhs)->layers;
    g_free(*rhs);
    *rhs = NULL;
}

static bool read_chunk_from_cache_layer_device(SyxCowCacheDevice* sccd,
                                               QEMUIOVector* qiov,
                                               size_t qiov_offset,
                                               uint64_t blk_offset)
{
    gpointer data_position = NULL;
    bool found = g_hash_table_lookup_extended(
        sccd->positions, GUINT_TO_POINTER(blk_offset), NULL, &data_position);

    // cache hit
    if (found) {
        void* data_position_ptr =
            g_array_element_ptr(sccd->data, GPOINTER_TO_UINT(data_position));
        assert(qemu_iovec_from_buf(qiov, qiov_offset, data_position_ptr,
                                   g_array_get_element_size(sccd->data)) ==
               g_array_get_element_size(sccd->data));
    }

    return found;
}

// len must be smaller than nb bytes to next aligned to chunk of blk_offset.
// static void write_to_cache_layer_device_unaligned(SyxCowCacheDevice* sccd,
// QEMUIOVector* qiov, size_t qiov_offset, uint64_t blk_offset, uint64_t len)
// {
//     const uint64_t chunk_size = g_array_get_element_size(sccd->data);
//
//     assert(ROUND_UP(blk_offset, chunk_size) - blk_offset <= len);
//     assert(IS_POWER_OF_TWO(chunk_size));
//
//     uint64_t blk_offset_aligned = ROUND_DOWN(blk_offset, chunk_size);
//
//     gpointer data_position = NULL;
//     bool found = g_hash_table_lookup_extended(sccd->positions,
//     GUINT_TO_POINTER(blk_offset_aligned), NULL, &data_position);
//
//     if (!found) {
//         data_position = GUINT_TO_POINTER(sccd->data->len);
//         sccd->data = g_array_set_size(sccd->data, sccd->data->len + 1);
//         g_hash_table_insert(sccd->positions, GUINT_TO_POINTER(blk_offset),
//         data_position);
//     }
//
//     void* data_position_ptr = g_array_element_ptr(sccd->data,
//     GPOINTER_TO_UINT(data_position));
//
//     assert(qemu_iovec_to_buf(qiov, qiov_offset, data_position_ptr,
//     g_array_get_element_size(sccd->data)) ==
//            g_array_get_element_size(sccd->data));
// }

// cache layer is allocated and all the basic checks are already done.
static void write_chunk_to_cache_layer_device(SyxCowCacheDevice* sccd,
                                              QEMUIOVector* qiov,
                                              size_t qiov_offset,
                                              uint64_t blk_offset)
{
    const uint64_t chunk_size = g_array_get_element_size(sccd->data);

    gpointer data_position = NULL;
    bool found = g_hash_table_lookup_extended(
        sccd->positions, GUINT_TO_POINTER(blk_offset), NULL, &data_position);

    if (!found) {
        data_position = GUINT_TO_POINTER(sccd->data->len);
        sccd->data = g_array_set_size(sccd->data, sccd->data->len + 1);
        g_hash_table_insert(sccd->positions, GUINT_TO_POINTER(blk_offset),
                            data_position);
    }

    void* data_position_ptr =
        g_array_element_ptr(sccd->data, GPOINTER_TO_UINT(data_position));

    assert(qemu_iovec_to_buf(qiov, qiov_offset, data_position_ptr,
                             chunk_size) == chunk_size);
}

static bool read_chunk_from_cache_layer(SyxCowCacheLayer* sccl,
                                        BlockBackend* blk, QEMUIOVector* qiov,
                                        size_t qiov_offset, uint64_t blk_offset)
{
    assert(!(qiov->size % sccl->chunk_size));

    SyxCowCacheDevice* cache_entry = g_hash_table_lookup(
        sccl->cow_cache_devices, GINT_TO_POINTER(blk_name_hash(blk)));

    // return early if nothing is registered
    if (!cache_entry) {
        return false;
    }

    assert(cache_entry && cache_entry->data);

    // try to read cached pages in current layer if something is registered.
    return read_chunk_from_cache_layer_device(cache_entry, qiov, qiov_offset,
                                              blk_offset);
}

// Returns false if could not write to current layer.
static bool write_to_cache_layer(SyxCowCacheLayer* sccl, BlockBackend* blk,
                                 int64_t offset, int64_t bytes,
                                 QEMUIOVector* qiov)
{
    if (qiov->size % sccl->chunk_size) {
        // todo: determine if it is worth developing an unaligned access
        // version.
        printf("error: 0x%zx %% 0x%lx == 0x%lx\n", qiov->size, sccl->chunk_size,
               qiov->size % sccl->chunk_size);
        exit(1);
    }

    SyxCowCacheDevice* cache_entry = g_hash_table_lookup(
        sccl->cow_cache_devices, GINT_TO_POINTER(blk_name_hash(blk)));

    if (unlikely(!cache_entry)) {
        cache_entry = g_new0(SyxCowCacheDevice, 1);
        cache_entry->data = g_array_sized_new(false, false, sccl->chunk_size,
                                              INITIAL_NB_CHUNKS_PER_DEVICE);
        cache_entry->positions =
            g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);
        g_hash_table_insert(sccl->cow_cache_devices,
                            GINT_TO_POINTER(blk_name_hash(blk)), cache_entry);
    }

    assert(cache_entry && cache_entry->data);

    if (cache_entry->data->len + (qiov->size / sccl->chunk_size) >
        sccl->max_nb_chunks) {
        return false;
    }

    // write cached page
    uint64_t blk_offset = offset;
    size_t qiov_offset = 0;
    for (; qiov_offset < qiov->size;
         blk_offset += sccl->chunk_size, qiov_offset += sccl->chunk_size) {
        write_chunk_to_cache_layer_device(cache_entry, qiov, qiov_offset,
                                          blk_offset);
    }

    return true;
}

void syx_cow_cache_read_entry(SyxCowCache* scc, BlockBackend* blk,
                              int64_t offset, int64_t bytes, QEMUIOVector* qiov,
                              size_t _qiov_offset, BdrvRequestFlags flags)
{
    SyxCowCacheLayer* layer;
    uint64_t blk_offset = offset;
    size_t qiov_offset = 0;
    uint64_t chunk_size = 0;

    // printf("[%s] Read 0x%zx bytes @addr %lx\n", blk_name(blk), qiov->size,
    // offset);

    // First read the backing block device normally.
    assert(blk_co_preadv(blk, offset, bytes, qiov, flags) >= 0);

    // Then fix the chunks that have been read from before.
    if (!QTAILQ_EMPTY(&scc->layers)) {
        for (; qiov_offset < qiov->size;
             blk_offset += chunk_size, qiov_offset += chunk_size) {
            QTAILQ_FOREACH(layer, &scc->layers, next)
            {
                chunk_size = layer->chunk_size;
                if (read_chunk_from_cache_layer(layer, blk, qiov, qiov_offset,
                                                blk_offset)) {
                    break;
                }
            }
        }
    }
}

bool syx_cow_cache_write_entry(SyxCowCache* scc, BlockBackend* blk,
                               int64_t offset, int64_t bytes,
                               QEMUIOVector* qiov, size_t qiov_offset,
                               BdrvRequestFlags flags)
{
    SyxCowCacheLayer* layer;

    // printf("[%s] Write 0x%zx bytes @addr %lx\n", blk_name(blk), qiov->size,
    // offset);

    layer = QTAILQ_FIRST(&scc->layers);
    if (layer) {
        assert(write_to_cache_layer(layer, blk, offset, bytes, qiov));
        return true;
    } else {
        return false;
    }
}
