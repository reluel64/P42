/* Virtual Memory Extent management
 * Part of P42 Kernel
 */

#include <pgmgr.h>
#include <vm.h>
#include <stddef.h>
#include <utils.h>
#include <cpu.h>
#include <platform.h>
#include <pfmgr.h>
#include <vm_extent.h>
 
extern struct vm_ctx vm_kernel_ctx;

static uint32_t vm_extent_avail
(
    struct list_head *lh,
    uint32_t  min_avail
);

static uint8_t vm_extent_joinable
(
    struct list_head *lh,
    struct vm_extent *ext
);

int32_t vm_extent_merge_in_hdr
(
    struct vm_extent_hdr *hdr
);

#define VM_SLOT_ALLOC_FLAGS  (VM_ALLOCATED | VM_PERMANENT | \
                              VM_HIGH_MEM  | VM_LOCKED)

static int vm_extent_alloc_tracking
(
    struct list_head *lh,
    uint32_t    ext_per_slot
)
{

    int           status            = 0;
    uint32_t      alloc_track_avail = 0;
    uint8_t       joinable          = 0;
    struct vm_extent_hdr *slot             = NULL;
    struct vm_extent   rem_ext           = VM_EXTENT_INIT;
    struct vm_extent   alloc_ext         = VM_EXTENT_INIT;
    struct vm_extent   orig_ext           = 
    {
        .base   = VM_BASE_AUTO, 
        .length = VM_SLOT_SIZE,
        .flags  = VM_HIGH_MEM
    };

    /* extract a free extent */
    status = vm_extent_extract(&vm_kernel_ctx.free_mem,
                               &orig_ext);

    if(status != VM_OK)
    {
        return(VM_NOMEM);
    }

    /* compute the remaining size so that we can insert it back */
    memcpy(&rem_ext, &orig_ext, sizeof(struct vm_extent));
    rem_ext.length -= VM_SLOT_SIZE;

    /* calculate the allocation address*/
    alloc_ext.base   = (orig_ext.base + orig_ext.length) - VM_SLOT_SIZE;
    alloc_ext.length = VM_SLOT_SIZE;
    alloc_ext.flags  = VM_SLOT_ALLOC_FLAGS;
    alloc_ext.prot   = VM_ATTR_WRITABLE;

    /* If the extent is either joinable or we will have place for it,
     * we will continue the execution. Otherwise we will exit.
     * One exception is if we allocate space to hold allocated memory.
     * In that case if we manage to allocate the slot, we will also have
     * a place where we will manage to store the extent
     */
   
    if(lh != &vm_kernel_ctx.alloc_mem)
    { 
        /* first check for available slots as it's less expensive */

        alloc_track_avail = vm_extent_avail(&vm_kernel_ctx.alloc_mem, 1);

        if(alloc_track_avail < 1)
        {
            joinable = vm_extent_joinable(&vm_kernel_ctx.alloc_mem,
                                          &alloc_ext);

            if(!joinable)
            {
                status = vm_extent_insert(&vm_kernel_ctx.free_mem,
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
                                      alloc_ext.prot,
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
        slot        = (struct vm_extent_hdr*)alloc_ext.base;

        /* init tracking slot */
        vm_extent_header_init(slot, ext_per_slot);

        /* add the slot to the list where it is required */
        linked_list_add_tail(lh, &slot->node);

        /* add memory to the tracking list */
        status = vm_extent_insert(&vm_kernel_ctx.alloc_mem,
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
                                  &orig_ext);

        return(VM_FAIL);
    }

    /* check if we have something to insert back */
    if(rem_ext.length > 0)
    {
        /* No failure, so we can put back the remaining extent */
        status = vm_extent_insert(&vm_kernel_ctx.free_mem,
                                  &rem_ext);
    }

    return(VM_OK);
}


static int vm_extent_release_tracking
(
    struct list_head *lh,
    uint32_t ext_per_slot,
    struct vm_extent_hdr *hdr
)
{
    int         status           = 0;
    uint8_t     joinable         = 0;
    uint32_t    free_track_avail = 0;
    uint32_t    alloc_track_avail = 0;
    struct vm_extent orig_extent      = VM_EXTENT_INIT;
    struct vm_extent free_extent      = VM_EXTENT_INIT;
    struct vm_extent rem_extent       = VM_EXTENT_INIT;
    struct vm_extent src_extent       = VM_EXTENT_INIT;


    src_extent.base = (virt_addr_t)hdr;
    src_extent.length = VM_SLOT_SIZE;

    /* extract the extent from the list */
    status = vm_extent_extract(lh, &src_extent);

    if(status != VM_OK)
    {
        return(VM_NOENT);
    }

    /* if there is anything in the header, do not free it */

    if(linked_list_count(&hdr->busy_ext) > 0)
    {
        return(VM_EXTENT_NOT_EMPTY);
    }

    /* remove the node from the list.*/
    linked_list_remove(lh, &hdr->node);
    
    memcpy(&orig_extent, &src_extent, sizeof(struct vm_extent));
    memcpy(&free_extent, &src_extent, sizeof(struct vm_extent));
 
    free_extent.flags = VM_HIGH_MEM;
    free_extent.prot  = 0;

    /* check if we have where to place the extent */
    free_track_avail = vm_extent_avail(lh, 1);

    if(free_track_avail < 1)
    {
        /* check if it is at least joinable */
        joinable = vm_extent_joinable(&vm_kernel_ctx.free_mem,
                                      &free_extent);

        /* not joinable either? - roll back and return */
        if(joinable == 0)
        {
            linked_list_add_head(lh, &hdr->node);
            status = vm_extent_insert(lh, &orig_extent);

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
        alloc_track_avail = vm_extent_avail(&vm_kernel_ctx.alloc_mem, 2);

        if(alloc_track_avail < 2)
        {
            /* check if we can join the left extent */
            joinable = vm_extent_joinable(&vm_kernel_ctx.alloc_mem,
                                          &src_extent);
            
            if(joinable == 0)
            {
                /* we are here so the left extent is not joinable - check
                 * if we can join the right extent
                 */
                joinable = vm_extent_joinable(&vm_kernel_ctx.alloc_mem,
                                              &rem_extent);

                /* we have nothing available and not joinable - roll back */
                if(joinable == 0)
                {
                    linked_list_add_tail(lh, &hdr->node);

                    status = vm_extent_insert(&vm_kernel_ctx.alloc_mem,
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
            vm_extent_header_init(hdr, ext_per_slot);
            linked_list_add_head(lh, &hdr->node);
            status = vm_extent_insert(lh, &orig_extent);

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
                              &free_extent);
    
    if(status != VM_OK && status != VM_NOENT)
    {
        kprintf("Could not insert extent to free list\n");
    }

    /* insert back the extent with the allocated memory */
    if(src_extent.length > 0)
    {
        status = vm_extent_insert(&vm_kernel_ctx.alloc_mem,
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
    struct list_head *lh,
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
    struct list_head   *lh,
    uint32_t      ext_per_slot,
    struct vm_extent_hdr *slot
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

/* insert an extent into a slot */
int vm_extent_insert
(
    struct list_head *lh,
    const struct vm_extent *ext
)
{
    struct list_node      *hdr_ln      = NULL;
    struct list_node      *next_hdr_ln = NULL;
    struct list_node      *ext_ln      = NULL;
    struct list_node      *m_ext_ln    = NULL;
    struct vm_extent_hdr  *hdr         = NULL;
    struct vm_extent_hdr  *f_hdr       = NULL;
    struct vm_extent      *c_ext       = NULL;
    struct vm_extent      *m_ext       = NULL;


    if((ext->flags & VM_REGION_MASK) == VM_REGION_MASK ||
       (ext->flags & VM_REGION_MASK) == 0)
    {
        kprintf("WHERE IS THIS MEMORY FROM?\n");
        return(VM_FAIL);
    }

    
    if(ext->length == 0)
    {
        return(VM_NOENT);
    }
    
    hdr_ln = linked_list_first(lh);
    f_hdr = (struct vm_extent_hdr*)hdr_ln;
    /* Start finding a free slot */
    while(hdr_ln)
    {
        next_hdr_ln = linked_list_next(hdr_ln);
        hdr = (struct vm_extent_hdr*)hdr_ln;

        if(((linked_list_count(&hdr->avail_ext) > 0) &&
           (linked_list_count(&hdr->avail_ext) < 
           linked_list_count(&f_hdr->avail_ext)))    ||
           (linked_list_count(&f_hdr->avail_ext) == 0))
        {
            f_hdr = hdr;
        }

        /* if hdr->avail == ext_per_slot, then do not enter
         * as we don't have anything to join
         */
        ext_ln = linked_list_first(&hdr->busy_ext);

        while(ext_ln)
        {
            c_ext = (struct vm_extent*)ext_ln;

            if(vm_extent_join(ext, c_ext) == VM_OK)
            {
                /*  if we did join, try to see if there are some 
                 *  additional extents that can be merged after this one
                 */
                m_ext_ln = linked_list_next(ext_ln);

                while(m_ext_ln)
                {
                    m_ext = (struct vm_extent*)m_ext_ln;

                    if(vm_extent_join(m_ext, c_ext) == VM_OK)
                    {
                        linked_list_remove(&hdr->busy_ext, &m_ext->node);
                        linked_list_add_tail(&hdr->avail_ext, &m_ext->node);
                    }

                    m_ext_ln = linked_list_next(m_ext_ln);
                }

                return(VM_OK);
            }

            ext_ln = linked_list_next(ext_ln);
        }

        hdr_ln = next_hdr_ln;
    }

    /* If we were unable to merge, just add the information 
     * where we found a free slot 
     */
    if(f_hdr != NULL)
    {
        /* remove the extent from the free extents */
        c_ext = (struct vm_extent*)linked_list_get_last(&f_hdr->avail_ext);

        if(c_ext != NULL)
        {
            linked_list_add_tail(&f_hdr->busy_ext, &c_ext->node);
            vm_extent_copy(c_ext, ext);
            return(VM_OK);
        }
    }

    return(VM_NOMEM);
}

/* extract an extent from the slot */

int vm_extent_extract
(
    struct list_head *lh,
    struct vm_extent *ext
)
{
    struct list_node      *hdr_ln      = NULL;
    struct list_node      *ext_ln      = NULL;
    struct vm_extent_hdr *hdr     = NULL;
    struct vm_extent   *best    = NULL;
    struct vm_extent   *cext    = NULL;
    int           found    = 0;

    /* no length? no entry */
    if((ext == NULL) || (ext->length == 0))
    {
        kprintf("NO LENGTH, NO ENTRY\n");
        return(VM_FAIL);
    }
    
    hdr_ln = linked_list_first(lh);

    while(hdr_ln)
    {
        hdr = (struct vm_extent_hdr*)hdr_ln;

        ext_ln = linked_list_first(&hdr->busy_ext);

        while(ext_ln)
        {
            cext = (struct vm_extent*)ext_ln;

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
            ext_ln = linked_list_next(ext_ln);
        }

        if(found)
        {
            break;
        }

        hdr_ln = linked_list_next(hdr_ln);
    }

    /* verify if we have at least something to return */
    if((best == NULL) || (best->length < ext->length))
    {
        return(VM_FAIL);
    }

    hdr = VM_EXTENT_TO_HEADER(best);

    /* export the extent */ 
    vm_extent_copy(ext, best);
    
    /* remove the extent from the busy extents */
    linked_list_remove(&hdr->busy_ext, &best->node);

    /* wipe the memory of the extent */
    memset(best, 0, sizeof(struct vm_extent));
    
    /* add the extent to the available extent list */
    linked_list_add_tail(&hdr->avail_ext, &best->node);    
    
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
    struct vm_extent *ext_left,
    const virt_addr_t virt_mid,
    const virt_size_t len_mid,
    struct vm_extent *ext_right
)
{

    ext_right->base = 0;
    ext_right->length = 0;

    if(!vm_is_in_range(ext_left->base, 
                       ext_left->length, 
                       virt_mid, 
                       len_mid))
    {
        return(-1);
    }
    
    ext_right->base = (virt_mid + len_mid) ;

    ext_right->length  = (ext_left->base + ext_left->length)  - 
                   (ext_right->base);

    ext_left->length  = virt_mid - (ext_left->base);

    /* make sure the flags are the same regardless
     * of what happens next 
     */
    ext_right->flags = ext_left->flags;
    ext_right->prot = ext_left->prot;

    if(ext_right->length == 0)
    {
        ext_right->base = 0;
        return(0);
    }

    if(ext_left->length == 0)
    {
        ext_left->base = ext_right->base;
        ext_left->length = ext_right->length;

        ext_right->base = 0;
        ext_right->length = 0;

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
    const struct vm_extent *src,
    const struct vm_extent *dest
)
{
    if(src->prot != dest->prot)
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
    const struct vm_extent *src,
    struct vm_extent *dest
)
{
    int can_join = 0;
    int ret_status = VM_OK;

    can_join = vm_extent_can_join(src, dest);

    switch(can_join)
    {
        case 1:
        {
            dest->base    = src->base;
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
    struct list_head *lh
)
{
    struct list_node   *src_hdr_ln  = NULL;
    struct list_node   *dst_hdr_ln  = NULL;
    struct list_node   *src_ext_ln = NULL;
    struct list_node   *dst_ext_ln = NULL;
    
    struct vm_extent_hdr *src_hdr = NULL;
    struct vm_extent_hdr *dst_hdr = NULL;
    struct vm_extent   *src_ext = NULL;
    struct vm_extent   *dst_ext = NULL;
    int            status   = VM_FAIL;

    if(lh == NULL)
    {
        return(VM_FAIL);
    }    

    dst_hdr_ln = linked_list_first(lh);

    while(dst_hdr_ln)
    {
        dst_hdr = (struct vm_extent_hdr*)dst_hdr_ln;
        dst_ext_ln = linked_list_first(&dst_hdr->busy_ext);

        while(dst_ext_ln)
        {
            dst_ext = (struct vm_extent*)dst_ext_ln;

            src_hdr_ln = linked_list_first(lh);

            while(src_hdr_ln)
            {
                src_hdr = (struct vm_extent_hdr*)src_hdr_ln;

                src_ext_ln = linked_list_first(&src_hdr->busy_ext);

                while(src_ext_ln)
                {
                    src_ext = (struct vm_extent*) src_ext_ln;

                    if(vm_extent_can_join(src_ext, dst_ext))
                    {
                        kprintf("SRC %x DST %x\n",src_ext->base, dst_ext->base);
                        //kprintf("Can be joined\n");
                    }

                    src_ext_ln = linked_list_next(src_ext_ln);
                }

                src_hdr_ln = linked_list_next(src_hdr_ln);
            }
            

            dst_ext_ln = linked_list_next(dst_ext_ln);
        }

        dst_hdr_ln = linked_list_next(dst_hdr_ln);
    }

   
    return(status);
}

static uint32_t vm_extent_avail
(
    struct list_head *lh,
    uint32_t  min_avail
)
{
    struct vm_extent_hdr *hdr = NULL;
    struct list_node   *node = NULL;
    uint32_t      free_slots = 0;

    node = linked_list_first(lh);

    while(node)
    {
        hdr = (struct vm_extent_hdr*)node;

        free_slots += linked_list_count(&hdr->avail_ext);

        if(min_avail != 0)
        {
            if(min_avail <= free_slots)
            {
                break;
            }
        }

        node = linked_list_next(node);
    }

    return(free_slots);
}

static uint8_t vm_extent_joinable
(
    struct list_head *lh,
    struct vm_extent *ext
)
{
    uint8_t joinable = 0;
    struct vm_extent_hdr *hdr = NULL;
    struct vm_extent   *pext = NULL;
    struct list_node *ln = NULL;
    struct list_node *en = NULL;

    ln = linked_list_first(lh);

    while(ln)
    {
        hdr = (struct vm_extent_hdr*) ln;

        en = linked_list_first(&hdr->busy_ext);

        while(en)
        {
            pext = (struct vm_extent*)en;
            
            if(vm_extent_can_join(ext, pext) > 0)
            {
                joinable = 1;
                break;
            }
            en = linked_list_next(en);
        }

        ln = linked_list_next(ln);
    }

    return(joinable);
}

int32_t vm_extent_merge_in_hdr
(
    struct vm_extent_hdr *hdr
)
{
    struct list_node *src_ln = NULL;
    struct list_node *dst_ln = NULL;
    struct vm_extent *dext = NULL;
    struct vm_extent *sext = NULL;

    dst_ln = linked_list_first(&hdr->busy_ext);

    while(dst_ln)
    {
        dext = (struct vm_extent*)dst_ln;
        src_ln = linked_list_next(dst_ln);

        while(src_ln)
        {
            sext = (struct vm_extent*)src_ln;

            if(vm_extent_join(sext, dext) == VM_OK)
            {
                linked_list_remove(&hdr->busy_ext, &sext->node);
                linked_list_add_tail(&hdr->avail_ext, &sext->node);
                break;
            }

            src_ln = linked_list_next(src_ln);
        }

        dst_ln = linked_list_next(dst_ln);
    }
    return(0);
}

void vm_extent_header_init
(
    struct vm_extent_hdr *hdr,
    uint32_t extent_count
)
{
    if(hdr != NULL)
    {
        kprintf("New header alloc %x\n", hdr);
        memset(hdr, 0, sizeof(struct vm_extent_hdr) + 
                       (extent_count * sizeof(struct vm_extent)));

        hdr->extent_count = extent_count;

        for(uint32_t i = 0; i < extent_count; i++)
        {
            linked_list_add_tail(&hdr->avail_ext, 
                                 &hdr->ext_area[i].node);
        }
    }
}

void vm_extent_copy
(
    struct vm_extent *dst,
    const struct vm_extent *src
)
{
    dst->base = src->base;
    dst->flags = src->flags;
    dst->length = src->length;
    dst->prot = src->prot;
}