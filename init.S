.section .text
.global _init

_init:
    la sp, __stack_end   # Init sp
    jal ra, _start
loop:
    j loop

