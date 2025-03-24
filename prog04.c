
#include <stdint.h>
// #include <stdlib.h>
#include <string.h>
#include <math.h>
#include "system.h"
#include "printf.h"


#define ABS(x) ((x) > 0 ? (x) : -(x))

void put_pixel(int x, int y, int col)
{
    DISP_SET_PIXEL_R(x, y, col & 0xff);
    DISP_SET_PIXEL_G(x, y, (col >> 8) & 0xff);
    DISP_SET_PIXEL_B(x, y, (col >> 16) & 0xff);
}

void draw_line(int px1, int py1, int px2, int py2, int col)
{
    int dx = ABS(px2 - px1);
    int dy = -ABS(py2 - py1);
    int sx = px1 < px2 ? 1 : -1;
    int sy = py1 < py2 ? 1 : -1;
    int err = dx + dy;

    for (;;)
    {
        put_pixel(px1, py1, col);
        int e2 = err * 2;

        if (e2 >= dy)
        {
            if (px1 == px2)
            {
                break;
            }

            err += dy;
            px1 += sx;
        }

        if (e2 <= dx)
        {
            if (py1 == py2)
            {
                break;
            }

            err += dx;
            py1 += sy;
        }
    }
}


int main(void)
{
    const int N_POINTS = 120;
    const float T_STEP = M_PI * 2.0 / (float)N_POINTS;

    float alpha = 2.0;
    float beta = 3.0;
    float delta = M_PI / 2.0;

    for (int i = 0; i < 1000; i++)
    {
        float t = 0.0;
        int p_ix = 0;
        int p_iy = 0;

        memset((char*)(DISP_VRAM_ADDR), 0x28, DISP_VRAM_SIZE);

        for (int it = 0; it <= N_POINTS; it++)
        {
            int ix = DISP_WIDTH / 2 + (int)(sinf(t * alpha + delta) * (float)(DISP_WIDTH / 2 - 5));
            int iy = DISP_HEIGHT / 2 + (int)(sinf(t * beta) * (float)(DISP_HEIGHT / 2 - 5));
        
            if (it > 0)
            {
                draw_line(ix, iy, p_ix, p_iy, 0x00ff00);
            }

            p_ix = ix;
            p_iy = iy;

            t += T_STEP;
        }

        delta += 0.05;
        DISP_FLUSH();
    }
}
