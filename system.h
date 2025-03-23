
#ifndef __SYSTEM_H
#define __SYSTEM_H

#define SERIAL_TX_DATA_ADDR 0x01000000
#define SERIAL_TX_FLAG_ADDR 0x01000001
#define SERIAL_RX_DATA_ADDR 0x01000002
#define SERIAL_RX_FLAG_ADDR 0x01000003

#define DISP_VSYNC_FLAG_ADDR 0x01000004
#define DISP_VRAM_ADDR       0x01000008

#define DISP_WIDTH     320
#define DISP_HEIGHT    200
#define DISP_VRAM_SIZE (DISP_WIDTH * DISP_HEIGHT * 3)

#define DISP_SET_PIXEL_R(x, y, val) (((char*)(DISP_VRAM_ADDR + ((y) * DISP_WIDTH + (x)) * 3))[0] = (val))
#define DISP_SET_PIXEL_G(x, y, val) (((char*)(DISP_VRAM_ADDR + ((y) * DISP_WIDTH + (x)) * 3))[1] = (val))
#define DISP_SET_PIXEL_B(x, y, val) (((char*)(DISP_VRAM_ADDR + ((y) * DISP_WIDTH + (x)) * 3))[2] = (val))
#define DISP_FLUSH() (((volatile char*)(DISP_VSYNC_FLAG_ADDR))[0] = 1)

void _putchar(char c);
int puts(const char* str);


#endif