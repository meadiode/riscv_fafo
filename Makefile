

ROOT_DIR = $(shell pwd)
BUILD_DIR = $(ROOT_DIR)/build

RAYLIB_PATH ?= ~/repos/raylib


RM      = rm
ECHO    = @echo
CP      = cp
MKDIR   = mkdir
GCC     =  gcc
CC      =  gcc
LD      =  ld
OBJCOPY =  objcopy
STRIP   =  strip


INCLUDE_PATHS = -I. -I$(RAYLIB_PATH)/src -I$(RAYLIB_PATH)/src/external

CFLAGS = -Wall -std=c99 -D_DEFAULT_SOURCE -Wno-missing-braces -Wunused-result
CFLAGS += -O3
# CFLAGS += -g -D_DEBUG

LDFLAGS = 

RV_CROSSCOMP = riscv32-unknown-elf-
RV_GCC 	   = $(RV_CROSSCOMP)gcc
RV_LD  	   = $(RV_CROSSCOMP)ld
RV_OBJCOPY = $(RV_CROSSCOMP)objcopy
RV_OBJDUMP = $(RV_CROSSCOMP)objdump


RV_CFLAGS = -march=rv32im -mabi=ilp32
RV_CFLAGS += -O3 -Wl,--gc-sections
# RV_CFLAGS += -g -O0 -Wl,--gc-sections

RV_DIS_FLAGS = -S -M no-aliases

all: device torus gpu_rv_device \
	$(BUILD_DIR)/prog01.elf \
	$(BUILD_DIR)/prog02.elf \
	$(BUILD_DIR)/prog03.elf \
	$(BUILD_DIR)/prog04.elf \
	$(BUILD_DIR)/prog05.elf

device: device.c rv_emu.c $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDE_PATHS) -c -o $(BUILD_DIR)/device.o device.c
	$(CC) $(CFLAGS) $(INCLUDE_PATHS) -c -o $(BUILD_DIR)/rv_emu.o rv_emu.c
	$(GCC) -o $@ $(BUILD_DIR)/device.o $(BUILD_DIR)/rv_emu.o $(CFLAGS) $(LDFLAGS) -lraylib -lm

gpu_rv_device: gpu_rv_device.c rv_emu.c $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDE_PATHS) -c -o $(BUILD_DIR)/gpu_rv_device.o gpu_rv_device.c
	$(CC) $(CFLAGS) $(INCLUDE_PATHS) -c -o $(BUILD_DIR)/rv_emu.o rv_emu.c
	$(GCC) -o $@ $(BUILD_DIR)/gpu_rv_device.o $(BUILD_DIR)/rv_emu.o $(CFLAGS) $(LDFLAGS) -lraylib -lm

torus: torus.c $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDE_PATHS) -c -o $(BUILD_DIR)/torus.o $<
	$(GCC) -o $@ $(BUILD_DIR)/torus.o $(CFLAGS) $(LDFLAGS) -lraylib -lm


$(BUILD_DIR):
	$(MKDIR) -p $(BUILD_DIR)

$(BUILD_DIR)/init.o: init.S $(BUILD_DIR)
	$(RV_GCC) -c $(RV_CFLAGS) init.S -o $(BUILD_DIR)/init.o

$(BUILD_DIR)/system.o: system.c $(BUILD_DIR)
	$(RV_GCC) -c $(RV_CFLAGS) system.c -o $(BUILD_DIR)/system.o

RV_COMMON_OBJS = $(BUILD_DIR)/init.o $(BUILD_DIR)/system.o

$(BUILD_DIR)/prog01.elf: prog01.c $(RV_COMMON_OBJS)
	$(RV_GCC) -c $(RV_CFLAGS) prog01.c -o $(BUILD_DIR)/prog01.o
	$(RV_GCC) -T linker.ld $(RV_CFLAGS) $(BUILD_DIR)/system.o $(BUILD_DIR)/prog01.o -o $(BUILD_DIR)/prog01.elf
	$(RV_OBJCOPY) -O binary $(BUILD_DIR)/prog01.elf $(BUILD_DIR)/prog01.bin
	$(RV_OBJDUMP) $(RV_DIS_FLAGS) $(BUILD_DIR)/prog01.elf > $(BUILD_DIR)/prog01.S

$(BUILD_DIR)/prog02.elf: prog02.c $(RV_COMMON_OBJS)
	$(RV_GCC) -c $(RV_CFLAGS) prog02.c -o $(BUILD_DIR)/prog02.o	
	$(RV_GCC) -c $(RV_CFLAGS) printf.c -o $(BUILD_DIR)/printf.o	
	$(RV_GCC) -T linker.ld $(RV_CFLAGS) $(BUILD_DIR)/system.o $(BUILD_DIR)/printf.o \
			  $(BUILD_DIR)/prog02.o -lm -o $(BUILD_DIR)/prog02.elf
	$(RV_OBJCOPY) -O binary $(BUILD_DIR)/prog02.elf $(BUILD_DIR)/prog02.bin
	$(RV_OBJDUMP) $(RV_DIS_FLAGS) $(BUILD_DIR)/prog02.elf > $(BUILD_DIR)/prog02.S

$(BUILD_DIR)/prog03.elf: prog03.c $(RV_COMMON_OBJS)
	$(RV_GCC) -c $(RV_CFLAGS) prog03.c -o $(BUILD_DIR)/prog03.o	
	$(RV_GCC) -T linker.ld $(RV_CFLAGS) $(BUILD_DIR)/system.o \
			  $(BUILD_DIR)/prog03.o -lm -o $(BUILD_DIR)/prog03.elf
	$(RV_OBJCOPY) -O binary $(BUILD_DIR)/prog03.elf $(BUILD_DIR)/prog03.bin
	$(RV_OBJDUMP) $(RV_DIS_FLAGS) $(BUILD_DIR)/prog03.elf > $(BUILD_DIR)/prog03.S

$(BUILD_DIR)/prog04.elf: prog04.c $(RV_COMMON_OBJS)
	$(RV_GCC) -c $(RV_CFLAGS) prog04.c -o $(BUILD_DIR)/prog04.o	
	$(RV_GCC) -T linker.ld $(RV_CFLAGS) $(BUILD_DIR)/system.o $(BUILD_DIR)/printf.o \
			  $(BUILD_DIR)/prog04.o -lm -o $(BUILD_DIR)/prog04.elf
	$(RV_OBJCOPY) -O binary $(BUILD_DIR)/prog04.elf $(BUILD_DIR)/prog04.bin
	$(RV_OBJDUMP) $(RV_DIS_FLAGS) $(BUILD_DIR)/prog04.elf > $(BUILD_DIR)/prog04.S


$(BUILD_DIR)/prog05.elf: prog05.c $(RV_COMMON_OBJS)
	$(RV_GCC) -c $(RV_CFLAGS) prog05.c -o $(BUILD_DIR)/prog05.o	
	$(RV_GCC) -T linker.ld $(RV_CFLAGS) $(BUILD_DIR)/system.o \
			  $(BUILD_DIR)/prog05.o -lm -o $(BUILD_DIR)/prog05.elf
	$(RV_OBJCOPY) -O binary $(BUILD_DIR)/prog05.elf $(BUILD_DIR)/prog05.bin
	$(RV_OBJDUMP) $(RV_DIS_FLAGS) $(BUILD_DIR)/prog05.elf > $(BUILD_DIR)/prog05.S

clean:
	$(RM) -rf $(BUILD_DIR)
	$(RM) -f device
	$(RM) -f torus


.PHONY: all clean