
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "system.h"
#include "printf.h"


int main(void)
{
    float t = 0.0;

    for (int i = 0; i < 10000; i++)
    {
        memset((char*)(DISP_VRAM_ADDR), 0xff, DISP_VRAM_SIZE);

        for (int ix = 0; ix < DISP_WIDTH; ix += 2)
        {
            float y = sinf(t + ((float)ix / (float)DISP_WIDTH) * M_PI * 3.0);
            int iy = DISP_HEIGHT / 2 + (int)(y * (DISP_HEIGHT / 2 - 10));
            DISP_SET_PIXEL_R(ix, iy, 0x20);
            DISP_SET_PIXEL_G(ix, iy, 0x20);
            DISP_SET_PIXEL_B(ix, iy, 0x20);
        }
        DISP_FLUSH();

        t += 0.15;
        if (t >= M_PI)
        {
            t -= M_PI;
        }
    }
}