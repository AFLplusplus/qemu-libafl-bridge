#pragma once

#include "qemu/osdep.h"
#include "migration/qemu-file.h"

#define QEMU_FILE_RAM_LIMIT (32 * 1024 * 1024)

QEMUFile* qemu_file_ram_read_new(uint8_t* input_buf, uint64_t input_buf_len);
QEMUFile* qemu_file_ram_write_new(uint8_t* output_buf, uint64_t output_buf_len);