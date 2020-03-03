#ifndef pagemgr_h
#define pagemgr_h

#include <stdint.h>
#define REMAP_TABLE_VADDR (0xFFFFFFFFFFE00000)
#define REMAP_TABLE_SIZE  (0x200000)
uint64_t pagemgr_boot_temp_map(uint64_t phys_addr);
uint64_t pagemgr_early_map(uint64_t vaddr, uint64_t paddr, uint64_t len, uint16_t attr);
int pagemgr_init(void);
#endif