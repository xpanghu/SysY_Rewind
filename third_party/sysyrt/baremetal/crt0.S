    .section .text.start
    .globl _start
    .type _start, @function

_start:
    la sp, __stack_top

    la t0, __bss_start
    la t1, __bss_end
1:
    bgeu t0, t1, 2f
    sw zero, 0(t0)
    addi t0, t0, 4
    j 1b

2:
    call main
    mv a0, a0
    call sysy_shutdown

3:
    wfi
    j 3b
