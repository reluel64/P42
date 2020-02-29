#ifndef vmmgr_h
#define vmmgr_h

#include <stdint.h>
#include <linked_list.h> 

#define VMMGR_BASE (0xffff800000000000)

#define VIRTUAL_MEMORY_UNRESERVABLE (1 << 0)
#define VMM_RES_KERNEL_IMAGE        (1 << 1)
#define VMM_PHYS_MM                 (1 << 2)


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
}vmmgr_rsrvd_t;



int vmmgr_init(void);
#endif 