
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <raylib.h>

#include "rv_emu.h"
#include "system.h"

static device_t dev = {0};

int main(int argc, char **argv)
{
    if (argc <= 1)
    {
        printf("Error: a 32-bit ELF file is expected as argument\n");
        exit(-1);
    }

    device_init(&dev,
                1024 * 1024 * 16,   0x08000000,    /* FLASH */
                1024 * 1024 * 8,    0x20000000,    /* RAM */
                64 + DISP_VRAM_SIZE, 0x01000000);  /* Peripherals: serial tx/rx, RTC, screen buffer 320x200 */

    if (!device_load_from_elf(&dev, argv[1]))
    {
        exit(-1);
    }

    if (argc >= 3)
    {
        if (!device_load_ilp_table(&dev, argv[2]))
        {
            exit(-1);
        }
    }


    InitWindow(640, 400, "RISC-V device");

    Image canvas = {0};
    canvas = GenImageColor(DISP_WIDTH, DISP_HEIGHT, LIGHTGRAY);
    ImageFormat(&canvas, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);

    ImageClearBackground(&canvas, LIGHTGRAY);
    Texture2D tex = LoadTextureFromImage(canvas);

    char prog_output[1024] = {0};
    int prog_output_n = 0;
    bool exit_reached = false;

    while (!WindowShouldClose())
    {
        BeginDrawing();
        ClearBackground(RAYWHITE);

        uint64_t frame_cycles = 0;
        uint64_t total_cycles = 0;

        while (!exit_reached)
        {
            if (!device_run_cycle(&dev))
            {
                printf("Error running a cycle!\n");
                fflush(stdout);
                exit_reached = true;
                break;
            }
            else
            {
                total_cycles++;
                frame_cycles++;

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
                if (dev.periph.data[0x24])
                {
                    dev.periph.data[0x24] = 0;
                    memcpy(canvas.data, &dev.periph.data[0x28], DISP_VRAM_SIZE);
                    printf("CPU cycles per frame: %lu\n", frame_cycles);
                    frame_cycles = 0;
                    fflush(stdout);
                    break;
                }

                /* The program wants to know what time is it */
                if (dev.periph.data[0x0c])
                {
                    dev.periph.data[0x0c] = 0;
                    *((uint32_t*)&dev.periph.data[0x04]) = (uint32_t)(GetTime() * 1000.0);
                }
            }

            if (dev.pc == dev.exit_addr || IsKeyPressed(KEY_X))
            {
                printf("Program done!\n");
                printf("Elapsed CPU cycles: %lu\n", total_cycles);
                
                printf("PROG OUTPUT: %s\n", prog_output);
                fflush(stdout);
                exit_reached = true;
                break;
            }
        }

        UpdateTexture(tex, canvas.data);
        DrawTexturePro(tex, (Rectangle){0.0, 0.0, 320.0, 200.0},
                       (Rectangle){0.0, 0.0, 640.0, 400.0},
                       (Vector2){0.0, 0.0}, 0.0, WHITE);
        DrawFPS(10, 10);
        EndDrawing();
    
        if (exit_reached && IsKeyPressed(KEY_SPACE))
        {
            break;
        }
    }

    CloseWindow();
}
