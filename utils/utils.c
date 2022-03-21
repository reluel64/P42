#include <stddef.h>
#include <serial.h>
#include <stdarg.h>
#include <stdint.h>
#include <utils.h>
#include <spinlock.h>

static spinlock_t kprintf_lock = SPINLOCK_INIT;

void* memset(void* ptr, int value, size_t num)
{
    uintptr_t mem = (uintptr_t)ptr;
    size_t   pos = 0;
    size_t   loops = 0;
    uint64_t fill = 0;

    fill = value;

    fill |= fill << 8  | fill << 16 | 
            fill << 24 | fill << 32 | 
            fill << 40 | fill << 48 |
            fill << 56;

    while(pos < num)
    {

        if(((num - pos) >= sizeof(uint64_t)) && 
            ((mem + pos) % sizeof(uint64_t)) == 0)
        {
            loops = ((num - pos) / sizeof(uint64_t));

            while(loops > 0)
            {
                *((uint64_t*)(mem + pos)) = fill;
                pos += sizeof(uint64_t);
                loops--;
            }
        }

        else if(((num - pos) >= sizeof(uint32_t)) && 
            ((mem + pos) % sizeof(uint32_t)) == 0)
        {
        
            loops = (num - pos) / sizeof(uint32_t);
            while(loops > 0)
            {
                *((uint32_t*)(mem + pos)) = (uint32_t)fill;
                pos += sizeof(uint32_t);
                loops --;
            }
        }


        else if(((num - pos) >= sizeof(uint16_t)) && 
            ((mem + pos) % sizeof(uint16_t)) == 0)
        {
           
            loops = (num - pos) / sizeof(uint16_t);
            while(loops > 0)
            {
                *((uint16_t*)(mem + pos)) = (uint16_t)fill;
                pos += sizeof(uint16_t);
                loops--;
            }
            
        }
        else
        {
            *((uint8_t*)(mem + pos)) = (uint8_t)fill;
            pos += sizeof(uint8_t);
        }
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
    
    spinlock_lock_int(&kprintf_lock);
    
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
    spinlock_unlock_int(&kprintf_lock);
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