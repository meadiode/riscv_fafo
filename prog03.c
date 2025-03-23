
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "system.h"
#include "printf.h"


int main(void)
{
    float t = 0.0;

    for (int i = 0; i < 200; i++)
    {
        memset((char*)(DISP_VRAM_ADDR), 0x28, DISP_VRAM_SIZE);

        for (int ix = 0; ix < DISP_WIDTH; ix++)
        {
            float y = sinf(t + ((float)ix / (float)DISP_WIDTH) * M_PI * 3.0);
            int iy = DISP_HEIGHT / 2 + (int)(y * (DISP_HEIGHT / 2 - 10));
            DISP_SET_PIXEL_G(ix, iy, 0xff);
        }
        DISP_FLUSH();

        t += 0.15;
    }
}