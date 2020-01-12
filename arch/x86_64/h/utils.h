#ifndef _utilsh_
#define _utilsh_

#include <stddef.h>
#include <stdint.h>

void *memset(void *ptr, int value, size_t num);
void *memcpy(void *dest, const void *src, size_t num);
int memcmp(const void *ptr1, const void *ptr2, size_t num);
int kprintf(char *fmt,...);
#ifndef min
    #define min(x,y) ((x) < (y) ? (x) : (y))
#endif

#ifndef max
    #define max(x,y) ((x) > (y) ? (x) : (y))
#endif

#define ALIGN_UP(value,align) (((value) + (align - 1)) & ~(align - 1))
#define ALIGN_DOWN(value,align) ((value) & (~(align - 1)))
#endif