#pragma once

#include "qapi/error.h"
#include "qemu/osdep.h"
#include "qemu/interval-tree.h"
#include "exec/cpu-defs.h"

struct libafl_mapinfo {
    uint64_t start;
    uint64_t end;
    uint64_t offset;
    const char* path;
    int flags;
    int is_priv;
    bool is_valid;
};

extern int libafl_force_dfl;

IntervalTreeNode* libafl_maps_first(IntervalTreeRoot* map_info);
IntervalTreeNode* libafl_maps_next(IntervalTreeNode* pageflags_maps_node,
                                   IntervalTreeRoot* proc_maps_node,
                                   struct libafl_mapinfo* ret);

uint64_t libafl_load_addr(void);
struct image_info* libafl_get_image_info(void);

abi_ulong libafl_get_initial_brk(void);
abi_ulong libafl_get_brk(void);
abi_ulong libafl_set_brk(abi_ulong new_brk);

int _libafl_qemu_user_init(int argc, char** argv, char** envp);

bool libafl_get_return_on_crash(void);
void libafl_set_return_on_crash(bool return_on_crash);

#ifdef AS_LIB
void libafl_qemu_init(int argc, char** argv);
#endif
