/* SPDX-License-Identifier: GPL-3.0-only */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Freestanding runtime for the link proof. Build with -fno-builtin. */
void *memset(void *destination, int byte, size_t count)
{
    unsigned char *write = destination;
    while (count--)
        *write++ = (unsigned char)byte;
    return destination;
}

void *memcpy(void *destination, const void *source, size_t count)
{
    unsigned char *write = destination;
    const unsigned char *read = source;
    while (count--)
        *write++ = *read++;
    return destination;
}

void *memmove(void *destination, const void *source, size_t count)
{
    if ((uintptr_t)destination <= (uintptr_t)source)
        return memcpy(destination, source, count);
    unsigned char *write = destination;
    const unsigned char *read = source;
    while (count) {
        --count;
        write[count] = read[count];
    }
    return destination;
}

int memcmp(const void *left, const void *right, size_t count)
{
    const unsigned char *a = left;
    const unsigned char *b = right;
    for (size_t i = 0; i < count; ++i)
        if (a[i] != b[i])
            return a[i] - b[i];
    return 0;
}
