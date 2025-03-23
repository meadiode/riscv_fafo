
#include <stddef.h>

#include "system.h"

extern char __heap_start[];
extern char __heap_end[];
static char *current_break = NULL;


#define TX_DATA ((char*)SERIAL_TX_DATA_ADDR)
#define TX_FLAG ((char*)SERIAL_TX_FLAG_ADDR)

void *_sbrk_r(void *reent_ptr, int nbytes)
{
    if (!current_break)
    {
        current_break = __heap_start;
    }
    
    char *old_break = current_break;
    current_break += nbytes;

    return old_break;
}


void _putchar(int c)
{
    TX_DATA[0] = (char)c;
    TX_FLAG[0] = 1;
}


int puts(const char *str)
{
    int idx = 0;
    while (str[idx])
    {
        _putchar(str[idx]);
        idx++;
    }

    return 1;
}
