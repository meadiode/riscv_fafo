
# Experiments with a minimal RISC-V emulator

The emulator:
 - Base RV32I, M, Zicond instructions
 - Bare implementation of: serial output, 320x200 display, RTC
 - Can run DOOM

## RISC-V GCC toolchain

Clone https://github.com/riscv-collab/riscv-gnu-toolchain.git

Configure & make:

```
./configure --prefix=[desired executables location] --with-arch=rv32i --with-abi=ilp32 --with-multilib-generator="rv32i-ilp32--;"
make
```

## RISC-V LLVM toolchain

Clone https://github.com/llvm/llvm-project.git

Configure & make:

```
mkdir build
cd build
cmake -G Ninja ../llvm -DLLVM_ENABLE_PROJECTS="clang;lld" -DLLVM_TARGETS_TO_BUILD="RISCV" -DLLVM_DEFAULT_TARGET_TRIPLE=riscv32-unknown-elf -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=[desired executable location]

ninja
ninja install
```
