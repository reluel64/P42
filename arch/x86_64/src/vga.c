#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <utils.h>
#include <vmmgr.h>
#define FB_PHYS_MEM (0xB8000)
#define VGA_MAX_ROW (25)
#define VGA_MAX_COL (80)
#define VGA_POS(x,y) (((x) + ((y) * VGA_MAX_ROW)))
#define FB_LEN (VGA_MAX_ROW * VGA_MAX_COL * sizeof(uint16_t))

typedef struct _vga
{
    uint16_t *base;
    uint8_t row;
    uint8_t col;
 
}vga_t;

static vga_t vga;

void vga_init()
{
    vga.base = (uint16_t*)vmmgr_map(NULL, FB_PHYS_MEM, 0, FB_LEN, VMM_ATTR_WRITABLE);


    vga.col = 0;
    vga.row = 0;
}

void vga_scroll()
{
    uint16_t *current_line = vga.base;
    uint16_t *next_line = current_line + (VGA_MAX_ROW * VGA_MAX_COL);

    for(uint16_t r = 0; r < VGA_MAX_ROW ; r++)
    {
        
        memcpy(current_line, next_line, sizeof(uint16_t) * VGA_MAX_COL);
        memset(next_line,0,sizeof(uint16_t) * VGA_MAX_COL);

        current_line = next_line;
        next_line+=VGA_MAX_COL;
    }

    vga.row = (vga.row > VGA_MAX_ROW ? (vga.row - VGA_MAX_ROW -1) : 0);
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
    if(vga.base == 0)
        return(0);
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