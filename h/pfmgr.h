#ifndef pfmgr_h
#define pfmgr_h

#include <stdint.h>
#include <defs.h>
#include <linked_list.h>
#include <platform.h>


#define LOW_MEMORY   (0x100000)

#define PHYS_ALLOC_CONTIG        (1 << 0)
#define PHYS_ALLOC_HIGHEST       (1 << 2)
#define PHYS_ALLOC_ISA_DMA       (1 << 3)
#define PHYS_ALLOC_CB_STOP       (1 << 4)
#define PHYS_ALLOC_PREFERED_ADDR (1 << 5)

struct pfmgr_cb_data;

typedef int (*alloc_cb)(struct pfmgr_cb_data *cb_dat, void *pv);
typedef int (*free_cb) (struct pfmgr_cb_data *cb_dat, void *pv);

struct pfmgr_cb_data
{
    phys_addr_t phys_base;
    phys_size_t avail_bytes;
    phys_size_t used_bytes;
};

struct pfmgr
{
    int  (*alloc)
    (
        phys_addr_t start, 
        phys_size_t pages, 
        uint8_t flags, 
        alloc_cb cb, 
        void *pv
    );
    
    int  (*dealloc)
    (
        free_cb cb, 
        void *pv
    );

};

struct pfmgr_base
{
    uint32_t domain_count;
    phys_addr_t physf_start;
    phys_addr_t physb_start;
    struct list_head freer;
    struct list_head busyr;
};

struct pfmgr_range_header
{
    struct list_node node;
    phys_addr_t next_range;
    phys_addr_t base;
    phys_size_t len;
    phys_size_t struct_len;
    uint32_t type;
};

struct pfmgr_free_range
{
    struct pfmgr_range_header hdr;
    phys_size_t total_pf;
    phys_size_t avail_pf;
    phys_size_t next_lkup;
    phys_addr_t bmp[0];
};

struct pfmgr_busy_range
{
    struct pfmgr_range_header hdr;
};


void pfmgr_early_init
(
    void
);

int pfmgr_init
(
    void
);

int pfmgr_alloc
(
    phys_addr_t start,
    phys_size_t pf, 
    uint8_t flags, 
    alloc_cb cb, 
    void *pv
);

int pfmgr_free
(
    free_cb cb,
    void *cb_pv
);

int pfmgr_show_free_memory
(
    void
);

extern phys_addr_t KERNEL_LMA;
extern phys_addr_t KERNEL_LMA_END;
extern phys_addr_t KERNEL_VMA;
extern phys_addr_t KERNEL_VMA_END;
extern phys_addr_t KERNEL_IMAGE_LEN;
extern phys_addr_t BOOTSTRAP_END;
extern phys_addr_t BOOT_PAGING;
extern phys_addr_t BOOT_PAGING_END;
extern phys_addr_t BOOT_PAGING_LENGTH;
extern phys_addr_t _code;
extern phys_addr_t _code_end;
extern phys_addr_t _data;
extern phys_addr_t _data_end;
extern phys_addr_t _rodata;
extern phys_addr_t _rodata_end;
extern phys_addr_t _bss;
extern phys_addr_t _bss_end;

#define _KERNEL_LMA         (((phys_addr_t)&KERNEL_LMA))
#define _KERNEL_LMA_END     (((phys_addr_t)&KERNEL_LMA_END))
#define _KERNEL_VMA         (((phys_addr_t)&KERNEL_VMA))
#define _KERNEL_VMA_END     (((phys_addr_t)&KERNEL_VMA_END))
#define _KERNEL_IMAGE_LEN   (((phys_addr_t)&KERNEL_IMAGE_LEN))
#define _BOOTSTRAP_END      (((phys_addr_t)&BOOTSTRAP_END))
#define _BOOT_PAGING        (((phys_addr_t)&BOOT_PAGING))
#define _BOOT_PAGING_END    (((phys_addr_t)&BOOT_PAGING_END))
#define _BOOT_PAGING_LENGTH (((phys_addr_t)&BOOT_PAGING_LENGTH))

#define PF_PER_ITEM (64)
#define BITMAP_SIZE_FOR_AREA(x)  (((x) >> 15))
#define GIGA_BYTE(x) ((x) * 1024ull * 1024ull *1024ull)
#define MEGA_BYTE(x) ((x) * 1024ull *1024ull)
#define KILO_BYTE(x) ((x) * 1024ull)
#define IN_SEGMENT(x,base,len) (((x)>=(base) && (x)<=(base)+(len)))
#define ISA_DMA_MEMORY_LENGTH MEGA_BYTE(16)
#define ISA_DMA_MEMORY_BEGIN  MEGA_BYTE(1)
#define REGION_COUNT(x) ((x) / sizeof(phys_mm_region_t))
#define PF_TO_BYTES(x) ((x) << 12)
#define BYTES_TO_PF(x) ((x) >> 12)
#define POS_TO_IX(x) ((x) % PF_PER_ITEM)
#define BMP_POS(x) ((x) >> 6)

#endif