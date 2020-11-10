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
    vga.base = (uint16_t*)vmmgr_map(NULL, 
                                    FB_PHYS_MEM, 
                                    0, 
                                    FB_LEN, 
                                    VMM_ATTR_WRITABLE|
                                    VMM_ATTR_STRONG_UNCACHED
                                    );

    vga.col = 0;
    vga.row = 0;

    /* clear the buffer */
    memset(vga.base, 0, FB_LEN);
}

int vga_print_internal(uint8_t *buf)
{
    size_t len = 0;
    uint32_t pos = 0;
    size_t clear_len = 0;

    len = strlen(buf);

    for(pos = 0; pos < len; pos++)
    {
        if(vga.col + vga.row * VGA_MAX_COL >= VGA_MAX_ROW * VGA_MAX_COL)
        {
            /* Scroll the text up a line */
            memcpy(vga.base, &vga.base[VGA_MAX_COL], 
                   sizeof(uint16_t) * 
                   (VGA_MAX_COL) * 
                   (VGA_MAX_ROW - 1)
                  );

            /* update the variables */
            vga.row = VGA_MAX_ROW - 1;
            vga.col = 0;

            /* clear the last line */
            memset(&vga.base[vga.col + VGA_MAX_COL * vga.row],
                    0, 
                    sizeof(uint16_t) * VGA_MAX_COL);
        }

        if(buf[pos] == '\n')
        {
            vga.row++;
            vga.col = 0;
            continue;
        }
        /* write to the framebuffer */
        vga.base[vga.col + VGA_MAX_COL * vga.row] = 
                    (uint16_t)buf[pos]  | (0x7 << 8);

        /* Next character */
        vga.col++;

        /* If we reached the end of the line, then
         * do a new line
         */ 
        if(vga.col >= VGA_MAX_COL)
        {
            vga.col = 0;
            vga.row++;
        }
    }
}


void vga_print(uint8_t *buf, uint8_t color, uint64_t len)
{
    uint16_t vga_ch = (uint16_t)color << 8; /* set the color now */
    uint8_t ch = 0;
    uint16_t pos;


    vga_print_internal(buf);
#if 0
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

   #endif
}
