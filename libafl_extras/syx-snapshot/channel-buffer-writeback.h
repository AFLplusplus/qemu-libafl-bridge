#pragma once

#include "qemu/osdep.h"
#include "migration/qemu-file.h"

#include "io/channel.h"
#include "qom/object.h"

#define QEMU_FILE_RAM_LIMIT (32 * 1024 * 1024)

#define TYPE_QIO_CHANNEL_BUFFER_WRITEBACK "qio-channel-buffer-writeback"
OBJECT_DECLARE_SIMPLE_TYPE(QIOChannelBufferWriteback, QIO_CHANNEL_BUFFER_WRITEBACK)

struct QIOChannelBufferWriteback {
    QIOChannel parent;

    size_t capacity;
    size_t usage;
    size_t offset;
    uint8_t* data;

    uint8_t* writeback_buf;
    size_t writeback_buf_capacity;
    size_t* writeback_buf_usage;

    bool internal_allocation;
};

QIOChannelBufferWriteback* qio_channel_buffer_writeback_new(size_t capacity, uint8_t* writeback_buf, size_t writeback_buf_capacity, size_t* writeback_buf_usage);

/**
 * qio_channel_buffer_new_external:
 * @buf: the buffer used
 * @capacity: the total capacity of the underlying buffer
 * @usage: The size actually used by the buffer
 */
QIOChannelBufferWriteback*
qio_channel_buffer_writeback_new_external(uint8_t* buf, size_t capacity, size_t usage, uint8_t* writeback_buf, size_t writeback_buf_capacity, size_t* writeback_buf_usage);
