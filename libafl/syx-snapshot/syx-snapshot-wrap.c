#include "qemu/osdep.h"
#include "libafl/syx-snapshot/syx-snapshot.h"

bool syx_snapshot_cow_cache_read_entry(BlockBackend* blk, int64_t offset,
                                       int64_t bytes, QEMUIOVector* qiov,
                                       size_t qiov_offset,
                                       BdrvRequestFlags flags) {
    return false;
}

bool syx_snapshot_cow_cache_write_entry(BlockBackend* blk, int64_t offset,
                                        int64_t bytes, QEMUIOVector* qiov,
                                        size_t qiov_offset,
                                        BdrvRequestFlags flags) {
    return false;
}