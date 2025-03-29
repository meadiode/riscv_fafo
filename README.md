
# Experiments with a minimal RISC-V emulator

The emulator:
 - Base RV32I instructions only
 - Bare implementation of: serial output, 320x200 display, RTC
 - Can run DOOM

## RISC-V GCC toolchain

Clone https://github.com/riscv-collab/riscv-gnu-toolchain.git

Configure & make:

```
./configure --prefix=/home/michael/bin/riscv --with-arch=rv32i --with-abi=ilp32 --with-multilib-generator="rv32i-ilp32--;"
make
```

