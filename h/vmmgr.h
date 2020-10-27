#ifndef vmmgr_h
#define vmmgr_h

#include <stdint.h>
#include <linked_list.h> 
#include <pagemgr.h>
#include <defs.h>

#define VIRTUAL_MEMORY_UNRESERVABLE (1 << 0)
#define VMM_RES_KERNEL_IMAGE        (1 << 1)
#define VMM_PHYS_MM                 (1 << 2)
#define VMM_REMAP_TABLE             (1 << 3)
#define VMM_RES_RSRVD               (1 << 4)
#define VMM_RES_FREE                (1 << 5)
#define VMM_ALLOW_SWAP              (1 << 6)
#define VMM_GUARD_MEMORY            (1 << 7)
#define VMM_RES_LOW                 (1 << 8)

#define VMM_ATTR_WRITABLE          PAGE_WRITABLE
#define VMM_ATTR_USER              PAGE_USER
#define VMM_ATTR_WRITE_THROUGH     PAGE_WRITE_THROUGH
#define VMM_ATTR_STRONG_UNCACHED   PAGE_STRONG_UNCACHED
#define VMM_ATTR_UNCACHEABLE       PAGE_UNCACHEABLE
#define VMM_ATTR_WRITE_BACK        PAGE_WRITE_BACK
#define VMM_ATTR_WRITE_PROTECT     PAGE_WRITE_PROTECT
#define VMM_ATTR_WRITE_COMBINE     PAGE_WRITE_COMBINE
#define VMM_ATTR_EXECUTABLE        PAGE_EXECUTABLE

typedef struct vmmgr_ctx_t
{
    list_head_t free_mem;  /* free memory ranges */
    list_head_t rsrvd_mem;  /* reserved memory ranges */
    uint16_t    free_ent_per_page;
    uint16_t    rsrvd_ent_per_page;
    uint16_t    low_ent_per_page;
    virt_addr_t vmmgr_base; /* base address where we will keep the structures */
    pagemgr_ctx_t pagemgr;
    spinlock_t   lock;
}vmmgr_ctx_t;

typedef struct vmmgr_free_mem_t
{
    virt_addr_t base;
    virt_size_t length;
}vmmgr_free_mem_t;



typedef struct vmmgr_rsrvd_mem_t
{
    virt_addr_t base;
    virt_size_t length;
    uint32_t  type;
}vmmgr_rsrvd_mem_t;

typedef struct vmmgr_rsrvd_mem_hdr_t
{
    list_node_t node;
    uint16_t avail;
    vmmgr_rsrvd_mem_t rsrvd[0];
}vmmgr_rsrvd_mem_hdr_t;

typedef struct vmmgr_free_mem_hdr_t
{
    list_node_t node;
    uint16_t avail;
    vmmgr_free_mem_t fmem[0];
}vmmgr_free_mem_hdr_t;

int vmmgr_init(void);

int vmmgr_change_attrib
(
    vmmgr_ctx_t *ctx, 
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t attr
);

void *vmmgr_map
(
    vmmgr_ctx_t *ctx, 
    phys_addr_t phys, 
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t attr
);

void *vmmgr_alloc
(
    vmmgr_ctx_t *ctx, 
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t attr
);

int vmmgr_unmap
(
    vmmgr_ctx_t *ctx, 
    void *vaddr, 
    virt_size_t len
);

int vmmgr_free
(
    vmmgr_ctx_t *ctx, 
    void *vaddr, 
    virt_size_t len
);

int vmmgr_reserve
(
    vmmgr_ctx_t *ctx, 
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t type
);


int vmmgr_temp_identity_unmap
(
    vmmgr_ctx_t *ctx,
    void *vaddr, 
    virt_size_t len
);

virt_addr_t vmmgr_temp_identity_map
(
    vmmgr_ctx_t *ctx,
    phys_addr_t phys, 
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t attr
);

#endif