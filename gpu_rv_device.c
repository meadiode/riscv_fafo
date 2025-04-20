
#include <raylib.h>
#include <rlgl.h>
#include <stdlib.h>
#include <stdint.h>
#include <rv_emu.h>

#define SUPPORT_TRACELOG        1
#define SUPPORT_TRACELOG_DEBUG  1
#include <utils.h>

#include "system.h"


#define NUM_CPUS  64u
#define ROM_SIZE  (1024u * 1024u * 16u)
#define RAM_SIZE  (1024u * 1024u * 8u)


typedef struct
{
    uint32_t regs[32];
    uint32_t pc;
    uint32_t exit_addr;
    uint8_t periph[40];

} cpu_t;


#define NUM_DISPS_IN_ROW    8
#define NUM_DISPS_IN_COLUMN (NUM_CPUS / NUM_DISPS_IN_ROW)
#define DISP_TEX_WIDTH      (NUM_DISPS_IN_ROW * DISP_WIDTH)
#define DISP_TEX_HEIGHT     (NUM_DISPS_IN_COLUMN * DISP_HEIGHT)

static uint8_t rom_img[ROM_SIZE] = {0};
static uint8_t ram_img[RAM_SIZE * NUM_CPUS] = {0};
static cpu_t cpus[NUM_CPUS] = {0};
static device_t dev = {0};

static uint32_t rv_cycles_count = 0;
static float rv_cycles_time = 0.0;

int main(void)
{
    InitWindow(1280, 800, "RISC-V device on GPU");

    SetExitKey(KEY_F4);
    SetTraceLogLevel(LOG_DEBUG);

    char *rv_emu_code = LoadFileText("./rv_emu.glsl");
    unsigned int rv_emu_shader = rlCompileShader(rv_emu_code, RL_COMPUTE_SHADER);
    unsigned int rv_emu_prog = rlLoadComputeShaderProgram(rv_emu_shader);
    UnloadFileText(rv_emu_code);

    uint32_t n_cycles = 50000;
    int n_cycles_loc = rlGetLocationUniform(rv_emu_prog, "n_cycles");

    uint32_t time_ms = 0;
    int time_ms_loc = rlGetLocationUniform(rv_emu_prog, "time_ms");

    device_init(&dev,
                ROM_SIZE,   0x08000000,    /* FLASH */
                RAM_SIZE,   0x20000000,    /* RAM */
                40 + DISP_VRAM_SIZE, 0x01000000);  /* Peripherals: serial tx/rx, RTC, screen buffer 320x200 */

    // device_load_from_elf(&dev, "./build/prog05.elf");
    device_load_from_elf(&dev, "./doomgeneric/doomgeneric/doomrv.elf");

    memcpy(rom_img, dev.rom.data, dev.rom.size);

    for (int i = 0; i < NUM_CPUS; i++)
    {
        memcpy(ram_img + i * RAM_SIZE, dev.ram.data, RAM_SIZE);
        cpus[i].pc = dev.pc;
    }

    unsigned int ssbo_rom = rlLoadShaderBuffer(ROM_SIZE, rom_img, RL_DYNAMIC_COPY);
    unsigned int ssbo_ram = rlLoadShaderBuffer(sizeof(ram_img), ram_img, RL_DYNAMIC_COPY);
    unsigned int ssbo_cpus = rlLoadShaderBuffer(sizeof(cpus), cpus, RL_DYNAMIC_COPY);

    Image img = GenImageColor(DISP_TEX_WIDTH, DISP_TEX_HEIGHT, GRAY);
    Texture disp_texts[2] = {0};
    disp_texts[0] = LoadTextureFromImage(img);
    disp_texts[1] = LoadTextureFromImage(img);
    UnloadImage(img);
    int tex_idx = 0;

    while (!WindowShouldClose())
    {
        
        rlEnableShader(rv_emu_prog);

        time_ms = (uint32_t)(GetTime() * 1000.0);
        rlSetUniform(time_ms_loc, &time_ms, RL_SHADER_UNIFORM_UINT, 1);
        rlSetUniform(n_cycles_loc, &n_cycles, RL_SHADER_UNIFORM_UINT, 1);
        rlBindImageTexture(disp_texts[tex_idx].id, 0, RL_PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, false);
        rlBindShaderBuffer(ssbo_cpus, 1);
        rlBindShaderBuffer(ssbo_rom, 2);
        rlBindShaderBuffer(ssbo_ram, 3);
        rlComputeShaderDispatch(NUM_DISPS_IN_ROW, NUM_DISPS_IN_COLUMN, 1);
        rlDisableShader();

        rv_cycles_count += n_cycles;
        rv_cycles_time += GetFrameTime();

        if (rv_cycles_time >= 1.0)
        {
            printf("GPU CPU cycles per second: %u\n", (uint32_t)((float)rv_cycles_count / rv_cycles_time));
            rv_cycles_count = 0;
            rv_cycles_time = 0.0;
        }

        BeginDrawing();
        ClearBackground(WHITE);

        rlReadShaderBuffer(ssbo_cpus, cpus, sizeof(cpus), 0);

        bool all_done = true;
        for (int i = 0; i < NUM_CPUS; i++)
        {
            if (!cpus[i].periph[0x24])
            {
                all_done = false;
                break;
            }
        }

        if (all_done)
        {
            tex_idx = (tex_idx + 1) % 2;
            for (int i = 0; i < NUM_CPUS; i++)
            {
                cpus[i].periph[0x24] = 0;
            }
            rlUpdateShaderBuffer(ssbo_cpus, cpus, sizeof(cpus), 0);
        }

        DrawTexturePro(disp_texts[(tex_idx + 1) % 2],
                       (Rectangle){0.0, 0.0, DISP_TEX_WIDTH, DISP_TEX_HEIGHT},
                       (Rectangle){0.0, 0.0, 1280.0, 800.0},
                       (Vector2){0.0, 0.0}, 0.0, WHITE);

        DrawFPS(10, 10);
        
#ifdef EMU_CROSSCHECK
        bool emres = true;
        bool mismatch = false;

        for (int i = 0; i < n_cycles; i++)
        {
            emres = emres && device_run_cycle(&dev);
        }

        if (!emres)
        {
            printf("CPU CPU emulationg fail!\n");
            printf("PC: 0x%08X\n", dev.pc);
            break;
        }

        rlReadShaderBuffer(ssbo_ram, cpus, sizeof(cpu_t) * NUM_CPUS, 0);

        for (int cpuid = 0; cpuid < NUM_CPUS; cpuid++)
        {
            if (dev.pc != cpus[cpuid].pc || memcmp(dev.regs, cpus[cpuid].regs, sizeof(uint32_t) * 32))
            {
                printf("Emulation mismatch!\n");
                printf("CPU CPU PC: 0x%08X  GPU CPU PC: 0x%08X\n\n", dev.pc, cpus[cpuid].pc);

                for (int i = 0; i < 32; i++)
                {
                    printf("CPU CPU R%02u: 0x%08X  GPU CPU R%02u: 0x%08X\n", i, dev.regs[i], i, cpus[cpuid].regs[i]);
                }
                mismatch = true;
                printf("GPU CPU %u failed!\n", cpuid);
                break;
            }
        }

        if (mismatch)
        {
            break;
        }
#endif

        EndDrawing();

        if (IsKeyPressed(KEY_LEFT_BRACKET))
        {
            n_cycles /= 2;
            printf("RV-CPU cycles per dispatch decreased: %u\n", n_cycles);
        }

        if (IsKeyPressed(KEY_RIGHT_BRACKET))
        {
            n_cycles *= 2;
            printf("RV-CPU cycles per dispatch increased: %u\n", n_cycles);
        }

    }

    CloseWindow();

    return 0;
}