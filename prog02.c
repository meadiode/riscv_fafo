
#include <stdint.h>
#include "system.h"
#include "printf.h"

volatile float foo = 1.0;

int main(void)
{
    const float N = 1.42069;

    printf("\n");
    for (int i = 0; i < 10; i++)
    {
        printf("N^%d = %0.8f\n", i, foo);
        foo *= N;
    }
}