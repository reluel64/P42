#ifndef pagemgr_h
#define pagemgr_h

#include <stdint.h>
#include <pfmgr.h>
#include <types.h>
#include <defs.h>
#include <spinlock.h>
#define REMAP_TABLE_VADDR (0xFFFFFFFFFFE00000)
#define REMAP_TABLE_SIZE  (0x200000)


#define PAGE_WRITABLE      (1 << 0)
#define PAGE_USER          (1 << 1)
#define PAGE_WRITE_THROUGH (1 << 2)
#define PAGE_NO_CACHE      (1 << 3)
#define PAGE_EXECUTABLE    (1 << 4)
#define PAGE_GUARD         (1 << 5)


typedef struct pagemgr_ctx_t
{
     phys_addr_t page_phys_base; /* physical location of the first
                                  * level of paging
                                  */ 
    spinlock_t  lock;
}pagemgr_ctx_t;


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