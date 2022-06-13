/* Virtual Memory API */
#include <vm.h>
#include <vm_extent.h>
#include <vm_space.h>
#include <utils.h>
#include <pfmgr.h>
#include <paging.h>

vm_ctx_t vm_kernel_ctx;

void vm_ctx_show
(
    vm_ctx_t *ctx
)
{
    list_node_t *node = NULL;
    list_node_t *next_node = NULL;
    vm_slot_hdr_t *hdr = NULL;
    vm_extent_t *e = NULL;
    virt_size_t alloc_len = 0;
    virt_size_t free_len = 0;

    if(ctx == NULL)
    {
        ctx = &vm_kernel_ctx;
    }

    kprintf("FREE_DESC_PER_PAGE %d\n",ctx->free_per_slot);
    kprintf("ALLOC_DESC_PER_PAGE %d\n",ctx->alloc_per_slot);



    node = linked_list_first(&ctx->free_mem);

    kprintf("----LISTING FREE RANGES----\n");

    while(node)
    {
        hdr = (vm_slot_hdr_t*)node;

        next_node = linked_list_next(node);
        kprintf("============================================\n");
        kprintf("HDR 0x%x AVAIL %d\n",hdr, hdr->avail);
        kprintf("============================================\n");
        for(uint16_t i = 0; i < ctx->free_per_slot; i++)
        {
            e  = &hdr->array[i];

            if(e->length != 0)
            {
                kprintf("IX %d: BASE 0x%x LENGTH 0x%x FLAGS %x EFLAGS %x\n",i, 
                        e->base,
                        e->length, 
                        e->flags, 
                        e->eflags);
                        
                free_len += e->length; 
            }
        }

        node = next_node;
    }

    node = linked_list_first(&ctx->alloc_mem);

    kprintf("----LISTING ALLOCATED RANGES----\n");

    while(node)
    {
        next_node = linked_list_next(node);
        hdr = (vm_slot_hdr_t*)node;
        kprintf("============================================\n");
        kprintf("HDR 0x%x AVAIL %d\n",hdr, hdr->avail);
        kprintf("============================================\n");
        for(uint16_t i = 0; i < ctx->alloc_per_slot; i++)
        {
            e  = &hdr->array[i];
            if(e->length != 0)
            {
                kprintf("IX %d: BASE 0x%x LENGTH 0x%x FLAGS %x EFLAGS %x\n",i, 
                        e->base,
                        e->length, 
                        e->flags, 
                        e->eflags);

                alloc_len += e->length;
            }
        }

        node = next_node;
    }

    kprintf("ALLOCATED: 0x%x bytes\nFREE: 0x%x bytes\n", alloc_len, free_len);

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
            .base    =  (virt_addr_t)&_code,
            .length  =  (virt_addr_t)&_code_end - (virt_addr_t)&_code,
            .flags   = VM_PERMANENT | VM_MAPPED | VM_LOCKED,
            .eflags  = VM_ATTR_EXECUTABLE
        },
        {
            .base   =  (virt_addr_t)&_data,
            .length =  (virt_addr_t)&_data_end -  (virt_addr_t)&_data,
            .flags   = VM_PERMANENT | VM_MAPPED | VM_LOCKED,
            .eflags  = VM_ATTR_WRITABLE
        },
        {
            .base   =  (virt_addr_t)&_rodata,
            .length = (virt_addr_t)&_rodata_end - (virt_addr_t)&_rodata,
            .flags  = VM_PERMANENT | VM_MAPPED | VM_LOCKED,
            .eflags = 0
        },
        {
            .base   = (virt_addr_t)&_bss,
            .length =  (virt_addr_t)&_bss_end - (virt_addr_t)&_bss,
            .flags  = VM_PERMANENT | VM_MAPPED | VM_LOCKED,
            .eflags = VM_ATTR_WRITABLE
        },
        /* Reserve remapping table */
        {
            .base   =  REMAP_TABLE_VADDR,
            .length =  REMAP_TABLE_SIZE,
            .flags  = VM_PERMANENT | VM_MAPPED | VM_LOCKED,
            .eflags = VM_ATTR_WRITABLE,
        },
        /* reserve head of tracking for free addresses */
        {
            .base   =  (virt_addr_t)linked_list_first(&ctx->free_mem),
            .length =  VM_SLOT_SIZE,
            .flags  = VM_PERMANENT | VM_ALLOCATED | VM_LOCKED,
            .eflags = VM_ATTR_WRITABLE,
        },
        /* reserve tracking for allocated addresses */
        {
            .base   =  (virt_addr_t)linked_list_first(&ctx->alloc_mem),
            .length =  VM_SLOT_SIZE,
            .flags  = VM_PERMANENT | VM_ALLOCATED | VM_LOCKED,
            .eflags = VM_ATTR_WRITABLE
        },
    };

    rsrvd_count = sizeof(re) / sizeof(vm_extent_t);

    for(uint32_t i = 0; i < rsrvd_count; i++)
    {
        kprintf("Reserving 0x%x - 0x%x\n", re[i].base, re[i].length);
        if(vm_space_alloc(ctx, re[i].base, re[i].length, re[i].flags, re[i].eflags) == VM_INVALID_ADDRESS)
        {
            kprintf("FAILED to reserve memory\n");
            while(1);
        }
    }
}

static int vm_ctx_init
(
    vm_ctx_t    *ctx,
    virt_addr_t free_mem_track,
    virt_addr_t alloc_mem_track,
    virt_size_t free_track_size,
    virt_size_t alloc_track_size,
    uint32_t    flags
)
{
    virt_addr_t   vm_base = 0;
    virt_addr_t   vm_max  = 0;
    vm_slot_hdr_t *hdr    = NULL;
    vm_extent_t   ext     = VM_EXTENT_INIT;
    int           status  = 0;

    vm_max = cpu_virt_max();

    /* check for valid context */
    if(ctx == NULL)
    {
        return(-1);
    }
    
    kprintf("Initializing VM context 0x%x\n", ctx);

    /* check if we have room for slots */
    if((alloc_mem_track < VM_SLOT_SIZE) || 
       (free_mem_track  < VM_SLOT_SIZE))
    {
        return(-1);
    }

    /* The only moment we are going with high memory
     * is when we are initializing the vm kernel context
     * and since we already have the page manager initialized,
     * we should avoid zero-ing the structure
     */ 
    if(flags != VM_CTX_PREFER_HIGH_MEMORY)
    {
        memset(ctx, 0, sizeof(vm_ctx_t));
    }

    vm_base = (~vm_base) - (vm_max >> 1);

    ctx->vm_base = vm_base;
    ctx->flags   = flags;

    /* setup lists */
    linked_list_init(&ctx->free_mem);
    linked_list_init(&ctx->alloc_mem);

    /* set spin lock */
    spinlock_init(&ctx->lock);

    /* Prepare initializing free memory tracking */
    hdr = (vm_slot_hdr_t*) free_mem_track;
    
    /* Clear the memory */
    memset(hdr,    0, free_track_size);
    
    linked_list_add_head(&ctx->free_mem,  &hdr->node);
    
    ctx->alloc_track_size = alloc_track_size;
    ctx->free_track_size = free_track_size;

    /* How many free entries can we store per slot */
    ctx->free_per_slot = (free_track_size - sizeof(vm_slot_hdr_t)) /
                                                  sizeof(vm_extent_t);

    /* How many allocated entries can we store per slot */
    ctx->alloc_per_slot = (alloc_track_size - sizeof(vm_slot_hdr_t)) /
                                                   sizeof(vm_extent_t);

    hdr->avail = ctx->free_per_slot;

    memset(&ext, 0, sizeof(vm_extent_t));
 
    /* Insert higher memory */
    ext.base   = vm_base;
    ext.length = (((uintptr_t)-1) - vm_base) + 1;
    ext.flags  = VM_HIGH_MEM;

    vm_extent_insert(&ctx->free_mem, 
                     ctx->free_per_slot, 
                     &ext);
 
    /* Insert lower memory */
    ext.base   = 0;
    ext.length = ((vm_max >> 1) - ext.base) + 1;
    ext.flags  = VM_LOW_MEM;

    vm_extent_insert(&ctx->free_mem, 
                     ctx->free_per_slot, 
                     &ext);

    /* Setup tracking for allocated memory */
    hdr = (vm_slot_hdr_t*)alloc_mem_track;

    memset(hdr,    0, alloc_track_size);

    linked_list_add_head(&ctx->alloc_mem, &hdr->node);
    hdr->avail = ctx->alloc_per_slot;

    return(VM_OK);
}

int vm_init(void)
{
    int status = 0;
    virt_addr_t vm_base = 0;
    virt_addr_t vm_max  = 0;


    vm_max = cpu_virt_max();

    vm_base = (~vm_base) - (vm_max >> 1);
    
    memset(&vm_kernel_ctx, 0, sizeof(vm_ctx_t));

    if(pgmgr_kernel_ctx_init(&vm_kernel_ctx.pgmgr) == -1)
        return(VM_FAIL);


    kprintf("Initializing Virtual Memory Manager\n");
    /* Allocate backend for the free memory tracking */
    status = pgmgr_allocate_backend(&vm_kernel_ctx.pgmgr,
                                    vm_base,
                                    VM_SLOT_SIZE,
                                    NULL);
    kprintf("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
    if(status != 0)
    {
        kprintf("Failed to allocate backend for Virtual Memory Manager\n");
        while(1);
    }
    /* Allocate page for the free memory tracking */
    status = pgmgr_allocate_pages(&vm_kernel_ctx.pgmgr,
                                  vm_base,
                                  VM_SLOT_SIZE,
                                  NULL,
                                  VM_ATTR_WRITABLE);
                                  
    if(status != 0)
    {
        kprintf("Failed to allocate tracking backend for Virtual Memory Manager\n");
        while(1);
    }

    /* Allocate backend for allocated memory tracking */
    status = pgmgr_allocate_backend(&vm_kernel_ctx.pgmgr,
                                    vm_base + VM_SLOT_SIZE,
                                    VM_SLOT_SIZE,
                                    NULL);
    
    if(status != 0)
    {
        kprintf("Failed to allocate tracking backend for Virtual Memory Manager\n");
        while(1);
    }

    /* Allocate page for allocated memory tracking */
    status = pgmgr_allocate_pages(&vm_kernel_ctx.pgmgr,
                                  vm_base + VM_SLOT_SIZE,
                                  VM_SLOT_SIZE,
                                  NULL,
                                  VM_ATTR_WRITABLE);
    
    if(status != 0)
    {
        kprintf("Failed to allocate tracking\n");
        while(1);
    }

    /* Initialize the context */
    status = vm_ctx_init(&vm_kernel_ctx,
                         vm_base,
                         vm_base + VM_SLOT_SIZE,
                         VM_SLOT_SIZE,
                         VM_SLOT_SIZE,
                         VM_CTX_PREFER_HIGH_MEMORY);

    kprintf("INIT DONE\n");


    vm_setup_protected_regions(&vm_kernel_ctx);
    vm_ctx_show(&vm_kernel_ctx);
    return(status);
}

int vm_user_ctx_init
(
    vm_ctx_t *ctx
)
{
    int status = VM_OK;
    virt_addr_t free_track  = VM_INVALID_ADDRESS;
    virt_addr_t alloc_track = VM_INVALID_ADDRESS;

    /* Allocate tracking from the kernel context */
    free_track = vm_alloc(&vm_kernel_ctx, 
                          VM_BASE_AUTO,
                          VM_SLOT_SIZE,
                          VM_PERMANENT | VM_LOCKED,
                          VM_ATTR_WRITABLE);

    if(free_track == VM_INVALID_ADDRESS)
    {
        return(VM_FAIL);
    }

    alloc_track = vm_alloc(&vm_kernel_ctx, 
                          VM_BASE_AUTO,
                          VM_SLOT_SIZE,
                          VM_PERMANENT | VM_LOCKED,
                          VM_ATTR_WRITABLE);
    
    if(alloc_track == VM_INVALID_ADDRESS)
    {
        vm_free(&vm_kernel_ctx, 
                 free_track, 
                 VM_SLOT_SIZE);
                 
        return(VM_FAIL);
    }

    status = vm_ctx_init(ctx, 
                         free_track,
                         alloc_track,
                         VM_SLOT_SIZE,
                         VM_SLOT_SIZE,
                         VM_CTX_PREFER_LOW_MEMORY);

    if(status == VM_FAIL)
    {
        vm_free(&vm_kernel_ctx, 
                 free_track, 
                 VM_SLOT_SIZE);
        
        vm_free(&vm_kernel_ctx, 
                alloc_track, 
                VM_SLOT_SIZE);
        
        return(status);
    }

    status = pgmgr_ctx_init(&ctx->pgmgr);

    if(status == VM_FAIL)
    {
        vm_free(&vm_kernel_ctx, 
                 free_track, 
                 VM_SLOT_SIZE);
        
        vm_free(&vm_kernel_ctx, 
                alloc_track, 
                VM_SLOT_SIZE);
    }

    return(status);
}

int vm_ctx_destroy
(
    vm_ctx_t *ctx
)
{

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
    uint8_t int_status = 0;

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

    spinlock_lock_int(&ctx->lock, &int_status);

    /* Allocate virtual space */    
    space_addr = vm_space_alloc(ctx, 
                          virt, 
                          len, 
                          alloc_flags, 
                          mem_flags);
    
    if(space_addr == VM_INVALID_ADDRESS)
    {
        spinlock_unlock_int(&ctx->lock, int_status);
        return(VM_INVALID_ADDRESS);
    }
      
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
    
    /* In case of error, free the allocated virtual space */
    if(ret_address == VM_INVALID_ADDRESS)
    {
        vm_space_free(ctx, space_addr, len, NULL, NULL);
    }

    spinlock_unlock_int(&ctx->lock, int_status);    

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
    uint8_t int_status = 0;
    
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

    spinlock_lock_int(&ctx->lock, &int_status);

    space_addr = vm_space_alloc(ctx, 
                               virt, 
                               len, 
                               alloc_flags, 
                               mem_flags);



    if(space_addr == VM_INVALID_ADDRESS)
    {
        spinlock_unlock_int(&ctx->lock, int_status);
        return(VM_INVALID_ADDRESS);
    }

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

    if(ret_address == VM_INVALID_ADDRESS)
    {
        vm_space_free(ctx, space_addr, len, NULL, NULL);
    }

    spinlock_unlock_int(&ctx->lock, int_status);

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
    uint8_t     int_flags           = 0;

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

    spinlock_lock_int(&ctx->lock, &int_flags);

    /* release the space that we want to change attributes to */
    status = vm_space_free(ctx, 
                           vaddr, 
                           len, 
                           &current_alloc_flags, 
                           &current_mem_flags);
    
    new_mem_flags = (current_mem_flags & ~clear_mem_flags) & 
                    (current_mem_flags | set_mem_flags);

    if(status != 0)
    {
        spinlock_unlock_int(&ctx->lock, int_flags);
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

     spinlock_unlock_int(&ctx->lock, int_flags);

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
    uint8_t int_flags = 0;

    if(ctx == NULL)
       ctx = &vm_kernel_ctx;
     
    /* vaddr and len must be page aligned */
    if((vaddr % PAGE_SIZE) || (len % PAGE_SIZE) || (vaddr == VM_INVALID_ADDRESS))
    {
        return(VM_FAIL);
    }

    /* Lock the VM contexxt */
    spinlock_lock_int(&ctx->lock, &int_flags);

    status =  vm_space_free(ctx, vaddr, len, NULL, NULL);
    
    if(status != VM_OK)
    {
        kprintf("%s %d ERROR\n",__FUNCTION__,__LINE__);
        spinlock_unlock_int(&ctx->lock, int_flags);
        while(1);
    }
    
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
    
    spinlock_unlock_int(&ctx->lock, int_flags);

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
    uint8_t int_flags = 0;
    int status = 0;
    uint32_t old_flags = 0;

    if(ctx == NULL)
        ctx = &vm_kernel_ctx;
     
    /* Check if vaddr and len are page aligned */
    if((vaddr % PAGE_SIZE) || 
       (len % PAGE_SIZE)   || 
       (vaddr == VM_INVALID_ADDRESS))
    {
       return(VM_FAIL);
    }

    spinlock_lock_int(&ctx->lock, &int_flags);
        
    status =  vm_space_free(ctx, vaddr, len, &old_flags, NULL);
    
    if(status != VM_OK)
    {
        kprintf("ERROR\n");
        spinlock_unlock_int(&ctx->lock, int_flags);
        return(VM_FAIL);
    }
    
    if(~old_flags & VM_LAZY)
    {        
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
    }

    spinlock_unlock_int(&ctx->lock, int_flags);

    if(status != 0)
    {
        kprintf("FAILED TO FREE\n");
        return(VM_FAIL);
    }
   
   return(VM_OK);
}


int vm_fault_handler
(
    vm_ctx_t    *ctx,
    virt_addr_t vaddr,
    uint32_t    reason
)
{
    int status = 0;

    switch(reason)
    {
        case VM_FAULT_NOT_PRESENT:
            kprintf("%x NOT PRESENT\n");
            break;

        case VM_FAULT_WRITE:
            kprintf("%x NOT_WRITABLE\n");
            break;
        
        case VM_INSTRUCTION_FETCH:
            kprintf("%x NO EXEC\n");
            break;

        default:
            status = VM_FAIL;
            break;
    }

    return(status);
}