use core::{mem::transmute, ptr::copy_nonoverlapping};
use num::Num;

pub mod amd64;

/*
  int libafl_qemu_write_reg(int reg, uint8_t* val);
  int libafl_qemu_read_reg(int reg, uint8_t* val);
  int libafl_qemu_num_regs(void);
  int libafl_qemu_set_breakpoint(uint64_t addr);
  int libafl_qemu_remove_breakpoint(uint64_t addr);
*/

extern "C" {
    fn libafl_qemu_write_reg(reg: i32, val: *const u8) -> i32;
    fn libafl_qemu_read_reg(reg: i32, val: *mut u8) -> i32;
    fn libafl_qemu_num_regs() -> i32;
    fn libafl_qemu_set_breakpoint(addr: u64) -> i32;
    fn libafl_qemu_remove_breakpoint(addr: u64) -> i32;
    fn libafl_qemu_run() -> i32;

    static guest_base: isize;
}

pub struct QemuEmulator {}

impl QemuEmulator {
    pub fn write_mem(&mut self, addr: isize, buf: &[u8]) {
        let host_addr = self.g2h(addr);
        unsafe { copy_nonoverlapping(buf.as_ptr() as *const u8, host_addr, buf.len()) }
    }

    pub fn read_mem(&mut self, addr: isize, buf: &mut [u8]) {
        let host_addr = self.g2h(addr);
        unsafe {
            copy_nonoverlapping(
                host_addr as *const u8,
                buf.as_mut_ptr() as *mut u8,
                buf.len(),
            )
        }
    }

    pub fn num_regs(&self) -> i32 {
        unsafe { libafl_qemu_num_regs() }
    }

    pub fn write_reg<T>(&mut self, reg: i32, val: T) -> Result<(), String>
    where
        T: Num + PartialOrd + Copy,
    {
        let success = unsafe { libafl_qemu_write_reg(reg, &val as *const _ as *const u8) };
        if success != 0 {
            Ok(())
        } else {
            Err(format!("Failed to write to register {}", reg))
        }
    }

    pub fn read_reg<T>(&mut self, reg: i32) -> Result<T, String>
    where
        T: Num + PartialOrd + Copy,
    {
        let mut val = T::zero();
        let success = unsafe { libafl_qemu_read_reg(reg, &mut val as *mut _ as *mut u8) };
        if success != 0 {
            Ok(val)
        } else {
            Err(format!("Failed to read register {}", reg))
        }
    }

    pub fn set_breakpoint(&mut self, addr: isize) {
        unsafe { libafl_qemu_set_breakpoint(addr as u64) };
    }

    pub fn remove_breakpoint(&mut self, addr: isize) {
        unsafe { libafl_qemu_remove_breakpoint(addr as u64) };
    }

    pub fn run(&mut self) {
        unsafe { libafl_qemu_run() };
    }

    pub fn g2h(&self, addr: isize) -> *mut u8 {
        unsafe { transmute(addr + guest_base) }
    }

    pub fn h2g(&self, addr: isize) -> *mut u8 {
        unsafe { transmute(addr - guest_base) }
    }

    pub fn new() -> Self {
        Self {}
    }
}
