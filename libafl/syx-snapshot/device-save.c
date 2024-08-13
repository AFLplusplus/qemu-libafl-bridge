#include "qemu/osdep.h"

#include "migration/qemu-file.h"
#include "io/channel-buffer.h"
#include "migration/vmstate.h"
#include "qemu/main-loop.h"

#include "libafl/syx-misc.h"
#include "libafl/syx-snapshot/channel-buffer-writeback.h"
#include "libafl/syx-snapshot/device-save.h"

#include "migration/savevm.h"

extern SaveState savevm_state;
extern int vmstate_save(QEMUFile* f, SaveStateEntry* se, JSONWriter* vmdesc);

static bool libafl_restoring_devices = false;

bool libafl_devices_is_restoring(void) { return libafl_restoring_devices; }

// iothread must be locked
DeviceSaveState* device_save_all(void)
{
    return device_save_kind(DEVICE_SNAPSHOT_ALL, NULL);
}

static int is_in_list(char* str, char** list)
{
    while (*list) {
        if (!strcmp(str, *list)) {
            return 1;
        }
        list++;
    }
    return 0;
}

DeviceSaveState* device_save_kind(DeviceSnapshotKind kind, char** names)
{
    DeviceSaveState* dss = g_new0(DeviceSaveState, 1);
    SaveStateEntry* se;

    dss->kind = DEVICE_SAVE_KIND_FULL;
    dss->save_buffer = g_new(uint8_t, QEMU_FILE_RAM_LIMIT);

    QIOChannelBufferWriteback* wbioc = qio_channel_buffer_writeback_new(
        QEMU_FILE_RAM_LIMIT, dss->save_buffer, QEMU_FILE_RAM_LIMIT,
        &dss->save_buffer_size);
    QIOChannel* ioc = QIO_CHANNEL(wbioc);

    QEMUFile* f = qemu_file_new_output(ioc);

    QTAILQ_FOREACH(se, &savevm_state.handlers, entry)
    {
        int ret;

        if (se->is_ram) {
            continue;
        }
        if (!strcmp(se->idstr, "globalstate")) {
            continue;
        }
        switch (kind) {
        case DEVICE_SNAPSHOT_ALLOWLIST:
            if (!is_in_list(se->idstr, names)) {
                continue;
            }
            break;
        case DEVICE_SNAPSHOT_DENYLIST:
            if (is_in_list(se->idstr, names)) {
                continue;
            }
            break;
        default:
            break;
        }

        // SYX_PRINTF("Saving section %s...\n", se->idstr);

        ret = vmstate_save(f, se, NULL);

        if (ret) {
            SYX_PRINTF("Device save all error: %d\n", ret);
            abort();
        }
    }

    qemu_put_byte(f, QEMU_VM_EOF);

    qemu_fclose(f);

    return dss;
}

void device_restore_all(DeviceSaveState* dss)
{
    assert(dss->save_buffer != NULL);

    QIOChannelBuffer* bioc = qio_channel_buffer_new_external(
        dss->save_buffer, QEMU_FILE_RAM_LIMIT, dss->save_buffer_size);
    QIOChannel* ioc = QIO_CHANNEL(bioc);

    QEMUFile* f = qemu_file_new_input(ioc);

    bool save_libafl_restoring_devices = libafl_restoring_devices;
    libafl_restoring_devices = true;

    qemu_load_device_state(f);

    libafl_restoring_devices = save_libafl_restoring_devices;

    object_unref(OBJECT(bioc));
    qemu_fclose(f);
}

void device_free_all(DeviceSaveState* dss) { g_free(dss->save_buffer); }

char** device_list_all(void)
{
    SaveStateEntry* se;

    size_t size = 1;
    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) { size++; }

    char** list = malloc(size * sizeof(char*));
    size_t i = 0;
    QTAILQ_FOREACH(se, &savevm_state.handlers, entry)
    {
        if (se->is_ram) {
            continue;
        }
        if (!strcmp(se->idstr, "globalstate")) {
            continue;
        }

        list[i] = se->idstr;
        i++;
    }
    list[i] = NULL;

    return list;
}
