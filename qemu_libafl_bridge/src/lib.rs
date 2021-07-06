use core::{convert::Into, mem::transmute, ptr::copy_nonoverlapping};
use num::Num;
use std::{slice::from_raw_parts, str::from_utf8_unchecked};

pub mod amd64;
pub mod x86;

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

    fn strlen(s: *const u8) -> usize;

    static exec_path: *const u8;
    static guest_base: isize;
}

pub struct QemuEmulator {}

impl QemuEmulator {
    pub fn write_mem<T>(&mut self, addr: isize, buf: &[T]) {
        let host_addr = self.g2h(addr);
        unsafe { copy_nonoverlapping(buf.as_ptr() as *const _ as *const u8, host_addr, buf.len()) }
    }

    pub fn read_mem<T>(&mut self, addr: isize, buf: &mut [T]) {
        let host_addr = self.g2h(addr);
        unsafe {
            copy_nonoverlapping(
                host_addr as *const u8,
                buf.as_mut_ptr() as *mut _ as *mut u8,
                buf.len(),
            )
        }
    }

    pub fn num_regs(&self) -> i32 {
        unsafe { libafl_qemu_num_regs() }
    }

    pub fn write_reg<R, T>(&mut self, reg: R, val: T) -> Result<(), String>
    where
        T: Num + PartialOrd + Copy,
        R: Into<i32>,
    {
        let reg = reg.into();
        let success = unsafe { libafl_qemu_write_reg(reg, &val as *const _ as *const u8) };
        if success != 0 {
            Ok(())
        } else {
            Err(format!("Failed to write to register {}", reg))
        }
    }

    pub fn read_reg<R, T>(&mut self, reg: R) -> Result<T, String>
    where
        T: Num + PartialOrd + Copy,
        R: Into<i32>,
    {
        let reg = reg.into();
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

    pub fn exec_path(&self) -> &str {
        unsafe { from_utf8_unchecked(from_raw_parts(exec_path, strlen(exec_path) + 1)) }
    }

    pub fn new() -> Self {
        Self {}
    }
}
