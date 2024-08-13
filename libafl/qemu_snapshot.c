#include "libafl/qemu_snapshot.h"

#include "sysemu/runstate.h"
#include "migration/snapshot.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "hw/core/cpu.h"
#include "sysemu/hw_accel.h"
#include <stdlib.h>
#include <string.h>

static void save_snapshot_cb(void* opaque)
{
    char* name = (char*)opaque;
    Error* err = NULL;
    if (!save_snapshot(name, true, NULL, false, NULL, &err)) {
        error_report_err(err);
        error_report("Could not save snapshot");
    }
    free(opaque);
}

static void load_snapshot_cb(void* opaque)
{
    char* name = (char*)opaque;
    Error* err = NULL;

    int saved_vm_running = runstate_is_running();
    vm_stop(RUN_STATE_RESTORE_VM);

    bool loaded = load_snapshot(name, NULL, false, NULL, &err);

    if (!loaded) {
        error_report_err(err);
        error_report("Could not load snapshot");
    }
    if (loaded && saved_vm_running) {
        vm_start();
    }
    free(opaque);
}

void libafl_save_qemu_snapshot(char* name, bool sync)
{
    // use snapshots synchronously, use if main loop is not running
    if (sync) {
        // TODO: eliminate this code duplication
        // by passing a heap-allocated buffer from rust to c,
        // which c needs to free
        Error* err = NULL;
        if (!save_snapshot(name, true, NULL, false, NULL, &err)) {
            error_report_err(err);
            error_report("Could not save snapshot");
        }
        return;
    }
    char* name_buffer = malloc(strlen(name) + 1);
    strcpy(name_buffer, name);
    aio_bh_schedule_oneshot_full(qemu_get_aio_context(), save_snapshot_cb,
                                 (void*)name_buffer, "save_snapshot");
}

void libafl_load_qemu_snapshot(char* name, bool sync)
{
    // use snapshots synchronously, use if main loop is not running
    if (sync) {
        // TODO: see libafl_save_qemu_snapshot
        Error* err = NULL;

        int saved_vm_running = runstate_is_running();
        vm_stop(RUN_STATE_RESTORE_VM);

        bool loaded = load_snapshot(name, NULL, false, NULL, &err);

        if (!loaded) {
            error_report_err(err);
            error_report("Could not load snapshot");
        }
        if (loaded && saved_vm_running) {
            vm_start();
        }
        return;
    }
    char* name_buffer = malloc(strlen(name) + 1);
    strcpy(name_buffer, name);
    aio_bh_schedule_oneshot_full(qemu_get_aio_context(), load_snapshot_cb,
                                 (void*)name_buffer, "load_snapshot");
}
