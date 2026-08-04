#pragma once

#include <stdbool.h>
#include <signal.h>

// same as sigaction, while saving the old actions
int libafl_sigaction(int signum, const struct sigaction* act, struct sigaction* oldact);

// forward a fatal signal according to the saved action
G_NORETURN void libafl_sigaction_fatal(int signum, siginfo_t* info, void* ucontext);

// raise a new signal using the saved actions
G_NORETURN void libafl_sigaction_raise(int signum);
