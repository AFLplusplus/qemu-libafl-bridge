#pragma once

#include "qemu/osdep.h"
#include "qapi/error.h"

struct libafl_mapinfo {
    target_ulong start;
    target_ulong end;
    target_ulong offset;
    const char* path;
    int flags;
    int is_priv;
    bool is_valid;
};

IntervalTreeNode * libafl_maps_first(IntervalTreeRoot * map_info);
IntervalTreeNode * libafl_maps_next(IntervalTreeNode *pageflags_maps_node, IntervalTreeRoot *proc_maps_node, struct libafl_mapinfo* ret);
