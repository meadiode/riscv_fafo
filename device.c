
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <raylib.h>

#include "rv_emu.h"
#include "system.h"

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
    uint32_t type;
    uint32_t offset;
    uint32_t vaddr;
    uint32_t paddr;
    uint32_t filesz;
    uint32_t memsz;
    uint32_t flags;
    uint32_t align;

} prog_hdr_t;


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


static device_t dev = {0};

int main(int argc, char **argv)
{
    if (argc <= 1)
    {
        printf("Error: a 32-bit ELF file is expected as argument\n");
        exit(-1);
    }

    FILE *elf = fopen(argv[1], "rb");

    elf_hdr_t elf_hdr = {0};

    fread(&elf_hdr, 1, sizeof(elf_hdr), elf);

    if (elf_hdr.machine != 0x00f3 || elf_hdr.e_ident.bitness != 1)
    {
        printf("Error: this ELF file is not RISC-V 32bit\n");
        fclose(elf);
        exit(-1);
    }

    prog_hdr_t *prog_table = malloc(sizeof(prog_hdr_t) * elf_hdr.phnum);
    fread(prog_table, sizeof(prog_hdr_t), elf_hdr.phnum, elf);


    device_init(&dev,
                1024 * 200,         0x08000000,   /* FLASH */
                1024 * 16,          0x20000000,   /* RAM */
                8 + DISP_VRAM_SIZE, 0x01000000);  /* Peripherals: serial tx/rx, screen buffer 320x200 */

    for (int i = 0; i < elf_hdr.phnum; i++)
    {
        if (prog_table[i].type == 1) /* PT_LOAD */
        {
            fseek(elf, prog_table[i].offset, SEEK_SET);
            for (int j = 0; j < prog_table[i].memsz; j++)
            {
                uint8_t b;
                fread(&b, 1, 1, elf);
                if (!device_write(&dev, prog_table[i].vaddr + j, &b, 1))
                {
                    printf("Error writing to the device address: 0x%08X\n", prog_table[i].vaddr + j);
                    break;
                }
            }
        }
    }

    fseek(elf, elf_hdr.shoff, SEEK_SET);

    sec_hdr_t *sec_table = malloc(sizeof(sec_hdr_t) * elf_hdr.shnum);
    fread(sec_table, sizeof(sec_hdr_t), elf_hdr.shnum, elf);
    int strtab_id = -1;
    int symtab_id = -1;

    /* Find string and symbol table indices */
    for (int i = 0; i < elf_hdr.shnum; i++)
    {
        fseek(elf, sec_table[elf_hdr.shstrndx].offset + sec_table[i].name, SEEK_SET);
        char sname[16] = {0};
        fread(sname, 1, sizeof(".strtab"), elf);

        if (sec_table[i].type == 0x03 && !strcmp(".strtab", sname))
        {
            strtab_id = i;
        }
        else if (sec_table[i].type == 0x02 && !strcmp(".symtab", sname))
        {
            symtab_id = i;
        }
    }

    /* Find the _exit symbol address */
    fseek(elf, sec_table[symtab_id].offset, SEEK_SET);
    sym_t *symbols = malloc(sec_table[symtab_id].size);
    fread(symbols, 1, sec_table[symtab_id].size, elf);

    uint32_t exit_addr = 0x0;

    for (int i = 0; i < sec_table[symtab_id].size / sizeof(sym_t); i++)
    {
        if ((symbols[i].info & 0x0f) == 0x02) /* STT_FUNC */
        {
            char sname[200] = {0};
            int ssize = 0;
            char c = 0;
            fseek(elf, sec_table[strtab_id].offset + symbols[i].name, SEEK_SET);
            do
            {
                fread(&c, 1, 1, elf);
                sname[ssize++] = c;
            }
            while(c);

            if (!strcmp("_exit", sname))
            {
                exit_addr = symbols[i].value;
                break;
            }
        }
    }

    fclose(elf);
    free(prog_table);
    free(sec_table);
    free(symbols);

    printf("_exit address: 0x%08X\n", exit_addr);
    
    InitWindow(400, 400, "RISC-V device");

    Image canvas = {0};
    canvas = GenImageColor(DISP_WIDTH, DISP_HEIGHT, LIGHTGRAY);
    ImageFormat(&canvas, PIXELFORMAT_UNCOMPRESSED_R8G8B8);

    ImageClearBackground(&canvas, LIGHTGRAY);
    Texture2D tex = LoadTextureFromImage(canvas);

    char prog_output[1024] = {0};
    int prog_output_n = 0;
    bool exit_reached = false;

    while (!WindowShouldClose())
    {
        BeginDrawing();
        ClearBackground(RAYWHITE);

        uint64_t frame_start_cycles = dev.elapsed_cycles;

        while (!exit_reached)
        {
            if (!device_run_cycle(&dev))
            {
                printf("Error!\n");
                fflush(stdout);
                exit_reached = true;
                break;
            }
            else
            {
                /* The program has something to say */
                if (dev.periph.data[1] && prog_output_n < (sizeof(prog_output) - 1))
                {
                    dev.periph.data[1] = 0;
                    prog_output[prog_output_n++] = dev.periph.data[0];

                    if (prog_output[prog_output_n - 1] == '\n')
                    {
                        printf("PROG OUTPUT: %s", prog_output);
                        fflush(stdout);
                        memset(prog_output, 0, sizeof(prog_output));
                        prog_output_n = 0;
                    }
                }

                /* The program has something to show */
                if (dev.periph.data[4])
                {
                    dev.periph.data[4] = 0;
                    memcpy(canvas.data, &dev.periph.data[8], DISP_VRAM_SIZE);
                    printf("Frame CPU cycles: %lu\n", dev.elapsed_cycles - frame_start_cycles);
                    fflush(stdout);
                    break;
                }
            }

            if (dev.pc == exit_addr || IsKeyPressed(KEY_X))
            {
                printf("Program done!\n");
                printf("Elapsed CPU cycles: %lu\n", dev.elapsed_cycles);
                
                printf("PROG OUTPUT: %s\n", prog_output);
                fflush(stdout);
                exit_reached = true;
                break;
            }
        }

        UpdateTexture(tex, canvas.data);
        DrawTexture(tex, 40, 100, WHITE);
        DrawFPS(10, 10);
        EndDrawing();
    
        if (exit_reached && IsKeyPressed(KEY_SPACE))
        {
            break;
        }
    }

    CloseWindow();
}
