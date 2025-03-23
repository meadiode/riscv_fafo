#include "system.h"

char test_str[] = "the quick brown fox jumps over the lazy dog 123";

void my_toupper(char *s)
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


int my_strcmp(const char *s1, const char *s2)
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
    my_toupper(test_str);
    
    puts(test_str);
    _putchar('\n');

    int res;
    res = my_strcmp(test_str, "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 123");
    puts(res == 0 ? "TEST 1: OK\n" : "TEST 1: NOT OK\n");

    res = my_strcmp(test_str, "THE qUICK BROWN FOX JUMPS OVER THE LAZY DOG 123");
    puts(res > 0 ? "TEST 2: OK\n" : "TEST 2: NOT OK\n");

    res = my_strcmp(test_str, "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 12");
    puts(res < 0 ? "TEST 3: OK\n" : "TEST 3: NOT OK\n");
}