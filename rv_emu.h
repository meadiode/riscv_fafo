#ifndef __RV_EMU_H
#define __RV_EMU_H

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>

typedef struct
{
    uint32_t origin;
    uint32_t size;
    uint8_t *data;

} mem_t;


typedef struct
{
    uint32_t addr;
    uint32_t offset;
    uint32_t size;

} ilp_entry_t;


typedef struct
{
    struct device_t *dev;
    uint32_t thread_id;

} ilp_thread_data_t;


typedef struct
{
    uint32_t regs[32];
    uint32_t pc;
    uint32_t exit_addr;

    mem_t rom;
    mem_t ram;
    mem_t periph;

    uint32_t          ilp_n_blocks;
    uint32_t          ilp_n_threads;
    uint32_t          ilp_cur_id;
    uint32_t          ilp_cur_items;

    ilp_entry_t       *ilp_map;
    uint32_t          *ilp_table;

    pthread_t         *ilp_threads;
    ilp_thread_data_t *ilp_threads_data;
    uint32_t          *ilp_slice;
    pthread_barrier_t ilp_barrier1;
    pthread_barrier_t ilp_barrier2;

} device_t;


typedef struct 
{
    struct
    {
        uint8_t magic[4];
        uint8_t bitness;
        uint8_t data;
        uint8_t version;
        uint8_t os_abi;
        uint8_t abi_ver;
        uint8_t pad[7];

    } e_ident;

    uint16_t type;
    uint16_t machine;
    uint32_t version;
    
    uint32_t entry;
    uint32_t phoff;
    uint32_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;

} elf_hdr_t;


typedef struct
{
    uint32_t name;
    uint32_t type;
    uint32_t flags;
    uint32_t addr;
    uint32_t offset;
    uint32_t size;
    uint32_t link;
    uint32_t info;
    uint32_t addralign;
    uint32_t entsize;

} sec_hdr_t;


typedef struct
{
    uint32_t name;
    uint32_t value;
    uint32_t size;
    uint8_t  info;
    uint8_t  other;
    uint16_t shndx;

} sym_t;


void device_init(device_t *dev,
                 uint32_t rom_size, uint32_t rom_origin,
                 uint32_t ram_size, uint32_t ram_origin,
                 uint32_t periph_size, uint32_t periph_origin);
bool device_load_from_elf(device_t *dev, const char *elf_file_name);
bool device_load_ilp_table(device_t *dev, const char *ilp_file_name);
void device_uninit(device_t *dev);
bool device_write(device_t *dev, uint32_t addr, const uint8_t *data, uint32_t size);
bool device_read(device_t *dev, uint32_t addr, uint8_t *data, uint32_t size);
void device_set_reg(device_t *dev, int rd, uint32_t val);
bool device_run_instruction(device_t *dev, uint32_t inst, uint32_t pc_ro);
bool device_run_cycle(device_t *dev);



#endif