#pragma once

#include "qemu/osdep.h"
#include "qapi/error.h"

#include "tcg/tcg.h"
#include "tcg/helper-info.h"

void tcg_gen_callN(void* func, TCGHelperInfo* info, TCGTemp* ret,
                   TCGTemp** args);

TranslationBlock* libafl_tb_lookup(CPUState* cpu, vaddr pc, uint64_t cs_base,
                                   uint32_t flags, uint32_t cflags);

TranslationBlock* libafl_tb_gen_code(CPUState* cpu, vaddr pc, uint64_t cs_base,
                                     uint32_t flags, int cflags);

void libafl_tb_add_jump(TranslationBlock* tb, int n, TranslationBlock* tb_next);
