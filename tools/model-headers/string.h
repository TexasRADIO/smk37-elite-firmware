/* Declarations for the compile-only layout model. There is no C runtime link. */
#ifndef MODEL_STRING_H
#define MODEL_STRING_H
#include <stddef.h>
void *memset(void *destination, int byte, size_t count);
void *memmove(void *destination, const void *source, size_t count);
void *memcpy(void *destination, const void *source, size_t count);
int memcmp(const void *left, const void *right, size_t count);
#endif
