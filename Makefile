

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
CFLAGS += -g -D_DEBUG

LDFLAGS = 

RV_CROSSCOMP = riscv32-unknown-elf-
RV_GCC 	   = $(RV_CROSSCOMP)gcc
RV_LD  	   = $(RV_CROSSCOMP)ld
RV_OBJCOPY = $(RV_CROSSCOMP)objcopy
RV_OBJDUMP = $(RV_CROSSCOMP)objdump


RV_CFLAGS = -march=rv32i -mabi=ilp32
# RV_CFLAGS += -Os -Wl,--gc-sections
RV_CFLAGS += -g -O0

RV_DIS_FLAGS = -d -S -M no-aliases

all: rv_emu torus $(BUILD_DIR)/prog01.elf $(BUILD_DIR)/prog02.elf

rv_emu: rv_emu.c $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDE_PATHS) -c -o $(BUILD_DIR)/rv_emu.o $<
	$(GCC) -o $@ $(BUILD_DIR)/rv_emu.o $(CFLAGS) $(LDFLAGS) -lm

torus: torus.c $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDE_PATHS) -c -o $(BUILD_DIR)/torus.o $<
	$(GCC) -o $@ $(BUILD_DIR)/torus.o $(CFLAGS) $(LDFLAGS) -lraylib -lm


$(BUILD_DIR):
	$(MKDIR) -p $(BUILD_DIR)

$(BUILD_DIR)/init.o: init.S $(BUILD_DIR)
	$(RV_GCC) -c $(RV_CFLAGS) init.S -o $(BUILD_DIR)/init.o

$(BUILD_DIR)/prog01.elf: prog01.c $(BUILD_DIR)/init.o
	$(RV_GCC) -c $(RV_CFLAGS) prog01.c -o $(BUILD_DIR)/prog01.o	
	$(RV_GCC) -T linker.ld $(RV_CFLAGS) $(BUILD_DIR)/prog01.o -o $(BUILD_DIR)/prog01.elf
	$(RV_OBJCOPY) -O binary $(BUILD_DIR)/prog01.elf $(BUILD_DIR)/prog01.bin
	$(RV_OBJDUMP) $(RV_DIS_FLAGS) $(BUILD_DIR)/prog01.elf > $(BUILD_DIR)/prog01.S

$(BUILD_DIR)/prog02.elf: prog02.c $(BUILD_DIR)/init.o
	$(RV_GCC) -c $(RV_CFLAGS) prog02.c -o $(BUILD_DIR)/prog02.o	
	$(RV_GCC) -T linker.ld $(RV_CFLAGS) $(BUILD_DIR)/prog02.o -lm -o $(BUILD_DIR)/prog02.elf
	$(RV_OBJCOPY) -O binary $(BUILD_DIR)/prog02.elf $(BUILD_DIR)/prog02.bin
	$(RV_OBJDUMP) $(RV_DIS_FLAGS) $(BUILD_DIR)/prog02.elf > $(BUILD_DIR)/prog02.S

clean:
	$(RM) -rf $(BUILD_DIR)
	$(RM) -f rv_emu
	$(RM) -f torus


.PHONY: all clean