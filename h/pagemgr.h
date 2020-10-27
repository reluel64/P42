#ifndef pagemgr_h
#define pagemgr_h

#include <stdint.h>
#include <pfmgr.h>
#include <types.h>
#include <defs.h>
#include <spinlock.h>
#define REMAP_TABLE_VADDR (0xFFFFFFFFFFE00000)
#define REMAP_TABLE_SIZE  (0x200000)


#define PAGE_WRITABLE        (1 << 0)
#define PAGE_USER            (1 << 1)
#define PAGE_EXECUTABLE      (1 << 4)
#define PAGE_GUARD           (1 << 5)
#define PAGE_STRONG_UNCACHED (1 << 6)
#define PAGE_UNCACHEABLE     (1 << 7)
#define PAGE_WRITE_COMBINE   (1 << 8)
#define PAGE_WRITE_THROUGH   (1 << 9)
#define PAGE_WRITE_BACK      (1 << 10)
#define PAGE_WRITE_PROTECT   (1 << 11)

#define PAT_UNCACHEABLE       (0x0)
#define PAT_WRITE_COMBINING   (0x1)
#define PAT_WRITE_THROUGH     (0x4)
#define PAT_WRITE_PROTECTED   (0x5)
#define PAT_WRITE_BACK        (0x6)
#define PAT_UNCACHED          (0x7)

#define PAT_MSR               (0x277)

typedef struct pagemgr_ctx_t
{
     phys_addr_t page_phys_base; /* physical location of the first
                                  * level of paging
                                  */ 
    spinlock_t  lock;
}pagemgr_ctx_t;


typedef struct pat_bits_t
{
    uint32_t pa0:3;
    uint32_t rsrvd0:5;
    uint32_t pa1:3;
    uint32_t rsrvd1:5;
    uint32_t pa2:3;
    uint32_t rsrvd2:5;
    uint32_t pa3:3;
    uint32_t rsrvd3:5;
    uint32_t pa4:3;
    uint32_t rsrvd4:5;
    uint32_t pa5:3;
    uint32_t rsrvd5:5;
    uint32_t pa6:3;
    uint32_t rsrvd6:5;
    uint32_t pa7:3;
    uint32_t rsrvd7:5;
    
}__attribute__ ((packed)) pat_bits_t;

typedef union pat_t
{
    uint64_t pat;
    pat_bits_t fields;
}__attribute__ ((packed)) pat_t;

int         pagemgr_init(pagemgr_ctx_t *ctx);
virt_addr_t pagemgr_boot_temp_map(phys_addr_t phys_addr);
virt_addr_t pagemgr_boot_temp_map_big(virt_addr_t phys_addr, virt_size_t len);
int         pagemgr_boot_temp_unmap_big(virt_addr_t vaddr, virt_size_t len);
void        pagemgr_boot_temp_map_init(void);
int         pagemgr_install_handler(void);
uint64_t    page_manager_get_base(void);

virt_addr_t pagemgr_alloc(pagemgr_ctx_t *ctx, virt_addr_t virt, virt_size_t length, uint32_t attr);
virt_addr_t pagemgr_map(pagemgr_ctx_t *ctx, virt_addr_t virt, phys_addr_t phys, virt_size_t length, uint32_t attr);
int         pagemgr_attr_change(pagemgr_ctx_t *ctx, virt_addr_t vaddr, virt_size_t len, uint32_t attr);
int         pagemgr_free(pagemgr_ctx_t *ctx, virt_addr_t vaddr, virt_size_t len);
int         pagemgr_unmap(pagemgr_ctx_t *ctx, virt_addr_t vaddr, virt_size_t len);
uint8_t     pagemgr_nx_support(void);
uint8_t     pagemgr_pml5_support(void);
int         pagemgr_per_cpu_init(void);

#endif