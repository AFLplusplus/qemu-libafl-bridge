#pragma once

#include "qemu/osdep.h"
#include "qapi/error.h"

#include "tcg/tcg-op.h"
#include "tcg/tcg-internal.h"
#include "tcg/tcg-temp-internal.h"

size_t libafl_jit_trace_edge_hitcount(uint64_t data, uint64_t id);
size_t libafl_jit_trace_edge_single(uint64_t data, uint64_t id);

size_t libafl_jit_trace_block_hitcount(uint64_t data, uint64_t id);
size_t libafl_jit_trace_block_single(uint64_t data, uint64_t id);
