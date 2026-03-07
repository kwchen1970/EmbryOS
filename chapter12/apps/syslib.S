#include "syscode.h"

    .section .text
    .align 2
    .globl user_exit, user_yield, user_spawn, user_put, user_get
    .globl user_create, user_read, user_write, user_size, user_delete
    .globl user_gettime
    .globl user_sleep, ctx_switch, ctx_start

    # void user_exit(void);

user_exit:
    li      a7, SYS_EXIT
    ecall
    ret

    # void user_yield(void);
user_yield:
    li      a7, SYS_YIELD
    ecall
    ret

    # void user_spawn(int app, int row, char col, int wd, int ht,
    #                 const void *args, int sz)
user_spawn:
    li      a7, SYS_SPAWN
    ecall
    ret

    # void user_put(int row, int col, cell_t cell)
user_put:
    li      a7, SYS_PUT
    ecall
    ret

    # int user_get(int block)
user_get:
    li      a7, SYS_GET
    ecall
    ret

    # int user_create(void)
user_create:
    li      a7, SYS_CREATE
    ecall
    ret

    # int user_read(int file, int off, void* dst, int n)
user_read:
    li      a7, SYS_READ
    ecall
    ret

    # int user_write(int file, int off, const void* src, int n)
user_write:
    li      a7, SYS_WRITE
    ecall
    ret

    # int user_size(int file)
user_size:
    li      a7, SYS_SIZE
    ecall
    ret

    # void user_delete(int file)
user_delete:
    li      a7, SYS_DELETE
    ecall
    ret

user_gettime:
    li      a7, SYS_GETTIME
    ecall
    ret

user_sleep:
    li    a7, SYS_SLEEP
    ecall 
    ret

#if __riscv_xlen == 64
    #define REG_S sd
    #define REG_L ld
    #define WORD_SIZE 8
#else
    #define REG_S sw
    #define REG_L lw
    #define WORD_SIZE 4
#endif

# void ctx_switch(void **old_sp, void *new_sp)
#   Saves current context into *old_sp, switches to new_sp, restores.
ctx_switch:
    addi sp, sp, -(16*WORD_SIZE)

    REG_S s0,  1*WORD_SIZE(sp)
    REG_S s1,  2*WORD_SIZE(sp)
    REG_S s2,  3*WORD_SIZE(sp)
    REG_S s3,  4*WORD_SIZE(sp)
    REG_S s4,  5*WORD_SIZE(sp)
    REG_S s5,  6*WORD_SIZE(sp)
    REG_S s6,  7*WORD_SIZE(sp)
    REG_S s7,  8*WORD_SIZE(sp)
    REG_S s8,  9*WORD_SIZE(sp)
    REG_S s9, 10*WORD_SIZE(sp)
    REG_S s10,11*WORD_SIZE(sp)
    REG_S s11,12*WORD_SIZE(sp)
    REG_S ra,  0*WORD_SIZE(sp)

    REG_S sp, 0(a0)          # *old_sp = current sp
    mv sp, a1                # switch to new stack

    REG_L ra,  0*WORD_SIZE(sp)
    REG_L s0,  1*WORD_SIZE(sp)
    REG_L s1,  2*WORD_SIZE(sp)
    REG_L s2,  3*WORD_SIZE(sp)
    REG_L s3,  4*WORD_SIZE(sp)
    REG_L s4,  5*WORD_SIZE(sp)
    REG_L s5,  6*WORD_SIZE(sp)
    REG_L s6,  7*WORD_SIZE(sp)
    REG_L s7,  8*WORD_SIZE(sp)
    REG_L s8,  9*WORD_SIZE(sp)
    REG_L s9, 10*WORD_SIZE(sp)
    REG_L s10,11*WORD_SIZE(sp)
    REG_L s11,12*WORD_SIZE(sp)

    addi sp, sp, (16*WORD_SIZE)
    ret

# void ctx_start(void **save_sp, void *new_sp)
#   Saves current sp, switches to new stack, and calls exec_user().
ctx_start:
    addi sp, sp, -(16*WORD_SIZE)

    REG_S s0,  1*WORD_SIZE(sp)
    REG_S s1,  2*WORD_SIZE(sp)
    REG_S s2,  3*WORD_SIZE(sp)
    REG_S s3,  4*WORD_SIZE(sp)
    REG_S s4,  5*WORD_SIZE(sp)
    REG_S s5,  6*WORD_SIZE(sp)
    REG_S s6,  7*WORD_SIZE(sp)
    REG_S s7,  8*WORD_SIZE(sp)
    REG_S s8,  9*WORD_SIZE(sp)
    REG_S s9, 10*WORD_SIZE(sp)
    REG_S s10,11*WORD_SIZE(sp)
    REG_S s11,12*WORD_SIZE(sp)
    REG_S ra,  0*WORD_SIZE(sp)

    REG_S sp, 0(a0)          # *save_sp = current sp
    mv sp, a1                # switch to new stack
    la a0, exec_user
    jalr a0                  # call exec_user() (doesn't return)
    
