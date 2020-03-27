#ifndef pagemgr_h
#define pagemgr_h

#include <stdint.h>
#include <physmm.h>
#include <vmmgr.h>
#include <defs.h>
#define REMAP_TABLE_VADDR (0xFFFFFFFFFFE00000)
#define REMAP_TABLE_SIZE  (0x200000)


#define PAGE_WRITABLE      (1 << 0)
#define PAGE_USER          (1 << 1)
#define PAGE_WRITE_THROUGH (1 << 2)
#define PAGE_NO_CACHE      (1 << 3)
#define PAGE_EXECUTABLE    (1 << 4)

typedef struct
{
    virt_addr_t (*alloc)   (virt_addr_t vaddr, virt_size_t len, uint32_t attr);
    virt_addr_t (*map)     (virt_addr_t vaddr, phys_addr_t paddr, virt_size_t len, uint32_t attr);
    int      (*attr)    (virt_addr_t vaddr, virt_size_t len, uint32_t attr);
    int      (*dealloc) (virt_addr_t vaddr, virt_size_t len);
    int      (*unmap)   (virt_addr_t vaddr, virt_size_t len);
}pagemgr_t;

pagemgr_t * pagemgr_get(void);
int pagemgr_init(void);
virt_addr_t pagemgr_boot_temp_map(phys_addr_t phys_addr);
int pagemgr_install_handler(void);
#endif