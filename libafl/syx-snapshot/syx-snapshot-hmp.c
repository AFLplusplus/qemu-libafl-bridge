#include "libafl/syx-snapshot/syx-snapshot.h"
#include "libafl/syx-snapshot/device-save.h"
//#include "syx-snapshot/syx-snapshot-hmp.h"

// Static snapshot variable
static SyxSnapshot* current_snapshot = NULL;




/**
 * Create a new snapshot and store it in the static variable.
 */
void hmp_syx_snapshot_new(Monitor *mon, const QDict *qdict) {

    if (current_snapshot != NULL) {
        syx_snapshot_free(current_snapshot);
    }
    current_snapshot = syx_snapshot_new(true, false, DEVICE_SNAPSHOT_ALL, NULL);
}

void hmp_syx_snapshot_init(Monitor *mon, const QDict *qdict) {
    syx_snapshot_init(false);
}

    /**
     * Restore the root snapshot from the static variable.
     */
    void hmp_syx_snapshot_root_restore(Monitor * mon, const QDict* qdict)
    {
        if (current_snapshot != NULL) {
            syx_snapshot_root_restore(current_snapshot);
        }
}