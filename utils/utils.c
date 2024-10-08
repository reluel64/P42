#include <stddef.h>
#include <serial.h>
#include <stdarg.h>
#include <stdint.h>
#include <utils.h>
#include <spinlock.h>

void* memset(void* ptr, int value, size_t num)
{
    uint8_t* start = (uint8_t*)ptr;
    size_t   pos = 0;
    size_t wval = value;
#if 0
    wval = value & 0xff;

    for (int i = 1; i < sizeof(size_t); i++)
    {
        wval |= (wval << 8 * i);
    }

    /* Zero unaligned memory byte by byte */
    while ((size_t)start % 2 && pos < num)
    {
        start[0] = (uint8_t)value;
        start++;
        pos++;
    }

    while (num - pos >= sizeof(size_t))
    {
        *(size_t*)(&start[pos]) = wval;
        pos += sizeof(size_t);
    }

    while (num - pos >= sizeof(uint32_t))
    {
        *(uint32_t*)(&start[pos]) = (uint32_t)wval;
        pos += sizeof(uint32_t);
    }

    while (num - pos >= sizeof(uint16_t))
    {
        *(uint16_t*)(&start[pos]) = (uint16_t)wval;
        pos += sizeof(uint16_t);
    }


#endif

    while (num - pos >= sizeof(uint8_t))
    {
        *(uint8_t*)(&start[pos]) = (uint8_t)wval;
        pos += sizeof(uint8_t);
    }

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
    for(size_t i = 0; i < num; i++)
    {
        if(((char*)ptr1)[i] != ((char*)ptr2)[i])
        {
            return(((char*)ptr1)[i] - ((char*)ptr2)[i]);
        }
    }
    return(0);
}


static char * itoa(unsigned long value, char * str, int base)
{
     char * rc;
    char * ptr;
    char * low;
    // Check for supported base.
    if ( base < 2 || base > 36 )
    {
        *str = '\0';
        return str;
    }
    rc = ptr = str;
    // Set '-' for negative decimals.
    if ( value < 0 && base == 10 )
    {
        *ptr++ = '-';
    }
    // Remember where the numbers start.
    low = ptr;
    // The actual conversion.
    do
    {
        // Modulo is negative for negative value. This trick makes abs() unnecessary.
        *ptr++ = "zyxwvutsrqponmlkjihgfedcba"
                 "9876543210123456789"
                 "abcdefghijklmnopqrstuvwxyz"[35 + value % base];
        value /= base;
    } while ( value );
    // Terminating the string.
    *ptr-- = '\0';
    // Invert the numbers.
    while ( low < ptr )
    {
        char tmp = *low;
        *low++ = *ptr;
        *ptr-- = tmp;
    }
    return rc;
}





int kprintf(char *fmt,...)
{
    
    va_list lst;
    va_start(lst,fmt);
    char *str = NULL;
    char ch = 0;
    char nbuf[64];
    uint64_t num = 0;
    
    while(fmt[0]!= '\0')
    {
        if(fmt[0] == '%')
        {
            switch(fmt[1])
            {
                case 's':
                {
                    str = va_arg(lst, char*);
                    for(int i = 0; str[i]; i++)
                    {
                        write_serial(str[i]);
                    }
                    break;
                }
                case 'c':
                {
                    ch = va_arg(lst, int);
                    write_serial(ch);
                    break;
                }
                case 'd':
                case 'i':
                 {
                    num = va_arg(lst,int64_t);
                    itoa((int64_t)num, nbuf, 10);
                    for(int i = 0; nbuf[i]; i++)
                    {
                        write_serial(nbuf[i]);
                    }
                    break;
                }
                case 'x':
                {
                    num = va_arg(lst,uint64_t);
                    itoa(num, nbuf,  16);
                    for(int i = 0; nbuf[i]; i++)
                    {
                        write_serial(nbuf[i]);
                    }
                    break;
                }

                default:
                 write_serial(fmt[1]);
                break;

            }
            fmt++;
        }
        else
        {
        write_serial(fmt[0]);
        }
        fmt++;
    }
    va_end(lst);

    return(0);
}

#if 0
int strcmp(const char *str1, const char *str2)
{
    const unsigned char *p1 = (const unsigned char*)str1;
    const unsigned char *p2 = (const unsigned char*)str2;

    while(*p1 == *p2 && *p1)
    {
        p1++;
        p2++;
    }

    return(*p1 - *p2);
}

size_t strlen(const char *str)
{
    size_t i = 0;

    while(*str)
    {
        str++;
        i++;
    }

    return(i);
}
#endif



void  *binary_search
(
    const void *array,
    const size_t elem_count,
    size_t elem_sz,
    int (*compare)(void *elem, void *pv),
    void *pv
)
{
    uint8_t * start = NULL;
    size_t mid = 0;
    void *elem = NULL;
    int cmp = 0;

    start = (uint8_t *) array;

    for(mid = elem_count; mid != 0; mid >>= 1)
    {
        /* start at half the interval */
        elem = (void*)(start +  (mid >> 1) * elem_sz);

        cmp = compare(elem, pv);

        /* found it - break the loop */
        if(cmp == 0)
        {
            break;
        }
        
        /* key > elem  - we have to go on the right */
        if(cmp > 0)
        {  
            /* advance the new half */
            start = (uint8_t *)elem + elem_sz;
            mid --;
        }

        /* otherwise we keep going left by dividing the interval in 2*/

        /* make sure the elem is set to NULL in case we wnd the loop */
        elem = NULL;
    }

    return(elem);
}


int insertion_sort
(
    void *array,
    const size_t element_count,
    const size_t element_sz,
    int (*compare) (void *left, void *right, void *pv),
    void *pv
)
{
    size_t i = 1;
    size_t j = 0;
    uint8_t *left = NULL;
    uint8_t *right = NULL;
    int ret = -1;
    uint8_t temp = 0;
    uint8_t stop = 0;

    if(compare != NULL && array != NULL)
    {
        while(i < element_count)
        {
            j = i;

            while(j > 0)
            {
                left = (uint8_t *)array + ((j - 1) * element_sz);
                right = (uint8_t *)array + (j * element_sz);

                ret = compare(left, right, pv);

                if(ret > 0)
                {
                    for(size_t e = 0; e < element_sz; e++)
                    {
                        temp = *left;
                        *left = *right;
                        *right = temp;
                        left++;
                        right++;
                    }

                    ret = 0;
                }
                else if(ret < 0)
                {
                    stop = 1;
                    break;
                }

                j--;
            }

            i++;

            if(stop)
            {
                break;
            }
        }
    }

    return(ret);
}