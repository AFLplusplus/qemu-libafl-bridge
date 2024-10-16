#include "qemu/osdep.h"
#include "libafl/utils.h"
#include <execinfo.h>

#define MAX_NB_ADDRESSES 32

uintptr_t libafl_qemu_host_page_size(void)
{
    return qemu_real_host_page_size();
}

void libafl_qemu_backtrace(void)
{
    void* addresses[MAX_NB_ADDRESSES] = {0};

    int nb_addresses = backtrace(addresses, MAX_NB_ADDRESSES);
    char** symbols = backtrace_symbols(addresses, nb_addresses);

    for (int i = 0; i < nb_addresses; ++i) {
        fprintf(stderr, "[%p] %s]\n", addresses[i], symbols[i]);
    }

    if (nb_addresses == MAX_NB_ADDRESSES) {
        fprintf(stderr, "... and continues...\n");
    }

    free(symbols);
}