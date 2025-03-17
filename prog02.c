
#include <stdint.h>

#define OUTPUT  ((char*)0x01000000)

volatile float foo = 0.0;
volatile float bar = 12.324;

int main(void)
{
    foo = 42.69;
    foo /= bar;
}