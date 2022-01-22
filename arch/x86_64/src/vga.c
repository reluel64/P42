#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <utils.h>
#include <vm.h>

#define FB_PHYS_MEM (0xB8000)
#define VGA_MAX_ROW (25)
#define VGA_MAX_COL (80)

#define VGA_POS(x,y)  (((x) + ((y) * VGA_MAX_ROW)))
#define FB_LEN        (VGA_MAX_ROW * VGA_MAX_COL * sizeof(uint16_t))
#define FB_ALLOC_SIZE  ALIGN_UP(FB_LEN, PAGE_SIZE)

typedef struct _vga
{
    uint16_t *base;
    uint8_t row;
    uint8_t col;
    spinlock_t lock;
}vga_t;

static vga_t vga;

void vga_init()
{
    vga.base = (uint16_t*)vm_map(NULL, VM_BASE_AUTO, 
                                FB_ALLOC_SIZE, 
                                FB_PHYS_MEM,
                                0,
                                VM_ATTR_WRITABLE|
                                VM_ATTR_STRONG_UNCACHED);

    vga.col = 0;
    vga.row = 0;
    spinlock_init(&vga.lock);
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
                   (VGA_MAX_ROW - 1));

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
    int int_status = 0;

    if(vga.base == NULL)
        return;

    spinlock_lock(&vga.lock);

    vga_print_internal(buf);
    
    spinlock_unlock(&vga.lock);
}
