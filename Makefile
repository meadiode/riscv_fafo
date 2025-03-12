


ROOT_DIR = $(shell pwd)
BUILD_DIR = $(ROOT_DIR)/build

BINARY_NAME = rv_emu

RM      = rm
ECHO    = @echo
CP      = cp
MKDIR   = mkdir
GCC     =  gcc
CC      =  gcc
LD      =  ld
OBJCOPY =  objcopy
STRIP   =  strip


INCLUDE_PATHS = 

CFLAGS = -Wall -std=c99 -D_DEFAULT_SOURCE -Wno-missing-braces -Wunused-result
CFLAGS += -g -D_DEBUG

LDLIBS = -lm
LDFLAGS = 

RV_CROSSCOMP = riscv32-unknown-elf-
RV_GCC 	   = $(RV_CROSSCOMP)gcc
RV_LD  	   = $(RV_CROSSCOMP)ld
RV_OBJCOPY = $(RV_CROSSCOMP)objcopy
RV_OBJDUMP = $(RV_CROSSCOMP)objdump

all: $(BINARY_NAME) $(BUILD_DIR)/prog01.elf


$(BINARY_NAME): rv_emu.c $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDE_PATHS) -c -o $(BUILD_DIR)/rv_emu.o $<
	$(GCC) -o $@ $(BUILD_DIR)/rv_emu.o $(CFLAGS) $(LDFLAGS) $(LDLIBS)


$(BUILD_DIR):
	$(MKDIR) -p $(BUILD_DIR)

$(BUILD_DIR)/prog01.elf: prog01.c init.S $(BUILD_DIR)
	$(RV_GCC) -c -march=rv32i -mabi=ilp32 -ffreestanding -nostdlib -Os prog01.c -o $(BUILD_DIR)/prog01.o
	$(RV_GCC) -c -march=rv32i -mabi=ilp32 -ffreestanding -nostdlib -Os init.S -o $(BUILD_DIR)/init.o
	$(RV_LD) -T linker.ld $(BUILD_DIR)/init.o $(BUILD_DIR)/prog01.o -o $(BUILD_DIR)/prog01.elf
	$(RV_OBJCOPY) -O binary $(BUILD_DIR)/prog01.elf $(BUILD_DIR)/prog01.bin
	$(RV_OBJDUMP) -d -M no-aliases $(BUILD_DIR)/prog01.elf > $(BUILD_DIR)/prog01.S


clean:
	$(RM) -rf $(BUILD_DIR)
	$(RM) -f $(BINARY_NAME)


.PHONY: all clean