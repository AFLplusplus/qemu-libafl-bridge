#pragma once

#include "qemu/osdep.h"
#include "tcg/tcg.h"

void tcg_gen_callN(void* func, TCGHelperInfo* info, TCGTemp* ret,
                   TCGTemp** args);

