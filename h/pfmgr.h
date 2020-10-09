#ifndef pfmgr_h
#define pfmgr_h
#include <stdint.h>
#include <defs.h>
#include <linked_list.h>
#define PAGE_SIZE (0x1000)
#define PF_PER_ITEM (64)

#define LOW_MEMORY   (0x100000)
#define ALLOC_CONTIG  (1 << 0)
#define ALLOC_HIGHEST (1 << 2)
#define ALLOC_ISA_DMA (1 << 3)
#define ALLOC_CB_STOP (1 << 4)

typedef phys_size_t (*alloc_cb)(phys_addr_t phys, phys_size_t count, void *pv);
typedef int     (*free_cb) (phys_addr_t *phys, phys_size_t *count, void *pv);

typedef struct
{
    int  (*alloc)(phys_size_t pages, uint8_t flags, alloc_cb cb, void *pv);
    int  (*dealloc)(free_cb cb, void *pv);
}pfmgr_t;

typedef struct pfmgr_base_t
{
    uint32_t domain_count;
    phys_addr_t physf_start;
    phys_addr_t physb_start;
    list_head_t freer;
    list_head_t busyr;
}pfmgr_base_t;

typedef struct pfmgr_range_header_t
{
    /* The next entry from the node will 
     * point to a physical address not a virtual one
     * while early page frame allocator is in place, that
     * being before pfmgr_init is called
     */ 

     
    list_node_t node;
    phys_addr_t base;
    phys_size_t len;
    phys_size_t struct_len;
    uint32_t domain_id;
    uint32_t type;
}pfmgr_range_header_t;

typedef struct pfmgr_free_range_t
{
    pfmgr_range_header_t hdr;
    phys_size_t total_pf;
    phys_size_t avail_pf;
    phys_addr_t bmp[0];
}pfmgr_free_range_t;

typedef struct pfmgr_busy_range_t
{
    pfmgr_range_header_t hdr;
    
}pfmgr_busy_range_t;


void     pfmgr_early_init(void);
int      pfmgr_init(void);
pfmgr_t *pfmgr_get(void);

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


#define BITMAP_SIZE_FOR_AREA(x)  (((x) / PAGE_SIZE) / 8)
#define GIGA_BYTE(x) ((x) * 1024ull * 1024ull *1024ull)
#define MEGA_BYTE(x) ((x) * 1024ull *1024ull)
#define KILO_BYTE(x) ((x) * 1024ull)
#define IN_SEGMENT(x,base,len) (((x)>=(base) && (x)<=(base)+(len)))
#define ISA_DMA_MEMORY_LENGTH MEGA_BYTE(16)
#define ISA_DMA_MEMORY_BEGIN  MEGA_BYTE(1)
#define REGION_COUNT(x) ((x) / sizeof(phys_mm_region_t))
#define PAGES_TO_BYTES(x) ((x) * PAGE_SIZE)
#define BYTES_TO_PAGES(x) ((x) / PAGE_SIZE)



#endif