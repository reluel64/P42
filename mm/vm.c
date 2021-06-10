
#include <vm.h>
#include <vm_extent.h>
#include <vm_space.h>
#include <utils.h>
#include <pfmgr.h>
#include <paging.h>

static vm_ctx_t kernel_ctx;

void vm_list_entries()
{
    list_node_t *node = NULL;
    list_node_t *next_node = NULL;
    vm_slot_hdr_t *hdr = NULL;
    vm_extent_t *e = NULL;

    kprintf("FREE_DESC_PER_PAGE %d\n",kernel_ctx.free_per_slot);
    kprintf("ALLOC_DESC_PER_PAGE %d\n",kernel_ctx.alloc_per_slot);

    node = linked_list_first(&kernel_ctx.free_mem);

    kprintf("----LISTING FREE RANGES----\n");

    while(node)
    {
        hdr = (vm_slot_hdr_t*)node;

        next_node = linked_list_next(node);

        for(uint16_t i = 0; i < kernel_ctx.free_per_slot; i++)
        {
            e  = &hdr->array[i];

            if(e->length != 0)
                kprintf("BASE 0x%x LENGTH 0x%x FLAGS %x\n",e->base,e->length, e->flags);
        }

        node = next_node;
    }

    node = linked_list_first(&kernel_ctx.alloc_mem);

    kprintf("----LISTING ALLOCATED RANGES----\n");

    while(node)
    {
        next_node = linked_list_next(node);
        hdr = (vm_slot_hdr_t*)node;

        for(uint16_t i = 0; i < kernel_ctx.alloc_per_slot; i++)
        {
            e  = &hdr->array[i];
            if(e->length != 0)
                kprintf("BASE 0x%x LENGTH 0x%x FLAGS %x\n",e->base,e->length, e->flags);
        }

        node = next_node;
    }
    kprintf("DONE\n");

}



static int vm_setup_protected_regions
(
    vm_ctx_t *ctx
)
{

    vm_slot_hdr_t *hdr = NULL;
    uint32_t   rsrvd_count = 0;

    vm_extent_t re[] = 
    {
        /* Reserve kernel image - only the higher half */
        {
            .base   =  _KERNEL_VMA     + _BOOTSTRAP_END,
            .length =  _KERNEL_VMA_END - (_KERNEL_VMA + _BOOTSTRAP_END),
            .flags   = VM_PERMANENT | VM_ALLOCATED | VM_LOCKED,
        },
        /* Reserve remapping table */
        {
            .base   =  REMAP_TABLE_VADDR,
            .length =  REMAP_TABLE_SIZE,
            .flags   = VM_PERMANENT | VM_ALLOCATED | VM_LOCKED,
        },
        /* reserve head of tracking for free addresses */
        {
            .base   =  (virt_addr_t)linked_list_first(&ctx->free_mem),
            .length =  VM_SLOT_SIZE,
            .flags   = VM_PERMANENT | VM_ALLOCATED | VM_LOCKED,
        },
        {
            .base   =  (virt_addr_t)linked_list_first(&ctx->alloc_mem),
            .length =  VM_SLOT_SIZE,
            .flags   = VM_PERMANENT | VM_ALLOCATED | VM_LOCKED,
        },
        
    };

    rsrvd_count = sizeof(re) / sizeof(vm_extent_t);

    for(uint32_t i = 0; i < rsrvd_count; i++)
    {
        kprintf("Reserving %x - %x\n", re[i].base, re[i].length);
        if(vm_space_alloc(ctx, re[i].base, re[i].length, re[i].flags, 0) == 0)
        {
            kprintf("FAILED\n");
        }
    }

    vm_space_free(ctx, re[0].base, re[0].length);
}
 
int vm_init(void)
{


    virt_addr_t           vm_base = 0;
    virt_addr_t           vm_max  = 0;
    uint32_t              offset  = 0;
    vm_slot_hdr_t         *hdr = NULL;
    vm_extent_t           ext;
    int status            = 0;

    vm_max = cpu_virt_max();

    memset(&kernel_ctx, 0, sizeof(vm_ctx_t));
    
    if(pagemgr_init(&kernel_ctx.pagemgr) == -1)
        return(VM_FAIL);

    vm_base = (~vm_base) - (vm_max >> 1);

    kprintf("Initializing Virtual Memory Manager BASE - 0x%x\n",vm_base);

    kernel_ctx.vm_base = vm_base;
    kernel_ctx.flags   = VM_CTX_PREFER_HIGH_MEMORY;

    linked_list_init(&kernel_ctx.free_mem);
    linked_list_init(&kernel_ctx.alloc_mem);
      
    spinlock_init(&kernel_ctx.lock);

   status = pgmgr_alloc(&kernel_ctx.pagemgr,
                        kernel_ctx.vm_base,
                        VM_SLOT_SIZE,
                        PAGE_WRITABLE | 
                        PAGE_WRITE_THROUGH);

    if(status)
    {
        kprintf("Failed to initialize VMM\n");
        while(1);
    }

     hdr = (vm_slot_hdr_t*) kernel_ctx.vm_base;

    /* Clear the memory */
    memset(hdr,    0, VM_SLOT_SIZE);

    linked_list_add_head(&kernel_ctx.free_mem,  &hdr->node);

    /* How many free entries can we store per slot */
    kernel_ctx.free_per_slot = (VM_SLOT_SIZE - sizeof(vm_slot_hdr_t)) /
                                               sizeof(vm_extent_t);

    /* How many allocated entries can we store per slot */
    kernel_ctx.alloc_per_slot = (VM_SLOT_SIZE - sizeof(vm_slot_hdr_t)) /
                                                sizeof(vm_extent_t);

    hdr->avail = kernel_ctx.free_per_slot;

    memset(&ext, 0, sizeof(vm_extent_t));
 
    /* Insert higher memory */
    ext.base   = kernel_ctx.vm_base;
    ext.length = (((uintptr_t)-1) - kernel_ctx.vm_base) + 1;
    ext.flags  = VM_HIGH_MEM;

    vm_extent_insert(&kernel_ctx.free_mem, 
                     kernel_ctx.free_per_slot, 
                     &ext);

    /* Insert lower memory */
    ext.base   = 0;
    ext.length = ((vm_max >> 1) - ext.base) + 1;
    ext.flags  = VM_LOW_MEM;

    vm_extent_insert(&kernel_ctx.free_mem, 
                      kernel_ctx.free_per_slot, 
                      &ext);

    status = pgmgr_alloc(&kernel_ctx.pagemgr,
                        kernel_ctx.vm_base + VM_SLOT_SIZE,
                        VM_SLOT_SIZE,
                        PAGE_WRITABLE | 
                        PAGE_WRITE_THROUGH);

    if(status)
    {
        return(-1);
    }

    hdr = (vm_slot_hdr_t*)(kernel_ctx.vm_base + VM_SLOT_SIZE);

    memset(hdr,    0, VM_SLOT_SIZE);

    linked_list_add_head(&kernel_ctx.alloc_mem, &hdr->node);
    hdr->avail = kernel_ctx.alloc_per_slot;

    vm_setup_protected_regions(&kernel_ctx);
    vm_list_entries();


    return(VM_OK);
}




virt_addr_t vm_alloc
(
    vm_ctx_t *ctx, 
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t alloc_flags,
    uint32_t page_flags
)
{
    virt_addr_t addr = 0;
    int status = 0;

    if(len % PAGE_SIZE)
        return(0);

    if(ctx == NULL)
        ctx = &kernel_ctx;


    /* Allocate virtual memory */
    addr = vm_space_alloc(ctx, 
                          virt, 
                          len, 
                          alloc_flags, 
                          page_flags);

    if(addr == 0)
        return(0);
    

    /* Check if we also need to allocate physical space now */
    if(alloc_flags & VM_ALLOC_NOW)
    {
        status = pgmgr_alloc(&ctx->pagemgr,
                            addr,
                            len,
                            page_flags);
    }

    if(status != 0)
    {
        vm_space_free(ctx, addr, len);
        return(0);
    }

    return(addr);
}


int vm_unmap
(
    vm_ctx_t *ctx, 
    virt_addr_t vaddr, 
    virt_size_t len
)
{
   return(0);
}


int vm_free
(
    vm_ctx_t *ctx, 
    virt_addr_t vaddr, 
    virt_size_t len
)
{
    return(0);
}
virt_addr_t vm_map
(
    vm_ctx_t *ctx, 
    virt_addr_t virt, 
    virt_size_t len, 
    phys_addr_t phys, 
    uint32_t alloc_flags,
    uint32_t page_flags
)
{
    virt_addr_t addr = 0;
    int status = 0;
#if 0
    if(len % PAGE_SIZE || phys % PAGE_SIZE)
        return(0);
#endif
    if(ctx == NULL)
        ctx = &kernel_ctx;


    alloc_flags = (alloc_flags & ~VM_MEM_TYPE_MASK) | VM_MAPPED;

    /* Allocate virtual memory */
    addr = vm_space_alloc(ctx, 
                          virt, 
                          len, 
                          alloc_flags, 
                          page_flags);

    if(addr == 0)
        return(0);
    
        kprintf("ADDR %x VIRT %x LEN %x FLAGS %x\n",addr, virt, len, alloc_flags) ;

    /* Check if we also need to allocate physical space now */
 
    status = pgmgr_map(&ctx->pagemgr,
                            addr,
                            len,
                            phys,
                            PAGE_WRITABLE);
    
    kprintf("STATUS %x\n",status);
    if(status != 0)
    {
        vm_space_free(ctx, addr, len);
        return(0);
    }

    return(addr);
}

int vm_temp_identity_unmap
(
    vm_ctx_t *ctx,
    virt_addr_t vaddr, 
    virt_size_t len
)
{
    while(1);
}

virt_addr_t vm_temp_identity_map
(
    vm_ctx_t *ctx,
    phys_addr_t phys, 
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t attr
)
{
    while(1);
}

int vm_reserve
(
    vm_ctx_t *ctx, 
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t type
)
{
    while(1);
}

int vm_change_attrib
(
    vm_ctx_t *ctx, 
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t attr
)
{
    while(1);
}
