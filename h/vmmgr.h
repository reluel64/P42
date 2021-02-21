#ifndef vm_h
#define vm_h

#include <stdint.h>
#include <linked_list.h> 
#include <pagemgr.h>
#include <defs.h>

#define VIRTUAL_MEMORY_UNRESERVABLE (1 << 0)
#define VM_RES_KERNEL_IMAGE        (1 << 1)
#define VM_PHYS_MM                 (1 << 2)
#define VM_REMAP_TABLE             (1 << 3)
#define VM_RES_RSRVD               (1 << 4)
#define VM_RES_FREE                (1 << 5)
#define VM_ALLOW_SWAP              (1 << 6)
#define VM_GUARD_MEMORY            (1 << 7)
#define VM_RES_LOW                 (1 << 8)

#define VM_ATTR_WRITABLE          PAGE_WRITABLE
#define VM_ATTR_USER              PAGE_USER
#define VM_ATTR_WRITE_THROUGH     PAGE_WRITE_THROUGH
#define VM_ATTR_STRONG_UNCACHED   PAGE_STRONG_UNCACHED
#define VM_ATTR_UNCACHEABLE       PAGE_UNCACHEABLE
#define VM_ATTR_WRITE_BACK        PAGE_WRITE_BACK
#define VM_ATTR_WRITE_PROTECT     PAGE_WRITE_PROTECT
#define VM_ATTR_WRITE_COMBINE     PAGE_WRITE_COMBINE
#define VM_ATTR_EXECUTABLE        PAGE_EXECUTABLE



typedef struct vmctx_t
{
    list_head_t free_mem;  /* free memory ranges */
    list_head_t rsrvd_mem;  /* reserved memory ranges */
    list_head_t alloc_mem; /* allocated memory */
    uint16_t    free_per_slot;
    uint16_t    rsrvd_per_slot;
    uint16_t    alloc_per_slot;
    uint16_t    low_ent_per_page;
    virt_addr_t vm_base; /* base address where we will keep the structures */
    pagemgr_ctx_t pagemgr;
    spinlock_t   lock;
}vm_ctx_t;

typedef struct vm_free_mem_t
{
    virt_addr_t base;
    virt_size_t length;
}vm_free_mem_t;



typedef struct vm_rsrvd_mem_t
{
    virt_addr_t base;
    virt_size_t length;
    uint32_t  type;
}vm_rsrvd_mem_t;

typedef struct vm_alloc_mem_t
{
    virt_addr_t base;
    virt_size_t length;
    uint32_t flags;
}vm_alloc_mem_t;

typedef struct vm_rsrvd_mem_hdr_t
{
    list_node_t node;
    uint16_t avail;
    vm_rsrvd_mem_t array[0];
}vm_rsrvd_mem_hdr_t;

typedef struct vm_free_mem_hdr_t
{
    list_node_t node;
    uint16_t avail;
    vm_free_mem_t array[0];
}vm_free_mem_hdr_t;

typedef struct vm_alloc_mem_hdr_t
{
    list_node_t node;
    uint16_t avail;
    vm_alloc_mem_t array[0];
}vm_alloc_mem_hdr_t;


int vm_init(void);

int vm_change_attrib
(
    vm_ctx_t *ctx, 
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t attr
);

virt_addr_t vm_map
(
    vm_ctx_t *ctx, 
    phys_addr_t phys, 
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t attr
);

virt_addr_t vm_alloc
(
    vm_ctx_t *ctx, 
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t attr
);

int vm_unmap
(
    vm_ctx_t *ctx, 
    virt_addr_t vaddr, 
    virt_size_t len
);

int vm_free
(
    vm_ctx_t *ctx, 
    virt_addr_t vaddr, 
    virt_size_t len
);

int vm_reserve
(
    vm_ctx_t *ctx, 
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t type
);


int vm_temp_identity_unmap
(
    vm_ctx_t *ctx,
    virt_addr_t vaddr, 
    virt_size_t len
);

virt_addr_t vm_temp_identity_map
(
    vm_ctx_t *ctx,
    phys_addr_t phys, 
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t attr
);

#endif