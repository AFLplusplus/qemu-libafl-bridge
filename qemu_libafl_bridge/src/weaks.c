#include <stdlib.h>
#include <stdint.h>

__attribute__((weak)) int libafl_qemu_write_reg(int reg, uint8_t* val) {
  (void)reg;
  (void)val;
  return 0;
}

__attribute__((weak)) int libafl_qemu_read_reg(int reg, uint8_t* val) {
  (void)reg;
  (void)val;
  return 0;
}

__attribute__((weak)) int libafl_qemu_num_regs(void) {
  return 0;
}

__attribute__((weak)) int libafl_qemu_set_breakpoint(uint64_t addr) {
  (void)addr;
  return 0;
}

__attribute__((weak)) int libafl_qemu_remove_breakpoint(uint64_t addr) {
  (void)addr;
  return 0;
}

__attribute__((weak)) int libafl_qemu_run() {
  return 0;
}

__attribute__((weak)) size_t guest_base = 0;
