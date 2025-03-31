#include "qemu/osdep.h"
#include "qemu.h"
#include "loader.h"
#include "exec/exec-all.h"

#include "libafl/user.h"

extern abi_ulong target_brk, initial_target_brk;

static struct image_info libafl_image_info;

static struct libafl_qemu_sig_ctx libafl_qemu_sig_ctx = {0};

// if true, target crashes will issue an exit request and return to harness.
// if false, target crahes will raise the appropriate signal.
static bool libafl_return_on_crash = false;

libafl_qemu_on_signal_hdlr libafl_signal_hdlr = NULL;

void host_signal_handler(int host_sig, siginfo_t* info, void* puc);

void libafl_qemu_native_signal_handler(int host_sig, siginfo_t* info, void* puc)
{
    host_signal_handler(host_sig, info, puc);
}

void libafl_set_in_target_signal_ctx(void)
{
    libafl_qemu_sig_ctx.in_qemu_sig_hdlr = true;
    libafl_qemu_sig_ctx.is_target_signal = true;
}

void libafl_set_in_host_signal_ctx(void)
{
    libafl_qemu_sig_ctx.in_qemu_sig_hdlr = true;
    libafl_qemu_sig_ctx.is_target_signal = false;
}

void libafl_unset_in_signal_ctx(void)
{
    libafl_qemu_sig_ctx.in_qemu_sig_hdlr = false;
}

struct libafl_qemu_sig_ctx* libafl_qemu_signal_context(void)
{
    return &libafl_qemu_sig_ctx;
}

uint64_t libafl_load_addr(void) { return libafl_image_info.load_addr; }

struct image_info* libafl_get_image_info(void) { return &libafl_image_info; }

uint64_t libafl_get_brk(void) { return (uint64_t)target_brk; }

uint64_t libafl_get_initial_brk(void) { return (uint64_t)initial_target_brk; }

uint64_t libafl_set_brk(uint64_t new_brk)
{
    uint64_t old_brk = (uint64_t)target_brk;
    target_brk = (abi_ulong)new_brk;
    return old_brk;
}

void libafl_set_return_on_crash(bool return_on_crash)
{
    libafl_return_on_crash = return_on_crash;
}

bool libafl_get_return_on_crash(void) { return libafl_return_on_crash; }

void libafl_set_on_signal_handler(libafl_qemu_on_signal_hdlr hdlr)
{
    libafl_signal_hdlr = hdlr;
}

#ifdef AS_LIB
void libafl_qemu_init(int argc, char** argv)
{
    // main function in usermode has an env parameter but is unused in practice.
    _libafl_qemu_user_init(argc, argv, NULL);
}
#endif
