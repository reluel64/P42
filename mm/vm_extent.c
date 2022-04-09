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

extern virt_size_t __max_linear_address(void);
extern vm_ctx_t vm_kernel_ctx;
static int vm_virt_is_present
(
    virt_addr_t virt,
    virt_size_t len,
    list_head_t *lh,
    uint32_t ent_per_slot
);


/* allocate tracking slot */

int vm_extent_alloc_slot
(
    list_head_t *lh,
    uint32_t ext_per_slot
)
{
    virt_addr_t   addr = 0;
    vm_extent_t   fext = VM_EXTENT_INIT;
    vm_slot_hdr_t *new_slot = NULL;
    vm_extent_t   alloc_ext = VM_EXTENT_INIT;
    int          status = 0;

    kprintf("+++++++++++++++++++++++++++++++++++++++++++\n");
    memset(&fext, 0, sizeof(vm_extent_t));

    /* we want tracking memory to come from the high memory area */
    fext.base   = VM_BASE_AUTO;
    fext.length = VM_SLOT_SIZE;
    fext.flags  = VM_HIGH_MEM;

    /* extract a free slot that would fit our allocation*/ 
    status = vm_extent_extract(&vm_kernel_ctx.free_mem, 
                                vm_kernel_ctx.free_per_slot,  
                                &fext);
                
    if(status < 0)
    {
        return(VM_FAIL);
    }

    /* Acquire backend for the page */
    status = pgmgr_allocate_backend(&vm_kernel_ctx.pgmgr,
                                    fext.base,
                                    VM_SLOT_SIZE,
                                    NULL);

    if(status != 0)
    {
        kprintf("Failed to allocate extent\n");
        return(VM_FAIL);
    }

    pgmgr_invalidate(&vm_kernel_ctx.pgmgr,
                     fext.base,
                     VM_SLOT_SIZE);

    /* Allocate the page */
    status = pgmgr_allocate_pages(&vm_kernel_ctx.pgmgr,
                                  fext.base,
                                  VM_SLOT_SIZE,
                                  NULL,
                                  VM_ATTR_WRITABLE);
    
    if(status != 0)
    {
        /* If we failed to allocate the page, then release the backend */
        status = pgmgr_release_backend(&vm_kernel_ctx.pgmgr, 
                                       fext.base, 
                                       VM_SLOT_SIZE, 
                                       NULL);

        if(status != 0)
        {
            kprintf("Failed to release backend\n");
            while(1);
        }
        return(VM_FAIL);
    }

    /* Invalidate the page table */
    
    pgmgr_invalidate(&vm_kernel_ctx.pgmgr,
                     fext.base,
                     VM_SLOT_SIZE);

    if(status != 0)
    {
        return(VM_FAIL);
    }

    new_slot = (vm_slot_hdr_t*)fext.base;

    /* clear the slot memory */
    memset(new_slot, 0, VM_SLOT_SIZE);

    /* prepare the new slot */
    new_slot->avail = ext_per_slot;

    /* add it where it belongs */
    linked_list_add_tail(lh, &new_slot->node);

    /* take out the slot we've acquired */
    fext.base   += VM_SLOT_SIZE;
    fext.length -= VM_SLOT_SIZE;

    /* insert the remaining free memory in the list */
    vm_extent_insert(&vm_kernel_ctx.free_mem, 
                     vm_kernel_ctx.free_per_slot, 
                     &fext);
    
    
    /* prepare to add the new slot to the allocated memory */
    memset(&alloc_ext, 0, sizeof(vm_extent_t));
    
    alloc_ext.base = (virt_addr_t)new_slot;
    alloc_ext.length = VM_SLOT_SIZE;

    /* Memory is allocated and MUST NOT be swapped and cannot be freed */
    alloc_ext.flags = VM_ALLOCATED | 
                      VM_PERMANENT | 
                      VM_HIGH_MEM  |
                      VM_LOCKED;
    
    /* insert the allocated memory into the list */

    status = vm_extent_insert(&vm_kernel_ctx.alloc_mem, 
                      vm_kernel_ctx.alloc_per_slot, 
                      &alloc_ext);
    
    /* Plot twist - we don't have extents to store the 
     * newly allocated slot
     */ 
    if(status == VM_NOMEM)
    {
        /* let's call the routine again. Yes, we're recursing */
        status = vm_extent_alloc_slot(&vm_kernel_ctx.alloc_mem, 
                                      vm_kernel_ctx.alloc_per_slot);

        if(status != 0)
        {
            kprintf("CANNOT ALLOCATE A NEW SLOT\n");
            while(1);
        }

        /* Let's try again */
        status = vm_extent_insert(&vm_kernel_ctx.alloc_mem, 
                                  vm_kernel_ctx.alloc_per_slot,       
                                  &alloc_ext);

        if(status != 0)
        {
            kprintf("WE'RE DOOMED\n");
            while(1);
        }
    }

    return(VM_OK);
}

/* release a tracking slog */

int vm_extent_release_slot
(
    list_head_t   *lh,
    vm_slot_hdr_t *slot,
    uint32_t      ext_per_slot
)
{
    vm_extent_t aext = VM_EXTENT_INIT;
    int status = 0;
    if(slot->avail != ext_per_slot)
    {
        return(VM_FAIL);
    }

    /* Check if the slot is in the list */
    if(linked_list_find_node(lh, &slot->node) < 0)
    {
        return(VM_FAIL);
    }

    aext.base   = (virt_addr_t)slot;
    aext.length = VM_SLOT_SIZE;
    aext.flags  = VM_HIGH_MEM;

    linked_list_remove(lh, &slot->node);

    status = vm_extent_extract(&vm_kernel_ctx.alloc_mem, 
                               vm_kernel_ctx.alloc_per_slot,
                               &aext);

    /* Extraction failed */
    if(status != VM_OK)
    {
        /* add it back */
        linked_list_add_tail(lh, &slot->node);
        return(-1);
    }

    status = pgmgr_release_pages(&vm_kernel_ctx.pgmgr,
                                 aext.base, aext.length, NULL);
    /* Page release failed */
    if(status != 0)
    {
        linked_list_add_tail(lh, &slot->node);
        status = vm_extent_insert(&vm_kernel_ctx.alloc_mem,
                                   vm_kernel_ctx.alloc_per_slot,
                                   &aext);
        
        kprintf("FAILED TO RELEASE PAGES\n");

        if(status != 0)
        {
            while(1);
        }

        return(VM_FAIL);
    }

    status = pgmgr_release_backend(&vm_kernel_ctx.pgmgr,
                                   aext.base, aext.length, NULL);
    /* Backend release failed */
   if(status != 0)
   {
        linked_list_add_tail(lh, &slot->node);

        status = vm_extent_insert(&vm_kernel_ctx.alloc_mem,
                       vm_kernel_ctx.alloc_per_slot,
                       &aext);

        if(status != VM_OK)
        {
            kprintf("COULD NOT RE-INSERT\n");
            while(1);
        }

       status = pgmgr_allocate_pages(&vm_kernel_ctx.pgmgr,
                                     aext.base, 
                                     VM_SLOT_SIZE,
                                     NULL,
                                     aext.eflags);

        if(status != 0)
        {
            while(1);
        }

        return(VM_FAIL);
   }

    status = vm_extent_insert(&vm_kernel_ctx.free_mem,
                              vm_kernel_ctx.free_per_slot,
                              &aext);

    /* Insertion failed - try to revert everything */
    if(status != 0)
   {
        linked_list_add_tail(lh, &slot->node);

        status = vm_extent_insert(&vm_kernel_ctx.alloc_mem,
                       vm_kernel_ctx.alloc_per_slot,
                       &aext);

        if(status != VM_OK)
        {
            kprintf("COULD NOT RE-INSERT\n");
            while(1);
        }


        status = pgmgr_allocate_backend(&vm_kernel_ctx.pgmgr,
                                    aext.base,
                                    VM_SLOT_SIZE,
                                    NULL);
        /* STOP */
        if(status != 0)
        {
            while(1);
        }

       status = pgmgr_allocate_pages(&vm_kernel_ctx.pgmgr,
                                     aext.base, 
                                     VM_SLOT_SIZE,
                                     NULL,
                                     aext.eflags);
        /* STOP */
        if(status != 0)
        {
            while(1);
        }

        return(VM_FAIL);
   }

   return(VM_OK);
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
        limit = len - 1;
    
    if(req_len >= 1)
        req_limit = req_len - 1;

    req_end = req_base + req_limit;
    end     = base     + limit;
    
    if(req_base >= base && req_end <= end)
        return(1);
    
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
    vm_extent_t *ext
)
{
    list_node_t     *en        = NULL;
    list_node_t     *next_en   = NULL;
    vm_slot_hdr_t   *hdr       = NULL;
    vm_slot_hdr_t   *f_hdr     = NULL;
    vm_extent_t     *c_ext     = NULL;
    vm_extent_t     *f_ext     = NULL;
    
    if((ext->flags & VM_REGION_MASK) == VM_REGION_MASK ||
       (ext->flags & VM_REGION_MASK) == 0)
    {
        kprintf("WHERE IS THIS MEMORY FROM?\n");
        return(VM_FAIL);
    }

   // kprintf("INSERTING %x %x\n",ext->base, ext->length);
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

        if(hdr->avail > 0)
        {
            for(uint32_t i = 0; i < ext_per_slot; i++)
            {
                c_ext = &hdr->array[i];    
                /* Try to join the extents */
                if(vm_extent_join(ext, c_ext) == VM_OK)
                {
                    return(VM_OK);
                }

                /* Check if we can place the extent here */
                else if((c_ext->length == 0) && (f_ext == NULL))
                {
                    /* Save the free slot */
                    f_ext = c_ext;
                }
                
            }
        }

        en = next_en;
    }

    /* If we were unable to merge, just add the information 
     * where we found a free slot 
     */
    if(f_ext != NULL)
    {
        memcpy(f_ext, ext, sizeof(vm_extent_t));
        hdr = VM_EXTENT_TO_HEADER(f_ext);
        hdr->avail--;
        return(VM_OK);
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
            cext = &hdr->array[i];

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
                        best = cext;

                    if((best->length < ext->length   && 
                        cext->length >= ext->length) ||
                        (best->length > cext->length  && 
                        cext->length >= ext->length))
                    {
                        best = cext;
                    }
                }
            }
        }

        if(found)
            break;

        hn = next_hn;
    }

    /* verify if we have at least something to return */
    if((best == NULL) || (best->length < ext->length))
    {
        return(VM_FAIL);
    }

    hdr = VM_EXTENT_TO_HEADER(best);

    if(hdr->avail < ext_per_slot)
        hdr->avail++;

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
    dst->eflags = src->eflags;

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

/* Join two extents */

int vm_extent_join
(
    vm_extent_t *src,
    vm_extent_t *dest
)
{
    if(src->eflags != dest->eflags)
    {
        return(VM_FAIL);
    }
    else if(src->eflags != dest->eflags)
    {
        return(VM_FAIL);
    }
    else if ((dest->base > src->base) && 
             (dest->base == src->base + src->length))
    {
        dest->base = src->base;
        dest->length += src->length;

        /* we merged the extent in dest so the source must be cleared */
    
        return(VM_OK);
    }
    else if((dest->base < src->base) && 
            (dest->base + dest->length == src->base ))
    {
        dest->length += src->length;

        /* we merged the extent in dest so the source must be cleared */
        
        return(VM_OK);
    }

    return(VM_FAIL);
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

    do
    {
        merged = 0;
        dst_ln = linked_list_first(lh);
        
        while(dst_ln)
        {   
            dst_hdr = (vm_slot_hdr_t*)dst_ln;
            
            /* cycle through the potential destination extents */
            
            for(uint32_t d_ix = 0; d_ix < ext_per_slot; d_ix++)
            {
                dst_ext = &dst_hdr->array[d_ix];

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
                        src_ext = &src_hdr->array[s_ix];

                        /* skip empty extents and ourselves */
                        
                        if((src_ext->length == 0) || (src_ext == dst_ext))
                            continue;

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
                                src_hdr->avail++;
                            else
                                kprintf("SOMETHING IS WRONG\n");
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
        if(hdr->avail == 0 || hdr->avail == ext_per_slot)
        {
            node = linked_list_next(node);
            continue;;
        }

        for(uint32_t i = 0; i < ext_per_slot; i++)
        {
            cursor = &hdr->array[i];

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
        cursor = &hdr->array[i];

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

    return(status);
}

/* Defragment the extents by moving them to headers which
 * have space left
 */
int vm_extent_defragment
(
    list_head_t *lh,
    uint32_t    ext_per_slot
)
{
    vm_slot_hdr_t *src_hdr   = NULL;
    vm_slot_hdr_t *dst_hdr   = NULL;
    vm_extent_t   *src_ext   = NULL;
    vm_extent_t   *dst_ext   = NULL;
    list_node_t   *src_ln    = NULL;
    list_node_t   *dst_ln    = NULL;
    uint8_t       compacted  = 0;
    uint8_t       defrag_sts = VM_FAIL;

    src_ln = linked_list_first(lh);

    while(src_ln)
    {
        compacted = 0;
        src_hdr = (vm_slot_hdr_t*)src_ln;

        for(uint32_t si = 0; si < ext_per_slot; si++)
        {
            src_ext = &dst_hdr->array[si];

            /* skip empty extents */
            if(src_ext->length == 0)
            {
                src_ln = linked_list_next(src_ln);
            }

            dst_ln = linked_list_first(lh);

            while(dst_ln)
            {
                dst_hdr = (vm_slot_hdr_t*)dst_ln;

                /* skip headers that are empty or full */
                if((dst_hdr->avail == 0)  || (dst_hdr->avail == ext_per_slot))
                {
                    dst_ln = linked_list_next(dst_ln);
                    continue;
                }
                
                for(uint32_t di = 0 ; di < ext_per_slot; di++)
                {
                    dst_ext = &dst_hdr->array[di];

                    /* if we found an empty extent, we can insert it here */
                    if(dst_ext->length == 0)
                    {
                        /* copy the extent */
                        memcpy(dst_ext, src_ext, sizeof(vm_extent_t));

                        /* clear the slot from the source */
                        memset(src_ext, 0, sizeof(vm_extent_t));

                        /* One more available in the source */
                        src_hdr->avail++;

                        /* One less available in the destination */
                        dst_hdr->avail--;
                        compacted  = 1;
                        defrag_sts = VM_OK;
                        break;
                    }
                }

                if(compacted)
                {
                    break;
                }

                dst_ln = linked_list_next(dst_ln);
            }
        }
        src_ln = linked_list_next(src_ln);
    }

    return(defrag_sts);
}
