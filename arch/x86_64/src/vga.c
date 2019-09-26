#include <stdint.h>

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

void vga_scroll(uint16_t rows)
{
    uint16_t *scr_start = vga.base + vga.row * VGA_MAX_COL;

    for(uint16_t r = rows * VGA_MAX_COL; r > 0 ; r--)
    {
        scr_start[r - 1] = scr_start[r]; 
    }

    vga.row = (vga.row > rows ? (vga.row - rows) : 0);
    vga.col = 0;
}

void vga_print(uint8_t *buf, uint8_t color, uint64_t len)
{
    uint16_t vga_ch = (uint16_t)color << 8; /* set the color now */
    uint8_t ch = 0;
    uint16_t pos;

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
           vga_scroll(25);
       }

    
    pos = vga.row * VGA_MAX_COL + vga.col;
    vga_ch &= 0xff00; /* clear the character part */
    vga.base[pos] = (vga_ch | ch);
    vga.col++;

   }
}