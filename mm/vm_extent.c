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

static int vm_virt_is_present
(
    virt_addr_t virt,
    virt_size_t len,
    list_head_t *lh,
    uint32_t ent_per_slot
);


int vm_extent_alloc_slot
(
    vm_ctx_t *ctx, 
    list_head_t *lh,
    uint32_t ext_per_slot
)
{
    virt_addr_t  addr = 0;
    vm_extent_t   fext;
    vm_slot_hdr_t *head_slot = NULL;
    vm_slot_hdr_t *new_slot = NULL;
    vm_extent_t alloc_ext;

    int          status = 0;
    kprintf("+++++++++++++++++++++++++++++++++++++++++++\n");
    memset(&fext, 0, sizeof(vm_extent_t));

    /* we want free memory to come from the high memory area */
    fext.base   = VM_BASE_AUTO;
    fext.length = VM_SLOT_SIZE;
    fext.flags  = VM_HIGH_MEM;

    /* extract a free slot that would fit our allocation*/ 
    status = vm_extent_extract(&ctx->free_mem, 
                                ctx->free_per_slot,  
                                &fext);
                
    if(status < 0)
    {
        return(VM_FAIL);
    }

    status = pgmgr_alloc(&ctx->pgmgr,
                        fext.base, 
                        VM_SLOT_SIZE, 
                        PAGE_WRITABLE);

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
    vm_extent_insert(&ctx->free_mem, 
                      ctx->free_per_slot, 
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

    status = vm_extent_insert(&ctx->alloc_mem, 
                      ctx->alloc_per_slot, 
                      &alloc_ext);
    
    /* Plot twist - we don't have extents to store the 
     * newly allocated slot
     */ 
    if(status == VM_NOMEM)
    {
        /* let's call the routine again. Yes, we're recursing */
        status = vm_extent_alloc_slot(ctx, 
                                    &ctx->alloc_mem, 
                                    ctx->alloc_per_slot);

        if(status != 0)
        {
            kprintf("CANNOT ALLOCATE A NEW SLOT\n");
            while(1);
        }

        /* Let's try again */
        status = vm_extent_insert(&ctx->alloc_mem, 
                      ctx->alloc_per_slot, 
                      &alloc_ext);

        if(status != 0)
        {
            kprintf("WE'RE DOOMED\n");
            while(1);
        }
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

int vm_extent_insert
(
    list_head_t *lh,
    uint32_t ext_per_slot,
    const vm_extent_t *ext
)
{
    list_node_t     *en        = NULL;
    list_node_t     *next_en   = NULL;
    vm_slot_hdr_t   *hdr        = NULL;
    vm_extent_t     *c_ext     = NULL;

    if((ext->flags & VM_REGION_MASK) == VM_REGION_MASK ||
       (ext->flags & VM_REGION_MASK) == 0)
    {
        kprintf("WHERE IS THIS MEMORY FROM?\n");
        while(1);
    }

   // kprintf("INSERTING %x %x\n",ext->base, ext->length);
    if(!ext->length)
        return(VM_NOENT);

    en = linked_list_first(lh);

    if(en == NULL)
        return(VM_FAIL);

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

                if(!c_ext->length)
                {
                    memcpy(c_ext, ext, sizeof(vm_extent_t));
                    hdr->avail--;
                    return(VM_OK);
                }
            }
        }

        en = next_en;
    }

    return(VM_NOMEM);
}

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
        return(-1);

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

            if(!cext->length)
                continue;

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

    if(!best || (best->length < ext->length))
    {
        return(VM_FAIL);
    }

    hdr = EXTENT_TO_HEADER(best);

    if(hdr->avail < ext_per_slot)
        hdr->avail++;
    else
        kprintf("ALREADY AT MAX\n");

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



/* vm_split_block - split a block 
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