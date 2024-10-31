#pragma once

#include "qemu/osdep.h"
#include "qapi/error.h"

#include "tcg/tcg.h"
#include "tcg/helper-info.h"

void tcg_gen_callN(void *func, TCGHelperInfo *info,
                          TCGTemp *ret, TCGTemp **args);
