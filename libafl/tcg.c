#include "libafl/tcg.h"

#include "exec/exec-all.h"
#include "accel/tcg/internal-common.h"

TranslationBlock* libafl_tb_gen_code(CPUState* cpu, vaddr pc, uint64_t cs_base,
                                     uint32_t flags, int cflags)
{
    mmap_lock();
    TranslationBlock* tb = tb_gen_code(cpu, pc, cs_base, flags, cflags);
    mmap_unlock();

    return tb;
}
