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

void vga_write(uint8_t *buf)
{
    
    for(int i = 0; buf[i]; i++)
    {
        if(buf[i] == '\n' || vga.row >=VGA_MAX_ROW)
        {
            if(vga.row + 1 < VGA_MAX_ROW)
            {
                vga.row++;
                vga.col = 0;
                continue;
            }
            else
            {
                for(int r = 1; r < VGA_MAX_ROW; r++)
                {
                    for(int c = 0; c < VGA_MAX_COL; c++ )
                    {
                        vga.base[VGA_POS(c, r-1)] = vga.base[VGA_POS(c, r)];
                    }
                }


                   vga.col = 0;
                vga.row = VGA_MAX_ROW -1;
                /* Clear the last row */
                for(int c = 0; c<VGA_MAX_COL;c++)
                {
                    vga.base[VGA_POS(c, vga.row)] = 0;
                }

             

            }
        }
       
        if(vga.row < VGA_MAX_ROW && vga.col < VGA_MAX_COL)
            vga.base[VGA_POS(vga.col,vga.row)] = (uint16_t)buf[i] | (0x7 << 8); 

            if(++vga.col >= VGA_MAX_COL)
            {
                vga.row++;
                vga.col = 0;
            }

            
    }
}