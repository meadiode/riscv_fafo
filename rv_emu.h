#ifndef __RV_EMU_H
#define __RV_EMU_H

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

typedef struct
{
    uint32_t origin;
    uint32_t size;
    uint8_t *data;

} mem_t;


typedef struct
{
    uint32_t regs[32];
    uint32_t pc;
    uint64_t elapsed_cycles;

    mem_t rom;
    mem_t ram;
    mem_t periph;

} device_t;


void device_init(device_t *dev,
                 uint32_t rom_size, uint32_t rom_origin,
                 uint32_t ram_size, uint32_t ram_origin,
                 uint32_t periph_size, uint32_t periph_origin);
void device_uninit(device_t *dev);
bool device_write(device_t *dev, uint32_t addr, const uint8_t *data, uint32_t size);
bool device_read(device_t *dev, uint32_t addr, uint8_t *data, uint32_t size);
void device_set_reg(device_t *dev, int rd, uint32_t val);
bool device_run_cycle(device_t *dev);



#endif