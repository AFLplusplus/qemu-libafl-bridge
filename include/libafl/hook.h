#pragma once

#include "qemu/osdep.h"
#include "qapi/error.h"

#include "tcg/tcg-op.h"
#include "tcg/tcg-internal.h"
#include "tcg/tcg-temp-internal.h"

#define LIBAFL_TABLES_SIZE 16384
#define LIBAFL_TABLES_HASH(p) (((13*((size_t)(p))) ^ (((size_t)(p)) >> 15)) % LIBAFL_TABLES_SIZE)
#define LIBAFL_MAX_INSNS 16

void tcg_gen_callN(TCGHelperInfo *info, TCGTemp *ret, TCGTemp **args);

TranslationBlock *libafl_gen_edge(CPUState *cpu, target_ulong src_block,
                                  target_ulong dst_block, int exit_n,
                                  target_ulong cs_base, uint32_t flags,
                                  int cflags);

void libafl_gen_cmp(target_ulong pc, TCGv op0, TCGv op1, MemOp ot);
void libafl_gen_backdoor(target_ulong pc);

extern target_ulong libafl_gen_cur_pc;

struct libafl_hook {
    target_ulong addr;
    // void (*callback)(uint64_t, target_ulong);
    uint64_t data;
    size_t num;
    TCGHelperInfo helper_info;
    struct libafl_hook* next;
};

extern struct libafl_hook* libafl_qemu_instruction_hooks[LIBAFL_TABLES_SIZE];
extern size_t libafl_qemu_hooks_num;

size_t libafl_qemu_add_instruction_hooks(target_ulong pc, void (*callback)(uint64_t data, target_ulong pc),
                                         uint64_t data, int invalidate);
size_t libafl_qemu_remove_instruction_hooks_at(target_ulong addr, int invalidate);
int libafl_qemu_remove_instruction_hook(size_t num, int invalidate);
struct libafl_hook* libafl_search_instruction_hook(target_ulong addr);

struct libafl_backdoor_hook {
    void (*exec)(uint64_t data, CPUArchState* cpu, target_ulong pc);
    uint64_t data;
    size_t num;
    TCGHelperInfo helper_info;
    struct libafl_backdoor_hook* next;
};

extern struct libafl_backdoor_hook* libafl_backdoor_hooks;

size_t libafl_add_backdoor_hook(void (*exec)(uint64_t data, CPUArchState* cpu, target_ulong pc),
                                uint64_t data);
int libafl_qemu_remove_backdoor_hook(size_t num, int invalidate);

struct libafl_edge_hook {
    uint64_t (*gen)(uint64_t data, target_ulong src, target_ulong dst);
    // void (*exec)(uint64_t data, uint64_t id);
    size_t (*jit)(uint64_t data, uint64_t id); // optional opt
    uint64_t data;
    size_t num;
    uint64_t cur_id;
    TCGHelperInfo helper_info;
    struct libafl_edge_hook* next;
};

extern struct libafl_edge_hook* libafl_edge_hooks;

size_t libafl_add_edge_hook(uint64_t (*gen)(uint64_t data, target_ulong src, target_ulong dst),
                            void (*exec)(uint64_t data, uint64_t id),
                            uint64_t data);
int libafl_qemu_remove_edge_hook(size_t num, int invalidate);
bool libafl_qemu_edge_hook_set_jit(size_t num, size_t (*jit)(uint64_t, uint64_t)); // no param names to avoid to be marked as safe

struct libafl_block_hook {
    uint64_t (*gen)(uint64_t data, target_ulong pc);
    void (*post_gen)(uint64_t data, target_ulong pc, target_ulong block_length);
    // void (*exec)(uint64_t data, uint64_t id);
    size_t (*jit)(uint64_t data, uint64_t id); // optional opt
    uint64_t data;
    size_t num;
    TCGHelperInfo helper_info;
    struct libafl_block_hook* next;
};

extern struct libafl_block_hook* libafl_block_hooks;

size_t libafl_add_block_hook(uint64_t (*gen)(uint64_t data, target_ulong pc),
                             void (*post_gen)(uint64_t data, target_ulong pc, target_ulong block_length),
                             void (*exec)(uint64_t data, uint64_t id), uint64_t data);
int libafl_qemu_remove_block_hook(size_t num, int invalidate);
bool libafl_qemu_block_hook_set_jit(size_t num, size_t (*jit)(uint64_t, uint64_t)); // no param names to avoid to be marked as safe

struct libafl_rw_hook {
    uint64_t (*gen)(uint64_t data, target_ulong pc, TCGTemp* addr, MemOpIdx oi);
    /*void (*exec1)(uint64_t data, uint64_t id, target_ulong addr);
    void (*exec2)(uint64_t data, uint64_t id, target_ulong addr);
    void (*exec4)(uint64_t data, uint64_t id, target_ulong addr);
    void (*exec8)(uint64_t data, uint64_t id, target_ulong addr);
    void (*execN)(uint64_t data, uint64_t id, target_ulong addr, size_t size);*/
    uint64_t data;
    size_t num;
    TCGHelperInfo helper_info1;
    TCGHelperInfo helper_info2;
    TCGHelperInfo helper_info4;
    TCGHelperInfo helper_info8;
    TCGHelperInfo helper_infoN;
    struct libafl_rw_hook* next;
};

// alias
#define libafl_read_hook libafl_rw_hook
#define libafl_write_hook libafl_rw_hook

extern struct libafl_rw_hook* libafl_read_hooks;
extern struct libafl_rw_hook* libafl_write_hooks;

size_t libafl_add_read_hook(uint64_t (*gen)(uint64_t data, target_ulong pc, TCGTemp *addr, MemOpIdx oi),
                             void (*exec1)(uint64_t data, uint64_t id, target_ulong addr),
                             void (*exec2)(uint64_t data, uint64_t id, target_ulong addr),
                             void (*exec4)(uint64_t data, uint64_t id, target_ulong addr),
                             void (*exec8)(uint64_t data, uint64_t id, target_ulong addr),
                             void (*execN)(uint64_t data, uint64_t id, target_ulong addr, size_t size),
                             uint64_t data);
size_t libafl_add_write_hook(uint64_t (*gen)(uint64_t data, target_ulong pc, TCGTemp *addr, MemOpIdx oi),
                             void (*exec1)(uint64_t data, uint64_t id, target_ulong addr),
                             void (*exec2)(uint64_t data, uint64_t id, target_ulong addr),
                             void (*exec4)(uint64_t data, uint64_t id, target_ulong addr),
                             void (*exec8)(uint64_t data, uint64_t id, target_ulong addr),
                             void (*execN)(uint64_t data, uint64_t id, target_ulong addr, size_t size),
                             uint64_t data);

int libafl_qemu_remove_read_hook(size_t num, int invalidate);
int libafl_qemu_remove_write_hook(size_t num, int invalidate);

void libafl_gen_read(TCGTemp *addr, MemOpIdx oi);
void libafl_gen_write(TCGTemp *addr, MemOpIdx oi);

struct libafl_cmp_hook {
    uint64_t (*gen)(uint64_t data, target_ulong pc, size_t size);
    /*void (*exec1)(uint64_t data, uint64_t id, uint8_t v0, uint8_t v1);
    void (*exec2)(uint64_t data, uint64_t id, uint16_t v0, uint16_t v1);
    void (*exec4)(uint64_t data, uint64_t id, uint32_t v0, uint32_t v1);
    void (*exec8)(uint64_t data, uint64_t id, uint64_t v0, uint64_t v1);*/
    uint64_t data;
    size_t num;
    TCGHelperInfo helper_info1;
    TCGHelperInfo helper_info2;
    TCGHelperInfo helper_info4;
    TCGHelperInfo helper_info8;
    struct libafl_cmp_hook* next;
};

extern struct libafl_cmp_hook* libafl_cmp_hooks;

size_t libafl_add_cmp_hook(uint64_t (*gen)(uint64_t data, target_ulong pc, size_t size),
                           void (*exec1)(uint64_t data, uint64_t id, uint8_t v0, uint8_t v1),
                           void (*exec2)(uint64_t data, uint64_t id, uint16_t v0, uint16_t v1),
                           void (*exec4)(uint64_t data, uint64_t id, uint32_t v0, uint32_t v1),
                           void (*exec8)(uint64_t data, uint64_t id, uint64_t v0, uint64_t v1),
                           uint64_t data);
int libafl_qemu_remove_cmp_hook(size_t num, int invalidate);

struct syshook_ret {
    target_ulong retval;
    bool skip_syscall;
};

struct libafl_pre_syscall_hook {
    struct syshook_ret (*callback)(uint64_t data, int sys_num, target_ulong arg0,
                                   target_ulong arg1, target_ulong arg2,
                                   target_ulong arg3, target_ulong arg4,
                                   target_ulong arg5, target_ulong arg6,
                                   target_ulong arg7);
    uint64_t data;
    size_t num;
    struct libafl_pre_syscall_hook* next;
};

struct libafl_post_syscall_hook {
    target_ulong (*callback)(uint64_t data, target_ulong ret, int sys_num,
                             target_ulong arg0, target_ulong arg1,
                             target_ulong arg2, target_ulong arg3,
                             target_ulong arg4, target_ulong arg5,
                             target_ulong arg6, target_ulong arg7);
    uint64_t data;
    size_t num;
    struct libafl_post_syscall_hook* next;
};

extern struct libafl_pre_syscall_hook* libafl_pre_syscall_hooks;
extern struct libafl_post_syscall_hook* libafl_post_syscall_hooks;

size_t libafl_add_pre_syscall_hook(struct syshook_ret (*callback)(
                                     uint64_t data, int sys_num, target_ulong arg0,
                                     target_ulong arg1, target_ulong arg2,
                                     target_ulong arg3, target_ulong arg4,
                                     target_ulong arg5, target_ulong arg6,
                                     target_ulong arg7),
                                   uint64_t data);
size_t libafl_add_post_syscall_hook(target_ulong (*callback)(
                                      uint64_t data, target_ulong ret, int sys_num,
                                      target_ulong arg0, target_ulong arg1,
                                      target_ulong arg2, target_ulong arg3,
                                      target_ulong arg4, target_ulong arg5,
                                      target_ulong arg6, target_ulong arg7),
                                    uint64_t data);

int libafl_qemu_remove_pre_syscall_hook(size_t num);
int libafl_qemu_remove_post_syscall_hook(size_t num);

struct libafl_new_thread_hook {
    bool (*callback)(uint64_t data, uint32_t tid);
    uint64_t data;
    size_t num;
    struct libafl_new_thread_hook* next;
};

extern struct libafl_new_thread_hook* libafl_new_thread_hooks;

size_t libafl_add_new_thread_hook(bool (*callback)(uint64_t data, uint32_t tid),
                                  uint64_t data);
int libafl_qemu_remove_new_thread_hook(size_t num);

void libafl_tcg_gen_asan(TCGTemp * addr, size_t size);