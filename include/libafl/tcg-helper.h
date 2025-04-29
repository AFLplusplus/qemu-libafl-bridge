DEF_HELPER_FLAGS_2(libafl_qemu_handle_breakpoint, TCG_CALL_NO_RWG,
    void, env, i64)
DEF_HELPER_FLAGS_3(libafl_qemu_handle_custom_insn, TCG_CALL_NO_RWG,
    void, env, i64, i32)
