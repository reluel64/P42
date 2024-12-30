#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <utils.h>
#include <vm.h>
#include <io.h>
#include <port.h>
#define FB_PHYS_MEM (0xB8000)
#define VGA_MAX_ROW (25)
#define VGA_MAX_COL (80)

#define VGA_POS(x,y)  (((x) + ((y) * VGA_MAX_ROW)))
#define FB_LEN        (VGA_MAX_ROW * VGA_MAX_COL * sizeof(uint16_t))
#define FB_ALLOC_SIZE  ALIGN_UP(FB_LEN, PAGE_SIZE)

struct vga
{
    uint16_t *base;
    uint32_t row;
    uint32_t col;
    struct spinlock lock;
};

static struct vga vga;

static size_t vga_write
(
    void *fd_data,
    const void *buf,
    size_t length
);

static int vga_open
(
    void **fd_data,
    char *path,
    int flags,
    int mode
);

static int vga_io_register(void);

void vga_init()
{
    vga.base = (uint16_t*)vm_map(NULL, 
                                 VM_BASE_AUTO, 
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
    __outb(0x3D4, 0x0A);
	__outb(0x3D5, 0x20);
    vga_io_register();
}
static void vga_new_line
(
    struct vga *vga
)
{
    uint16_t *line      = NULL;
    uint16_t *next_line = NULL;
    
    for(size_t i = 0; i < VGA_MAX_ROW - 1; i++)
    {
        line = vga->base + i * VGA_MAX_COL;
        next_line = vga->base + ((i + 1) * VGA_MAX_COL); 

        memcpy(line, next_line, sizeof(uint16_t) * VGA_MAX_COL);
    }

    memset(next_line, 0, sizeof(uint16_t) * VGA_MAX_COL);
}

static int vga_open
(
    void **fd_data,
    char *path,
    int flags,
    int mode
)
{
    *fd_data = &vga;

    return(0);
}
 

static size_t vga_write
(
    void *fd_data,
    const void *buf,
    size_t length
)
{
    struct vga *vga = NULL;
    uint16_t character = 0;
    const char *in_buf = NULL;
    uint16_t *line = NULL;

    vga = fd_data;
    in_buf = buf;

    for(int i = 0; i < length; i++)
    {        
        /* advance line */
        if(in_buf[i] == '\n')
        {
            vga->row++;
            vga->col = 0;
            continue;
        }

        /* if we reached the end of the line, advance one line */
        if(vga->col == VGA_MAX_COL)
        {
            vga->col = 0;
            vga->row++;
        }

        /* in case where the user added too many new lines, rows could
         * be way out of bounds. therefore we will advance as needed
        */
        while(vga->row > VGA_MAX_ROW - 1)
        {
            vga_new_line(vga);
            vga->row--;
        }

        line = vga->base + (vga->row * VGA_MAX_COL);
        character = (uint16_t)in_buf[i]  | (0x7 << 8);


        line[vga->col] = character;

        vga->col++;
    }

    return(0);
}

static int32_t vga_ioctl
(
    
)
{
    return(0);
}

static int32_t vga_close
(
    
)
{
    return(0);
}


static struct io_device_node vga_entry = 
{
    .name       = "vga",
    .open_func  = vga_open,
    .write_func = vga_write,
    .read_func  = NULL,
    .close_func = vga_close,
    .ioctl_func = vga_ioctl
};



static int vga_io_register(void)
{
   
    return(io_entry_register(&vga_entry));
}