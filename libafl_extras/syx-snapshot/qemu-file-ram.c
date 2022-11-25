#include "qemu/osdep.h"
#include "migration/qemu-file.h"
#include "qemu-file-ram.h"

// TODO QIOChannelBuffer and the new qemu_file_new_output

static ssize_t qemu_file_ram_get_buffer(void* opaque, uint8_t* buf, int64_t pos, size_t size, Error **errp);
static int qemu_file_ram_close_and_save(void* opaque, Error **errp);
static int qemu_file_ram_close(void* opaque, Error **errp);
static ssize_t qemu_file_ram_writev(void *opaque, struct iovec *iov, int iovcnt, int64_t pos, Error **errp);

typedef struct QEMUFileRAM_opaque_s {
    uint64_t pos;
    uint8_t* buf;
    uint64_t buf_len;

    uint8_t* output_buf;
    uint64_t output_buf_len;
} QEMUFileRAM_opaque_t;

QEMUFileOps QEMUFileRamOps_read = {
    .get_buffer = qemu_file_ram_get_buffer,
    .close = qemu_file_ram_close,
};

QEMUFileOps QEMUFileRamOps_write = {
    .writev_buffer = qemu_file_ram_writev,
    .close = qemu_file_ram_close_and_save,
};

static ssize_t qemu_file_ram_get_buffer(void* opaque, uint8_t* buf, int64_t pos, size_t size, Error **errp) {
    QEMUFileRAM_opaque_t* qemu_file_opaque = (QEMUFileRAM_opaque_t*) opaque;

    memcpy(buf, (void*)(qemu_file_opaque->buf + pos), size);

    return size;
}

static int qemu_file_ram_close_and_save(void* opaque, Error **errp) {
    QEMUFileRAM_opaque_t* qemu_file_opaque = (QEMUFileRAM_opaque_t*) opaque;

    assert(qemu_file_opaque->output_buf_len >= qemu_file_opaque->pos);

    memcpy((void*) (qemu_file_opaque->output_buf), qemu_file_opaque->buf, qemu_file_opaque->pos);
    qemu_file_opaque->output_buf_len = qemu_file_opaque->pos;

    return 0;
}

static int qemu_file_ram_close(void* opaque, Error **errp) {
    return 0;
}


static ssize_t qemu_file_ram_writev(void *opaque, struct iovec *iov, int iovcnt, int64_t pos, Error **errp) {
    QEMUFileRAM_opaque_t* qemu_file_opaque = (QEMUFileRAM_opaque_t*) opaque;
    ssize_t total_written = 0;

    for (int iov_idx = 0; iov_idx < iovcnt; ++iov_idx) {
        assert(qemu_file_opaque->pos + iov[iov_idx].iov_len <= qemu_file_opaque->buf_len);
        memcpy(qemu_file_opaque->buf + qemu_file_opaque->pos, iov[iov_idx].iov_base, iov[iov_idx].iov_len);
        qemu_file_opaque->pos += iov[iov_idx].iov_len;
        total_written += iov[iov_idx].iov_len;
    }

    return total_written;
}


QEMUFile* qemu_file_ram_read_new(uint8_t* input_buf, uint64_t input_buf_len) {
    QEMUFileRAM_opaque_t* opaque = g_new0(QEMUFileRAM_opaque_t, 1);

    opaque->buf = input_buf;
    opaque->buf_len = input_buf_len;

    return qemu_fopen_ops(opaque, &QEMUFileRamOps_read);
}

QEMUFile* qemu_file_ram_write_new(uint8_t* output_buf, uint64_t output_buf_len) {
    QEMUFileRAM_opaque_t* opaque = g_new0(QEMUFileRAM_opaque_t, 1);

    opaque->buf = g_new0(uint8_t, QEMU_FILE_RAM_LIMIT);
    opaque->buf_len = QEMU_FILE_RAM_LIMIT;
    opaque->output_buf = output_buf;
    opaque->output_buf_len = output_buf_len;

    return qemu_fopen_ops(opaque, &QEMUFileRamOps_write);
}
