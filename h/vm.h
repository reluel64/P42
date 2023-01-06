#ifndef vm_h
#define vm_h

#include <defs.h>
#include <stdint.h>
#include <linked_list.h> 
#include <pgmgr.h>

/* Memory flags */
#define VM_ATTR_WRITABLE          PGMGR_WRITABLE
#define VM_ATTR_USER              PGMGR_USER
#define VM_ATTR_WRITE_THROUGH     PGMGR_WRITE_THROUGH
#define VM_ATTR_STRONG_UNCACHED   PGMGR_STRONG_UNCACHED
#define VM_ATTR_UNCACHEABLE       PGMGR_UNCACHEABLE
#define VM_ATTR_WRITE_BACK        PGMGR_WRITE_BACK
#define VM_ATTR_WRITE_PROTECT     PGMGR_WRITE_PROTECT
#define VM_ATTR_WRITE_COMBINE     PGMGR_WRITE_COMBINE
#define VM_ATTR_EXECUTABLE        PGMGR_EXECUTABLE



/* Allocation Flags */
#define VM_LOW_MEM     (1 << 0)  /* Low memory is used                  */
#define VM_HIGH_MEM    (1 << 1)  /* High memory is used                 */
#define VM_MAPPED      (1 << 2)  /* Memory is used for mapping          */
#define VM_ALLOCATED   (1 << 3)  /* Memory is used for allocation       */
#define VM_PERMANENT   (1 << 4)  /* Memory cannot be swaped             */
#define VM_LOCKED      (1 << 5)  /* Memory cannot be freed              */
#define VM_LAZY        (1 << 7)  /* Memory will be alocated when needed */
#define VM_LAZY_FREE   (1 << 8)  /* Free memory lazily                  */
#define VM_GUARD_PAGES (1 << 9)  /* VM has guard pages                  */
#define VM_CONTIG_PHYS (1 << 10) /* Backing memory is contigous        */


#define VM_BASE_AUTO (~0ull)    /* Find the best memory from the either high
                                 * or low memory 
                                 */

#define VM_FAULT_NOT_PRESENT     (1 << 0)
#define VM_FAULT_WRITE           (1 << 1)
#define VM_INSTRUCTION_FETCH     (1 << 2)

#define VM_CTX_PREFER_HIGH_MEMORY VM_HIGH_MEM
#define VM_CTX_PREFER_LOW_MEMORY  VM_LOW_MEM
#define VM_CTX_HAS_GUARD_PAGES    VM_GUARD_PAGES

#define VM_CTX_GUARD_PAGES_COUNT  (1)

#define VM_REGION_MASK (VM_LOW_MEM | VM_HIGH_MEM)
#define VM_MEM_TYPE_MASK (VM_ALLOCATED | VM_MAPPED)

/* Errors */
#define VM_OK (0x0)
#define VM_FAIL (-1)
#define VM_NOMEM (-2)
#define VM_NOENT (-3)

#define VM_INVALID_ADDRESS ((virt_size_t) -1)

#define VM_EXTENT_INIT {.base   = 0, \
                        .length = 0, \
                        .flags  = 0, \
                        .data   = 0  \
                       }

/* Virtual memory context */
typedef struct vm_ctx_t
{
    list_head_t free_mem;  /* free memory ranges*/
    list_head_t alloc_mem; /* allocated memory */
    uint16_t    free_per_slot;
    uint16_t    alloc_per_slot;
    virt_addr_t vm_base; /* base address where 
                          * we will keep the structures for the current context
                          * This must be available only in kernel context
                          */
    pgmgr_ctx_t pgmgr;    /* backing page manager */
    
    spinlock_t   lock;
    uint32_t     flags;
    uint8_t      guard_pages;
    virt_size_t  alloc_track_size;
    virt_size_t  free_track_size;

}vm_ctx_t;

/* Virtual memory extent */
typedef struct vm_extent_t
{
    virt_addr_t base;
    virt_size_t length;
    uint32_t    flags;

    union 
    {
        /* In case the extent is representing allocated
         * memory, then eflags would contain memory flags like
         * protection type (R/W/X), caching type, etc.
         */ 
        void    *data;     /* extent specific data */
        uint32_t eflags;   
    };

}vm_extent_t;

/* Virtual memory extent header */
typedef struct vm_slot_hdr_t
{
    list_node_t node;
    uint32_t avail;
    uint8_t  type;
    vm_extent_t array[];
}vm_slot_hdr_t;


virt_addr_t vm_map
(
    vm_ctx_t *ctx, 
    virt_addr_t virt, 
    virt_size_t len, 
    phys_addr_t phys, 
    uint32_t alloc_flags,
    uint32_t mem_flags
);

virt_addr_t vm_alloc
(
    vm_ctx_t *ctx, 
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t alloc_flags,
    uint32_t mem_flags
);

int vm_change_attr
(
    vm_ctx_t *ctx,
    virt_addr_t vaddr,
    virt_size_t len,
    uint32_t  set_mem_flags,
    uint32_t  clear_mem_flags,
    uint32_t *old_mem_flags
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

int vm_fault_handler
(
    vm_ctx_t    *ctx,
    virt_addr_t vaddr,
    uint32_t    reason
);

int vm_init
(
    void
);

void vm_ctx_show
(
    vm_ctx_t *ctx
);

int vm_user_ctx_init
(
    vm_ctx_t *ctx
);

#endif