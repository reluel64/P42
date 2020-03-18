#ifndef pagemgr_h
#define pagemgr_h

#include <stdint.h>
#define REMAP_TABLE_VADDR (0xFFFFFFFFFFE00000)
#define REMAP_TABLE_SIZE  (0x200000)


#define PAGE_WRITABLE      (1 << 0)
#define PAGE_USER          (1 << 1)
#define PAGE_WRITE_THROUGH (1 << 2)
#define PAGE_NO_CACHE      (1 << 3)
#define PAGE_EXECUTABLE    (1 << 4)

typedef struct
{
    uint64_t (*alloc) (uint64_t vaddr, uint64_t len, uint32_t attr);
    uint64_t (*map) (uint64_t vaddr, uint64_t paddr, uint64_t len, uint32_t attr);
    int      (*attr) (uint64_t vaddr, uint64_t len, uint32_t attr);
}pagemgr_t;

pagemgr_t * pagemgr_get(void);
int pagemgr_init(void);
uint64_t pagemgr_boot_temp_map(uint64_t phys_addr);
#endif