#include "libafl/syx-snapshot/syx-cow-cache.h"

#include "block/block_int-common.h"
#include "sysemu/block-backend.h"
#include "block/qdict.h"
#include "block/block_int.h"
#include "qemu/option.h"
#include "qemu/cutils.h"

#include "libafl/syx-snapshot/syx-snapshot.h"

#include <unistd.h>
#include <dirent.h>

#define IS_POWER_OF_TWO(x) (__builtin_popcountll(x) == 1)

typedef struct BDRVSyxCowCacheState {
    uint32_t id;
    BlockDriverState* clone_bs;
} BDRVSyxCowCacheState;

// Checks that every opened file is opened in read-only mode.
// It's a sanity check for the cow-cache mode, no file should ever be opened in
// RW mode It's because the LibAFL QEMU process can open these files multiple
// times in different processes to enable multi-core fuzzing. Another good
// side-effect is the fact that disks will never be polluted by a fuzzing run,
// the disk remains unchanged.
void syx_cow_cache_check_files_ro(void)
{
    BlockDriverState* bs = NULL;
    const BdrvChild* root = NULL;
    DIR* d;
    struct dirent* dir;
    struct stat st;
    char path[PATH_MAX] = {0};
    char real_path[PATH_MAX] = {0};

    d = opendir("/proc/self/fd");
    assert(d);

    BlockBackend* blk = NULL;
    while ((blk = blk_all_next(blk)) != NULL) {
        if ((root = blk_root(blk))) {
            bs = root->bs;
            assert(bs);

            while ((dir = readdir(d)) != NULL) {
                int fd = atoi(dir->d_name);
                if (fd > 2) {
                    assert(fstat(fd, &st) == 0);
                    if (S_ISREG(st.st_mode)) {
                        strcpy(path, "/proc/self/fd/");
                        strcat(path, dir->d_name);
                        ssize_t nb_bytes = readlink(path, real_path, PATH_MAX);
                        assert(nb_bytes > 0);
                        real_path[nb_bytes] = '\0';

                        if (!strcmp(bs->filename, real_path)) {
                            int res = fcntl(fd, F_GETFL);
                            if ((res & O_ACCMODE) != O_RDONLY) {
                                fprintf(
                                    stderr,
                                    "A file opened by QEMU is in RW mode: "
                                    "%s.\nThis is a bug, please report it.\n",
                                    bs->filename);
                                abort();
                            }
                        }
                    }
                }
            }

            rewinddir(d);
        }
    }

    closedir(d);
}

SyxCowCache* syx_cow_cache_new(void)
{
    SyxCowCache* cache = g_new0(SyxCowCache, 1);

    QTAILQ_INIT(&cache->layers);

    return cache;
}

static void syx_cow_cache_add_blk(SyxCowCache* scc)
{
    SyxCowCacheLayer* layer;
    SyxCowCacheDevice dev;

    layer = QTAILQ_FIRST(&scc->layers);
    assert(layer);

    dev.data = g_array_sized_new(false, false, layer->chunk_size,
                                 INITIAL_NB_CHUNKS_PER_DEVICE);
    dev.positions =
        g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, NULL);

    g_array_append_val(layer->blks, dev);
}

static gchar* g_array_element_ptr(GArray* array, guint position)
{
    assert(position < array->len);
    return array->data + position * g_array_get_element_size(array);
}

SyxCowCache* syx_cow_cache_push(SyxCowCache* scc, uint64_t chunk_size,
                                uint64_t max_size)
{
    SyxCowCacheLayer* new_layer = g_new0(SyxCowCacheLayer, 1);
    SyxCowCache* new_scc = syx_cow_cache_new();

    // Re-insert older layers
    SyxCowCacheLayer* layer;
    QTAILQ_FOREACH(layer, &scc->layers, next)
    {
        QTAILQ_INSERT_HEAD(&new_scc->layers, layer, next);
    }

    // Init new layer
    new_layer->blks = g_array_new(false, false, sizeof(SyxCowCacheDevice));
    new_layer->chunk_size = chunk_size;
    new_layer->max_nb_chunks = max_size;

    assert(IS_POWER_OF_TWO(chunk_size));
    assert(!(max_size % chunk_size));

    // Insert new layer at the top
    QTAILQ_INSERT_HEAD(&scc->layers, new_layer, next);

    return new_scc;
}

void syx_cow_cache_pop(SyxCowCache* scc) { assert(false && "TODO"); }

void syx_cow_cache_flush_highest_layer(SyxCowCache* scc)
{
    SyxCowCacheLayer* layer = QTAILQ_FIRST(&scc->layers);
    SyxCowCacheDevice* blk;

    for (int i = 0; i < layer->blks->len; ++i) {
        blk = &g_array_index(layer->blks, SyxCowCacheDevice, i);

        g_hash_table_remove_all(blk->positions);
        g_array_set_size(blk->data, 0);
    }
}

// Returns a pointer to chunk to read from, if it exists.
// If nothing was found in the cache for the given offset, NULL is returned.
static void* coroutine_fn get_read_chunk(SyxCowCacheDevice* sccd, int64_t aligned_offset)
{
    const int64_t chunk_size = g_array_get_element_size(sccd->data);
    assert(QEMU_IS_ALIGNED(aligned_offset, chunk_size));
    gpointer data_position = NULL;

    bool found = g_hash_table_lookup_extended(sccd->positions,
                                              GUINT_TO_POINTER(aligned_offset),
                                              NULL, &data_position);

    if (found) {
        return g_array_element_ptr(sccd->data, GPOINTER_TO_UINT(data_position));
    }

    return NULL;
}

// returns a pointer to write the chunk to.
// if it does not exist and child is non-NULL, it is prefilled with child data.
// in other words, it is guaranteed to be valid to write to the pointer with
// chunk_size bytes child should be NULL iif it is planned to fully fill the
// chunk after the call.
static void* coroutine_fn reserve_write_chunk(SyxCowCacheDevice* sccd, BdrvChild* child,
                                 int64_t aligned_offset)
{
    const int64_t chunk_size = g_array_get_element_size(sccd->data);
    assert(QEMU_IS_ALIGNED(aligned_offset, chunk_size));
    gpointer data_position = NULL;

    bool found = g_hash_table_lookup_extended(sccd->positions,
                                              GUINT_TO_POINTER(aligned_offset),
                                              NULL, &data_position);

    if (!found) {
        printf("\t\tAddr 0x%lx: not found\n", aligned_offset);
        data_position = GUINT_TO_POINTER(sccd->data->len);
        sccd->data = g_array_set_size(sccd->data, sccd->data->len + 1);
        g_hash_table_insert(sccd->positions, GUINT_TO_POINTER(aligned_offset),
                            data_position);

        if (child) {
            bdrv_co_pread(child, aligned_offset, chunk_size, data_position, 0);
        }
    }

    return g_array_element_ptr(sccd->data, GPOINTER_TO_UINT(data_position));
}

static void coroutine_fn write_chunk_to_cache_layer_device(SyxCowCacheDevice* sccd,
                                               const QEMUIOVector* qiov,
                                               BdrvChild* child,
                                               const int64_t offset, const int64_t bytes,
                                               const int64_t chunk_size)
{
    int64_t size_written = 0;
    int64_t size_to_write;
    void* data_position;
    int64_t offset_begin_aligned, offset_begin_remainder, offset_end_aligned,
        offset_end_remainder, offset_aligned_size, nb_middle_chunks;

    // chunk size should be a power of 2
    assert(IS_POWER_OF_TWO(chunk_size));

    offset_begin_aligned = ROUND_DOWN(offset, chunk_size);
    offset_begin_remainder = offset % chunk_size;

    // number of chunks that can be written without alignment issues
    nb_middle_chunks =
        (ROUND_DOWN(offset + bytes, chunk_size) - ROUND_UP(offset, chunk_size)) / chunk_size;

    offset_end_aligned = ROUND_DOWN(offset + bytes, chunk_size);
    offset_end_remainder = (offset + bytes) % chunk_size;

    // total size effectively reserved in the cache buffer
    offset_aligned_size =
        ROUND_UP(offset + bytes, chunk_size) - offset_begin_aligned;

    assert((offset_aligned_size % chunk_size) ==
           0); // aligned size should be... aligned

    // Handle unaligned start
    if (offset_begin_remainder) {
        size_to_write =
            MIN((offset_begin_aligned + chunk_size) - offset, bytes);

        data_position =
            reserve_write_chunk(sccd, child, offset_begin_aligned);

        printf("\t[unaligned begin] Chunk write @addr 0x%lx\n", offset_begin_aligned);

        qemu_iovec_to_buf(qiov, size_written,
                          data_position + offset_begin_remainder,
                          size_to_write);

        size_written += size_to_write;

        if (size_written == bytes) {
            goto end;
        }
    }

    // write every chunk until (potentially) unaligned end.
    for (int64_t i = 0; i < nb_middle_chunks; ++i) {
        printf("\tChunk write @addr 0x%lx\n", offset + size_written);

        // get cache pointer, either fresh of already allocated
        data_position =
            reserve_write_chunk(sccd, NULL, offset + size_written);

        // overwrite cache with full chunk
        assert(size_written <= qiov->size);
        qemu_iovec_to_buf(qiov, size_written, data_position, chunk_size);

        // Update size_written
        size_written += chunk_size;
    }

    // Handle unaligned end
    if (offset_end_remainder) {
        size_to_write = bytes - size_written;

        printf("\t[unaligned end] Chunk write @addr 0x%lx (size %ld)\n", offset_end_aligned, size_to_write);

        data_position =
            reserve_write_chunk(sccd, child, offset_end_aligned);

        printf("\t[unaligned end] Writting %ld bytes to data_position (offset %ld)...\n", size_to_write, size_written);
        qemu_iovec_to_buf(qiov, size_written, data_position, size_to_write);

        size_written += size_to_write;
    }

end:
    assert(size_written == bytes);
}


static void coroutine_fn read_chunk_from_cache_layer_device(SyxCowCacheDevice* sccd,
                                               QEMUIOVector* qiov,
                                               const int64_t offset, const int64_t bytes,
                                               const uint64_t chunk_size)
{
    printf("\t\tread chunk (chunk size: 0x%lx)\n", chunk_size);
    int64_t size_read = 0;
    int64_t size_to_read;
    void* data_position;
    int64_t offset_begin_aligned, offset_begin_remainder, offset_end_aligned,
        offset_end_remainder, offset_aligned_size, nb_middle_chunks;

    // chunk size should be a power of 2
    assert(IS_POWER_OF_TWO(chunk_size));

    offset_begin_aligned = ROUND_DOWN(offset, chunk_size);
    offset_begin_remainder = offset % chunk_size;

    // number of chunks that can be read without alignment issues
    nb_middle_chunks =
        (ROUND_DOWN(offset + bytes, chunk_size) - ROUND_UP(offset, chunk_size)) / chunk_size;

    offset_end_aligned = ROUND_DOWN(offset + bytes, chunk_size);
    offset_end_remainder = (offset + bytes) % chunk_size;

    // total size effectively reserved in the cache buffer
    offset_aligned_size =
        ROUND_UP(offset + bytes, chunk_size) - offset_begin_aligned;

    assert((offset_aligned_size % chunk_size) == 0); // aligned size should be... aligned

    // Handle unaligned start
    printf("A\n");
    if (offset_begin_remainder) {
        size_to_read =
            MIN((offset_begin_aligned + chunk_size) - offset, bytes);

        printf("\t[unaligned begin] Read chunk @addr 0x%lx (size %ld)\n", offset_end_aligned, size_to_read);

        data_position =
            get_read_chunk(sccd, offset_begin_aligned);

        if (data_position) {
            printf("[read] cache hit!\n");
            qemu_iovec_from_buf(qiov, size_read,
                              data_position + offset_begin_remainder,
                              size_to_read);
        }

        size_read += size_to_read;

        if (size_read == bytes) {
            return;
        }
    }

    printf("B\n");
    // write every chunk until (potentially) unaligned end.
    for (int64_t i = 0; i < nb_middle_chunks; ++i) {
        data_position =
            get_read_chunk(sccd, offset + size_read);

        printf("\tRead chunk @addr 0x%lx (size %ld)\n", offset + size_read, chunk_size);

        // Cache hit, we must update the qiov
        if (data_position) {
            // printf("cache hit!\n");
            qemu_iovec_from_buf(qiov, size_read,
                              data_position,
                              chunk_size);
        }

        size_read += chunk_size;
    }

    printf("C. offset_end_remainder: %ld. offset + bytes = %ld\n", offset_end_remainder, offset + bytes);
    // Handle unaligned end
    if (offset_end_remainder) {
        printf("READ REMINDER....\n");
        size_to_read = bytes - size_read;
        assert(size_to_read == offset_end_remainder);

        printf("\tRead chunk @addr 0x%lx (size %ld)\n", offset_end_aligned, chunk_size);

        data_position =
            get_read_chunk(sccd, offset_end_aligned);

        // Cache hit, we must update the qiov
        if (data_position) {
            printf("cache hit!\n");
            qemu_iovec_from_buf(qiov, size_read,
                              data_position,
                              size_to_read);
        }

        size_read += size_to_read;
    }

    printf("D\n");
    assert(size_read == bytes);
}

static void coroutine_fn write_chunk_to_cache_layer(SyxCowCacheLayer* sccl, BdrvChild* child, const uint32_t id,
                                        const QEMUIOVector* qiov, const int64_t offset,
                                        const int64_t bytes)
{
    assert(id < sccl->blks->len);
    SyxCowCacheDevice* cache_entry =
        &g_array_index(sccl->blks, SyxCowCacheDevice, id);
    assert(cache_entry && cache_entry->data);

    // write qiov to cached pages in current layer.
    write_chunk_to_cache_layer_device(cache_entry, qiov, child, offset, bytes,
                                       sccl->chunk_size);
}

static void coroutine_fn read_chunk_from_cache_layer(SyxCowCacheLayer* sccl, const uint32_t id,
                                        QEMUIOVector* qiov, const int64_t offset,
                                        const int64_t bytes)
{
    assert(id < sccl->blks->len);
    SyxCowCacheDevice* cache_entry =
        &g_array_index(sccl->blks, SyxCowCacheDevice, id);
    assert(cache_entry && cache_entry->data);

    // try to read cached pages in current layer if something is registered.
    read_chunk_from_cache_layer_device(cache_entry, qiov, offset, bytes,
                                       sccl->chunk_size);
}


static int syx_cow_cache_do_preadv(BlockDriverState* bs, int64_t offset, int64_t bytes,
                        QEMUIOVector* qiov, BdrvRequestFlags flags)
{
    BDRVSyxCowCacheState* s = bs->opaque;
    SyxCowCache* scc = syx_snapshot_current_scc();
    SyxCowCacheLayer* layer = QTAILQ_FIRST(&scc->layers);
    size_t qiov_sz;

    printf("[%d] Read @addr 0x%lx -> 0x%lx\n", s->id, offset, offset + bytes);
    for (int64_t i = ROUND_DOWN(offset, layer->chunk_size); i < ROUND_UP(offset + bytes, layer->chunk_size); i += layer->chunk_size) {
        printf("[%d]\tReading @addr 0x%lx\n", s->id, i);
    }


    qiov_sz = iov_size(qiov->iov, qiov->niov);
    assert(qiov_sz == qiov->size);

    assert(scc);

    // First read the backing block device normally.
    bdrv_co_preadv(bs->file, offset, bytes, qiov, flags);

    // Then fix the result with the chunks that have been written before.
    // Start from the oldest layer, and go towards most recent layers
    // use the real nb of bytes read, it could be lower than bytes with some
    // blkdevs.
    // A better strategy could be to go from top to bottom, and only read once for each sector. It is not
    // so easy though, since unaligned start / end would require special treatment. For "full" chunks, this works.
    QTAILQ_FOREACH_REVERSE(layer, &scc->layers, next)
    {
        read_chunk_from_cache_layer(layer, s->id, qiov, offset, bytes);
    }

    return bytes;
}

// ----- QEMU plug -----
// TODO: cleanup, move in another directory

static int syx_cow_cache_open(BlockDriverState* bs, QDict* options, int _flags,
                              Error** errp)
{
    BDRVSyxCowCacheState* state = bs->opaque;
    SyxCowCache* current_cache = syx_snapshot_current_scc();
    char tmp[32];

    static int ctr = 0;
    state->id = ctr++;

    assert(current_cache);
    syx_cow_cache_add_blk(current_cache);

    // Open child bdrv (files only are supported atm)
    int ret = bdrv_open_file_child(NULL, options, "file", bs, errp);
    if (ret < 0) {
        return ret;
    }

    assert(bs->file);
    assert(!strcmp(bs->file->bs->drv->format_name, "file"));
    assert(sizeof(tmp) == sizeof(bs->node_name));

    // exchange node names so that future references to the file falls back to
    // our hook
    pstrcpy(tmp, sizeof(bs->node_name), bs->node_name);
    pstrcpy(bs->node_name, sizeof(bs->node_name), bs->file->bs->node_name);
    pstrcpy(bs->file->bs->node_name, sizeof(bs->node_name), tmp);

    // child should never have 'write' or 'write_unchanged' permission
    assert(!(bs->file->perm & (BLK_PERM_WRITE | BLK_PERM_WRITE_UNCHANGED)));

    // Reuse file parameters
    bs->total_sectors = bs->file->bs->total_sectors;
    bs->supported_read_flags = bs->file->bs->supported_read_flags;
    bs->supported_write_flags =
        BDRV_REQ_WRITE_UNCHANGED |
        (BDRV_REQ_FUA & bs->file->bs->supported_write_flags);
    bs->supported_zero_flags =
        BDRV_REQ_WRITE_UNCHANGED |
        ((BDRV_REQ_FUA | BDRV_REQ_MAY_UNMAP | BDRV_REQ_NO_FALLBACK) &
         bs->file->bs->supported_zero_flags);

    printf("\t[%d] linked to %s\n", state->id, bs->file->bs->filename);

    // debug, to remove
    {
        char clone_file[PATH_MAX + 32];
        Error* err = NULL;
        sprintf(clone_file, "%s.clone", bs->file->bs->filename);
        QDict *clone_options = qdict_new();
        qdict_put_str(clone_options, "driver", "file");
        qdict_put_str(clone_options, "filename", clone_file);

        BlockDriverState* clone = bdrv_open(NULL, NULL, clone_options, BDRV_O_RDWR | BDRV_O_NOSYX, &err);
        if (clone == NULL) {
            assert(false);
        }
        state->clone_bs = clone;
    }

    return 0;
}

static void syx_cow_cache_check_bs_equality(BlockDriverState* bs, int64_t offset, int64_t bytes, int64_t chunk_size) {
    BDRVSyxCowCacheState* s = bs->opaque;
    QEMUIOVector qiov_scc, qiov_clone;

    char* buf_scc = malloc(bytes);
    qemu_iovec_init_buf(&qiov_scc, buf_scc, bytes);

    char* buf_clone = malloc(bytes);
    qemu_iovec_init_buf(&qiov_clone, buf_clone, bytes);

    printf("Comparison starts...\n");

    // read scc
    syx_cow_cache_do_preadv(bs, offset, bytes, &qiov_scc, 0);

    // read clone
    s->clone_bs->drv->bdrv_co_preadv(s->clone_bs, offset, bytes, &qiov_clone, 0);

    // compare QIOVs
    char* buf_scc_out = malloc(bytes);
    qemu_iovec_to_buf(&qiov_scc, 0, buf_scc_out, bytes);

    char* buf_clone_out = malloc(bytes);
    qemu_iovec_to_buf(&qiov_clone, 0, buf_clone_out, bytes);

    if (memcmp(buf_scc_out, buf_clone_out, bytes) != 0) {
        for (int i = 0; i < bytes; ++i) {
            if (buf_scc_out[i] != buf_clone_out[i]) {
                printf("\t\tdifference on chunk 0x%lx\n", ROUND_DOWN(offset + i, chunk_size));
            }
        }

        // assert(false && "Bug in the syx-cow-cache bdrv found.");
        printf("Bug in the syx-cow-cache bdrv found.\n");
        while (true) {
            sleep(1);
        }
    }

    printf("Comparison successful.\n");

    free(buf_scc);
    free(buf_clone);
    free(buf_scc_out);
    free(buf_clone_out);
}

static int coroutine_fn GRAPH_RDLOCK
syx_cow_cache_co_preadv(BlockDriverState* bs, int64_t offset, int64_t bytes,
                        QEMUIOVector* qiov, BdrvRequestFlags flags)
{
    SyxCowCache* scc = syx_snapshot_current_scc();
    SyxCowCacheLayer* layer = QTAILQ_FIRST(&scc->layers);

    syx_cow_cache_check_bs_equality(bs, offset, bytes, layer->chunk_size);

    syx_cow_cache_do_preadv(bs, offset, bytes, qiov, flags);

    // debug, to remove...
    syx_cow_cache_check_bs_equality(bs, offset, bytes, layer->chunk_size);

    return bytes;
}

static int coroutine_fn GRAPH_RDLOCK
syx_cow_cache_co_pwritev(BlockDriverState* bs, int64_t offset, int64_t bytes,
                         QEMUIOVector* qiov, BdrvRequestFlags flags)
{
    BDRVSyxCowCacheState* s = bs->opaque;
    SyxCowCache* scc = syx_snapshot_current_scc();
    SyxCowCacheLayer* layer;

    layer = QTAILQ_FIRST(&scc->layers);
    assert(layer);

    for (int64_t i = ROUND_DOWN(offset, layer->chunk_size); i < ROUND_UP(offset + bytes, layer->chunk_size); i += layer->chunk_size) {
        printf("[%d]\tWriting chunk @ 0x%lx\n", s->id, i);
    }

    syx_cow_cache_check_bs_equality(bs, offset, bytes, layer->chunk_size);

    s->clone_bs->drv->bdrv_co_pwritev(s->clone_bs, offset, bytes, qiov, flags);

    write_chunk_to_cache_layer(layer, bs->file, s->id, qiov, offset, bytes);

    syx_cow_cache_check_bs_equality(bs, offset, bytes, layer->chunk_size);

    return bytes;
}

static int coroutine_fn GRAPH_RDLOCK syx_cow_cache_co_pwrite_zeroes(
    BlockDriverState* bs, int64_t offset, int64_t bytes, BdrvRequestFlags flags)
{
    printf("WRITE ZEROES\n");
    abort();
    return bdrv_co_pwrite_zeroes(bs->file, offset, bytes, flags);
}

static int coroutine_fn GRAPH_RDLOCK
syx_cow_cache_co_flush(BlockDriverState* bs)
{
    if (!bs->file) {
        return 0;
    }

    return bdrv_co_flush(bs->file->bs);
}

static int coroutine_fn GRAPH_RDLOCK
syx_cow_cache_co_pdiscard(BlockDriverState* bs, int64_t offset, int64_t bytes)
{
    return bdrv_co_pdiscard(bs->file, offset, bytes);
}

static void GRAPH_RDLOCK
syx_cow_cache_child_perm(BlockDriverState* bs, BdrvChild* c, BdrvChildRole role,
                         BlockReopenQueue* reopen_queue, uint64_t perm,
                         uint64_t shared, uint64_t* nperm, uint64_t* nshared)
{
    assert(role & BDRV_CHILD_FILTERED);

    bdrv_default_perms(bs, c, role, reopen_queue, perm, shared, nperm, nshared);

    // We do not need 'write' and 'write_unchanged' permissions, the child is
    // read-only anyway.
    // *nperm &= ~(BLK_PERM_WRITE | BLK_PERM_WRITE_UNCHANGED);
}

static BlockDriver bdrv_syx_cow_cache = {
    .format_name = "syx-cow-cache",
    .instance_size = sizeof(BDRVSyxCowCacheState),

    .bdrv_open = syx_cow_cache_open,
    // .bdrv_close                 = syx_cow_cache_close,

    .bdrv_co_preadv = syx_cow_cache_co_preadv,
    .bdrv_co_pwritev = syx_cow_cache_co_pwritev,
    .bdrv_co_pwrite_zeroes = syx_cow_cache_co_pwrite_zeroes,
    .bdrv_co_pdiscard = syx_cow_cache_co_pdiscard,
    .bdrv_co_flush = syx_cow_cache_co_flush,

    // .bdrv_co_preadv_snapshot       = syx_cow_cache_co_preadv_snapshot,
    // .bdrv_co_pdiscard_snapshot     = syx_cow_cache_co_pdiscard_snapshot,
    // .bdrv_co_snapshot_block_status = syx_cow_cache_co_snapshot_block_status,

    // .bdrv_refresh_filename      = syx_cow_cache_refresh_filename,

    .bdrv_child_perm = syx_cow_cache_child_perm,

    .is_filter = true,
};

static void syx_cow_cache_init(void) { bdrv_register(&bdrv_syx_cow_cache); }

block_init(syx_cow_cache_init);
