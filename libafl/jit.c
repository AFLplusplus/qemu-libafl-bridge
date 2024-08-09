#include "qemu/osdep.h"

#include "qapi/error.h"

#include "exec/exec-all.h"

#include "libafl/jit.h"

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
size_t __afl_map_size __attribute__((weak));

size_t libafl_jit_trace_edge_hitcount(uint64_t data, uint64_t id)
{
    TCGv_ptr map_ptr = tcg_constant_ptr(__afl_area_ptr_local);
    TCGv_i32 counter = tcg_temp_new_i32();
    tcg_gen_ld8u_i32(counter, map_ptr, (tcg_target_long)id);
    tcg_gen_addi_i32(counter, counter, 1);
    tcg_gen_st8_i32(counter, map_ptr, (tcg_target_long)id);
    return 3; // # instructions
}

size_t libafl_jit_trace_edge_single(uint64_t data, uint64_t id)
{
    TCGv_ptr map_ptr = tcg_constant_ptr(__afl_area_ptr_local);
    TCGv_i32 counter = tcg_temp_new_i32();
    tcg_gen_movi_i32(counter, 1);
    tcg_gen_st8_i32(counter, map_ptr, (tcg_target_long)id);
    return 2; // # instructions
}

uint64_t __prev_loc = 0;

size_t libafl_jit_trace_block_hitcount(uint64_t data, uint64_t id)
{
    TCGv_ptr map_ptr = tcg_constant_ptr(__afl_area_ptr_local);
    TCGv_ptr prev_loc_ptr = tcg_constant_ptr(&__prev_loc);

    TCGv_i32 counter = tcg_temp_new_i32();
    TCGv_i64 id_r = tcg_temp_new_i64();
    TCGv_i64 prev_loc = tcg_temp_new_i64();
    TCGv_ptr prev_loc2 = tcg_temp_new_ptr();

    // Compute location => 5 insn
    tcg_gen_ld_i64(prev_loc, prev_loc_ptr, 0);
    tcg_gen_xori_i64(prev_loc, prev_loc, (int64_t)id);
    tcg_gen_andi_i64(prev_loc, prev_loc, (int64_t)(__afl_map_size - 1));
    tcg_gen_trunc_i64_ptr(prev_loc2, prev_loc);
    tcg_gen_add_ptr(prev_loc2, map_ptr, prev_loc2);

    // Update map => 3 insn
    tcg_gen_ld8u_i32(counter, prev_loc2, 0);
    tcg_gen_addi_i32(counter, counter, 1);
    tcg_gen_st8_i32(counter, prev_loc2, 0);

    // Update prev_loc => 3 insn
    tcg_gen_movi_i64(id_r, (int64_t)id);
    tcg_gen_shri_i64(id_r, id_r, 1);
    tcg_gen_st_i64(id_r, prev_loc_ptr, 0);
    return 11; // # instructions
}

size_t libafl_jit_trace_block_single(uint64_t data, uint64_t id)
{
    TCGv_ptr map_ptr = tcg_constant_ptr(__afl_area_ptr_local);
    TCGv_ptr prev_loc_ptr = tcg_constant_ptr(&__prev_loc);

    TCGv_i32 counter = tcg_temp_new_i32();
    TCGv_i64 id_r = tcg_temp_new_i64();
    TCGv_i64 prev_loc = tcg_temp_new_i64();
    TCGv_ptr prev_loc2 = tcg_temp_new_ptr();

    // Compute location => 5 insn
    tcg_gen_ld_i64(prev_loc, prev_loc_ptr, 0);
    tcg_gen_xori_i64(prev_loc, prev_loc, (int64_t)id);
    tcg_gen_andi_i64(prev_loc, prev_loc, (int64_t)(__afl_map_size - 1));
    tcg_gen_trunc_i64_ptr(prev_loc2, prev_loc);
    tcg_gen_add_ptr(map_ptr, map_ptr, prev_loc2);

    // Update map => 2 insn
    tcg_gen_movi_i32(counter, 1);
    tcg_gen_st8_i32(counter, map_ptr, 0);

    // Update prev_loc => 3 insn
    tcg_gen_movi_i64(id_r, (int64_t)id);
    tcg_gen_shri_i64(id_r, id_r, 1);
    tcg_gen_st_i64(id_r, prev_loc_ptr, 0);
    return 10; // # instructions
}
