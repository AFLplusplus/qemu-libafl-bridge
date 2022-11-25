#include "qemu/osdep.h"
#include "device-save.h"
#include "migration/qemu-file.h"
#include "qemu-file-ram.h"
#include "migration/vmstate.h"
#include "qemu/main-loop.h"
#include "libafl_extras/syx-misc.h"

#include "migration/savevm.h"

///// From migration/savevm.c

#include "qapi/qapi-commands-migration.h"
#include "migration/vmstate.h"
#include "migration/register.h"
#include "qemu/uuid.h"

typedef struct CompatEntry {
    char idstr[256];
    int instance_id;
} CompatEntry;

typedef struct SaveStateEntry {
    QTAILQ_ENTRY(SaveStateEntry) entry;
    char idstr[256];
    uint32_t instance_id;
    int alias_id;
    int version_id;
    /* version id read from the stream */
    int load_version_id;
    int section_id;
    /* section id read from the stream */
    int load_section_id;
    const SaveVMHandlers *ops;
    const VMStateDescription *vmsd;
    void *opaque;
    CompatEntry *compat;
    int is_ram;
} SaveStateEntry;

typedef struct SaveState {
    QTAILQ_HEAD(, SaveStateEntry) handlers;
    SaveStateEntry *handler_pri_head[MIG_PRI_MAX + 1];
    int global_section_id;
    uint32_t len;
    const char *name;
    uint32_t target_page_bits;
    uint32_t caps_count;
    MigrationCapability *capabilities;
    QemuUUID uuid;
} SaveState;

int libafl_vmstate_save(QEMUFile *f, SaveStateEntry *se,
                        JSONWriter *vmdesc);

///// End migration/savevm.c

extern SaveState savevm_state;

extern void save_section_header(QEMUFile *f, SaveStateEntry *se, uint8_t section_type);
extern void save_section_footer(QEMUFile *f, SaveStateEntry *se);

// iothread must be locked
device_save_state_t* device_save_all(void) {
    device_save_state_t* dss = g_new0(device_save_state_t, 1);
    SaveStateEntry *se;

    dss->kind = DEVICE_SAVE_KIND_FULL;
    dss->save_buffer = g_new0(uint8_t, QEMU_FILE_RAM_LIMIT);

    QEMUFile* f = qemu_file_ram_write_new(dss->save_buffer, QEMU_FILE_RAM_LIMIT);
    
    QTAILQ_FOREACH(se, &savevm_state.handlers, entry) {
        int ret;

        if (se->is_ram) {
            continue;
        }
        if ((!se->ops || !se->ops->save_state) && !se->vmsd) {
            continue;
        }
        if (se->vmsd && !vmstate_save_needed(se->vmsd, se->opaque)) {
            continue;
        }
        if (!strcmp(se->idstr, "globalstate")) {
            continue;
        }

        // SYX_PRINTF("Saving section %s...\n", se->idstr);

        save_section_header(f, se, QEMU_VM_SECTION_FULL);

        ret = libafl_vmstate_save(f, se, NULL);

        if (ret) {
            SYX_PRINTF("Device save all error: %d\n", ret);
            abort();
        }

        save_section_footer(f, se);
    }

    printf("\n");

    qemu_put_byte(f, QEMU_VM_EOF);

    qemu_fclose(f);

    return dss;
}

void device_restore_all(device_save_state_t* device_save_state) {
	bool must_unlock_iothread = false;
    QEMUFile* f = qemu_file_ram_read_new(device_save_state->save_buffer, QEMU_FILE_RAM_LIMIT);
    
	if (!qemu_mutex_iothread_locked()) {
		qemu_mutex_lock_iothread();
		must_unlock_iothread = true;
	}

    qemu_load_device_state(f);
 
	if (must_unlock_iothread) {
		qemu_mutex_unlock_iothread();
	}

    qemu_fclose(f);
}

void device_free_all(device_save_state_t* dss) {
    g_free(dss->save_buffer);
}
