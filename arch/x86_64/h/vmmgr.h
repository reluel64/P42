#ifndef vmmgr_h
#define vmmgr_h

#include <stdint.h>
#include <linked_list.h> 

#define VMMGR_BASE (0xffff800000000000)

#define VIRTUAL_MEMORY_UNRESERVABLE (1 << 0)
#define VMM_RES_KERNEL_IMAGE        (1 << 1)
#define VMM_PHYS_MM                 (1 << 2)
#define VMM_REMAP_TABLE             (1 << 3)
#define VMM_RESERVED                (1 << 4)

typedef struct
{
    list_head_t free_mem;  /* free memory ranges */
    list_head_t rsrvd_mem;  /* reserved memory ranges */
    uint16_t    free_ent_per_page;
    uint16_t    rsrvd_ent_per_page;
    uint64_t    vmmgr_base; /* base address where we will keep the structures */
    
}vmmgr_t;

typedef struct
{
    uint64_t base;
    uint64_t length;
}vmmgr_free_mem_t;

typedef struct
{
    uint64_t base;
    uint64_t length;
    uint8_t  type;
}vmmgr_rsrvd_mem_t;


void *vmmgr_map(uint64_t phys, uint64_t virt, uint64_t len, uint16_t attr);
void *vmmgr_alloc(uint64_t len, uint16_t attr);
int vmmgr_init(void);
#endif 