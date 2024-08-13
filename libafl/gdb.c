#include "qemu/osdep.h"
#include "libafl/gdb.h"
#include "gdbstub/internals.h"

static struct libafl_custom_gdb_cmd* libafl_qemu_gdb_cmds;

void libafl_qemu_add_gdb_cmd(bool (*callback)(void*, uint8_t*, size_t), void* data)
{
    struct libafl_custom_gdb_cmd* c = malloc(sizeof(struct libafl_custom_gdb_cmd));
    c->callback = callback;
    c->data = data;
    c->next = libafl_qemu_gdb_cmds;
    libafl_qemu_gdb_cmds = c;
}

void libafl_qemu_gdb_reply(const uint8_t* buf, size_t len)
{
    g_autoptr(GString) hex_buf = g_string_new("O");
    gdb_memtohex(hex_buf, buf, len);
    gdb_put_packet(hex_buf->str);
}

bool libafl_qemu_gdb_exec(void) {
    struct libafl_custom_gdb_cmd** c = &libafl_qemu_gdb_cmds;
    bool recognized = false;
    while (*c) {
        recognized |= (*c)->callback((*c)->data, gdbserver_state.mem_buf->data, gdbserver_state.mem_buf->len);
        c = &(*c)->next;
    }
    return recognized;
}
