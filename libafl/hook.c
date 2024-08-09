#include "qemu/osdep.h"

#include "qapi/error.h"

#include "exec/exec-all.h"
#include "exec/tb-flush.h"

#include "libafl/hook.h"
#include "libafl/exit.h"

#ifndef TARGET_LONG_BITS
#error "TARGET_LONG_BITS not defined"
#endif

#if TARGET_LONG_BITS == 32
#define SHADOW_BASE (0x20000000)
#elif TARGET_LONG_BITS == 64
#define SHADOW_BASE (0x7fff8000)
#else
#error Unhandled TARGET_LONG_BITS value
#endif

void libafl_tcg_gen_asan(TCGTemp* addr, size_t size)
{
    if (size == 0)
        return;

    TCGv addr_val = temp_tcgv_tl(addr);
    TCGv k = tcg_temp_new();
    TCGv shadow_addr = tcg_temp_new();
    TCGv_ptr shadow_ptr = tcg_temp_new_ptr();
    TCGv shadow_val = tcg_temp_new();
    TCGv test_addr = tcg_temp_new();
    TCGv_ptr test_ptr = tcg_temp_new_ptr();

    tcg_gen_andi_tl(k, addr_val, 7);
    tcg_gen_addi_tl(k, k, size - 1);

    tcg_gen_shri_tl(shadow_addr, addr_val, 3);
    tcg_gen_addi_tl(shadow_addr, shadow_addr, SHADOW_BASE);
    tcg_gen_tl_ptr(shadow_ptr, shadow_addr);
    tcg_gen_ld8s_tl(shadow_val, shadow_ptr, 0);

    /*
     * Making conditional branches here appears to cause QEMU issues with dead
     * temporaries so we will instead avoid branches. We will cause the guest
     * to perform a NULL dereference in the event of an ASAN fault. Note that
     * we will do this by using a store rather than a load, since the TCG may
     * otherwise determine that the result of the load is unused and simply
     * discard the operation. In the event that the shadow memory doesn't
     * detect a fault, we will simply write the value read from the shadow
     * memory back to it's original location. If, however, the shadow memory
     * detects an invalid access, we will instead attempt to write the value
     * at 0x0.
     */
    tcg_gen_movcond_tl(TCG_COND_EQ, test_addr, shadow_val, tcg_constant_tl(0),
                       shadow_addr, tcg_constant_tl(0));

    if (size < 8) {
        tcg_gen_movcond_tl(TCG_COND_GE, test_addr, k, shadow_val, test_addr,
                           shadow_addr);
    }

    tcg_gen_tl_ptr(test_ptr, test_addr);
    tcg_gen_st8_tl(shadow_val, test_ptr, 0);
}
