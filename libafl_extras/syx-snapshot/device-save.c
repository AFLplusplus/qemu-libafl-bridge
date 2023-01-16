#include "qemu/osdep.h"
#include "device-save.h"
#include "migration/qemu-file.h"
#include "migration/vmstate.h"
#include "qemu/main-loop.h"
#include "libafl_extras/syx-misc.h"

#include "migration/savevm.h"

#define QEMU_FILE_RAM_LIMIT (32 * 1024 * 1024)

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

///// End migration/savevm.c

int libafl_restoring_devices;

extern SaveState savevm_state;

void save_section_header(QEMUFile *f, SaveStateEntry *se, uint8_t section_type);
void save_section_footer(QEMUFile *f, SaveStateEntry *se);
int vmstate_save(QEMUFile *f, SaveStateEntry *se, JSONWriter *vmdesc);

// iothread must be locked
device_save_state_t* device_save_all(void) {
    device_save_state_t* dss = g_new0(device_save_state_t, 1);
    SaveStateEntry *se;

    dss->kind = DEVICE_SAVE_KIND_FULL;
    dss->save_buffer = qio_channel_buffer_new(QEMU_FILE_RAM_LIMIT);

    QEMUFile* f = qemu_file_new_output(QIO_CHANNEL(dss->save_buffer));
    
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

        // printf("Saving section %p %s...\n", se, se->idstr);

        save_section_header(f, se, QEMU_VM_SECTION_FULL);

        ret = vmstate_save(f, se, NULL);

        if (ret) {
            SYX_PRINTF("Device save all error: %d\n", ret);
            abort();
        }

        save_section_footer(f, se);
    }

    printf("\n");

    qemu_put_byte(f, QEMU_VM_EOF);

    qemu_fflush(f);

    // fclose will call io_close and free device_save_state->save_buffer, don't do that
    //qemu_fclose(f);

    return dss;
}

void device_restore_all(device_save_state_t* dss) {
	  bool must_unlock_iothread = false;
	  
	  Error* errp = NULL;
	  qio_channel_io_seek(QIO_CHANNEL(dss->save_buffer), 0, SEEK_SET, &errp);
	  
    if(!dss->save_file) {
      dss->save_file = qemu_file_new_input(QIO_CHANNEL(dss->save_buffer));
    }
    
	  if (!qemu_mutex_iothread_locked()) {
		  qemu_mutex_lock_iothread();
		  must_unlock_iothread = true;
	  }
	  
	  int save_libafl_restoring_devices = libafl_restoring_devices;
	  libafl_restoring_devices = 1;

    qemu_load_device_state(dss->save_file);
 
    libafl_restoring_devices = save_libafl_restoring_devices;
 
	  if (must_unlock_iothread) {
		  qemu_mutex_unlock_iothread();
	  }
	  
    // qemu_fclose(f);
}

void device_free_all(device_save_state_t* dss) {
    // g_free(dss->save_buffer);
    Error* errp = NULL;
    qio_channel_close(QIO_CHANNEL(dss->save_buffer), &errp);
    object_unref(OBJECT(dss->save_buffer));
    if (dss->save_file)
      qemu_fclose(dss->save_file);
}
