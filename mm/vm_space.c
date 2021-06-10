#include <utils.h>
#include <vm.h>
#include <vm_extent.h>

static int vm_space_undo
(
    list_head_t *undo_from,
    list_head_t *undo_to,
    uint32_t    undo_from_ext_cnt,
    uint32_t    undo_to_ext_cnt,
    vm_extent_t *ext_left,
    vm_extent_t *ext_mid,
    vm_extent_t *ext_right
);

virt_addr_t vm_space_alloc
(
    vm_ctx_t *ctx,
    virt_addr_t addr,
    virt_size_t len,
    uint32_t flags,
    uint32_t eflags
)
{
    vm_extent_t req_ext;
    vm_extent_t rem_ext;
    vm_extent_t alloc_ext;
    int         status = 0;

    
    /* If we are going VM_BASE_AUTO, then we must 
     * know where to look - lower memory or higher memory
     */

    if(addr == VM_BASE_AUTO)
    {
    /* check if we're stupid or not */
        if((flags & VM_REGION_MASK) == VM_REGION_MASK)
        {
            return(0);
        }
        else if((flags & VM_REGION_MASK) == 0)
        {
            flags |= (ctx->flags & VM_REGION_MASK);
        }
    }

    if(((flags & VM_MEM_TYPE_MASK) == VM_MEM_TYPE_MASK) ||
        ((flags & VM_MEM_TYPE_MASK) == 0))
    {
        return(0);
    }
    /* clear the extent */
    memset(&req_ext, 0, sizeof(vm_extent_t));

    if(len % PAGE_SIZE)
        len = ALIGN_UP(len, PAGE_SIZE);

    if(addr != VM_BASE_AUTO)
    {
        if(addr % PAGE_SIZE)
            addr = ALIGN_DOWN(addr, PAGE_SIZE);
    }
    
    /* fill up the request extent */
    req_ext.base   = addr;
    req_ext.length = len;
    req_ext.flags  = flags & VM_REGION_MASK;

    /* acquire the extent */
    status = vm_extent_extract(&ctx->free_mem,
                               ctx->free_per_slot,
                               &req_ext);
    
    kprintf("EXTENT IS %x %x - %s\n",
            req_ext.base, 
            req_ext.length, 
            (req_ext.flags & VM_HIGH_MEM) == VM_HIGH_MEM ? "HIGH_MEM":"LOW_MEM");

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
        return(0);
    }
    
    memset(&rem_ext, 0, sizeof(vm_extent_t));
    
    /* split the extent if needed */
    /* in case we don't have the  preferred address,
     * we would set the address to req_ext.base
     * to do the split
     */ 
    if(addr == VM_BASE_AUTO)
        addr = req_ext.base;

    /* do the split - it also saves the flags */
    status = vm_extent_split(&req_ext, 
                              addr, 
                              len, 
                              &rem_ext);

    kprintf("AFTER SPLIT %x %x - %x %x - %x %x\n", 
            req_ext.base,
            req_ext.length, 
            rem_ext.base, 
            rem_ext.length, 
            addr, 
            len);

    /* Insert the left side - this is guaranteed to work 
     * If it doesn't...well...we're fucked
     */

    if(status < 0)
    {
        kprintf("Could not perform split\n");
        vm_extent_insert(&ctx->free_mem,
                        ctx->free_per_slot,
                        &req_ext);
        return(0);
    }

    vm_extent_insert(&ctx->free_mem,
                     ctx->free_per_slot,
                     &req_ext);

    /* set the alloc_ext to what 
     * we want to add to the 
     * allocated list 
     * We do this here as in case we need to vm_undo
     * to have the alloc_ext ready
     */

    alloc_ext.base = addr;
    alloc_ext.length = len;

    /* Make sure that the region mask is appropiately set */
    alloc_ext.flags = (flags & ~VM_REGION_MASK) | 
                     (req_ext.flags & VM_REGION_MASK);

    alloc_ext.eflags = eflags;

    /* If we have a right side, insert it */
    if(status > 0)
    {
        status = vm_extent_insert(&ctx->free_mem,
                                  ctx->free_per_slot,
                                  &rem_ext);

        /* Hehe... no slots?...try to allocate */
        if(status == VM_NOMEM)
        {
            status = vm_extent_alloc_slot(ctx, 
                                   &ctx->free_mem, 
                                   ctx->free_per_slot);
            
            /* status != 0? ...well..FUCK */
            if(status != 0)
            {
                /* we should revert what we did earlier and bail out, 
                 * but for now we will deadlock here
                 */
                vm_space_undo(&ctx->alloc_mem, 
                        &ctx->free_mem,
                        ctx->alloc_per_slot,
                        ctx->free_per_slot,
                        &req_ext,
                        &alloc_ext,
                        &rem_ext);

                return(0);
            }

            /* Ok, let's do this again, shall we? */
            status = vm_extent_insert(&ctx->free_mem,
                                      ctx->free_per_slot,
                                      &req_ext);
        }
    }

    status = vm_extent_insert(&ctx->alloc_mem, 
                               ctx->alloc_per_slot, 
                              &alloc_ext);

    
     /* Hehe... no slots?....again?...try to allocate */
    if(status == VM_NOMEM)
    {
        status = vm_extent_alloc_slot(ctx, 
                               &ctx->alloc_mem, 
                               ctx->alloc_per_slot);
        
        /* status != 0? ...well..FUCK */
        if(status != 0)
        {
            /* 
             * Undo the changes
             */

            vm_space_undo(&ctx->alloc_mem, 
                    &ctx->free_mem,
                    ctx->alloc_per_slot,
                    ctx->free_per_slot,
                    &req_ext,
                    &alloc_ext,
                    &rem_ext);
                    
            return(0);
        }

        /* Ok, let's do this again, shall we? */
        status = vm_extent_insert(&ctx->alloc_mem,
                                   ctx->alloc_per_slot,
                                   &alloc_ext);
    }

    if(!status)
    {
        return(alloc_ext.base);
    }
    else
    {
        /* That's not enough - we must undo any changes if we are
         * unable to allocate
         */

        kprintf("FAILED\n");
        status = vm_space_undo(&ctx->alloc_mem, 
                            &ctx->free_mem,
                            ctx->alloc_per_slot,
                            ctx->free_per_slot,
                            &req_ext,
                            &alloc_ext,
                            &rem_ext);
        return(0);
    }
}

int vm_space_free
(
    vm_ctx_t *ctx,
    virt_addr_t addr,
    virt_size_t len
)
{
    vm_extent_t req_ext;
    vm_extent_t rem_ext;
    vm_extent_t free_ext;
    int         status = 0;

    if(addr == VM_BASE_AUTO)
    {
        kprintf("Cannot use VM_BASE_AUTO while freeing\n");
        return(VM_FAIL);
    }

    if(len % PAGE_SIZE)
        len = ALIGN_UP(len, PAGE_SIZE);

    if(addr % PAGE_SIZE)
        addr = ALIGN_DOWN(addr, PAGE_SIZE);

    memset(&req_ext, 0, sizeof(vm_extent_t));


    /* set up the request */
    req_ext.base = addr;
    req_ext.length = len;

    status = vm_extent_extract(&ctx->alloc_mem, 
                                ctx->alloc_per_slot,
                                &req_ext);

    /* If the extent does not exist, then there is no memory allocated 
     * at that address
     */ 
    if(status < 0)
    {
        return(VM_FAIL);
    }


    if(req_ext.flags & VM_LOCKED)
    {
        vm_extent_insert(&ctx->alloc_mem,
                         ctx->alloc_per_slot,
                         &req_ext);
        kprintf("MEMORY is locked\n");
        return(VM_FAIL);
    }

    status = vm_extent_split(&req_ext, 
                             addr, 
                             len, 
                             &rem_ext);

    free_ext.base = addr;
    free_ext.length = addr;
    free_ext.flags = (req_ext.flags & VM_REGION_MASK);


    vm_extent_insert(&ctx->alloc_mem,
                    ctx->alloc_per_slot,
                    &req_ext);

    if(status > 0)
    {
        /* We have a remainder - insert it */
        status = vm_extent_insert(&ctx->alloc_mem,
                                  ctx->alloc_per_slot,
                                  &rem_ext);

        if(status == VM_NOMEM)
        {
            status = vm_extent_alloc_slot(ctx, 
                                  &ctx->alloc_mem,
                                  ctx->alloc_per_slot);

            if(status != 0)
            {
                kprintf("NO MEMORY\n");

                status = vm_space_undo(&ctx->free_mem, 
                             &ctx->alloc_mem,
                             ctx->free_per_slot,
                             ctx->alloc_per_slot,
                             &req_ext,
                             &free_ext,
                             &rem_ext);
                             
                return(status);
            }

            /* Do the insertion again */
            status = vm_extent_insert(&ctx->alloc_mem,
                                  ctx->alloc_per_slot,
                                  &rem_ext);

        }
    }

    status = vm_extent_insert(&ctx->free_mem, 
                               ctx->free_per_slot, 
                               &free_ext);

    if(status == VM_NOMEM)
    {
        /* We have a remainder - insert it */
        status = vm_extent_alloc_slot(ctx, 
                               &ctx->free_mem,
                               ctx->free_per_slot);

        if(status != 0)
        {
            status = vm_space_undo(&ctx->free_mem, 
                             &ctx->alloc_mem,
                             ctx->free_per_slot,
                             ctx->alloc_per_slot,
                             &req_ext,
                             &free_ext,
                             &rem_ext);
            return(status);
        }

        /* Do the insertion again */
        status = vm_extent_insert(&ctx->free_mem,
                                  ctx->free_per_slot,
                                  &free_ext);
    }

    return(status);
}

static int vm_space_undo
(
    list_head_t *undo_from,
    list_head_t *undo_to,
    uint32_t    undo_from_ext_cnt,
    uint32_t    undo_to_ext_cnt,
    vm_extent_t *ext_left,
    vm_extent_t *ext_mid,
    vm_extent_t *ext_right
)
{
    int status = 0;


    /* Do some sanity checks */

    if(ext_left->base + ext_left->length > ext_mid->base)
        return(-1);

    if(ext_mid->base + ext_mid->length > ext_right->base)
        return(-1);

    status = vm_extent_extract(undo_to, 
                               undo_to_ext_cnt,
                               ext_left);

    if(status < 0)
    {
        kprintf("Left extent is not here....\n");
    }
    
    status = vm_extent_extract(undo_to,
                               undo_to_ext_cnt,
                               ext_right);

    if(status < 0)
    {
        kprintf("Right extent is not here\n");
    }

    status = vm_extent_extract(undo_from,
                               undo_from_ext_cnt,
                               ext_mid);
    
    if(status < 0)
    {
        kprintf("Middle extent is not here\n");
    }

    ext_left->length += ext_mid->length + 
                        ext_right->length;

    status = vm_extent_insert(undo_to,
                              undo_to_ext_cnt,
                              ext_left);
    
    return(status);
    
}