#include <stddef.h>


void *memset(void *ptr, int value, size_t num)
{
    for(size_t i = 0; i < num; i++)
        ((char*)ptr)[i] = (char)value;

    return(ptr);
}

void *memcpy(void *dest, const void *src, size_t num)
{
    for(size_t i = 0; i < num; i++)
        ((char*)dest)[i] = ((char*)src)[i];

    return(dest);
}

int memcmp(const void *ptr1, const void *ptr2, size_t num)
{
    char byte1 = 0;
    char byte2 = 0;

    for(size_t i = 0; i < num; i++)
    {
        if(((char*)ptr1)[i] != ((char*)ptr2)[i])
        {
            return(((char*)ptr1)[i] - ((char*)ptr2)[i]);
        }
    }
    return(0);
}
