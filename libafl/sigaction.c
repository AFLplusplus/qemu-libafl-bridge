#include "qemu/osdep.h"

#include <signal.h>

#include "libafl/sigaction.h"

struct saved_action {
    volatile sig_atomic_t valid;
    struct sigaction action;
};

// host actions before qemu stuff
// we will save libafl actions here
static struct saved_action saved_actions[_NSIG];

static bool callable_action(const struct sigaction* act)
{
    return act->sa_handler != SIG_DFL
        && act->sa_handler != SIG_IGN
        && act->sa_handler != SIG_ERR;
}

static bool valid_signal(int signum)
{
    return signum > 0 && signum < _NSIG;
}

static bool valid_saved_action(int signum) {
    return valid_signal(signum) && saved_actions[signum].valid;
}

int libafl_sigaction(int signum, const struct sigaction* act, struct sigaction* oldact)
{
    struct sigaction prev, curr;

    if (act == NULL || !valid_signal(signum)) {
        return sigaction(signum, act, oldact);
    }

    struct saved_action* saved = &saved_actions[signum];
    bool do_save = !saved->valid;

    if (do_save) {
        if (sigaction(signum, NULL, &curr) < 0) {
            return -1;
        }

        saved->action = curr;
        saved->valid = 1;
    }

    if (sigaction(signum, act, &prev) < 0) {
        if (do_save) {
            saved->valid = 0;
        }
        return -1;
    }

    if (oldact != NULL) {
        *oldact = prev;
    }

    return 0;
}

G_NORETURN
static void libafl_sigaction_raise_dft(int signum)
{
    struct sigaction action = { 0 };
    sigset_t set;

    if (!valid_signal(signum)) {
        _exit(EXIT_FAILURE);
    }

    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);

    if (sigaction(signum, &action, NULL) < 0) {
        _exit(EXIT_FAILURE);
    }

    sigemptyset(&set);
    sigaddset(&set, signum);

    if (pthread_sigmask(SIG_UNBLOCK, &set, NULL) != 0) {
        _exit(EXIT_FAILURE);
    }

    raise(signum);

    _exit(128 + signum);
}

G_NORETURN
void libafl_sigaction_fatal(int signum, siginfo_t* info, void* ucontext)
{
    if (valid_saved_action(signum)) {
        struct sigaction saved = saved_actions[signum].action;

        if (callable_action(&saved)) {
            // if action is valid and callable
            if (saved.sa_flags & SA_SIGINFO) {
                saved.sa_sigaction(signum, info, ucontext);
            } else {
                saved.sa_handler(signum);
            }
        }
    }

    // fall back to default action
    libafl_sigaction_raise_dft(signum);
}

G_NORETURN
void libafl_sigaction_raise(int signum)
{
    sigset_t set;

    if (!valid_signal(signum)) {
        _exit(EXIT_FAILURE);
    }

    if (!valid_saved_action(signum)) {
        goto raise_dft;
    }

    struct sigaction saved = saved_actions[signum].action;

    if (!callable_action(&saved)) {
        goto raise_dft;
    }

    if (sigaction(signum, &saved, NULL) < 0) {
        goto raise_dft;
    }

    sigemptyset(&set);
    sigaddset(&set, signum);

    if (pthread_sigmask(SIG_UNBLOCK, &set, NULL) != 0) {
        goto raise_dft;
    }

    raise(signum);

raise_dft:
    libafl_sigaction_raise_dft(signum);
}
