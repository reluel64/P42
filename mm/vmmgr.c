/* Virtual Memory Manager
 * Part of P42 Kernel
 */

#include <paging.h>
#include <pagemgr.h>
#include <vmmgr.h>
#include <stddef.h>
#include <utils.h>
#include <cpu.h>
#include <platform.h>
#include <pfmgr.h>

extern virt_size_t __max_linear_address(void);

#define VM_SLOT_SIZE (PAGE_SIZE)

#define EXTENT_TO_HEADER(entry)         (void*)(((virt_addr_t)(entry)) - \
                                              (((virt_addr_t)(entry)) %  \
                                              VM_SLOT_SIZE ))

static vm_ctx_t kernel_ctx;

static int vm_extent_acquire
(
    list_head_t *lh,
    uint32_t    ext_per_slot,
    virt_addr_t virt,
    virt_size_t len,
    vm_extent_t **out_ext
);

static int vm_virt_is_present
(
    virt_addr_t virt,
    virt_size_t len,
    list_head_t *lh,
    uint32_t ent_per_slot
);

static int vm_extent_insert
(
    vm_ctx_t *ctx,
    list_head_t *lh,
    uint32_t ent_per_slot,
    vm_extent_t *ext
);

static int vm_insert_extent_in_slot
(
    vm_slot_hdr_t *hdr,
    vm_extent_t   *ext,
    uint32_t      ext_per_slot
);

void vm_list_entries()
{
    list_node_t *node = NULL;
    list_node_t *next_node = NULL;
    vm_free_mem_t *fmem = NULL;
    vm_rsrvd_mem_t *rsrvd  = NULL;
    vm_free_mem_hdr_t *fh = NULL;
    vm_rsrvd_mem_hdr_t *rh = NULL;

    kprintf("FREE_DESC_PER_PAGE %d\n",kernel_ctx.free_per_slot);
    kprintf("RSRV_DESC_PER_PAGE %d\n",kernel_ctx.rsrvd_per_slot);

    node = linked_list_first(&kernel_ctx.free_mem);

    kprintf("----LISTING FREE RANGES----\n");

    while(node)
    {
        fh = (vm_free_mem_hdr_t*)node;

        next_node = linked_list_next(node);

        for(uint16_t i = 0; i < kernel_ctx.free_per_slot; i++)
        {
            fmem  = &fh->array[i];

            if(fmem->base != 0  && fmem->length != 0)
                kprintf("BASE 0x%x LENGTH 0x%x\n",fmem->base,fmem->length);
        }

        node = next_node;
    }
#if 1
    node = linked_list_first(&kernel_ctx.rsrvd_mem);

    kprintf("----LISTING RESERVED RANGES----\n");

    while(node)
    {
        next_node = linked_list_next(node);
        rh = (vm_rsrvd_mem_hdr_t*)node;

        for(uint16_t i = 0; i < kernel_ctx.rsrvd_per_slot; i++)
        {
            rsrvd  = &rh->array[i];
            if(rsrvd->base != 0  && rsrvd->length != 0)
                kprintf("BASE 0x%x LENGTH 0x%x\n",rsrvd->base,rsrvd->length);
        }

        node = next_node;
    }
#endif
}


static int vm_setup_protected_regions
(
    vm_ctx_t *ctx
)
{
    vm_extent_t          *ext     = NULL;
    vm_extent_t           rem_extent; 
    int status = 0;

    vm_extent_t rsrvd_extents[] = 
    {
        {
            .base   =  _KERNEL_VMA     + _BOOTSTRAP_END
            .length =  _KERNEL_VMA_END - _KERNEL_VMA,
            .type   = VM_PERMANENT,
        },
        {
            .base   =  REMAP_TABLE_VADDR
            .length =  REMAP_TABLE_SIZE,
            .type   = VM_PERMANENT,
        },
        {
            .base   =  _KERNEL_VMA     + _BOOTSTRAP_END
            .length =  VM_SLOT_SIZE,
            .type   = VM_PERMANENT,
        },
        {
            .base   =  _KERNEL_VMA     + _BOOTSTRAP_END
            .length = VM_SLOT_SIZE,
            .type   = VM_PERMANENT,
        },
    }

    vm_extent_acquire(&ctx->free_mem,
                       ctx->free_per_slot,
                      _KERNEL_VMA     + _BOOTSTRAP_END,
                      _KERNEL_VMA_END - _KERNEL_VMA,
                      &ext);

    status = vm_extent_split(ext,
                    _KERNEL_VMA     + _BOOTSTRAP_END,
                    _KERNEL_VMA_END - _KERNEL_VMA,
                    &rem_extent);

    if(status > 0)
    {
        vm_extent_insert(ctx, 
                        &ctx->alloc_mem, 
                        ctx->alloc_per_slot,
                        &rem_extent);
    }

    

    /* Reserve kernel image - only the higher half */
    vm_reserve(&kernel_ctx,
                _KERNEL_VMA     + _BOOTSTRAP_END,
                _KERNEL_VMA_END - _KERNEL_VMA,
                VM_RES_KERNEL_IMAGE);

    /* Reserve remapping table */
    vm_reserve(&kernel_ctx,
                REMAP_TABLE_VADDR,
                REMAP_TABLE_SIZE,
                VM_REMAP_TABLE);

    /* reserve head of tracking for free addresses */
    vm_reserve(&kernel_ctx,
                (virt_addr_t)fh_hi,
                VM_SLOT_SIZE,
                VM_RES_FREE);

    vm_reserve(&kernel_ctx,
                (virt_addr_t)fh_lo,
                VM_SLOT_SIZE,
                VM_RES_FREE);

    /* reserve head of tracking for allocated addresses */
    vm_reserve(&kernel_ctx,
                (virt_addr_t)ah,
                VM_SLOT_SIZE,
                VM_RES_FREE);

    /* reserve head of tracking for reserved addresses 
     * Yes, that some kind of a chicken-egg situation
     */
    vm_reserve(&kernel_ctx,
                (virt_addr_t)rh,
                VM_SLOT_SIZE,
                VM_RES_RSRVD);
}
 
int vm_init(void)
{


    virt_addr_t           vm_base = 0;
    virt_addr_t           vm_max  = 0;
    uint32_t              offset  = 0;
    vm_slot_hdr_t         *hdr = NULL;
    vm_extent_t           ext;

    vm_max = cpu_virt_max();

    memset(&kernel_ctx, 0, sizeof(vm_ctx_t));
    
    if(pagemgr_init(&kernel_ctx.pagemgr) == -1)
        return(-1);

    vm_base = (~vm_base) - (vm_max >> 1);

    kprintf("Initializing Virtual Memory Manager BASE - 0x%x\n",vm_base);

    kernel_ctx.vm_base = vm_base;

    linked_list_init(&kernel_ctx.free_mem);
    linked_list_init(&kernel_ctx.alloc_mem);
    spinlock_init(&kernel_ctx.lock);

    hdr = (vm_slot_hdr_t*)pagemgr_alloc(&kernel_ctx.pagemgr,
                                        kernel_ctx.vm_base,
                                        VM_SLOT_SIZE,
                                        PAGE_WRITABLE | 
                                        PAGE_WRITE_THROUGH);

    if(hdr == NULL)
    {
        kprintf("Failed to initialize VMM\n");
        while(1);
    }

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
    ext.length = (((uintptr_t)-1) - kernel_ctx.vm_base)+1;
    ext.flags  = VM_HIGH_MEM;

    vm_extent_insert(&kernel_ctx.free_mem, 
                     hdr->avail, 
                     &ext);

    /* Insert lower memory */
    ext.base   = PAGE_SIZE;
    ext.length = (vm_max >> 1);
    ext.flags  = VM_LOW_MEM;

    vm_extent_insert(&kernel_ctx.free_mem, 
                      hdr->avail, 
                      &ext);


    return(0);
}

static int vm_alloc_slot
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
    
    memset(&fext, 0, sizeof(vm_extent_t));

    /* we want free memory to come from the high memory area */
    fext.length = VM_SLOT_SIZE;
    fext.flags  = VM_HIGH_MEM;

    /* extract a free slot that would fit our allocation*/ 
    status = vm_extent_extract(&ctx->free_mem, 
                                ctx->free_per_slot,  
                                &fext);
                
    if(status < 0)
    {
        return(-1);
    }

    new_slot = (vm_slot_hdr_t*)pagemgr_alloc(&ctx->pagemgr,
                                             fext.base, 
                                             VM_SLOT_SIZE, 
                                             PAGE_WRITABLE);

    if(new_slot == NULL)
    {
        return(-1);
    }

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

    /* Memory is allocated and MUST NOT be swapped */
    alloc_ext.flags = VM_ALLOC | VM_PERMANENT;
    
    /* insert the allocated memory into the list */

    status = vm_extent_insert(&ctx->alloc_mem, 
                      ctx->alloc_per_slot, 
                      &alloc_ext);
    
    /* Plot twist - we don't have extents to store the 
     * newly allocated slot
     */ 
    if(status == -ENOMEM)
    {
        /* let's call the routine again. Yes, we're recursing */
        status = vm_alloc_slot(ctx, 
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

    return(0);
}

/*
 * vmm_is_in_range - checks if a segment is in the range of another segment
 */

static inline int vmm_is_in_range
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

/*
 * vmm_touches_range - checks if a segment touches another segment
 */

 static inline int vmm_touches_range
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

    if(base >= req_base)
    {
        if((req_end <= end && req_end >= base) || (req_end >= end))
            return(1);
    }
    else if(req_end >= end)
    {
        if((base <= req_base && req_base <= end) || (base >= req_base))
            return(1);
    }
    else if(req_base >= base && req_end <= end)
        return(1);

    return(0);
}


static int vm_virt_is_present
(
    virt_addr_t virt,
    virt_size_t len,
    list_head_t *lh,
    uint32_t    ext_per_slot
)
{
    list_node_t *node      = NULL;
    list_node_t *next_node = NULL;
    vm_slot_hdr_t *hdr = NULL;

    node = linked_list_first(lh);

    while(node)
    {
        next_node  = linked_list_next(node);

        hdr = (vm_slot_hdr_t*)node;

        for(uint16_t i = 0; i < ext_per_slot; i++)
        {
            if(!hdr->array[i].length)
                continue;

            if(vmm_is_in_range(hdr->array[i].base, 
                               hdr->array[i].length, 
                               virt, 
                               len))
            {
                return(1);
            }
        }

        node = next_node;
    }

    return(0);
}

static int vm_extent_insert
(
    list_head_t *lh,
    uint32_t ext_per_slot,
    vm_extent_t *ext
)
{
    list_node_t     *en        = NULL;
    list_node_t     *next_en   = NULL;
    vm_slot_hdr_t *hdr        = NULL;
    vm_extent_t     *c_ext     = NULL;

    if(!ext->length)
        return(-ENOENT);

    en = linked_list_first(lh);

    if(en == NULL)
        return(-1);

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
                    return(0);
                }
            }
        }

        en = next_en;
    }

    return(-ENOMEM);
}

static int vm_extent_extract
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
    if(!ext || !ext->length  || !ext->flags)
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

            if(best == NULL)
                best = cext;

            if(ext->base != 0)
            {
                if(vmm_is_in_range(cext->base, 
                                   cext->length, 
                                   ext->base, 
                                   ext->length))
                {
                    found = 1;
                    best = cext;
                    break;
                }
            }
            else if(best != NULL)
            {
                if((best->length < ext->length   && 
                    cext->length >= ext->length) ||
                   (best->length > cext->length  && 
                   cext->length >= ext->length))
                {
                    best = cext;
                }
            }
        }

        if(found)
            break;

        hn = next_hn;
    }

    if(!best || (best->length < ext->length))
    {
        return(-1);
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
     * so that an insert will not require
     * a potential re-iteration
     * */
    if(linked_list_first(lh) != &hdr->node)
    {
        linked_list_remove(lh, &hdr->node);
        linked_list_add_head(lh, &hdr->node);
    }

    return(0);

}

/* vm_merge_free_block - merge adjacent memory blocks */

static int vm_extent_merge
(
    list_head_t *lst,
    uint32_t    per_slot,
    virt_addr_t virt,
    virt_size_t len
)
{
    list_node_t       *fn           = NULL;
    list_node_t       *next_fn      = NULL;
    vm_slot_hdr_t   *fh           = NULL;
    vm_slot_hdr_t   *to_merge_hdr = NULL;
    vm_extent_t       *fdesc        = NULL;
    vm_extent_t       *to_merge     = NULL;
    uint8_t            merged       = 0;

    fn = linked_list_first(lst);

    while(fn)
    {
        merged = 0;
        next_fn = linked_list_next(fn);

        fh = (vm_extent_t*)fn;

        for(uint16_t i = 0; i < per_slot; i++)
        {
            fdesc = &fh->array[i];

            /* Merge from left */
            if(fdesc->base == virt + len)
            {
                fdesc->base    = virt;
                fdesc->length += len;

                if(merged == 1)
                {
                    to_merge->base = 0;
                    to_merge->length = 0;
                    to_merge_hdr = EXTENT_TO_HEADER(to_merge);

                    if(per_slot > to_merge_hdr->avail)
                        to_merge_hdr->avail++;
                    else
                        kprintf("WARNING %s %d\n",__FUNCTION__,__LINE__);
                }

                to_merge = fdesc;
                virt = to_merge->base;
                len  = to_merge->length;

                /* Start again */
                next_fn = linked_list_first(lst);
                merged = 1;
                break;
            }
            /* Merge from right */
            else if(fdesc->base + fdesc->length == virt)
            {
                fdesc->length += len;

                if(merged == 1)
                {
                    to_merge->base = 0;
                    to_merge->length = 0;
                    to_merge_hdr = ENTRY_TO_HEADER(to_merge);
                    
                    if(per_slot > to_merge_hdr->avail)
                        to_merge_hdr->avail++;
                    else
                        kprintf("WARNING %s %d\n",__FUNCTION__,__LINE__);
                }

                /* Start again */
                to_merge = fdesc;
                virt = to_merge->base;
                len  = to_merge->length;
                next_fn = linked_list_first(lst);
                merged = 1;
                break;
            }
        }

        fn = next_fn;
    }

    return(merged ? 0 : -1);
}



/* vm_split_block - split a block 
 * and return the remaining block size
 * */

static inline int vm_extent_split
(
    vm_extent_t *src,
    const virt_addr_t virt,
    const virt_size_t len,
    vm_extent_t *dst
)
{

    dst->base = 0;
    dst->length = 0;

    if(vmm_is_in_range(src->base, 
                       src->length, 
                       virt, 
                       len))
    {
        dst->base = (virt + len) ;
        dst->length  = (src->base + src->length)  - 
                       (dst->base);
        src->base  = virt - (src->base);

        /* make sure the flags are the same regardless
         * of what happens next 
         */
        dst->flags = src->flags;

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
    
    return(-1);
    
}


virt_addr_t vm_space_alloc
(
    vm_ctx_t *ctx,
    virt_addr_t addr,
    virt_size_t len,
    uint32_t flags
)
{
    vm_extent_t req_ext;
    vm_extent_t rem_ext;
    vm_extent_t alloc_ext;

    int status = 0;

    /* check if we're stupid or not */
    if((flags & VM_REGION_MASK) == VM_REGION_MASK)
        return(-1);

    if((flags & VM_MEM_TYPE_MASK) == VM_MEM_TYPE_MASK)
        return(-1);

    /* clear the extent */
    memset(&req_ext, 0, sizeof(vm_extent_t));

    if(len % PAGE_SIZE)
        len = ALIGN_UP(len, PAGE_SIZE);

    if(addr % PAGE_SIZE)
        len = ALIGN_DOWN(len, PAGE_SIZE);

    /* fill up the request extent */
    req_ext.base   = addr;
    req_ext.length = len;
    req_ext.flags  = flags & VM_REGION_MASK;

    /* acquire the extent */
    status = vm_extent_extract(&ctx->free_mem,
                               ctx->free_per_slot,
                               &req_ext);
    
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

    if(status < 0)
    {
        kprintf("OOOPS...no memory\n");
        return(NULL);
    }
    
    memset(&rem_ext, 0, sizeof(vm_extent_t));
    
    /* split the extent if needed */
    /* in case we don't have the a preferred address,
     * we would set the address to req_ext.base
     * to do the split
     */ 
    if(addr == 0)
        addr = req_ext.base;

    /* do the split */
    status = vm_extent_split(&req_ext, 
                              addr, 
                              len, 
                              &rem_ext);

    /* Insert the left side - this is guaranteed to work 
     * If it doesn't...well...we're fucked
     */

    vm_extent_insert(&ctx->free_mem,
                     ctx->free_per_slot,
                     &req_ext);

    /* If we have a right side, insert it */
    if(status > 0)
    {
        status = vm_extent_insert(&ctx->free_mem,
                                  ctx->free_per_slot,
                                  &req_ext);

        /* Hehe... no slots?...try to allocate */
        if(status == -ENOMEM)
        {
            status = vm_alloc_slot(ctx, 
                                   &ctx->free_mem, 
                                   ctx->free_per_slot);
            
            /* status != 0? ...well..FUCK */
            if(status != 0)
            {
                /* we should revert what we did earlier and bail out, 
                 * but for now we will deadlock here
                 */
                kprintf("OOOPS....no memory chief\n");
                while(1);
            }

            /* Ok, let's do this again, shall we? */
            status = vm_extent_insert(&ctx->free_mem,
                                       ctx->free_per_slot,
                                      &req_ext);
        }
    }

    /* set the alloc_ext to what 
     * we want to add to the 
     * allocated list 
     */

    alloc_ext.base = addr;
    alloc_ext.length = len;
    alloc_ext.flags = flags;

    status = vm_extent_insert(&ctx->alloc_mem, 
                               ctx->alloc_per_slot, 
                              &alloc_ext);

    
     /* Hehe... no slots?....again?...try to allocate */
    if(status == -ENOMEM)
    {
        status = vm_alloc_slot(ctx, 
                               &ctx->alloc_mem, 
                               ctx->alloc_per_slot);
        
        /* status != 0? ...well..FUCK */
        if(status != 0)
        {
            /* we should revert what we did earlier and bail out, 
                * but for now we will deadlock here
                */
            kprintf("OOOPS....no memory chief\n");
            while(1);
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

        return(NULL);
    }

}

