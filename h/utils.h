#ifndef utils_h
#define utils_h

#include <stddef.h>
#include <stdint.h>

void  *binary_search
(
    const void *array,
    const size_t elem_count,
    size_t elem_sz,
    int (*compare)(void *elem, void *pv),
    void *pv
);

int insertion_sort
(
    void *array,
    const size_t element_count,
    const size_t element_sz,
    int (*compare) (void *left, void *right, void *pv),
    void *pv
);

void *memset(void *ptr, int value, size_t num);
void *memcpy(void *dest, const void *src, size_t num);
int memcmp(const void *ptr1, const void *ptr2, size_t num);
int kprintf(char *fmt,...);

int strcmp(const char *str1, const char *str2);
size_t strlen(const char *str);

#ifndef min
    #define min(x,y) ((x) < (y) ? (x) : (y))
#endif

#ifndef max
    #define max(x,y) ((x) > (y) ? (x) : (y))
#endif

#define ALIGN_UP(value,align) (((value) + ((align) - 1)) & ~((align) - 1))
#define ALIGN_DOWN(value,align) ((value) & (~((align) - 1)))
#endif