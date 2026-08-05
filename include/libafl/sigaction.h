#pragma once

#include <stdbool.h>
#include <signal.h>

#ifdef CONFIG_USER_ONLY
enum libafl_qemu_fatal_signal_kind {
    LIBAFL_QEMU_FATAL_NONE = 0,
    LIBAFL_QEMU_FATAL_HOST = 1,
    LIBAFL_QEMU_FATAL_TARGET = 2,
};

enum libafl_qemu_fatal_signal_kind libafl_qemu_fatal_signal(void);
void libafl_qemu_set_fatal_signal(enum libafl_qemu_fatal_signal_kind kind);
#endif

// same as sigaction, while saving the old actions
int libafl_sigaction(int signum, const struct sigaction* act, struct sigaction* oldact);

// forward a fatal signal according to the saved action
G_NORETURN void libafl_sigaction_forward(int signum, siginfo_t* info, void* ucontext);

// raise a new signal using the saved actions
G_NORETURN void libafl_sigaction_raise(int signum);

G_NORETURN void libafl_sigaction_abort(void);
