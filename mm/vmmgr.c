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
    list_head_t *lh,
    uint32_t ext_per_slot,
    vm_extent_t *ext
);

static int vm_extent_extract
(
    list_head_t *lh,
    uint32_t    ext_per_slot,
    vm_extent_t *ext
);

static int vm_undo
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
    uint32_t flags
);


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
                kprintf("BASE 0x%x LENGTH 0x%x\n",e->base,e->length);
        }

        node = next_node;
    }
#if 1
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
                kprintf("BASE 0x%x LENGTH 0x%x\n",e->base,e->length);
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

    vm_slot_hdr_t *hdr = NULL;
    uint32_t   rsrvd_count = 0;

    vm_extent_t re[] = 
    {
        /* Reserve kernel image - only the higher half */
        {
            .base   =  _KERNEL_VMA     + _BOOTSTRAP_END,
            .length =  _KERNEL_VMA_END - _KERNEL_VMA,
            .flags   = VM_PERMANENT,
        },
        /* Reserve remapping table */
        {
            .base   =  REMAP_TABLE_VADDR,
            .length =  REMAP_TABLE_SIZE,
            .flags   = VM_PERMANENT,
        },
        /* reserve head of tracking for free addresses */
        {
            .base   =  (virt_addr_t)linked_list_first(&ctx->free_mem),
            .length =  VM_SLOT_SIZE,
            .flags   = VM_PERMANENT,
        },
        {
            .base   =  (virt_addr_t)linked_list_first(&ctx->alloc_mem),
            .length =  VM_SLOT_SIZE,
            .flags   = VM_PERMANENT,
        },
        
    };

    rsrvd_count = sizeof(re) / sizeof(vm_extent_t);

    for(uint32_t i = 0; i < rsrvd_count; i++)
    {
        vm_space_alloc(ctx, re[i].base, re[i].length, re[i].flags);
    }
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
                     kernel_ctx.free_per_slot, 
                     &ext);

    /* Insert lower memory */
    ext.base   = 0;
    ext.length = ((vm_max >> 1) - ext.base) + 1;
    ext.flags  = VM_LOW_MEM;

    vm_extent_insert(&kernel_ctx.free_mem, 
                      kernel_ctx.free_per_slot, 
                      &ext);

    hdr = (vm_slot_hdr_t*)pagemgr_alloc(&kernel_ctx.pagemgr,
                                        kernel_ctx.vm_base + VM_SLOT_SIZE,
                                        VM_SLOT_SIZE,
                                        PAGE_WRITABLE | 
                                        PAGE_WRITE_THROUGH);
    
    if(hdr == NULL)
    {
        return(-1);
    }

    memset(hdr,    0, VM_SLOT_SIZE);
    linked_list_add_head(&kernel_ctx.alloc_mem, &hdr->node);
    hdr->avail = kernel_ctx.alloc_per_slot;

    vm_setup_protected_regions(&kernel_ctx);
    vm_list_entries();
while(1);
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
    alloc_ext.flags = VM_ALLOCED | VM_PERMANENT;
    
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

/*
 * vm_touches_range - checks if a segment touches another segment
 */

 static inline int vm_touches_range
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

            if(vm_is_in_range(hdr->array[i].base, 
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

   // kprintf("INSERTING %x %x\n",ext->base, ext->length);
    if(!ext->length)
        return(VM_NOENT);

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

    return(VM_NOMEM);
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

            if(best == NULL)
                best = cext;

            if(ext->base != 0)
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
            else if(best != NULL)
            {
                if((cext->flags & VM_REGION_MASK) == 
                   (ext->flags  & VM_REGION_MASK))
                {
                    best = NULL;
                    continue;
                }

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
     * so that an immediate insert will not require
     * a potential re-iteration
     * */
    if(linked_list_first(lh) != &hdr->node)
    {
        linked_list_remove(lh, &hdr->node);
        linked_list_add_head(lh, &hdr->node);
    }

    return(0);

}

/* vm_merge_free_block - merge adjacent memory blocks 
 * This routine requires to be re-written
 */
#if 0
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
#endif


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

    if(vm_is_in_range(src->base, 
                       src->length, 
                       virt, 
                       len))
    {
        dst->base = (virt + len) ;
        dst->length  = (src->base + src->length)  - 
                       (dst->base);
        src->length  = virt - (src->base);

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
    int         status = 0;

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
        addr = ALIGN_DOWN(addr, PAGE_SIZE);

    /* fill up the request extent */
    req_ext.base   = addr;
    req_ext.length = len;
    req_ext.flags  = flags & VM_REGION_MASK;

    /* acquire the extent */
    status = vm_extent_extract(&ctx->free_mem,
                               ctx->free_per_slot,
                               &req_ext);

//kprintf("EXTENT IS %x %x\n",req_ext.base, req_ext.length);
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
    /* in case we don't have the a preferred address,
     * we would set the address to req_ext.base
     * to do the split
     */ 
    if(addr == 0)
        addr = req_ext.base;
//kprintf("BEFORE SPLIT %x %x - %x %x - %x %x\n", req_ext.base,req_ext.length, rem_ext.base, rem_ext.length, addr, len);
    /* do the split */
    status = vm_extent_split(&req_ext, 
                              addr, 
                              len, 
                              &rem_ext);
    kprintf("AFTER SPLIT %x %x - %x %x - %x %x\n", req_ext.base,req_ext.length, rem_ext.base, rem_ext.length, addr, len);
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
                                  &rem_ext);

        /* Hehe... no slots?...try to allocate */
        if(status == VM_NOMEM)
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
                vm_undo(&ctx->alloc_mem, 
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

    /* set the alloc_ext to what 
     * we want to add to the 
     * allocated list 
     */

    alloc_ext.base = addr;
    alloc_ext.length = len;
    alloc_ext.flags = flags | req_ext.flags & VM_REGION_MASK;

    status = vm_extent_insert(&ctx->alloc_mem, 
                               ctx->alloc_per_slot, 
                              &alloc_ext);

    
     /* Hehe... no slots?....again?...try to allocate */
    if(status == VM_NOMEM)
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
            vm_undo(&ctx->alloc_mem, 
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
        status = vm_undo(&ctx->alloc_mem, 
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
    
    /* If the extent does not exist, the there is no memory allocated 
     * at that address
     */ 
    if(status < 0)
    {
        return(-1);
    }

    status = vm_extent_split(&req_ext, 
                             addr, 
                             len, 
                             &rem_ext);

    free_ext.base = addr;
    free_ext.length = addr;
    free_ext.flags = req_ext.flags & VM_REGION_MASK;


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
            status = vm_alloc_slot(ctx, 
                                  &ctx->alloc_mem,
                                  ctx->alloc_per_slot);

            if(status != 0)
            {
                kprintf("NO MEMORY\n");

                status = vm_undo(&ctx->free_mem, 
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
        status = vm_alloc_slot(ctx, 
                               &ctx->free_mem,
                               ctx->free_per_slot);

        if(status != 0)
        {
            status = vm_undo(&ctx->free_mem, 
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

static int vm_undo
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

virt_addr_t vm_alloc
(
    vm_ctx_t *ctx, 
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t attr
)
{
    while(1);
}


int vm_unmap
(
    vm_ctx_t *ctx, 
    virt_addr_t vaddr, 
    virt_size_t len
)
{
    while(1);
}


int vm_free
(
    vm_ctx_t *ctx, 
    virt_addr_t vaddr, 
    virt_size_t len
)
{
    while(1);
}
virt_addr_t vm_map
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