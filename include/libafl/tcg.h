#pragma once

#include "qemu/osdep.h"
#include "tcg/tcg.h"

void tcg_gen_callN(void* func, TCGHelperInfo* info, TCGTemp* ret,
                   TCGTemp** args);

// exit tb in tcg if libafl_loop_exit is set
void libafl_gen_loop_exit_check(void);

// exit via longjmp if libafl_loop_exit is set
void libafl_loop_exit_if_requested(void);
