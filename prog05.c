
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "system.h"

#define ABS(x) ((x) > 0 ? (x) : -(x))
#define DEG2RAD (M_PI / 180.0)

#define TORUS_OSEGS 22
#define TORUS_ISEGS 22
#define TORUS_OR    0.35
#define TORUS_IR    0.2

#define N_VERTICES  (TORUS_ISEGS * TORUS_OSEGS)
#define N_LINES     (TORUS_ISEGS * TORUS_OSEGS * 2)

typedef float matrix_t[16];
float vertices[3 * N_VERTICES] = {0};
uint16_t indices[N_LINES * 2] = {0};


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


void mat_mul(const float *mat_a, const float *mat_b, float *res)
{
    res[0]  = mat_a[0] *  mat_b[0] + mat_a[1]  * mat_b[4] + mat_a[2]  * mat_b[8]  + mat_a[3]  * mat_b[12];
    res[1]  = mat_a[0] *  mat_b[1] + mat_a[1]  * mat_b[5] + mat_a[2]  * mat_b[9]  + mat_a[3]  * mat_b[13];
    res[2]  = mat_a[0] *  mat_b[2] + mat_a[1]  * mat_b[6] + mat_a[2]  * mat_b[10] + mat_a[3]  * mat_b[14];
    res[3]  = mat_a[0] *  mat_b[3] + mat_a[1]  * mat_b[7] + mat_a[2]  * mat_b[11] + mat_a[3]  * mat_b[15];
    res[4]  = mat_a[4] *  mat_b[0] + mat_a[5]  * mat_b[4] + mat_a[6]  * mat_b[8]  + mat_a[7]  * mat_b[12];
    res[5]  = mat_a[4] *  mat_b[1] + mat_a[5]  * mat_b[5] + mat_a[6]  * mat_b[9]  + mat_a[7]  * mat_b[13];
    res[6]  = mat_a[4] *  mat_b[2] + mat_a[5]  * mat_b[6] + mat_a[6]  * mat_b[10] + mat_a[7]  * mat_b[14];
    res[7]  = mat_a[4] *  mat_b[3] + mat_a[5]  * mat_b[7] + mat_a[6]  * mat_b[11] + mat_a[7]  * mat_b[15];
    res[8]  = mat_a[8] *  mat_b[0] + mat_a[9]  * mat_b[4] + mat_a[10] * mat_b[8]  + mat_a[11] * mat_b[12];
    res[9]  = mat_a[8] *  mat_b[1] + mat_a[9]  * mat_b[5] + mat_a[10] * mat_b[9]  + mat_a[11] * mat_b[13];
    res[10] = mat_a[8] *  mat_b[2] + mat_a[9]  * mat_b[6] + mat_a[10] * mat_b[10] + mat_a[11] * mat_b[14];
    res[11] = mat_a[8] *  mat_b[3] + mat_a[9]  * mat_b[7] + mat_a[10] * mat_b[11] + mat_a[11] * mat_b[15];
    res[12] = mat_a[12] * mat_b[0] + mat_a[13] * mat_b[4] + mat_a[14] * mat_b[8]  + mat_a[15] * mat_b[12];
    res[13] = mat_a[12] * mat_b[1] + mat_a[13] * mat_b[5] + mat_a[14] * mat_b[9]  + mat_a[15] * mat_b[13];
    res[14] = mat_a[12] * mat_b[2] + mat_a[13] * mat_b[6] + mat_a[14] * mat_b[10] + mat_a[15] * mat_b[14];
    res[15] = mat_a[12] * mat_b[3] + mat_a[13] * mat_b[7] + mat_a[14] * mat_b[11] + mat_a[15] * mat_b[15];
}


void mat_make_tx(float *mat,
                 float px, float py, float pz,
                 float ax, float ay, float az,
                 float sx, float sy, float sz)
{

    float mat_s[16] = 
    {
        sx,  0.0, 0.0, 0.0,
        0.0,  sy, 0.0, 0.0,
        0.0, 0.0,  sz, 0.0,
        0.0, 0.0, 0.0, 1.0,
    };

    float csx = cosf(ax);
    float csy = cosf(ay);
    float csz = cosf(az);
    float snx = sinf(ax);
    float sny = sinf(ay);
    float snz = sinf(az);

    float mat_rx[16] =
    {
        1.0, 0.0,  0.0, 0.0,
        0.0, csx, -snx, 0.0,
        0.0, snx,  csx, 0.0,
        0.0, 0.0,  0.0, 1.0,        
    };

    float mat_ry[16] =
    {
         csy, 0.0, sny, 0.0,
         0.0, 1.0, 0.0, 0.0,
        -sny, 0.0, csy, 0.0,
         0.0, 0.0, 0.0, 1.0,  
    };

    float mat_rz[16] =
    {
        csz, -snz, 0.0, 0.0,
        snz,  csz, 0.0, 0.0,
        0.0,  0.0, 1.0, 0.0,
        0.0,  0.0, 0.0, 1.0,
    };

    mat_mul(mat_rx, mat_ry, mat);
    mat_mul(mat, mat_rz, mat_rx);
    mat_mul(mat_rx, mat_s, mat);

    mat[3 * 4 + 0] = px;
    mat[3 * 4 + 1] = py;
    mat[3 * 4 + 2] = pz;
}


void transform_vertices(const float *verts, const float *mat, float *tx_verts, int n_vertices)
{
    for (int i = 0; i < n_vertices; i++)
    {
        float x = verts[i * 3 + 0];
        float y = verts[i * 3 + 1];
        float z = verts[i * 3 + 2];
        tx_verts[i * 3 + 0] = mat[0] * x + mat[4] * y + mat[8]  * z + mat[12];
        tx_verts[i * 3 + 1] = mat[1] * x + mat[5] * y + mat[9]  * z + mat[13];
        tx_verts[i * 3 + 2] = mat[2] * x + mat[6] * y + mat[10] * z + mat[14];
    }
}


void init_torus(void)
{
    const float ALPHA = (360.0 / TORUS_OSEGS) * DEG2RAD; 
    const float BETA = (360.0 / TORUS_ISEGS) * DEG2RAD; 

    for (int i = 0; i < TORUS_OSEGS; i++)
    {
        for (int j = 0; j < TORUS_ISEGS; j++)
        {
            float px = cosf(BETA * j) * TORUS_IR + TORUS_OR;
            float pz = sinf(BETA * j) * TORUS_IR;
            float rpx = cosf(ALPHA * i) * px;
            float rpy = sinf(ALPHA * i) * px;

            vertices[(i * TORUS_ISEGS + j) * 3 + 0] = rpx; 
            vertices[(i * TORUS_ISEGS + j) * 3 + 1] = rpy; 
            vertices[(i * TORUS_ISEGS + j) * 3 + 2] = pz;

            uint16_t n_iidx = i < (TORUS_OSEGS - 1) ? (i + 1) : 0;
            uint16_t n_jidx = j < (TORUS_ISEGS - 1) ? (j + 1) : 0;

            indices[(i * TORUS_ISEGS + j) * 4 + 0] = TORUS_ISEGS * i + j;
            indices[(i * TORUS_ISEGS + j) * 4 + 1] = TORUS_ISEGS * i + n_jidx;
            indices[(i * TORUS_ISEGS + j) * 4 + 2] = TORUS_ISEGS * i + j; 
            indices[(i * TORUS_ISEGS + j) * 4 + 3] = TORUS_ISEGS * n_iidx + j;
        }
    }
}


int main(void)
{

    init_torus();

    float tx_verts[sizeof(vertices)] = {0};
    float tx_mat[16] = {0};
    float t = 0.0;

    for (int i = 0; i < 10000; i++)
    {
        memset((char*)(DISP_VRAM_ADDR), 0xff, DISP_VRAM_SIZE);
        
        mat_make_tx(tx_mat,
                    160.0, 0.0, 100.0,
                    45.0 * DEG2RAD, 19.0 * DEG2RAD + t * 0.7, t,
                    160.0, 160.0, 160.0);

        transform_vertices(vertices, tx_mat, tx_verts, N_VERTICES);

        for (int i = 0; i < N_LINES; i++)
        {
            uint16_t idx1 = indices[i * 2 + 0];
            uint16_t idx2 = indices[i * 2 + 1];

            int px1 = (int)(tx_verts[idx1 * 3 + 0]);
            int py1 = (int)(tx_verts[idx1 * 3 + 2]);

            int px2 = (int)(tx_verts[idx2 * 3 + 0]);
            int py2 = (int)(tx_verts[idx2 * 3 + 2]);

            draw_line(px1, py1, px2, py2, 0x202020);
        }
        DISP_FLUSH();

        t += 0.05;
    }
}
