/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Helper file for declaring TCG helper functions.
 * This one expands generation functions for tcg opcodes.
 */

#ifndef HELPER_GEN_COMMON_H
#define HELPER_GEN_COMMON_H

#define HELPER_H "accel/tcg/tcg-runtime.h"
#include "exec/helper-gen.h.inc"
#undef  HELPER_H

//// --- Begin LibAFL code ---
#define HELPER_H "libafl/tcg-helper.h"
#include "exec/helper-gen.h.inc"
#undef  HELPER_H
//// --- End LibAFL code ---

#endif /* HELPER_GEN_COMMON_H */
