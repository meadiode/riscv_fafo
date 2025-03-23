
#ifndef __SYSTEM_H
#define __SYSTEM_H

#define SERIAL_TX_DATA_ADDR 0x01000000
#define SERIAL_TX_FLAG_ADDR 0x01000001
#define SERIAL_RX_DATA_ADDR 0x01000002
#define SERIAL_RX_FLAG_ADDR 0x01000003

#define VRAM_ADDR 0x01000004

void _putchar(int c);
int puts(const char* str);


#endif