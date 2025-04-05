
#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qapi/qmp/qdict.h"
#include "qemu/option.h"
#include "qemu/main-loop.h"
#include "block/qdict.h"
#include "libafl/system.h"



#define NOT_DONE 0x7fffffff

static void blk_rw_done(void *opaque, int ret)
{
    *(int *)opaque = ret;
}

int libafl_blk_write(BlockBackend *blk, void *buf, int64_t offset, int64_t sz)
{
	void *pattern_buf = NULL;
	QEMUIOVector qiov;
	int async_ret = NOT_DONE;

	qemu_iovec_init(&qiov, 1);
	qemu_iovec_add(&qiov, buf, sz);

	blk_aio_pwritev(blk, offset, &qiov, 0, blk_rw_done, &async_ret);
	while (async_ret == NOT_DONE) {
		main_loop_wait(false);
	}

	//printf("async_ret: %d\n", async_ret);
	//g_assert(async_ret == 0);

	g_free(pattern_buf);
	qemu_iovec_destroy(&qiov);
    return async_ret;
}