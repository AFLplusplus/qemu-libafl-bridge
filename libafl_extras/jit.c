#include "qemu/osdep.h"
#include "qapi/error.h"

#include "exec/exec-all.h"

#include "jit.h"

#ifndef TARGET_LONG_BITS
#error "TARGET_LONG_BITS not defined"
#endif

/*
pub extern "C" fn trace_edge_hitcount(id: u64, _data: u64) {
    unsafe {
        EDGES_MAP[id as usize] = EDGES_MAP[id as usize].wrapping_add(1);
    }
}

pub extern "C" fn trace_edge_single(id: u64, _data: u64) {
    unsafe {
        EDGES_MAP[id as usize] = 1;
    }
}
*/

// from libafl_targets coverage.rs
// correct size doesn't matter here
uint8_t __afl_area_ptr_local[65536] __attribute__((weak));

size_t libafl_jit_trace_edge_hitcount(uint64_t data, uint64_t id) {
    TCGv_ptr map_ptr = tcg_constant_ptr(__afl_area_ptr_local);
    TCGv_i32 counter = tcg_temp_new_i32();
    tcg_gen_ld8u_i32(counter, map_ptr, (tcg_target_long)id);
    tcg_gen_addi_i32(counter, counter, 1);
    tcg_gen_st8_i32(counter, map_ptr, (tcg_target_long)id);
    return 3; // # instructions
}

size_t libafl_jit_trace_edge_single(uint64_t data, uint64_t id) {
    TCGv_ptr map_ptr = tcg_constant_ptr(__afl_area_ptr_local);
    TCGv_i32 counter = tcg_temp_new_i32();
    tcg_gen_movi_i32(counter, 1);
    tcg_gen_st8_i32(counter, map_ptr, (tcg_target_long)id);
    return 2; // # instructions
}
