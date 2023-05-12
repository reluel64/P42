/* Virtual Memory Extent management
 * Part of P42 Kernel
 */

#include <paging.h>
#include <pgmgr.h>
#include <vm.h>
#include <stddef.h>
#include <utils.h>
#include <cpu.h>
#include <platform.h>
#include <pfmgr.h>
#include <vm_extent.h>

extern vm_ctx_t vm_kernel_ctx;

static int vm_virt_is_present
(
    virt_addr_t virt,
    virt_size_t len,
    list_head_t *lh,
    uint32_t ent_per_slot
);

static uint32_t vm_extent_avail
(
    list_head_t *lh
);

static uint8_t vm_extent_joinable
(
    list_head_t *lh,
    uint32_t ext_per_slot,
    vm_extent_t *ext
);

static uint8_t vm_extent_merge_in_hdr
(
    vm_slot_hdr_t *slot,
    uint32_t      extent_per_slot,
    uint32_t      max_loops
);

#define VM_SLOT_ALLOC_FLAGS  (VM_ALLOCATED | VM_PERMANENT | \
                              VM_HIGH_MEM  | VM_LOCKED)

static int vm_extent_alloc_tracking
(
    list_head_t *lh,
    uint32_t    ext_per_slot
)
{

    int           status            = 0;
    uint32_t      alloc_track_avail = 0;
    uint8_t       joinable          = 0;
    vm_slot_hdr_t *slot             = NULL;
    vm_extent_t   rem_ext           = VM_EXTENT_INIT;
    vm_extent_t   alloc_ext         = VM_EXTENT_INIT;
    vm_extent_t   orig_ext           = 
    {
        .base   = VM_BASE_AUTO, 
        .length = VM_SLOT_SIZE,
        .flags  = VM_HIGH_MEM
    };

    /* extract a free extent */
    status = vm_extent_extract(&vm_kernel_ctx.free_mem,
                               ext_per_slot,
                               &orig_ext);

    if(status != VM_OK)
    {
        return(VM_NOMEM);
    }

    /* compute the remaining size so that we can insert it back */
    memcpy(&rem_ext, &orig_ext, sizeof(vm_extent_t));
    rem_ext.length -= VM_SLOT_SIZE;
    
    /* calculate the allocation address*/
    alloc_ext.base   = (orig_ext.base + orig_ext.length) - VM_SLOT_SIZE;
    alloc_ext.length = VM_SLOT_SIZE;
    alloc_ext.flags  = VM_SLOT_ALLOC_FLAGS;
    alloc_ext.eflags = VM_ATTR_WRITABLE;

    /* If the extent is either joinable or we will have place for it,
     * we will continue the execution. Otherwise we will exit.
     * One exception is if we allocate space to hold allocated memory.
     * In that case if we manage to allocate the slot, we will also have
     * a place where we will manage to store the extent
     */
   
    if(lh != &vm_kernel_ctx.alloc_mem)
    { 
        /* first check for available slots as it's less expensive */
        alloc_track_avail = vm_extent_avail(&vm_kernel_ctx.alloc_mem);

        if(alloc_track_avail < 1)
        {
            joinable = vm_extent_joinable(&vm_kernel_ctx.alloc_mem,
                                          vm_kernel_ctx.alloc_per_slot,
                                          &alloc_ext);

            if(!joinable)
            {
                status = vm_extent_insert(&vm_kernel_ctx.free_mem,
                                          vm_kernel_ctx.alloc_per_slot,
                                          &orig_ext);
                if(status != VM_OK)
                {
                    kprintf("CRITICAL ERROR: could not restore free extent\n");
                }

                return(VM_NOENT);
            }
        }
    }

    /* allocate backend */
    status = pgmgr_allocate_backend(&vm_kernel_ctx.pgmgr,
                                    alloc_ext.base,
                                    alloc_ext.length,
                                    NULL);

    pgmgr_invalidate(&vm_kernel_ctx.pgmgr,
                     alloc_ext.base,
                     alloc_ext.length);

    /* if we managed to allocate the backend, try to allocate the storage */
    if(status == 0)
    {
        status = pgmgr_allocate_pages(&vm_kernel_ctx.pgmgr,
                                      alloc_ext.base,
                                      alloc_ext.length,
                                      NULL,
                                      alloc_ext.eflags,
                                      0);

        pgmgr_invalidate(&vm_kernel_ctx.pgmgr,
                         alloc_ext.base,
                         alloc_ext.length);
                                      
        /* if we failed to allocate the storage, release the backend */
        if(status != 0)
        {
            status = pgmgr_release_backend(&vm_kernel_ctx.pgmgr,
                                          alloc_ext.base,
                                          alloc_ext.length,
                                          NULL);

            pgmgr_invalidate(&vm_kernel_ctx.pgmgr,
                     alloc_ext.base,
                     alloc_ext.length);
                     
            if(status != 0)
            {
                kprintf("CRITICAL ERROR: could not release the backend\n");
            }

            status = VM_FAIL;
        }
    }

    /* if all went fine so far, try to insert the new slot */
    if(status == 0)
    {
        slot        = (vm_slot_hdr_t*)alloc_ext.base;
        slot->avail = ext_per_slot;

        /* wipe out the memory */
        memset(slot, 0, sizeof(VM_SLOT_SIZE));

        /* add the slot to the list where it is required */
        linked_list_add_tail(lh, &slot->node);

        /* add memory to the tracking list */
        status = vm_extent_insert(&vm_kernel_ctx.alloc_mem,
                                  vm_kernel_ctx.alloc_per_slot,
                                  &alloc_ext);

        if(status != VM_OK)
        {
            kprintf("CRITICAL ERROR: we checked previously but now we cannot insert\n");
        }
    }

    /* if we failed, put back the original extent */

    if(status != VM_OK) 
    {
        status = vm_extent_insert(&vm_kernel_ctx.free_mem,
                                  vm_kernel_ctx.alloc_per_slot,
                                  &orig_ext);

        return(VM_FAIL);
    }

    /* check if we have something to insert back */
    if(rem_ext.length > 0)
    {
        /* No failure, so we can put back the remaining extent */
        status = vm_extent_insert(&vm_kernel_ctx.free_mem,
                                  vm_kernel_ctx.alloc_per_slot,
                                  &rem_ext);
    }

    return(VM_OK);
}


static int vm_extent_release_tracking
(
    list_head_t *lh,
    uint32_t ext_per_slot,
    vm_slot_hdr_t *hdr
)
{
    int         status           = 0;
    uint8_t     joinable         = 0;
    uint32_t    free_track_avail = 0;
    uint32_t    alloc_track_avail = 0;
    vm_extent_t orig_extent      = VM_EXTENT_INIT;
    vm_extent_t free_extent      = VM_EXTENT_INIT;
    vm_extent_t rem_extent       = VM_EXTENT_INIT;
    vm_extent_t src_extent       = VM_EXTENT_INIT;


    src_extent.base = (virt_addr_t)hdr;
    src_extent.length = VM_SLOT_SIZE;

    /* extract the extent from the list */
    status = vm_extent_extract(lh, ext_per_slot, &src_extent);

    if(status != VM_OK)
    {
        return(VM_NOENT);
    }

    /* if there is anything in the header, do not free it */

    if(hdr->avail < ext_per_slot)
    {
        return(VM_EXTENT_NOT_EMPTY);
    }

    /* remove the node from the list.*/
    linked_list_remove(lh, &hdr->node);
    
    memcpy(&orig_extent, &src_extent, sizeof(vm_extent_t));
    memcpy(&free_extent, &src_extent, sizeof(vm_extent_t));
    
    free_extent       = src_extent;
    free_extent.flags = VM_HIGH_MEM;
    free_extent.data  = NULL;

    /* check if we have where to place the extent */
    free_track_avail = vm_extent_avail(lh);

    if(free_track_avail < 1)
    {
        /* check if it is at least joinable */
        joinable = vm_extent_joinable(&vm_kernel_ctx.free_mem,
                                      ext_per_slot,
                                      &free_extent);

        /* not joinable either? - roll back and return */
        if(joinable == 0)
        {
            linked_list_add_head(lh, &hdr->node);
            status = vm_extent_insert(lh, ext_per_slot, &orig_extent);

            if(status != VM_OK)
            {
                kprintf("CRITICAL ERROR: could not re-insert allocated memory\n");
            }

            return(VM_EXTENT_EXAUSTED);
        }
    }

    /* split the extent as the memory to be freed can be part of a 
     * bigger extent and we only need a small portion of it 
     */
    status = vm_extent_split(&src_extent, 
                             (virt_addr_t)hdr, 
                             VM_SLOT_SIZE, 
                             &rem_extent);

    /* In case we splitted the extent, we must check if we have space
     * to do two inserts. this means we need an extra slot
     */
    if(status)
    {
        alloc_track_avail = vm_extent_avail(&vm_kernel_ctx.alloc_mem);

        if(alloc_track_avail < 2)
        {
            /* check if we can join the left extent */
            joinable = vm_extent_joinable(&vm_kernel_ctx.alloc_mem,
                                          ext_per_slot,
                                          &src_extent);
            
            if(joinable == 0)
            {
                /* we are here so the left extent is not joinable - check
                 * if we can join the right extent
                 */
                joinable = vm_extent_joinable(&vm_kernel_ctx.alloc_mem,
                                              ext_per_slot,
                                              &rem_extent);

                /* we have nothing available and not joinable - roll back */
                if(joinable == 0)
                {
                    linked_list_add_tail(lh, &hdr->node);

                    status = vm_extent_insert(&vm_kernel_ctx.alloc_mem,
                                              ext_per_slot,
                                              &orig_extent);
                    
                    if(status != VM_OK)
                    {
                        kprintf("CRITICAL ERROR: could not re-insert allocated memory\n");
                    }

                    return(VM_NOMEM);
                }
            }
        }
    }

    /* if we reached so far, we know that we can release the memory
     * from the VM perspective.
     * Let's free that memory.
     */

    /* wipe any information from the header */
    memset(hdr, 0, VM_SLOT_SIZE);

    /* release pages */
    status = pgmgr_release_pages(&vm_kernel_ctx.pgmgr,
                                 free_extent.base,
                                 free_extent.length,
                                 NULL);
    
    pgmgr_invalidate(&vm_kernel_ctx.pgmgr,
                     free_extent.base,
                     free_extent.length);


    if(status != VM_OK)
    {
        linked_list_add_tail(lh, &hdr->node);

        status = vm_extent_insert(&vm_kernel_ctx.alloc_mem,
                                  ext_per_slot,
                                  &orig_extent);

        if(status != VM_OK)
        {
            kprintf("CRITICAL ERROR: could not re-insert allocated memory\n");
        }

        return(VM_FAIL);
    }

    /* release page backend */
    status = pgmgr_release_backend(&vm_kernel_ctx.pgmgr,
                                   free_extent.base,
                                   free_extent.length,
                                   NULL);

    pgmgr_invalidate(&vm_kernel_ctx.pgmgr,
                     free_extent.base,
                     free_extent.length);

    if(status != VM_OK)
    {
        status = pgmgr_allocate_pages(&vm_kernel_ctx.pgmgr,
                                      free_extent.base,
                                      free_extent.length,
                                      NULL,
                                      VM_ATTR_WRITABLE,
                                      0);


        pgmgr_invalidate(&vm_kernel_ctx.pgmgr,
                     free_extent.base,
                     free_extent.length);

        if(status == 0)
        {
            hdr->avail = ext_per_slot;
            linked_list_add_head(lh, &hdr->node);
            status = vm_extent_insert(lh, ext_per_slot, &orig_extent);

            if(status != 0)
            {
                kprintf("CRITICAL ERROR: failed to roll-back\n");
            }
        }
        else
        {
            kprintf("CRITICAL ERROR: Failed to re-allocate pages\n");
        }
       

        kprintf("CRITICAL ERROR: could not release backend\n");

        return(VM_FAIL);
        
    }


    /* at this point we should have everything released.
     * let's update the tracking information
     */

    /* Insert the free memory*/
    status = vm_extent_insert(&vm_kernel_ctx.free_mem,
                              vm_kernel_ctx.free_per_slot,
                              &free_extent);
    
    if(status != VM_OK && status != VM_NOENT)
    {
        kprintf("Could not insert extent to free list\n");
    }

    /* insert back the extent with the allocated memory */
    if(src_extent.length > 0)
    {
        status = vm_extent_insert(&vm_kernel_ctx.alloc_mem,
                                  vm_kernel_ctx.alloc_per_slot,
                                  &src_extent);

        if(status != VM_OK && status != VM_NOENT)
        {
            kprintf("Could not insert extent to free list\n");
        }
    }

    /* insert back the remainig part with allocated memory...if any */
    if(rem_extent.length > 0)
    {
        status = vm_extent_insert(&vm_kernel_ctx.alloc_mem,
                                  vm_kernel_ctx.alloc_per_slot,
                                  &rem_extent);

        if(status != VM_OK && status != VM_NOENT)
        {
            kprintf("Could not insert extent to free list\n");
        }
    }

    return(VM_OK);
}

int vm_extent_alloc_slot
(
    list_head_t *lh,
    uint32_t    ext_per_slot
)
{
    int status = VM_FAIL;

    status = vm_extent_alloc_tracking(lh, ext_per_slot);

    if(status == VM_NOENT)
    {
        status = vm_extent_alloc_tracking(&vm_kernel_ctx.alloc_mem,
                                          vm_kernel_ctx.alloc_per_slot);
        
        if(status == VM_OK)
        {
            status = vm_extent_alloc_tracking(lh, ext_per_slot);
        }
    }

    return(status);
}

/* release a tracking slot */

int vm_extent_release_slot
(
    list_head_t   *lh,
    uint32_t      ext_per_slot,
    vm_slot_hdr_t *slot
)
{
    int status = 0;

    status = vm_extent_release_tracking(lh, ext_per_slot, slot);

    if(status == VM_EXTENT_EXAUSTED)
    {
        status = vm_extent_alloc_tracking(&vm_kernel_ctx.free_mem,
                                          vm_kernel_ctx.free_track_size);

        if(status == VM_OK)
        {
            status = vm_extent_release_tracking(lh, ext_per_slot, slot);
        }
    }

    return(status);
}

/*
 * vm_is_in_range - checks if a segment is in the range of another segment
 */

static inline int vm_is_in_range
(
    virt_addr_t base,
    virt_size_t len,
    virt_addr_t req_base,
    virt_size_t req_len
)
{
    virt_size_t limit     = 0;
    virt_size_t req_limit = 0;
    virt_size_t req_end   = 0;
    virt_size_t end       = 0;

    if(len >= 1)
    {
        limit = len - 1;
    }

    if(req_len >= 1)
    {
        req_limit = req_len - 1;
    }

    req_end = req_base + req_limit;
    end     = base     + limit;
    
    if(req_base >= base && req_end <= end)
    {
        return(1);
    }

    return(0);
}

#if 0
static inline int vm_is_in_range
(
    virt_addr_t base,
    virt_size_t len,
    virt_addr_t req_base,
    virt_size_t req_len
)
{
    virt_size_t base_diff = 0;

    /* requested base is out of left bound */
    if(base > req_base)
        return(0);

    /* calculate base difference */
    base_diff  = req_base - base_diff;

    /* If base diff is bigger than what length we have available, 
     * then this is a no go
     */
    if(base_diff > len)
        return(0);

    if(len >= 1)
        limit = len - 1;
    
    if(req_len >= 1)
        req_limit = req_len - 1;

    req_end = req_base + req_limit;
    end     = base     + limit;
    
    if(req_base >= base && req_end <= end)
        return(1);
    
    return(0);
}

#endif

/* insert an extent into a slot */

int vm_extent_insert
(
    list_head_t *lh,
    uint32_t ext_per_slot,
    const vm_extent_t *ext
)
{
    list_node_t     *en         = NULL;
    list_node_t     *next_en    = NULL;
    vm_slot_hdr_t   *hdr        = NULL;
    vm_slot_hdr_t   *f_hdr      = NULL;
    vm_extent_t     *c_ext      = NULL;
    //vm_extent_t     *f_ext      = NULL;
    uint32_t        least_avail = -1;

    if((ext->flags & VM_REGION_MASK) == VM_REGION_MASK ||
       (ext->flags & VM_REGION_MASK) == 0)
    {
        kprintf("WHERE IS THIS MEMORY FROM?\n");
        return(VM_FAIL);
    }

  //  kprintf("INSERTING %x %x\n",ext->base, ext->length);
    if(ext->length == 0)
    {
        return(VM_NOENT);
    }

    en = linked_list_first(lh);

    /* Start finding a free slot */
    while(en)
    {
        next_en = linked_list_next(en);
        hdr = (vm_slot_hdr_t*)en;

        if(least_avail > hdr->avail && hdr->avail > 0)
        {
            least_avail = hdr->avail;
            f_hdr = hdr;
        }

        /* if hdr->avail == ext_per_slot, then do not enter
         * as we don't have anything to join
         */
        if(hdr->avail < ext_per_slot)
        {
            for(uint32_t i = 0; i < ext_per_slot; i++)
            {
                c_ext = &hdr->extents[i];    

                /* Try to join the extents */
                if(vm_extent_join(ext, c_ext) == VM_OK)
                {
                    /* if the extent was joined, 
                     * try one more time on all extents from the header
                     */
                    vm_extent_merge_in_hdr(hdr, ext_per_slot, ext_per_slot / 4);
                    return(VM_OK);
                }
            }
        }
        en = next_en;
    }

    /* If we were unable to merge, just add the information 
     * where we found a free slot 
     */
    if(f_hdr != NULL)
    {
        /* try to go a faster way in a first instance*/
        if(hdr->next_free < ext_per_slot)
        {
            c_ext = f_hdr->extents + hdr->next_free;

            if(c_ext->length == 0)
            {
                memcpy(c_ext, ext, sizeof(vm_extent_t));
                f_hdr->avail--;
                f_hdr->next_free++;

                while(f_hdr->next_free < ext_per_slot)
                {
                    c_ext = f_hdr->extents + hdr->next_free;

                    if(c_ext->length == 0)
                    {
                        break;
                    }

                    f_hdr->next_free++;
                }
                return(VM_OK);
            }
        }

        for(uint32_t i = 0; i < ext_per_slot; i++)
        {
            c_ext = &f_hdr->extents[i];

            if(c_ext->length == 0)
            {
                memcpy(c_ext, ext, sizeof(vm_extent_t));
                f_hdr->avail--;
                return(VM_OK);
            }
        }
    }

    return(VM_NOMEM);
}

/* extract an extent from the slot */

int vm_extent_extract
(
    list_head_t *lh,
    uint32_t    ext_per_slot,
    vm_extent_t *ext
)
{
    list_node_t   *hn      = NULL;
    list_node_t   *next_hn = NULL;
    vm_slot_hdr_t *hdr     = NULL;
    vm_extent_t   *best    = NULL;
    vm_extent_t   *cext    = NULL;
    int           found    = 0;
    uint32_t      ext_pos  = 0;

    /* no length? no entry */
    if(!ext || !ext->length)
    {
        kprintf("NO LENGTH, NO ENTRY\n");
        return(VM_FAIL);
    }
    
    hn = linked_list_first(lh);

    while(hn)
    {
        next_hn = linked_list_next(hn);

        hdr = (vm_slot_hdr_t*)hn;

        /* if hdr->avail == ext_per_slot, we don't have
         * anything useful here
         */ 
        if(hdr->avail == ext_per_slot)
        {
            hn = next_hn;
            continue;
        } 

        for(uint32_t i = 0; i < ext_per_slot; i++)
        {
            cext = &hdr->extents[i];

            if(cext->length == 0)
            {
                continue;
            }

            if(ext->base != VM_BASE_AUTO)
            {
                if(vm_is_in_range(cext->base, 
                                   cext->length, 
                                   ext->base, 
                                   ext->length))
                {
                    found = 1;
                    best = cext;
                    break;
                }
            }
            else
            {
                /* Verify if we are in the right region */
                if((cext->flags & VM_REGION_MASK) == 
                   (ext->flags  & VM_REGION_MASK))
                {
                    if(best == NULL)
                    {
                        best = cext;
                        continue;
                    }
                    
                    if(((best->length < ext->length)   && 
                        (cext->length >= ext->length)) ||
                       ((best->length > cext->length)  && 
                        (cext->length >= ext->length)))
                    {
                        best = cext;
                    }
                }
            }
        }

        if(found)
        {
            break;
        }

        hn = next_hn;
    }

    /* verify if we have at least something to return */
    if((best == NULL) || (best->length < ext->length))
    {
        return(VM_FAIL);
    }

    hdr = VM_EXTENT_TO_HEADER(best);

    if(hdr->avail < ext_per_slot)
    {
        hdr->avail++;
    }

    ext_pos = best - &hdr->extents[0];

    if(hdr->next_free > ext_pos)
    {
        hdr->next_free = ext_pos;
    }

    /* export the slot */ 
    memcpy(ext, best, sizeof(vm_extent_t));

    /* clear the slot that we've acquired */
    memset(best, 0, sizeof(vm_extent_t));

    /* shift the header to be the first in list 
     * so that an immediate insert will not require
     * a potential re-iteration
     * */
    if(linked_list_first(lh) != &hdr->node)
    {
        linked_list_remove(lh, &hdr->node);
        linked_list_add_head(lh, &hdr->node);
    }

    return(VM_OK);

}

/* vm_extent_split - split an extent 
 * and return the remaining block size
 * */

int vm_extent_split
(
    vm_extent_t *src,
    const virt_addr_t virt,
    const virt_size_t len,
    vm_extent_t *dst
)
{

    dst->base = 0;
    dst->length = 0;

    if(!vm_is_in_range(src->base, 
                       src->length, 
                       virt, 
                       len))
    {
        return(-1);
    }
    
    dst->base = (virt + len) ;

    dst->length  = (src->base + src->length)  - 
                   (dst->base);

    src->length  = virt - (src->base);

    /* make sure the flags are the same regardless
     * of what happens next 
     */
    dst->flags = src->flags;
    dst->data = src->data;

    if(dst->length == 0)
    {
        dst->base = 0;
        return(0);
    }

    if(src->length == 0)
    {
        src->base = dst->base;
        src->length = dst->length;

        dst->base = 0;
        dst->length = 0;

        return(0);
    }

    return(1);
}

/* Check if  two extents can be joined
 * If they can be joined, the routine will return
 * 1 or 2 depending where the join can happen.
 * Otherwise, it will return 0
*/

static int vm_extent_can_join
(
    const vm_extent_t *src,
    const vm_extent_t *dest
)
{
    if(src->eflags != dest->eflags)
    {
        return(0);
    }
    else if(src->flags != dest->flags)
    {
        return(0);
    }
    else if ((dest->base > src->base) && 
             (dest->base == src->base + src->length))
    {
        return(1);
    }
    else if((dest->base < src->base) && 
            (dest->base + dest->length == src->base ))
    {
        return(2);
    }

    return(0);
}

/* Join two extents */

int vm_extent_join
(
    const vm_extent_t *src,
    vm_extent_t *dest
)
{
    int can_join = 0;
    int ret_status = VM_OK;

    can_join = vm_extent_can_join(src, dest);

    switch(can_join)
    {
        case 1:
        {
            dest->base = src->base;
            dest->length += src->length;
            break;
        }
        case 2:
        {
            dest->length += src->length;
            break;
        }
        default:
        {
            ret_status = VM_FAIL;
            break;
        }
    }

    return(ret_status);
}



/* Merge extents accross headers */

int vm_extent_merge
(
    list_head_t *lh,
    uint32_t ext_per_slot
)
{
    vm_slot_hdr_t *src_hdr = NULL;
    vm_slot_hdr_t *dst_hdr = NULL;
    vm_extent_t   *src_ext = NULL;
    vm_extent_t   *dst_ext = NULL;
    list_node_t   *src_ln  = NULL;
    list_node_t   *dst_ln  = NULL;
    uint8_t        merged = 0;
    int            status   = VM_FAIL;

    if(lh == NULL || ext_per_slot == 0)
    {
        return(VM_FAIL);
    }    
  
    kprintf("EXTENT MERGE\n");
    do
    {
        merged = 0;
        dst_ln = linked_list_first(lh);
        
        while(dst_ln)
        {   
            dst_hdr = (vm_slot_hdr_t*)dst_ln;
            
            /* if there are no extents, skip the header entirely */
            if(dst_hdr->avail == ext_per_slot)
            {
                dst_ln = linked_list_next(dst_ln);
                continue;
            }

            /* cycle through the potential destination extents */
            
            for(uint32_t d_ix = 0; d_ix < ext_per_slot; d_ix++)
            {
                dst_ext = &dst_hdr->extents[d_ix];

                /* if the extent slot is empty, skip it */
                if(dst_ext->length == 0)
                {
                    continue;
                }

                src_ln = linked_list_first(lh);
                /* cycle throught extents that might be merged */
                while(src_ln)
                {
                    src_hdr = (vm_slot_hdr_t*)src_ln;

                    /* Skip empty headers */
                    if(src_hdr->avail == ext_per_slot)
                    {
                        src_ln = linked_list_next(src_ln);
                        continue;
                    }

                    for(uint32_t s_ix = 0; s_ix < ext_per_slot; s_ix++)
                    {
                        src_ext = &src_hdr->extents[s_ix];

                        /* skip empty extents and ourselves */
                        
                        if((src_ext->length == 0) || (src_ext == dst_ext))
                        {
                            continue;
                        }
                        
                        /* Try to merge, and if we merged, increase the 
                         * available extents in the source slot
                         * because the extent from the source slot has been 
                         * merged with the extent from the destination slot 
                         */

                        if(vm_extent_join(src_ext, dst_ext) == VM_OK)
                        {   
                            merged = 1;
                            /* Clear the extent as it is merged in dest */
                            memset(src_ext, 0, sizeof(vm_extent_t));

                            if(src_hdr->avail < ext_per_slot)
                            {
                                src_hdr->avail++;
                            }
                            else
                            {
                                kprintf("SOMETHING IS WRONG\n");
                            }
                        }
                    }

                    src_ln = linked_list_next(src_ln);
                }
            }

            dst_ln = linked_list_next(dst_ln);
        }
        /* if we got something merged, then the status should be OK */
        if(merged != 0)
        {
            status = VM_OK;
        }

    }while(merged);

   
    return(status);
}

/* Compact extents inside all headers */

int vm_extent_compact_all_hdr
(
    list_head_t *lh,
    uint32_t ext_per_slot
)
{
    vm_slot_hdr_t *hdr       = NULL;
    vm_extent_t   *empty_ext = NULL;
    vm_extent_t   *cursor    = NULL;
    list_node_t   *node      = NULL;
    int           status     = VM_FAIL;

    if(lh == NULL || ext_per_slot == 0)
    {
        return(VM_FAIL);
    }    
  
    node = linked_list_first(lh);

    while(node)
    {
        hdr = (vm_slot_hdr_t*)node;
        empty_ext = NULL;

        /* Do not do compaction on empty/full headers */
        if((hdr->avail == 0) || (hdr->avail == ext_per_slot))
        {
            node = linked_list_next(node);
            continue;
        }

        for(uint32_t i = 0; i < ext_per_slot; i++)
        {
            cursor = &hdr->extents[i];

            if(empty_ext == NULL)
            {
                if(cursor->length == 0)
                {
                    empty_ext = cursor;
                }
            }
            else
            {
                memcpy(empty_ext, cursor, sizeof(vm_extent_t));
                memset(cursor, 0, sizeof(vm_extent_t));
                empty_ext = cursor;
                status = VM_OK;
            }
        }

        node = linked_list_next(node);
    }

    return(status);
}

/* Compact extents inside a header */

int vm_extent_compact_hdr
(
    vm_slot_hdr_t *hdr,
    uint32_t ext_per_slot
)
{
    vm_extent_t   *empty_ext = NULL;
    vm_extent_t   *cursor    = NULL;
    list_node_t   *node      = NULL;
    int           status     = VM_FAIL;

    if(hdr == NULL || ext_per_slot == 0)
    {
        return(VM_FAIL);
    }    
 

    if((hdr->avail == 0) || (hdr->avail == ext_per_slot))
    {
        return(VM_FAIL);
    }

    for(uint32_t i = 0; i < ext_per_slot; i++)
    {
        cursor = &hdr->extents[i];

        if(empty_ext == NULL)
        {
            if(cursor->length == 0)
            {
                empty_ext = cursor;
            }
        }
        else if(cursor->length > 0)
        {
            memcpy(empty_ext, cursor, sizeof(vm_extent_t));
            memset(cursor, 0, sizeof(vm_extent_t));
            empty_ext = cursor;
            status = VM_OK;
        }
    }

    return(status);
}

static uint32_t vm_extent_avail
(
    list_head_t *lh
)
{
    vm_slot_hdr_t *hdr = NULL;
    list_node_t   *node = NULL;
    uint32_t      free_slots = 0;

    node = linked_list_first(lh);

    while(node)
    {
        hdr = (vm_slot_hdr_t*)node;

        free_slots += hdr->avail; 

        node = linked_list_next(node);
    }

    return(free_slots);
}

static uint8_t vm_extent_joinable
(
    list_head_t *lh,
    uint32_t ext_per_slot,
    vm_extent_t *ext
)
{
    uint8_t joinable = 0;
    vm_slot_hdr_t *hdr = NULL;
    vm_extent_t   *pext = NULL;
    list_node_t *ln = NULL;

    ln = linked_list_first(lh);

    while(ln)
    {
        hdr = (vm_slot_hdr_t*) ln;

        if(hdr->avail == ext_per_slot)
        {
            ln = linked_list_next(ln);
            continue;
        }

        for(uint32_t i = 0; i < ext_per_slot; i++)
        {
            pext = &hdr->extents[i];

            if(vm_extent_can_join(ext, pext) > 0)
            {
                joinable = 1;
                break;
            }
        }

        ln = linked_list_next(ln);
    }

    return(joinable);
}


static uint8_t vm_extent_merge_in_hdr
(
    vm_slot_hdr_t *slot,
    uint32_t      extent_per_slot,
    uint32_t      max_loops
)
{
    vm_extent_t *src_ext      = NULL;
    vm_extent_t *dst_ext      = NULL;
    uint8_t     joined        = 0;
    uint32_t    loops         = 0;
    uint32_t    element_count = 0;
    uint8_t     result        = -1;
    

    /* if the slot is empty, we don't have what to join in the header */
    if(slot->avail == extent_per_slot)
    {
        return(result);
    }

    do
    {
        joined = 0;
       
        for(uint32_t i = 0; i < extent_per_slot; i++)
        {
            src_ext = &slot->extents[i];
            /* skip empty extents */
            if(src_ext->length == 0)
            {
                continue;
            }

            for(uint32_t j = i + 1; j < extent_per_slot; j++)
            {
                dst_ext = &slot->extents[j];
                /* skip */
                if((j == i) || (dst_ext->length == 0))
                {
                    continue;
                }
               
                if(vm_extent_join(src_ext, dst_ext) == VM_OK)
                {
                    memset(src_ext, 0, sizeof(vm_extent_t));
                    
                    if(slot->avail < extent_per_slot)
                    {
                        slot->avail++;
                    }
                    else
                    {
                        kprintf("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
                    }

                    if(slot->next_free > i)
                    {
                        slot->next_free = i;
                    }
                    result = 0;
                    joined = 1;
                }
            }
        }
        loops ++; 
    } while(joined && (loops < max_loops));

    return(result);
}