#include "qemu/osdep.h"
#include "migration/qemu-file.h"
#include "channel-buffer-writeback.h"
#include "../syx-misc.h"

QIOChannelBufferWriteback* qio_channel_buffer_writeback_new(size_t capacity, uint8_t* writeback_buf, size_t writeback_buf_capacity, size_t* writeback_buf_usage) {
    assert(writeback_buf != NULL);
    assert(writeback_buf_usage != NULL);

    QIOChannelBufferWriteback *ioc;

    ioc = QIO_CHANNEL_BUFFER_WRITEBACK(object_new(TYPE_QIO_CHANNEL_BUFFER_WRITEBACK));

    assert(writeback_buf != NULL);

    if (capacity) {
        ioc->data = g_new0(uint8_t, capacity);
        ioc->capacity = capacity;
        ioc->internal_allocation = true;
    }

    ioc->writeback_buf = writeback_buf;
    ioc->writeback_buf_capacity = writeback_buf_capacity;
    ioc->writeback_buf_usage = writeback_buf_usage;

    return ioc;
}

QIOChannelBufferWriteback*
qio_channel_buffer_writeback_new_external(uint8_t* buf, size_t capacity, size_t usage, uint8_t* writeback_buf, size_t writeback_buf_capacity, size_t* writeback_buf_usage) {
    assert(buf != NULL);
    assert(usage <= capacity);
    assert(writeback_buf != NULL);
    assert(writeback_buf_usage != NULL);

    QIOChannelBufferWriteback *ioc;

    ioc = QIO_CHANNEL_BUFFER_WRITEBACK(object_new(TYPE_QIO_CHANNEL_BUFFER_WRITEBACK));

    assert(writeback_buf != NULL);

    ioc->data = buf;
    ioc->capacity = capacity;
    ioc->usage = usage;
    ioc->internal_allocation = false;

    ioc->writeback_buf = writeback_buf;
    ioc->writeback_buf_capacity = writeback_buf_capacity;
    ioc->writeback_buf_usage = writeback_buf_usage;

    return ioc;
}


static void qio_channel_buffer_writeback_finalize(Object *obj) {
    QIOChannelBufferWriteback* bwioc = QIO_CHANNEL_BUFFER_WRITEBACK(obj);

    assert(bwioc->writeback_buf_capacity >= bwioc->usage);

    if (bwioc->writeback_buf) {
        memcpy(bwioc->writeback_buf, bwioc->data, bwioc->usage);
        *(bwioc->writeback_buf_usage) = bwioc->usage;
    }

    if (bwioc->internal_allocation) {
        g_free(bwioc->data);
        bwioc->data = NULL;
        bwioc->capacity = bwioc->usage = bwioc->offset = 0;
    }
}


static ssize_t qio_channel_buffer_writeback_readv(QIOChannel *ioc,
                                                    const struct iovec *iov,
                                                    size_t niov,
                                                    int **fds,
                                                    size_t *nfds,
                                                    int flags,
                                                    Error **errp)
{
    QIOChannelBufferWriteback* bwioc = QIO_CHANNEL_BUFFER_WRITEBACK(ioc);
    ssize_t ret = 0;
    size_t i;

    for (i = 0; i < niov; i++) {
        size_t want = iov[i].iov_len;

        if (bwioc->offset >= bwioc->usage) {
            break;
        }

        if ((bwioc->offset + want) > bwioc->usage)  {
            want = bwioc->usage - bwioc->offset;
        }

        memcpy(iov[i].iov_base, bwioc->data + bwioc->offset, want);
        ret += want;
        bwioc->offset += want;
    }

    return ret;
}

static ssize_t qio_channel_buffer_writeback_writev(QIOChannel* ioc,
                                    const struct iovec *iov,
                                    size_t niov,
                                    int *fds,
                                    size_t nfds,
                                    int flags,
                                    Error **errp)
{
    QIOChannelBufferWriteback* bwioc = QIO_CHANNEL_BUFFER_WRITEBACK(ioc);
    ssize_t ret = 0;
    size_t i;
    size_t towrite = 0;

    for (i = 0; i < niov; i++) {
        towrite += iov[i].iov_len;
    }

    assert(bwioc->offset + towrite <= bwioc->capacity);
    assert(bwioc->offset <= bwioc->usage);

    for (i = 0; i < niov; i++) {
        memcpy(bwioc->data + bwioc->offset,
               iov[i].iov_base,
               iov[i].iov_len);
        bwioc->offset += iov[i].iov_len;
        bwioc->usage += iov[i].iov_len;
        ret += iov[i].iov_len;
    }

    return ret;
}

static int qio_channel_buffer_writeback_set_blocking(QIOChannel *ioc G_GNUC_UNUSED,
                                           bool enabled G_GNUC_UNUSED,
                                           Error **errp G_GNUC_UNUSED)
{
    return 0;
}

static off_t qio_channel_buffer_writeback_seek(QIOChannel *ioc,
                                     off_t offset,
                                     int whence,
                                     Error **errp)
{
    QIOChannelBufferWriteback *bwioc = QIO_CHANNEL_BUFFER_WRITEBACK(ioc);
    off_t new_pos;

    switch(whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = (off_t) bwioc->offset + offset;
            break;
        case SEEK_END:
            new_pos = (off_t) bwioc->usage + offset;
            break;
        default:
            assert(false);
    }

    assert(new_pos >= 0 && new_pos <= bwioc->usage);

    bwioc->offset = new_pos;

    return new_pos;
}

static int qio_channel_buffer_writeback_close(QIOChannel *ioc,
                                              Error **errp)
{
    QIOChannelBufferWriteback* bwioc = QIO_CHANNEL_BUFFER_WRITEBACK(ioc);

    assert(bwioc->writeback_buf_capacity >= bwioc->usage);

    if (bwioc->writeback_buf) {
        memcpy(bwioc->writeback_buf, bwioc->data, bwioc->usage);
        *(bwioc->writeback_buf_usage) = bwioc->usage;
    }

    if (bwioc->internal_allocation) {
        g_free(bwioc->data);
        bwioc->data = NULL;
        bwioc->capacity = bwioc->usage = bwioc->offset = 0;
    }

    return 0;
}

typedef struct QIOChannelBufferWritebackSource QIOChannelBufferWritebackSource;
struct QIOChannelBufferWritebackSource {
    GSource parent;
    QIOChannelBufferWriteback *bioc;
    GIOCondition condition;
};

static gboolean
qio_channel_buffer_writeback_source_prepare(GSource *source,
                                  gint *timeout)
{
    QIOChannelBufferWritebackSource *bsource = (QIOChannelBufferWritebackSource *)source;

    *timeout = -1;

    return (G_IO_IN | G_IO_OUT) & bsource->condition;
}

static gboolean
qio_channel_buffer_writeback_source_check(GSource *source)
{
    QIOChannelBufferWritebackSource *bsource = (QIOChannelBufferWritebackSource *)source;

    return (G_IO_IN | G_IO_OUT) & bsource->condition;
}

static gboolean
qio_channel_buffer_writeback_source_dispatch(GSource *source,
                                   GSourceFunc callback,
                                   gpointer user_data)
{
    QIOChannelFunc func = (QIOChannelFunc)callback;
    QIOChannelBufferWritebackSource *bsource = (QIOChannelBufferWritebackSource *)source;

    return (*func)(QIO_CHANNEL(bsource->bioc),
                   ((G_IO_IN | G_IO_OUT) & bsource->condition),
                   user_data);
}

static void
qio_channel_buffer_writeback_source_finalize(GSource *source)
{
    QIOChannelBufferWritebackSource *ssource = (QIOChannelBufferWritebackSource *)source;

    object_unref(OBJECT(ssource->bioc));
}

GSourceFuncs qio_channel_buffer_writeback_source_funcs = {
        qio_channel_buffer_writeback_source_prepare,
        qio_channel_buffer_writeback_source_check,
        qio_channel_buffer_writeback_source_dispatch,
        qio_channel_buffer_writeback_source_finalize
};

static GSource *qio_channel_buffer_writeback_create_watch(QIOChannel *ioc,
                                                GIOCondition condition)
{
    QIOChannelBufferWriteback *bioc = QIO_CHANNEL_BUFFER_WRITEBACK(ioc);
    QIOChannelBufferWritebackSource *ssource;
    GSource *source;

    source = g_source_new(&qio_channel_buffer_writeback_source_funcs,
                          sizeof(QIOChannelBufferWritebackSource));
    ssource = (QIOChannelBufferWritebackSource *)source;

    ssource->bioc = bioc;
    object_ref(OBJECT(bioc));

    ssource->condition = condition;

    return source;
}

static void qio_channel_buffer_writeback_class_init(ObjectClass *klass,
                                          void *class_data G_GNUC_UNUSED)
{
    QIOChannelClass *ioc_klass = QIO_CHANNEL_CLASS(klass);

    ioc_klass->io_writev = qio_channel_buffer_writeback_writev;
    ioc_klass->io_readv = qio_channel_buffer_writeback_readv;
    ioc_klass->io_set_blocking = qio_channel_buffer_writeback_set_blocking;
    ioc_klass->io_seek = qio_channel_buffer_writeback_seek;
    ioc_klass->io_close = qio_channel_buffer_writeback_close;
    ioc_klass->io_create_watch = qio_channel_buffer_writeback_create_watch;
}

static const TypeInfo qio_channel_buffer_writeback_info = {
        .parent = TYPE_QIO_CHANNEL,
        .name = TYPE_QIO_CHANNEL_BUFFER_WRITEBACK,
        .instance_size = sizeof(QIOChannelBufferWriteback),
        .instance_finalize = qio_channel_buffer_writeback_finalize,
        .class_init = qio_channel_buffer_writeback_class_init,
};

static void qio_channel_buffer_writeback_register_types(void)
{
    type_register_static(&qio_channel_buffer_writeback_info);
}

type_init(qio_channel_buffer_writeback_register_types);
