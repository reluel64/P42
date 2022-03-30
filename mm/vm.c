/* Virtual Memory API */
#include <vm.h>
#include <vm_extent.h>
#include <vm_space.h>
#include <utils.h>
#include <pfmgr.h>
#include <paging.h>

vm_ctx_t vm_kernel_ctx;

void vm_list_entries()
{
    list_node_t *node = NULL;
    list_node_t *next_node = NULL;
    vm_slot_hdr_t *hdr = NULL;
    vm_extent_t *e = NULL;

    kprintf("FREE_DESC_PER_PAGE %d\n",vm_kernel_ctx.free_per_slot);
    kprintf("ALLOC_DESC_PER_PAGE %d\n",vm_kernel_ctx.alloc_per_slot);

    node = linked_list_first(&vm_kernel_ctx.free_mem);

    kprintf("----LISTING FREE RANGES----\n");

    while(node)
    {
        hdr = (vm_slot_hdr_t*)node;

        next_node = linked_list_next(node);
        kprintf("============================================\n");
        kprintf("HDR 0x%x AVAIL %x\n",hdr, hdr->avail);
        kprintf("============================================\n");
        for(uint16_t i = 0; i < vm_kernel_ctx.free_per_slot; i++)
        {
            e  = &hdr->array[i];

            if(e->length != 0)
                kprintf("IX %d: BASE 0x%x LENGTH 0x%x FLAGS %x EFLAGS %x\n",i, e->base,e->length, e->flags, e->eflags);
        }

        node = next_node;
    }

    node = linked_list_first(&vm_kernel_ctx.alloc_mem);

    kprintf("----LISTING ALLOCATED RANGES----\n");

    while(node)
    {
        next_node = linked_list_next(node);
        hdr = (vm_slot_hdr_t*)node;
        kprintf("============================================\n");
        kprintf("HDR 0x%x AVAIL %x\n",hdr, hdr->avail);
        kprintf("============================================\n");
        for(uint16_t i = 0; i < vm_kernel_ctx.alloc_per_slot; i++)
        {
            e  = &hdr->array[i];
            if(e->length != 0)
                kprintf("IX %d: BASE 0x%x LENGTH 0x%x FLAGS %x EFLAGS %x\n",i, e->base,e->length, e->flags, e->eflags);
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
        /* reserve tracking for allocated addresses */
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
        if(vm_space_alloc(ctx, re[i].base, re[i].length, re[i].flags, 0) == VM_INVALID_ADDRESS)
        {
            kprintf("FAILED\n");
        }
    }
}

int vm_init(void)
{
    virt_addr_t           vm_base = 0;
    virt_addr_t           vm_max  = 0;
    uint32_t              offset  = 0;
    vm_slot_hdr_t         *hdr = NULL;
    vm_extent_t           ext = VM_EXTENT_INIT;
    virt_size_t           out_length = 0;
    int status            = 0;

    vm_max = cpu_virt_max();

    memset(&vm_kernel_ctx, 0, sizeof(vm_ctx_t));
    
    if(pgmgr_init(&vm_kernel_ctx.pgmgr) == -1)
        return(VM_FAIL);

    vm_base = (~vm_base) - (vm_max >> 1);

    kprintf("Initializing Virtual Memory Manager BASE - 0x%x\n",vm_base);

    vm_kernel_ctx.vm_base = vm_base;
    vm_kernel_ctx.flags   = VM_CTX_PREFER_HIGH_MEMORY;

    linked_list_init(&vm_kernel_ctx.free_mem);
    linked_list_init(&vm_kernel_ctx.alloc_mem);

    spinlock_init(&vm_kernel_ctx.lock);

    status = pgmgr_allocate_backend(&vm_kernel_ctx.pgmgr,
                                    vm_kernel_ctx.vm_base,
                                    VM_SLOT_SIZE,
                                    NULL);
    if(status != 0)
    {
        kprintf("Failed to allocate backend for Virtual Memory Manager\n");
        while(1);
    }

    status = pgmgr_allocate_pages(&vm_kernel_ctx.pgmgr,
                                  vm_kernel_ctx.vm_base,
                                  VM_SLOT_SIZE,
                                  NULL,
                                  VM_ATTR_WRITABLE);
    
    if(status != 0)
    {
        kprintf("Failed to allocate free slot for Virtual Memory Manager\n");
        while(1);
    }

    hdr = (vm_slot_hdr_t*) vm_kernel_ctx.vm_base;
    
    /* Clear the memory */
    memset(hdr,    0, VM_SLOT_SIZE);

    linked_list_add_head(&vm_kernel_ctx.free_mem,  &hdr->node);

    /* How many free entries can we store per slot */
    vm_kernel_ctx.free_per_slot = (VM_SLOT_SIZE - sizeof(vm_slot_hdr_t)) /
                                               sizeof(vm_extent_t);

    /* How many allocated entries can we store per slot */
    vm_kernel_ctx.alloc_per_slot = (VM_SLOT_SIZE - sizeof(vm_slot_hdr_t)) /
                                                sizeof(vm_extent_t);

    hdr->avail = vm_kernel_ctx.free_per_slot;

    memset(&ext, 0, sizeof(vm_extent_t));
 
    /* Insert higher memory */
    ext.base   = vm_kernel_ctx.vm_base;
    ext.length = (((uintptr_t)-1) - vm_kernel_ctx.vm_base) + 1;
    ext.flags  = VM_HIGH_MEM;

    vm_extent_insert(&vm_kernel_ctx.free_mem, 
                     vm_kernel_ctx.free_per_slot, 
                     &ext);
 
    /* Insert lower memory */
    ext.base   = 0;
    ext.length = ((vm_max >> 1) - ext.base) + 1;
    ext.flags  = VM_LOW_MEM;

    vm_extent_insert(&vm_kernel_ctx.free_mem, 
                      vm_kernel_ctx.free_per_slot, 
                      &ext);

    status = pgmgr_allocate_backend(&vm_kernel_ctx.pgmgr,
                                    vm_kernel_ctx.vm_base,
                                    VM_SLOT_SIZE,
                                    NULL);
    
    if(status != 0)
    {
        kprintf("Failed to allocate tracking backend for Virtual Memory Manager\n");
        while(1);
    }

    status = pgmgr_allocate_pages(&vm_kernel_ctx.pgmgr,
                                  vm_kernel_ctx.vm_base + VM_SLOT_SIZE,
                                  VM_SLOT_SIZE,
                                  NULL,
                                  VM_ATTR_WRITABLE);
    
    if(status != 0)
    {
        kprintf("Failed to allocate tracking slot for Virtual Memory Manager\n");
        while(1);
    }

    hdr = (vm_slot_hdr_t*)(vm_kernel_ctx.vm_base + VM_SLOT_SIZE);

    memset(hdr,    0, VM_SLOT_SIZE);

    linked_list_add_head(&vm_kernel_ctx.alloc_mem, &hdr->node);
    hdr->avail = vm_kernel_ctx.alloc_per_slot;

    vm_setup_protected_regions(&vm_kernel_ctx);
    vm_list_entries();

    return(VM_OK);
}

virt_addr_t vm_alloc
(
    vm_ctx_t   *ctx, 
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t    alloc_flags,
    uint32_t    mem_flags
)
{
    virt_addr_t space_addr = 0;
    virt_addr_t ret_address = VM_INVALID_ADDRESS;
    virt_size_t out_len = 0;

    int status = 0;
    int int_status = 0;

    if(((virt != VM_BASE_AUTO) && (virt % PAGE_SIZE)) || 
        (len % PAGE_SIZE))
    {
        return(VM_INVALID_ADDRESS);
    }

    if(ctx == NULL)
    {
        ctx = &vm_kernel_ctx;
    }

    alloc_flags = (alloc_flags & ~VM_MEM_TYPE_MASK) | VM_ALLOCATED;

    spinlock_lock_int(&ctx->lock);

    /* Allocate virtual space */    
    space_addr = vm_space_alloc(ctx, 
                          virt, 
                          len, 
                          alloc_flags, 
                          mem_flags);

    spinlock_unlock_int(&ctx->lock);
    
    if(space_addr == VM_INVALID_ADDRESS)
    {
        return(VM_INVALID_ADDRESS);
    }

    /* Lock the page manager */
    pgmgr_ctx_lock(&ctx->pgmgr);
    
    /* Check if we also need to allocate physical space now */
    if(~alloc_flags & VM_LAZY)
    {
        status = pgmgr_allocate_backend(&ctx->pgmgr,
                                        space_addr,
                                        len,
                                        &out_len);

        if(status != 0)
        {
            /* release what was allocated for backend */
            status = pgmgr_release_backend(&ctx->pgmgr,
                                          space_addr,
                                          out_len,
                                          NULL);
        }
        else
        {
            /* allocate pages */
            status = pgmgr_allocate_pages(&ctx->pgmgr,
                                          space_addr,
                                          len,
                                          &out_len,
                                          mem_flags);

            if(status != 0)
            {
                /* Release what was allocated */
                status = pgmgr_release_pages(&ctx->pgmgr,
                                              space_addr,
                                              out_len,
                                              NULL);

                if(status != 0)
                {
                    kprintf("HALT: %s %s %d\n", __FILE__, __FUNCTION__ ,__LINE__);
                    while(1);
                }

                /* release the backend */
                status = pgmgr_release_backend(&ctx->pgmgr,
                                               space_addr,
                                               len,
                                               NULL);
                
                if(status != 0)
                {
                    kprintf("HALT %s %s %d\n", __FILE__, __FUNCTION__, __LINE__);
                    while(1);
                }
            }
            else
            {
                /* success */
                ret_address = space_addr;

                /* If we are successful, we have to update the 
                 * changes
                 */
                pgmgr_invalidate(&ctx->pgmgr,
                                     ret_address,
                                     len);
            }
        }
    }
    else
    {
        ret_address = space_addr;
    }

     pgmgr_ctx_unlock(&ctx->pgmgr);

    /* In case of error, free the allocated virtual space */
    if(ret_address == VM_INVALID_ADDRESS)
    {
        spinlock_lock_int(&ctx->lock);
        
        vm_space_free(ctx, space_addr, len, NULL, NULL);
        
        spinlock_unlock_int(&ctx->lock);
    }

    return(ret_address);
}


virt_addr_t vm_map
(
    vm_ctx_t *ctx, 
    virt_addr_t virt, 
    virt_size_t len, 
    phys_addr_t phys, 
    uint32_t    alloc_flags,
    uint32_t    mem_flags
)
{
    virt_addr_t space_addr = 0;
    virt_size_t out_len = 0;
    virt_addr_t ret_address = VM_INVALID_ADDRESS;
    int status = 0;
    int int_status = 0;
    
    if(len % PAGE_SIZE || phys % PAGE_SIZE)
    {
        return(VM_INVALID_ADDRESS);
    }

    if(ctx == NULL)
    {
        ctx = &vm_kernel_ctx;
    }

    alloc_flags = (alloc_flags & ~VM_MEM_TYPE_MASK) | VM_MAPPED;

    /* Allocate virtual memory */

    spinlock_lock_int(&ctx->lock);

    space_addr = vm_space_alloc(ctx, 
                               virt, 
                               len, 
                               alloc_flags, 
                               mem_flags);

    spinlock_unlock_int(&ctx->lock);

    if(space_addr == VM_INVALID_ADDRESS)
    {
        return(VM_INVALID_ADDRESS);
    }

    pgmgr_ctx_lock(&ctx->pgmgr);

    status = pgmgr_allocate_backend(&ctx->pgmgr,
                                        space_addr,
                                        len,
                                        &out_len);

    if(status != 0)
    {
        /* release what was allocated for backend */
        status = pgmgr_release_backend(&ctx->pgmgr,
                                      space_addr,
                                      out_len,
                                      NULL);
    }
    else
    {

        status = pgmgr_map_pages(&ctx->pgmgr,
                                      space_addr,
                                      len,
                                      &out_len,
                                      mem_flags,
                                      phys);

        if(status != 0)
        {
            /* Release what was allocated */
            status = pgmgr_unmap_pages(&ctx->pgmgr,
                                space_addr,
                                out_len,
                                NULL);

            if(status != 0)
            {
                kprintf("HALT: %s %s %d\n", __FILE__, __FUNCTION__ ,__LINE__);
                while(1);
            }

            /* release the backend */
            status = pgmgr_release_backend(&ctx->pgmgr,
                                           space_addr,
                                           len,
                                           NULL);
            
            if(status != 0)
            {
                kprintf("HALT %s %s %d\n", __FILE__, __FUNCTION__, __LINE__);
                while(1);
            }
        }
        else
        {
            ret_address = space_addr;

            /* If we are successful, we have to update the 
             * changes
             */
            pgmgr_invalidate(&ctx->pgmgr,
                                 ret_address,
                                 len);
        }
    }

    pgmgr_ctx_unlock(&ctx->pgmgr);

    if(ret_address == VM_INVALID_ADDRESS)
    {
        spinlock_lock_int(&ctx->lock);

        vm_space_free(ctx, space_addr, len, NULL, NULL);

        spinlock_unlock_int(&ctx->lock);
    }

    return(ret_address);
}


int vm_change_attr
(
    vm_ctx_t *ctx,
    virt_addr_t vaddr,
    virt_size_t len,
    uint32_t  set_mem_flags,
    uint32_t  clear_mem_flags,
    uint32_t *old_mem_flags
)
{
    int         status              = VM_OK;
    virt_addr_t new_mem             = 0;
    uint32_t    current_mem_flags   = 0;
    uint32_t    current_alloc_flags = 0;
    uint32_t    new_mem_flags       = 0;

    if(ctx == NULL)
    {
        ctx = &vm_kernel_ctx;
    }

    /* Check if we are aligned */
    if((vaddr % PAGE_SIZE) || (len % PAGE_SIZE))
    {
        kprintf("%s %d - Not aligned\n",__FUNCTION__,__LINE__);
        while(1);
        return(VM_FAIL);
    }
    
    /* acquire the spinlock of the context
     * We would need to keep this spinlock
     * until we change the vm space completely
     */ 

    spinlock_lock_int(&ctx->lock);

    /* release the space that we want to change attributes to */
    status = vm_space_free(ctx, 
                           vaddr, 
                           len, 
                           &current_alloc_flags, 
                           &current_mem_flags);
    
    new_mem_flags = (current_mem_flags & ~clear_mem_flags) & 
                    (current_mem_flags | set_mem_flags);

    kprintf("CURRENT FLAGS %x NEW_FLAGS %x CLEAR %x\n",current_mem_flags, 
                                                       new_mem_flags, 
                                                       clear_mem_flags);

    if(status != 0)
    {

        spinlock_unlock_int(&ctx->lock);
        while(1);
        return(VM_FAIL);
    }

    /* allocate it again with the new attributes */
     new_mem = vm_space_alloc(ctx, 
                              vaddr, 
                              len, 
                              current_alloc_flags, 
                              new_mem_flags);
                                 kprintf("FREED %x - %x\n",vaddr, len);

     if(new_mem == VM_INVALID_ADDRESS)
     {
         /* If we failed to allocate it with the new attributes,
          * try to restore its old status - this should merge it 
          * back
          */ 
         kprintf("TRYING AGAIN\n");
         new_mem = vm_space_alloc(ctx, 
                                 vaddr,
                                 len,
                                 current_alloc_flags,
                                 current_mem_flags);

        if(status != VM_INVALID_ADDRESS)
        {
            kprintf("FAILED to restore memory to original status\n");
            while(1);
        }
        return (VM_FAIL);
     }
     
     /* Try to change the attributes */
     status = pgmgr_change_attrib(&ctx->pgmgr, 
                                 vaddr, 
                                 len, 
                                 new_mem_flags);
     
     /* If we failed, then we have to change it back */
    if(status != 0)
    {
        /* restore attributes in the page */
        pgmgr_change_attrib(&ctx->pgmgr,
                           vaddr,
                           len,
                           current_mem_flags);
         /* remove the memory with the 'new' attributes */
       status = vm_space_free(ctx, 
                              vaddr, 
                              len, 
                              NULL, 
                              NULL);
         
        if(status != 0)
        {
            kprintf("OOPS...hanging\n");
            while(1);
        }

        /* add back the memory with the 'old' attributes */
        new_mem = vm_space_alloc(ctx, 
                                vaddr,
                                len,
                                current_alloc_flags,
                                current_mem_flags);
        if(new_mem == VM_INVALID_ADDRESS)
        {
            kprintf("FAILED TO ADD BACK MEMORY\n");
        }
    }

     spinlock_unlock_int(&ctx->lock);

     /* if we're ok and the user wants the old flags, give it to them */
    if((status == VM_OK) && (old_mem_flags != NULL))
    {
        *old_mem_flags = current_mem_flags;
    }

     return(status);
}

int vm_unmap
(
    vm_ctx_t *ctx, 
    virt_addr_t vaddr, 
    virt_size_t len
)
{
    int status = 0;
    virt_size_t out_len = 0;

    if(ctx == NULL)
       ctx = &vm_kernel_ctx;
     
    /* vaddr and len must be page aligned */
    if((vaddr % PAGE_SIZE) || (len % PAGE_SIZE) || (vaddr == VM_INVALID_ADDRESS))
    {
        return(VM_FAIL);
    }

    /* Lock the VM contexxt */
    spinlock_lock_int(&ctx->lock);

    status =  vm_space_free(ctx, vaddr, len, NULL, NULL);
    
    if(status != VM_OK)
    {
        kprintf("%s %d ERROR\n",__FUNCTION__,__LINE__);
        spinlock_unlock_int(&ctx->lock);
        while(1);
    }
    

    /* Lock the pgmgr context before releasing the 
     * VM context
     */
    pgmgr_ctx_lock(&ctx->pgmgr);
    spinlock_unlock_int(&ctx->lock);
    
    status = pgmgr_unmap_pages(&ctx->pgmgr,
                                vaddr,
                                len,
                                NULL);

    if(status == 0)
    {
        status = pgmgr_release_backend(&ctx->pgmgr,
                                        vaddr,
                                        len,
                                        NULL);
        
    }

    pgmgr_invalidate(&ctx->pgmgr,
                         vaddr,
                         len);
    
    /* Release the pgmgr context */
    pgmgr_ctx_unlock(&ctx->pgmgr);

    if(status != 0)
    {
        kprintf("FAILED TO UNMAP\n");
        return(VM_FAIL);
    }
   
   return(VM_OK);
}


int vm_free
(
    vm_ctx_t *ctx, 
    virt_addr_t vaddr, 
    virt_size_t len
)
{

    int status = 0;
    uint32_t old_flags = 0;

    if(ctx == NULL)
        ctx = &vm_kernel_ctx;
     
    /* Check if vaddr and len are page aligned */
    if((vaddr % PAGE_SIZE) || (len % PAGE_SIZE)|| (vaddr == VM_INVALID_ADDRESS))
    {
       return(VM_FAIL);
    }

    spinlock_lock_int(&ctx->lock);
        
    status =  vm_space_free(ctx, vaddr, len, &old_flags, NULL);
    
    if(status != VM_OK)
    {
        kprintf("ERROR\n");
        spinlock_unlock_int(&ctx->lock);
        return(VM_FAIL);
    }
    
    if(~old_flags & VM_LAZY)
    {
        pgmgr_ctx_lock(&ctx->pgmgr);
        spinlock_unlock_int(&ctx->lock);
        
        status = pgmgr_release_pages(&ctx->pgmgr,
                                vaddr,
                                len,
                                NULL);    
        if(status == 0)
        {
            status = pgmgr_release_backend(&ctx->pgmgr,
                                            vaddr,
                                            len,
                                            NULL);
        }
        pgmgr_invalidate(&ctx->pgmgr,
                             vaddr,
                             len);

        pgmgr_ctx_unlock(&ctx->pgmgr);
    }
    else
    {
        spinlock_unlock_int(&ctx->lock);
    }
    
    if(status != 0)
    {
        kprintf("FAILED TO FREE\n");
        return(VM_FAIL);
    }
   
   return(VM_OK);
}
