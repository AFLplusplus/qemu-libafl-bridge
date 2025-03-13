#pragma once

#include "qapi/error.h"
#include "qemu/osdep.h"
#include "qemu/interval-tree.h"

#include "exec/cpu-defs.h"

struct libafl_mapinfo {
    target_ulong start;
    target_ulong end;
    target_ulong offset;
    const char* path;
    int flags;
    int is_priv;
    bool is_valid;
};

struct libafl_qemu_sig_ctx {
    bool in_qemu_sig_hdlr; // we were inside qemu native signal handler
    bool is_target_signal; // if we were in qemu signal handle, true -> is a
                           // propagated target signal; false -> is a host qemu
                           // signal.
};

extern int libafl_force_dfl;

void libafl_qemu_native_signal_handler(int host_sig, siginfo_t* info,
                                       void* puc);

struct libafl_qemu_sig_ctx* libafl_qemu_signal_context(void);
void libafl_set_in_target_signal_ctx(void);
void libafl_set_in_host_signal_ctx(void);
void libafl_unset_in_signal_ctx(void);

void libafl_qemu_handle_crash(int host_sig, siginfo_t* info, void* puc);

IntervalTreeNode* libafl_maps_first(IntervalTreeRoot* map_info);
IntervalTreeNode* libafl_maps_next(IntervalTreeNode* pageflags_maps_node,
                                   IntervalTreeRoot* proc_maps_node,
                                   struct libafl_mapinfo* ret);

uint64_t libafl_load_addr(void);
struct image_info* libafl_get_image_info(void);

uint64_t libafl_get_initial_brk(void);
uint64_t libafl_get_brk(void);
uint64_t libafl_set_brk(uint64_t new_brk);

int _libafl_qemu_user_init(int argc, char** argv, char** envp);

bool libafl_get_return_on_crash(void);
void libafl_set_return_on_crash(bool return_on_crash);

#ifdef AS_LIB
void libafl_qemu_init(int argc, char** argv);
#endif
