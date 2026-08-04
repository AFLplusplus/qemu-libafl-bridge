#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"

#include "libafl/user.h"

extern abi_ulong target_brk, initial_target_brk;

static struct image_info libafl_image_info;

// if true, target crashes will issue an exit request and return to harness.
// if false, target crahes will raise the appropriate signal.
static bool libafl_return_on_crash = false;

uint64_t libafl_load_addr(void) { return libafl_image_info.load_addr; }

struct image_info* libafl_get_image_info(void) { return &libafl_image_info; }

abi_ulong libafl_get_brk(void) { return target_brk; }

abi_ulong libafl_get_initial_brk(void) { return initial_target_brk; }

abi_ulong libafl_set_brk(abi_ulong new_brk)
{
    abi_ulong old_brk = target_brk;
    target_brk = new_brk;
    return old_brk;
}

void libafl_set_return_on_crash(bool return_on_crash)
{
    libafl_return_on_crash = return_on_crash;
}

bool libafl_get_return_on_crash(void) { return libafl_return_on_crash; }

#ifdef AS_LIB
void libafl_qemu_init(int argc, char** argv)
{
    // main function in usermode has an env parameter but is unused in practice.
    _libafl_qemu_user_init(argc, argv, NULL);
}
#endif

static __thread volatile sig_atomic_t signal_kind = LIBAFL_QEMU_FATAL_NONE;

enum libafl_qemu_fatal_signal_kind libafl_qemu_fatal_signal(void) {
    return (enum libafl_qemu_fatal_signal_kind) signal_kind;
}

void libafl_qemu_set_fatal_signal(enum libafl_qemu_fatal_signal_kind kind) {
    signal_kind = (sig_atomic_t) kind;
}
