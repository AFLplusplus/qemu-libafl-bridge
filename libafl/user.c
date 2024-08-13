#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"

#include "libafl/user.h"

static void (*libafl_dump_core_hook)(int host_sig) = NULL;
static struct image_info libafl_image_info;

extern abi_ulong target_brk, initial_target_brk;

void host_signal_handler(int host_sig, siginfo_t* info, void* puc);

void libafl_qemu_handle_crash(int host_sig, siginfo_t* info, void* puc)
{
    host_signal_handler(host_sig, info, puc);
}

void libafl_dump_core_exec(int signal)
{
    if (libafl_dump_core_hook) {
        libafl_dump_core_hook(signal);
    }
}

uint64_t libafl_load_addr(void) { return libafl_image_info.load_addr; }

struct image_info* libafl_get_image_info(void) { return &libafl_image_info; }

uint64_t libafl_get_brk(void) { return (uint64_t)target_brk; }

uint64_t libafl_set_brk(uint64_t new_brk)
{
    uint64_t old_brk = (uint64_t)target_brk;
    target_brk = (abi_ulong)new_brk;
    return old_brk;
}
