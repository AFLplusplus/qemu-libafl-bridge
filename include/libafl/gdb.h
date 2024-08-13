#pragma once

#include "qemu/osdep.h"

struct libafl_custom_gdb_cmd {
    bool (*callback)(void*, uint8_t*, size_t);
    void* data;
    struct libafl_custom_gdb_cmd* next;
};

void libafl_qemu_add_gdb_cmd(bool (*callback)(void*, uint8_t*, size_t), void* data);
void libafl_qemu_gdb_reply(const uint8_t* buf, size_t len);
bool libafl_qemu_gdb_exec(void);
