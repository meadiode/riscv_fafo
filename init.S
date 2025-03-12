.section .text
.global _start

_start:
    la sp, __stack_end   # Init sp
    jal ra, main
loop:
    j loop

