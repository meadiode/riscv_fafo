
#include <stdint.h>

#define OUTPUT  ((char*)0x01000000)

char test_str[] = "the quick brown fox jumps over the lazy dog 123";

void toupper(char *s)
{
    while (*s)
    {
        if (*s >= 'a' && *s <= 'z')
        {
            *s -= ('a' - 'A');
        }
        s++;
    }
}


int strcmp(const char *s1, const char *s2)
{
    while (*s1 != 0 && *s1 == *s2)
    {
        s1++;
        s2++;
    }

    return *s2 - *s1;
}


int main(void)
{
    toupper(test_str);

    int res;

    res = strcmp(test_str, "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 123");
    OUTPUT[0] = res > 0 ? 1 : res < 0 ? -1 : 0;

    res = strcmp(test_str, "THE qUICK BROWN FOX JUMPS OVER THE LAZY DOG 123");
    OUTPUT[1] = res > 0 ? 1 : res < 0 ? -1 : 0;

    res = strcmp(test_str, "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 12");
    OUTPUT[2] = res > 0 ? 1 : res < 0 ? -1 : 0;


}