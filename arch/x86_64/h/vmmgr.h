#ifndef vmmgr_h
#define vmmgr_h

#include <stdint.h>
#include <linked_list.h> 
#include <pagemgr.h>
#include <defs.h>
#define VMMGR_BASE (0xffff800000000000)

#define VIRTUAL_MEMORY_UNRESERVABLE (1 << 0)
#define VMM_RES_KERNEL_IMAGE        (1 << 1)
#define VMM_PHYS_MM                 (1 << 2)
#define VMM_REMAP_TABLE             (1 << 3)
#define VMM_RES_RSRVD               (1 << 4)
#define VMM_RES_FREE                (1 << 5)

#define VMM_ATTR_WRITABLE          PAGE_WRITABLE
#define VMM_ATTR_USER              PAGE_USER
#define VMM_ATTR_WRITE_THROUGH     PAGE_WRITE_THROUGH
#define VMM_ATTR_NO_CACHE          PAGE_NO_CACHE
#define VMM_ATTR_EXECUTABLE        PAGE_EXECUTABLE

typedef struct
{
    list_head_t free_mem;  /* free memory ranges */
    list_head_t rsrvd_mem;  /* reserved memory ranges */
    uint16_t    free_ent_per_page;
    uint16_t    rsrvd_ent_per_page;
    virt_addr_t vmmgr_base; /* base address where we will keep the structures */
}vmmgr_t;

typedef struct
{
    virt_addr_t base;
    virt_size_t length;
}vmmgr_free_mem_t;

typedef struct
{
    virt_addr_t base;
    virt_size_t length;
    uint32_t  type;
}vmmgr_rsrvd_mem_t;

typedef struct
{
    list_node_t node;
    uint16_t avail;
    vmmgr_rsrvd_mem_t rsrvd[1];
}vmmgr_rsrvd_mem_hdr_t;

typedef struct
{
    list_node_t node;
    uint16_t avail;
    vmmgr_free_mem_t fmem[1];
}vmmgr_free_mem_hdr_t;

int vmmgr_change_attrib(virt_addr_t virt, virt_size_t len, uint32_t attr);
void *vmmgr_map(phys_addr_t phys, virt_addr_t virt, virt_size_t len, uint32_t attr);
void *vmmgr_alloc(virt_addr_t virt, virt_size_t len, uint32_t attr);
int vmmgr_unmap(void *vaddr, virt_size_t len);
int vmmgr_free(void *vaddr, virt_size_t len);
int vmmgr_reserve(virt_addr_t virt, virt_size_t len, uint32_t type);
int vmmgr_init(void);

#endif