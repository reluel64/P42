#ifndef vm_h
#define vm_h

#include <stdint.h>
#include <linked_list.h> 
#include <pagemgr.h>
#include <defs.h>

#define VM_ATTR_WRITABLE          PGMGR_WRITABLE
#define VM_ATTR_USER              PGMGR_USER
#define VM_ATTR_WRITE_THROUGH     PGMGR_WRITE_THROUGH
#define VM_ATTR_STRONG_UNCACHED   PGMGR_STRONG_UNCACHED
#define VM_ATTR_UNCACHEABLE       PGMGR_UNCACHEABLE
#define VM_ATTR_WRITE_BACK        PGMGR_WRITE_BACK
#define VM_ATTR_WRITE_PROTECT     PGMGR_WRITE_PROTECT
#define VM_ATTR_WRITE_COMBINE     PGMGR_WRITE_COMBINE
#define VM_ATTR_EXECUTABLE        PGMGR_EXECUTABLE

#define VM_LOW_MEM   (1 << 0)
#define VM_HIGH_MEM  (1 << 1)
#define VM_MAPPED    (1 << 2)
#define VM_ALLOCATED (1 << 3)
#define VM_PERMANENT (1 << 4)
#define VM_LOCKED    (1 << 5)

#define VM_BASE_AUTO (~0ull)


#define VM_CTX_PREFER_HIGH_MEMORY VM_HIGH_MEM
#define VM_CTX_PREFER_LOW_MEMORY  VM_LOW_MEM

#define VM_REGION_MASK (VM_LOW_MEM | VM_HIGH_MEM)
#define VM_MEM_TYPE_MASK (VM_ALLOCATED | VM_MAPPED)

#define VM_OK (0x0)
#define VM_FAIL (-1)
#define VM_NOMEM (-2)
#define VM_NOENT (-3)


typedef struct vm_ctx_t
{
    list_head_t free_mem;  /* free memory ranges */
    list_head_t alloc_mem; /* allocated memory */
    uint16_t    free_per_slot;
    uint16_t    alloc_per_slot;
    virt_addr_t vm_base; /* base address where we will keep the structures */
    pagemgr_ctx_t pagemgr;
    spinlock_t   lock;
    uint32_t     flags;
}vm_ctx_t;



typedef struct vm_extent_t
{
    virt_addr_t base;
    virt_size_t length;
    uint32_t flags;

    union 
    {
        void    *data;     /* extent specific data */
        uint32_t eflags;    
    };

}vm_extent_t;


typedef struct vm_slot_hdr_t
{
    list_node_t node;
    uint32_t avail;
    uint8_t  type;
    vm_extent_t array[];
}vm_slot_hdr_t;


typedef int (*vm_lookup_cb)
(
    vm_ctx_t *ctx, 
    vm_slot_hdr_t *hdr, 
    void *pv
);


int vm_extent_split
(
    vm_extent_t *src,
    const virt_addr_t virt,
    const virt_size_t len,
    vm_extent_t *dst
);

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