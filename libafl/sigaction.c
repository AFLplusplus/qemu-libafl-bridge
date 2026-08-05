#include "qemu/osdep.h"
#include <signal.h>

#include "libafl/sigaction.h"

#ifdef CONFIG_USER_ONLY
static __thread volatile sig_atomic_t signal_kind = LIBAFL_QEMU_FATAL_NONE;
#endif

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

static void unblock_signal(int signum)
{
    sigset_t set;

    sigemptyset(&set);
    sigaddset(&set, signum);

    if (pthread_sigmask(SIG_UNBLOCK, &set, NULL) != 0) {
        _exit(EXIT_FAILURE);
    }
}

static void raise_unblocked(int signum)
{
    unblock_signal(signum);
    raise(signum);
}

G_NORETURN
static void raise_dft(int signum)
{
    struct sigaction action = { 0 };

    if (!valid_signal(signum)) {
        _exit(EXIT_FAILURE);
    }

    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);

    if (sigaction(signum, &action, NULL) < 0) {
        _exit(EXIT_FAILURE);
    }

    raise_unblocked(signum);

    _exit(128 + signum);
}

int libafl_sigaction(int signum, const struct sigaction* act, struct sigaction* oldact)
{
    struct sigaction prev, curr, installed;

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

    installed = *act;
    installed.sa_flags |= saved->action.sa_flags & SA_ONSTACK;

    if (sigaction(signum, &installed, &prev) < 0) {
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

QEMU_DISABLE_CFI G_NORETURN
void libafl_sigaction_forward(int signum, siginfo_t* info, void* ucontext)
{
    if (valid_saved_action(signum)) {
        struct sigaction saved = saved_actions[signum].action;

        if (callable_action(&saved)) {
            // if action is valid and callable
            if (pthread_sigmask(SIG_BLOCK, &saved.sa_mask, NULL) != 0) {
                _exit(EXIT_FAILURE);
            }

            if ((saved.sa_flags & SA_NODEFER) && (sigismember(&saved.sa_mask, signum) == 0)) {
                unblock_signal(signum);
            }

            if (saved.sa_flags & SA_SIGINFO) {
                saved.sa_sigaction(signum, info, ucontext);
            } else {
                saved.sa_handler(signum);
            }
        }
    }

    // fall back to default action
    raise_dft(signum);
}

G_NORETURN
void libafl_sigaction_raise(int signum)
{
    if (!valid_signal(signum)) {
        _exit(EXIT_FAILURE);
    }

    if (valid_saved_action(signum)) {
        struct sigaction saved = saved_actions[signum].action;

        if (callable_action(&saved) && sigaction(signum, &saved, NULL) == 0) {
            raise_unblocked(signum);
        }
    } else {
        raise_unblocked(signum);
    }

    raise_dft(signum);
}

G_NORETURN void libafl_sigaction_abort(void)
{
#ifdef CONFIG_USER_ONLY
    libafl_qemu_set_fatal_signal(LIBAFL_QEMU_FATAL_HOST);
#endif

    libafl_sigaction_raise(SIGABRT);
}

#ifdef CONFIG_USER_ONLY
enum libafl_qemu_fatal_signal_kind libafl_qemu_fatal_signal(void) {
    return (enum libafl_qemu_fatal_signal_kind) signal_kind;
}

void libafl_qemu_set_fatal_signal(enum libafl_qemu_fatal_signal_kind kind) {
    signal_kind = (sig_atomic_t) kind;
}
#endif
