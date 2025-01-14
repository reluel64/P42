/* Virtual memory space management
 * Part of P42 Kernel
 */

#include <utils.h>
#include <vm.h>
#include <vm_extent.h>

static int vm_space_undo
(
    struct list_head *undo_from,
    struct list_head *undo_to,
    struct vm_extent *ext_left,
    struct vm_extent *ext_mid,
    struct vm_extent *ext_right
);

virt_addr_t vm_space_alloc
(
    struct vm_ctx    *ctx,
    virt_addr_t addr,
    virt_size_t len,
    uint32_t    flags,
    uint32_t    prot
)
{
    struct vm_extent req_ext       = VM_EXTENT_INIT;
    struct vm_extent rem_ext       = VM_EXTENT_INIT;
    struct vm_extent alloc_ext     = VM_EXTENT_INIT;
    int         split_status  = 0;
    int         status        = 0;

    
    /* If we are going VM_BASE_AUTO, then we must 
     * know where to look - lower memory or higher memory
     */

    if(addr == VM_BASE_AUTO)
    {
        /* check if we're stupid or not */
        if((flags & VM_REGION_MASK) == VM_REGION_MASK)
        {
            return(VM_INVALID_ADDRESS);
        }
        else if((flags & VM_REGION_MASK) == 0)
        {
            flags |= (ctx->flags & VM_REGION_MASK);
        }
    }

    if(((flags & VM_MEM_TYPE_MASK) == VM_MEM_TYPE_MASK) ||
        ((flags & VM_MEM_TYPE_MASK) == 0))
    {
        return(VM_INVALID_ADDRESS);
    }

    /* clear the extent */
    memset(&req_ext, 0, sizeof(struct vm_extent));

    if(len % PAGE_SIZE)
    {
        len = ALIGN_UP(len, PAGE_SIZE);
    }

    if(addr != VM_BASE_AUTO)
    {
        if(addr % PAGE_SIZE)
        {
            addr = ALIGN_DOWN(addr, PAGE_SIZE);
        }
    }
    
    /* fill up the request extent */
    req_ext.base   = addr;
    req_ext.length = len;
    req_ext.flags  = flags & VM_REGION_MASK;

    /* acquire the extent */
    status = vm_extent_extract(&ctx->free_mem,
                               &req_ext);

    if(status != VM_OK)
    {
        /* Try to merge the extents */
        status = vm_extent_merge(&ctx->free_mem);

        /* If status is VM_OK, try again */
        if(status == VM_OK)
        {
            status = vm_extent_extract(&ctx->free_mem,
                           &req_ext);
               
        }        
    }
#if 0
    kprintf("EXTENT IS %x %x - %s\n",
            req_ext.base, 
            req_ext.length, 
            (req_ext.flags & VM_HIGH_MEM) == VM_HIGH_MEM ? "HIGH_MEM":"LOW_MEM");
#endif
    /* Hmm...should the routine be smart?
     * make it stupid for now and report
     * error in case we cannot satisfy a 'prefferred' address
     */ 
#if 0
    /* If we've failed and we had an address,
     * try again by letting the extraction
     * routine decide the best slot
     */ 

    if(status < 0 && addr != 0)
    {
        /* clear the base as that's
         * how we tell to auto find the best slot 
         */
        req_ext.base = 0;
        status = vm_extent_extract(&ctx->free_mem,
                               ctx->free_per_slot,
                               &req_ext);
    }
#endif

    if(status < 0)
    {
        kprintf("OOOPS...no memory\n");
        return(VM_INVALID_ADDRESS);
    }
    
    memset(&rem_ext, 0, sizeof(struct vm_extent));
    
    /* split the extent if needed */
    /* in case we don't have the  preferred address,
     * we would set the address to req_ext.base
     * to do the split
     */ 
    
    if(addr == VM_BASE_AUTO)
    {
        addr = req_ext.base;
    }

    /* do the split - it also saves the flags */
    split_status = vm_extent_split(&req_ext, 
                                   addr, 
                                   len, 
                                   &rem_ext);

    /* Check if we managed to split the extent */
    if(split_status < 0)
    {
        kprintf("Could not perform split\n");
        status = vm_extent_insert(&ctx->free_mem,
                        &req_ext);

        if(status == VM_NOMEM)
        {
            kprintf("FATAL ERROR %s %d\n",__FUNCTION__,__LINE__);
            while(1);
        }
        
        return(VM_INVALID_ADDRESS);
    }

    /* Insert the left side - this is guaranteed to work 
     * If it doesn't...well...we're fucked
     */

    status = vm_extent_insert(&ctx->free_mem,
                            &req_ext);

    if(status == VM_NOMEM)
    {
        kprintf("FATAL ERROR %s %d\n",__FUNCTION__,__LINE__);
        while(1);
    }

    /* set the alloc_ext to what we want to add to the allocated list 
     * We do this here as in case we need to vm_undo
     * to have the alloc_ext ready
     */

    alloc_ext.base = addr;
    alloc_ext.length = len;

    /* Make sure that the region mask is appropiately set */
    alloc_ext.flags = (flags         & ~VM_REGION_MASK) | 
                      (req_ext.flags & VM_REGION_MASK);

    alloc_ext.prot = prot;

    /* If we have a right side, insert it */
    if(split_status > 0)
    {
        status = vm_extent_insert(&ctx->free_mem,
                                  &rem_ext);

        /* Hehe... no slots?...try to allocate */
        if(status == VM_NOMEM)
        {
            kprintf("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
            /* No memory ? try to merge the adjacent slots */
            status = vm_extent_merge(&ctx->free_mem);
            if(status != VM_OK)
            {
                status = vm_extent_alloc_slot(&ctx->free_mem, 
                                              ctx->free_per_slot);

                /* status != 0? ...well..FUCK */
                if(status != VM_OK)
                {
                    /* We failed to allocate so we must revert
                     * everything
                     */
                    vm_space_undo(&ctx->alloc_mem, 
                            &ctx->free_mem,
                            &req_ext,
                            &alloc_ext,
                            &rem_ext);

                    return(VM_INVALID_ADDRESS);
                }

                /* Ok, let's do this again, shall we? */
                status = vm_extent_insert(&ctx->free_mem,
                                          &req_ext);

                if(status != VM_OK)
                {
                    return(VM_FAIL);
                }
            }
        }
    }

    status = vm_extent_insert(&ctx->alloc_mem, 
                              &alloc_ext);

     /* Hehe... no slots?....again?...try to allocate */
    if(status == VM_NOMEM)
    {
        status = vm_extent_alloc_slot(&ctx->alloc_mem, 
                                     ctx->alloc_per_slot);

        if(status != VM_OK)
        {
            
            status = vm_extent_merge(&ctx->alloc_mem);

            /* status != 0? ...well..FUCK */
            if(status != VM_OK)
            {
                /* 
                 * Undo the changes
                 */

                vm_space_undo(&ctx->alloc_mem, 
                        &ctx->free_mem,
                        &req_ext,
                        &alloc_ext,
                        &rem_ext);
                        
                return(VM_INVALID_ADDRESS);
            }
        }

        /* Ok, let's do this again, shall we? */
        status = vm_extent_insert(&ctx->alloc_mem,
                                   &alloc_ext);
    }

    if(status != VM_OK)
    {
        /* That's not enough - we must undo any changes if we are
         * unable to allocate
         */

        kprintf("FAILED\n");
        vm_space_undo(&ctx->alloc_mem, 
                       &ctx->free_mem,
                       &req_ext,
                       &alloc_ext,
                       &rem_ext);

        return(VM_INVALID_ADDRESS);
    }

    return(alloc_ext.base);
}

int vm_space_free
(
    struct vm_ctx *ctx,
    virt_addr_t addr,
    virt_size_t len,
    uint32_t    *old_flags,
    uint32_t    *old_prot
)
{
    struct vm_extent req_ext  = VM_EXTENT_INIT;
    struct vm_extent rem_ext  = VM_EXTENT_INIT;
    struct vm_extent free_ext = VM_EXTENT_INIT;
    int         split_status = 0;
    int         status = 0;

    if(addr == VM_BASE_AUTO)
    {
        kprintf("Cannot use VM_BASE_AUTO while freeing\n");
        return(VM_FAIL);
    }

    if(len % PAGE_SIZE)
    {
        len = ALIGN_UP(len, PAGE_SIZE);
    }

    if(addr % PAGE_SIZE)
    {
        addr = ALIGN_DOWN(addr, PAGE_SIZE);
    }

    memset(&req_ext, 0, sizeof(struct vm_extent));

    /* set up the request */
    req_ext.base = addr;
    req_ext.length = len;
    
    status = vm_extent_extract(&ctx->alloc_mem, 
                                &req_ext);

    /* If the extent does not exist, then there is no memory allocated 
     * at that address
     */ 
    if(status < 0)
    {
        kprintf("Extent of base 0x%x and length of 0x%x not present\n",
                addr, len);
        return(VM_FAIL);
    }


    if(req_ext.flags & VM_LOCKED)
    {
        
        vm_extent_insert(&ctx->alloc_mem,
                         &req_ext);
        kprintf("MEMORY %x - %x is locked\n", addr, len);
        return(VM_FAIL);
    }

    split_status = vm_extent_split(&req_ext, 
                                  addr, 
                                  len, 
                                  &rem_ext);

    free_ext.base   = addr;
    free_ext.length = len;
    free_ext.flags  = (req_ext.flags & VM_REGION_MASK) ;

    /* If we failed to split, insert the unmodified extent back */
    if(split_status < 0)
    {
        status = vm_extent_insert(&ctx->alloc_mem,
                    &req_ext);

        if(status == VM_NOMEM)
        {
            kprintf("FATAL ERROR %s %d\n",__FILE__,__LINE__);
            while(1);
        }

        return(VM_FAIL);
    }

    status = vm_extent_insert(&ctx->alloc_mem,
                             &req_ext);

    /* This should not happen but if it does, suspend everything */

    if(status == VM_NOMEM)
    {
        kprintf("FATAL ERROR %s %d\n",__FILE__,__LINE__);
        while(1);
    }

    if(split_status > 0)
    {
        /* We have a remainder - insert it */
        status = vm_extent_insert(&ctx->alloc_mem,
                                  &rem_ext);

        if(status == VM_NOMEM)
        {
            status = vm_extent_alloc_slot(&ctx->alloc_mem,
                                      ctx->alloc_per_slot);
            
            if(status  != VM_OK)
            {
                status = vm_extent_merge(&ctx->alloc_mem);

                if(status != VM_OK)
                {
                    kprintf("NO MEMORY\n");

                    status = vm_space_undo(&ctx->free_mem, 
                                 &ctx->alloc_mem,
                                 &req_ext,
                                 &free_ext,
                                 &rem_ext);
                                 
                    return(status);
                }
            }
            /* Do the insertion again */
            status = vm_extent_insert(&ctx->alloc_mem,
                                  &rem_ext);

             if(status != VM_OK)
             {
                return(VM_FAIL);
             }

        }
    }

    status = vm_extent_insert(&ctx->free_mem,
                               &free_ext);

    if(status == VM_NOMEM)
    {
        kprintf("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
        status = vm_extent_merge(&ctx->free_mem);

        if(status != VM_OK)
        {

            /* We have a remainder - insert it */
            status = vm_extent_alloc_slot(&ctx->free_mem,
                                           ctx->free_per_slot);

            if(status != VM_OK)
            {
                status = vm_space_undo(&ctx->free_mem, 
                                 &ctx->alloc_mem,
                                 &req_ext,
                                 &free_ext,
                                 &rem_ext);
                return(status);
            }
        }

        /* Do the insertion again */
        status = vm_extent_insert(&ctx->free_mem,
                                  &free_ext);
    }
    
    /* Save the old allocation flags */
    if(old_flags != NULL)
    {
        *old_flags = req_ext.flags;
    }

    /* Save the old memory flags */
    if(old_prot != NULL)
    {
        *old_prot   = req_ext.prot;
    }

#if 0
    if(status == VM_OK)
    {
        vm_extent_merge(&ctx->free_mem, ctx->free_per_slot);
    }
#endif
    return(status);
}

static int vm_space_undo
(
    struct list_head *undo_from,
    struct list_head *undo_to,
    struct vm_extent *ext_left,
    struct vm_extent *ext_mid,
    struct vm_extent *ext_right
)
{
    int status = 0;


    /* Do some sanity checks */

    if(ext_left->base + ext_left->length > ext_mid->base)
    {
        return(-1);
    }

    if(ext_right->length > 0)
    {
        if(ext_mid->base + ext_mid->length > ext_right->base)
        {
            return(-1);
        }
    }
    
    status = vm_extent_extract(undo_to, 
                               ext_left);

    if(status < 0)
    {
        kprintf("Left extent is not here....\n");
    }
    
    /* don't take into account if the right extent length is 0
     * This could happen in scenarios where we split an extent
     * in half and we take the entire right part
     */
    if(ext_right->length > 0)
    {
        status = vm_extent_extract(undo_to,
                                   ext_right);

        if(status < 0)
        {
            kprintf("Right extent is not here\n");
        }
    }

    status = vm_extent_extract(undo_from,
                               ext_mid);
    
    if(status < 0)
    {
        kprintf("Middle extent is not here\n");
    }

    ext_left->length += ext_mid->length + 
                        ext_right->length;

    status = vm_extent_insert(undo_to,
                              ext_left);
    
    return(status);
    
}