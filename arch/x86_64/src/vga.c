#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#define FB_MEM (0xFFFFFFFF80000000 +  0xB8000)
#define VGA_MAX_ROW (25)
#define VGA_MAX_COL (80)
#define VGA_POS(x,y) (((x) + ((y) * VGA_MAX_ROW)))

typedef struct _vga
{
    uint16_t *base;
    uint8_t row;
    uint8_t col;
 
}vga_t;

static vga_t vga;

void vga_init()
{
    vga.base = (uint16_t*)FB_MEM;
    vga.col = 0;
    vga.row = 0;
}

void vga_scroll()
{
    uint16_t *current_line = vga.base;
    uint16_t *next_line = current_line + (VGA_MAX_ROW * VGA_MAX_COL * sizeof(uint16_t));

    for(uint16_t r = 0; r < VGA_MAX_ROW ; r--)
    {
        
        memcpy(current_line, next_line, sizeof(uint16_t) * VGA_MAX_COL);
        memset(next_line,0,sizeof(uint16_t) * VGA_MAX_COL);

        current_line = next_line;
        next_line+=VGA_MAX_COL;
    }

    vga.row = (vga.row > VGA_MAX_ROW ? (vga.row - VGA_MAX_ROW) : 0);
    vga.col = 0;
}

void vga_print(uint8_t *buf, uint8_t color, uint64_t len)
{
    uint16_t vga_ch = (uint16_t)color << 8; /* set the color now */
    uint8_t ch = 0;
    uint16_t pos;
    if(color == 0x0)
        color = 0x7;
    for(uint64_t i = 0; i < len && buf[i] != 0; i++)
    {
        ch = buf[i];

        /* This is a newline character
         * so we must increase the row and move the col to 0
         */
       if(ch == '\n')
       {
           vga.row++;
           vga.col = 0;
           continue;
       }

        if(vga.col >= VGA_MAX_COL)
        {
            vga.row++;
            vga.col = 0;
        }

       if(vga.row >= VGA_MAX_ROW)
       {
           vga_scroll();
       }

    
    pos = vga.row * VGA_MAX_COL + vga.col;
    vga_ch &= 0xff00; /* clear the character part */
    vga.base[pos] = (vga_ch | ch);
    vga.col++;

   }
}


int  vga_print_ch(uint8_t ch)
{
    uint16_t vga_ch = (uint16_t)0x7 << 8; /* set the color now */
    uint16_t pos;

    /* This is a newline character
     * so we must increase the row and move the col to 0
     */
    if(ch == '\n')
    {
        vga.row++;
        vga.col = 0;
        return 0;
    }

    if(vga.col >= VGA_MAX_COL)
    {
        vga.row++;
        vga.col = 0;
    }

    if(vga.row >= VGA_MAX_ROW)
    {
        vga_scroll();
    }

    pos = vga.row * VGA_MAX_COL + vga.col;
    vga_ch &= 0xff00; /* clear the character part */
    vga.base[pos] = (vga_ch | ch);
    vga.col++;

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
        *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"[35 + value % base];
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
                        vga_print_ch(str[i]);
                    }
                    break;
                }
                case 'c':
                {
                    ch = va_arg(lst, int);
                    vga_print_ch(ch);
                    break;
                }
                case 'd':
                case 'x':
                {
                    num = va_arg(lst,uint64_t);
                    itoa(num, nbuf, fmt[1]== 'd'? 10 : 16);
                    for(int i = 0; nbuf[i]; i++)
                    {
                        vga_print_ch(nbuf[i]);
                    }
                    break;
                }

                default:
                 vga_print_ch(fmt[1]);
                break;

            }
            fmt++;
        }
        else
        {
        vga_print_ch(fmt[0]);
        }
        fmt++;
    }
}