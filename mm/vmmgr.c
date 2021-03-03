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

#define EXTENT_IS_EMPTY(ext) (((!(ext)->base) && \
                             (!(ext)->length)))

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
    vm_rsrvd_mem_hdr_t   *rh      = NULL;
    vm_free_mem_hdr_t    *fh_hi   = NULL;
    vm_free_mem_hdr_t    *fh_lo   = NULL;
    vm_alloc_mem_hdr_t   *ah      = NULL;

    virt_addr_t           vm_base = 0;
    virt_addr_t           vm_max  = 0;
    uint32_t              offset  = 0;

    vm_max = cpu_virt_max();

    memset(&kernel_ctx, 0, sizeof(vm_ctx_t));
    
    if(pagemgr_init(&kernel_ctx.pagemgr) == -1)
        return(-1);

    vm_base = (~vm_base) - (vm_max >> 1);

    kprintf("Initializing Virtual Memory Manager BASE - 0x%x\n",vm_base);

    kernel_ctx.vm_base = vm_base;

    linked_list_init(&kernel_ctx.free_mem);
    linked_list_init(&kernel_ctx.rsrvd_mem);
    linked_list_init(&kernel_ctx.alloc_mem);
    spinlock_init(&kernel_ctx.lock);

    memset(&rem_extent, 0, sizeof(vm_extent_t));

    rh = (vm_rsrvd_mem_hdr_t*)pagemgr_alloc(&kernel_ctx.pagemgr,
                                                kernel_ctx.vm_base + 
                                                offset,
                                                VM_SLOT_SIZE,
                                                PAGE_WRITABLE | 
                                                PAGE_WRITE_THROUGH);

    offset += VM_SLOT_SIZE;

    fh_hi  = (vm_free_mem_hdr_t*)pagemgr_alloc(&kernel_ctx.pagemgr,
                                                kernel_ctx.vm_base + 
                                                offset,
                                                VM_SLOT_SIZE,
                                                PAGE_WRITABLE | 
                                                PAGE_WRITE_THROUGH);

    offset += VM_SLOT_SIZE;

    fh_lo  = (vm_free_mem_hdr_t*)pagemgr_alloc(&kernel_ctx.pagemgr,
                                                kernel_ctx.vm_base + 
                                                offset,
                                                VM_SLOT_SIZE,
                                                PAGE_WRITABLE | 
                                                PAGE_WRITE_THROUGH);

    offset += VM_SLOT_SIZE;

    ah    = (vm_alloc_mem_hdr_t*)pagemgr_alloc(&kernel_ctx.pagemgr,
                                                kernel_ctx.vm_base + 
                                                offset,
                                                VM_SLOT_SIZE,
                                                PAGE_WRITABLE | 
                                                PAGE_WRITE_THROUGH);


    kprintf("rsrvd_start 0x%x\n", rh);
    kprintf("free_start hi 0x%x\n", fh_hi);
    kprintf("free_start lo 0x%x\n", fh_lo);
    kprintf("alloc_start hi 0x%x\n", ah);

    if(rh == NULL    || 
       fh_hi == NULL || 
       fh_lo == NULL || 
       ah == NULL)
    {
        return(-1);
    }

    /* Clear the memory */
    memset(rh,    0, VM_SLOT_SIZE);
    memset(fh_hi, 0, VM_SLOT_SIZE);
    memset(fh_lo, 0, VM_SLOT_SIZE);
    memset(ah,    0, VM_SLOT_SIZE);
    
    linked_list_add_head(&kernel_ctx.rsrvd_mem, &rh->node);
    linked_list_add_head(&kernel_ctx.free_mem,  &fh_hi->node);
    linked_list_add_head(&kernel_ctx.free_mem,  &fh_lo->node);
    linked_list_add_head(&kernel_ctx.alloc_mem, &ah->node);

    /* How many free entries can we store per slot */
    kernel_ctx.free_per_slot = (VM_SLOT_SIZE - sizeof(vm_slot_hdr_t)) /
                                               sizeof(vm_extent_t);

    /* How many reserved entries can we store per slot */
    kernel_ctx.rsrvd_per_slot = (VM_SLOT_SIZE - sizeof(vm_slot_hdr_t)) /
                                                sizeof(vm_extent_t);

    /* How many allocated entries can we store per slot */
    kernel_ctx.alloc_per_slot = (VM_SLOT_SIZE - sizeof(vm_slot_hdr_t)) /
                                                sizeof(vm_extent_t);

    /* Get the start of the array */

    fh_hi->array[0].base   = kernel_ctx.vm_base;
    fh_hi->array[0].length = (((uintptr_t)-1) - kernel_ctx.vm_base)+1;

    /* the base of the lower memory cannot be 0, otherwise
     * we might be able to map/allocate from address 0 which would be a 
     * very bad thing 
     * */

    fh_lo->array[0].base = PAGE_SIZE;
    fh_lo->array[0].length = (vm_max >> 1);

    fh_hi->avail = kernel_ctx.free_per_slot;
    fh_lo->avail = kernel_ctx.free_per_slot;
    rh->avail    = kernel_ctx.rsrvd_per_slot;
    ah->avail    = kernel_ctx.alloc_per_slot;
 
    /* Start reserving entries that 
     * must remain in virtual memory 
     * no matter what
     * */



    return(0);
}

static virt_addr_t vm_alloc_slot(vm_ctx_t *ctx)
{
    vm_extent_t *fext = NULL;
    vm_slot_hdr_t *slot = NULL;
    vm_extent_t rem_ext;
    int          status = 0;
    
    /* extract the free slot */ 
    status = vm_extent_extract(&ctx->free_mem, 
                                ctx->free_per_slot, 
                                0, 
                                VM_SLOT_SIZE, 
                                &fext);
                
    if(status < 0)
    {
        return(0);
    }

    memset(&rem_ext, 0, sizeof(vm_extent_t));

    addr = pagemgr_alloc(&ctx->pagemgr,
                          fmem->base, 
                          VM_SLOT_SIZE, 
                          PAGE_WRITABLE);

    if(!addr)
    {
        return(0);
    }

    vm_extent_split(fext, 
                    fext->base, 
                    VM_SLOT_SIZE, 
                    &rem_ext);
    

    
    /* update the free slot */
    fext->base   += VM_SLOT_SIZE;
    fext->length -= VM_SLOT_SIZE;

    /* update the header if necessary */
    if(fext->length == 0)
    {
        memset(fext, 0, sizeof(vm_extent_t));
        
        slot = EXTENT_TO_HEADER(fext);

        slot->avail++;
    }

    memset((void*)addr, 0, VM_SLOT_SIZE);

    return(addr);
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
    uint32_t ent_per_slot
)
{
    list_node_t *node      = NULL;
    list_node_t *next_node = NULL;
    vm_slot_hdr_t *eh = NULL;

    node = linked_list_first(lh);

    while(node)
    {
        next_node  = linked_list_next(node);

        eh = (vm_slot_hdr_t*)node;

        for(uint16_t i = 0; i < ent_per_slot; i++)
        {
            if(EXTENT_IS_EMPTY(&eh->array[i]))
                continue;

            if(vmm_is_in_range(eh->array[i].base, 
                                 eh->array[i].length, 
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

static int vm_insert_extent_in_slot
(
    vm_slot_hdr_t *hdr,
    vm_extent_t   *ext,
    uint32_t      ext_per_slot
)
{
    vm_extent_t *c_ext = NULL;

    for(uint32_t i = 0; i < ext_per_slot; i++)
    {
        c_ext = &hdr->array[i];    

        if(EXTENT_IS_EMPTY(c_ext))
        {
            memcpy(c_ext, ext, sizeof(vm_extent_t));
            hdr->avail--;
            return(0);
        }
    }

    return(-1);
}

static int vm_extent_insert
(
    list_head_t *lh,
    uint32_t ent_per_slot,
    vm_extent_t *ext
)
{
    list_node_t     *en        = NULL;
    list_node_t     *next_en   = NULL;
    vm_slot_hdr_t *eh        = NULL;
    vm_extent_t     *edesc     = NULL;
    vm_extent_t     rsrv_ext;

    if(EXTENT_IS_EMPTY(ext))
        return(-ENOENT);

    en = linked_list_first(lh);

    if(en == NULL)
        return(-1);

    /* Start finding a free slot */

    while(en)
    {
        next_en = linked_list_next(en);
        eh = (vm_slot_hdr_t*)en;

        if(eh->avail > 0)
        {
            /* try to insert the slot */
            if(!vm_insert_extent_in_slot(eh, ext, ent_per_slot))
                return(0);
        }

        en = next_en;
    }

    return(-ENOMEM);
}

static int vm_extent_extract
(
    list_head_t *lh,
    uint32_t    ext_per_slot,
    virt_addr_t virt,
    virt_size_t len,
    vm_extent_t *out_ext
)
{
    list_node_t   *hn      = NULL;
    list_node_t   *next_hn = NULL;
    vm_slot_hdr_t *hdr     = NULL;
    vm_extent_t   *best    = NULL;
    vm_extent_t   *cext    = NULL;
    int           found    = 0;

    /* no length? no entry */
    if(len == 0)
        return(-1);

    hn = linked_list_first(lh);

    while(hn)
    {
        next_hn = linked_list_next(hn);

        hdr = (vm_slot_hdr_t*)hn;

        /* if hdr->avail == ctx->free_per_slot, we don't have
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

            if(EXTENT_IS_EMPTY(cext))
                continue;

            if(best == NULL)
                best = cext;

            if(virt != 0)
            {
                if(vmm_is_in_range(cext->base, 
                                   cext->length, 
                                   virt, 
                                   len))
                {
                    found = 1;
                    break;
                }
            }
            else if(best != NULL && hdr->type == VM_FREE_HDR)
            {
                if((best->length < len && cext->length >= len) ||
                   (best->length > cext->length && cext->length >= len))
                {
                    best = cext;
                }
            }
        }

        if(found)
            break;

        hn = next_hn;
    }

    if(best != NULL)
    {
        if(best->length < len)
            best = NULL;
    }

    if(best != NULL)
    {
        hdr = EXTENT_TO_HEADER(best);

        if(hdr->avail < ext_per_slot)
            hdr->avail++;
        else
            kprintf("ALREADY AT MAX\n");

        /* export the slot */ 
        memcpy(out_ext, best, sizeof(vm_extent_t));

        /* clear the slot that we've acquired */
        memset(best, 0, sizeof(vm_extent_t));
    }

    return((best == NULL) ? -1 : 0);

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

/* vm_acquire_free_slot - releases a slot */

static int vm_release_mem
(
    vm_ctx_t      *ctx,
    virt_addr_t    virt,
    virt_size_t    len
)
{
    list_node_t       *fn         = NULL;
    list_node_t       *next_fn    = NULL;
    vm_alloc_mem_t    *adesc      = NULL;
    vm_alloc_mem_t    *from_slot  = NULL;
    vm_alloc_mem_hdr_t *fh         = NULL;
    uint8_t            found      = 0;

    fn = linked_list_first(&ctx->free_mem);


    if(vm_acquire_alloc_slot(ctx, virt, len, &adesc) < 0)
        return(-1);


    return(0);

}

/* 
 * vm_acquire_mem - acquires memory 
 */

static virt_addr_t vm_acquire_mem
(
    vm_ctx_t *ctx,
    virt_addr_t virt,
    virt_size_t len,
    uint32_t    type
)
{
    vm_free_mem_t         *fmem      = NULL;
    virt_addr_t           ret_addr   = 0;
    virt_addr_t           map_addr   = 0;
    virt_addr_t           rem_virt   = 0;
    virt_size_t           rem_len    = 0;
    int                   rc         = 0;


    if(ctx == NULL)
        ctx = &kernel_ctx;

    if(len % PAGE_SIZE)
        len = ALIGN_UP(len, PAGE_SIZE);

    if(vm_acquire_free_slot(ctx, virt, len, &fmem) < 0)
    {
        return(0);
    }

    if(vm_split_block(&fmem->base,
                      &fmem->length,
                      ret_addr, 
                      len, 
                      &rem_virt,
                      &rem_len) > 0)
    {
        rc = vm_put_free_mem(ctx, rem_virt, rem_len);

        /* if we cannot store the free entry, then we 
         * must revert the change to avoid loosing free memory
         */ 
        if(rc < 0)
        {
            if(vm_merge_free_block(ctx, rem_virt, rem_len) < 0)
            {
                kprintf("ERROR while reversing the allocation\n");
                while(1);
            }
        }
    }


    return(0);
}


/* vm_reserve - reserve virtual memory */

int vm_reserve
(
    vm_ctx_t    *ctx, 
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t    type
)
{
    vm_extent_t *fext = NULL;
    vm_extent_t rem_ext;
    int                   int_status = 0;

    if(ctx == NULL)
        ctx = &kernel_ctx;

    memset(&rem, 0, sizeof(vm_free_mem_t));
    
    spinlock_lock_int(&ctx->lock, &int_status);
    

    if(vm_extent_acquire(&ctx->free_mem, 
                          ctx->free_per_slot, 
                          virt, 
                          len, 
                          &fext) < 0)
    {
        spinlock_unlock_int(&ctx->lock, int_status);
        return(-1);
    }


    fn = linked_list_first(&ctx->free_mem);

    while(fn)
    {
        next_fn = linked_list_next(fn);

        fh = (vm_free_mem_hdr_t*)fn;

        for(uint16_t i = 0; i < ctx->free_per_slot; i++)
        {
            fdesc = &fh->array[i];

            if(vmm_is_in_range(fdesc->base, fdesc->length, virt, len))
            {    
                rc = vm_add_reserved(ctx, virt, len, type);

                if(vm_split_free_block(&fh->array[i], virt, len, &rem) > 0)
                {
                    rc = vm_put_free_mem(ctx, &rem);

                    if(rc < 0)
                    {
                        ;
                    }
                }
                spinlock_unlock_int(&ctx->lock, int_status);
                return(rc);
            }
        }

        if(next_fn == NULL)
        {
            kprintf("Not enough space!!!\n");
            break;
        }
 
        fn = next_fn;
    }

    rc = vm_add_reserved(ctx, &rsrvd);
    
    spinlock_unlock_int(&ctx->lock, int_status);

    return(rc);
}

#if 0
/* vm_map - map phyical address to virtual address */
virt_addr_t vm_map
(
    vm_ctx_t *ctx,
    phys_addr_t phys, 
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t    attr
)
{
    list_node_t          *fn         = NULL;
    list_node_t          *next_fn    = NULL;
    vm_free_mem_t     *fdesc      = NULL;
    vm_free_mem_t      rem;
    vm_free_mem_t     *from_slot  = NULL;
    vm_free_mem_hdr_t *fh         = NULL;
    virt_addr_t           ret_addr   = 0;
    virt_addr_t           map_addr   = 0;
    virt_addr_t           rem_virt   = 0;
    virt_size_t           rem_len    = 0
    int                   int_status = 0;

    if(ctx == NULL)
        ctx = &kernel_ctx;

    if(len % PAGE_SIZE)
        len = ALIGN_UP(len, PAGE_SIZE);

    ret_addr = virt;

    spinlock_lock_int(&ctx->lock, &int_status);

    fn = linked_list_first(&ctx->free_mem);

    while(fn)
    {
        next_fn = linked_list_next(fn);
        fh = (vm_free_mem_hdr_t*)fn;

        if(fh->avail == 0)
        {
            fn = next_fn;
            continue;
        }

        for(uint16_t i  = 0; i < ctx->free_per_slot; i++)
        {
            fdesc = &fh->array[i];

            if(fdesc->length == 0)
                continue;

            if(from_slot == NULL)
                from_slot = fdesc;

            if(virt != 0)
            {
                if(VIRT_FROM_FREE_BLOCK(virt, len, from_slot))
                {
                    ret_addr = virt;
                    break;
                }

                from_slot = NULL;
            }
            else if(from_slot)
            {
                /* Save the best-fit slot found so far */

                if(from_slot->length < len && fdesc->length >= len)
                {
                    from_slot = fdesc;
                }
                else if((from_slot->length > fdesc->length) &&
                        (fdesc->length >= len))
                {
                    from_slot = fdesc;
                }
            }
        }

        if(next_fn == NULL && virt != 0 && ret_addr == 0)
        {
            kprintf("RESTARTING\n");
            virt = 0;
            fn = linked_list_first(&ctx->free_mem);
            from_slot = NULL;
            continue;
        }

        if(ret_addr == virt)
            break;

        fn = next_fn;
    }

    if(from_slot->length < len)
    {
        spinlock_unlock_int(&ctx->lock, int_status);
        return(0);
    }

    if(ret_addr == 0)
        ret_addr = from_slot->base;

    map_addr = pagemgr_map(&ctx->pagemgr, 
                            ret_addr, 
                            phys, 
                            len, 
                            attr);

    if(map_addr == 0)
    {
        pagemgr_unmap(&ctx->pagemgr,
                       from_slot->base, 
                       len);
        spinlock_unlock_int(&ctx->lock, int_status);
        return(0);
    }

    if(vm_split_free_block(from_slot, ret_addr, len, &rem) > 0)
    {
        vm_put_free_mem(ctx, &rem);
    }

    spinlock_unlock_int(&ctx->lock, int_status);

    return(ret_addr);
}

/* vm_alloc - allocate virtual memory */
virt_addr_t vm_alloc
(
    vm_ctx_t *ctx,
    virt_addr_t virt, 
    virt_size_t len,
    uint32_t    attr
)
{
    list_node_t          *fn         = NULL;
    list_node_t          *next_fn    = NULL;
    virt_addr_t           ret_addr   = 0;
    virt_size_t           dist       = 0;
    vm_free_mem_t     *fdesc      = NULL;
    vm_free_mem_t      rem;
    vm_free_mem_t     *from_slot  = NULL;
    vm_free_mem_hdr_t *fh         = NULL;
    virt_addr_t           near_virt  = 0;
    int                   int_status = 0;

    if(ctx == NULL)
        ctx = &kernel_ctx;

    if(len % PAGE_SIZE)
        len = ALIGN_UP(len, PAGE_SIZE);

    memset(&rem, 0, sizeof(vm_free_mem_t));

    spinlock_lock_int(&ctx->lock, &int_status);
    
    fn = linked_list_first(&ctx->free_mem);

    while(fn)
    {
        next_fn = linked_list_next(fn);
        fh = (vm_free_mem_hdr_t*) fn;

        if(fh->avail == 0)
        {
            fn = next_fn;
            continue;
        }

        for(uint16_t i  = 0; i < ctx->free_per_slot; i++)
        {
            fdesc = &fh->array[i];

            if(fdesc->length == 0)
                continue;

            if(from_slot == NULL)
                from_slot = fdesc;

            if(virt != 0)
            {
                if(VIRT_FROM_FREE_BLOCK(virt, len, from_slot))
                {
                    kprintf("FROM_SLOT 0x%x 0x%x\n",from_slot->base,from_slot->length);
                    ret_addr = virt;
                    break;
                }

                from_slot = NULL;
            }
            else if(from_slot)
            {
                /* Save the best-fit slot found so far */
                if(from_slot->length < len && fdesc->length >= len)
                    from_slot = fdesc;

                else if((from_slot->length > fdesc->length) &&
                        (fdesc->length >= len))
                    from_slot = fdesc;
            }
        }

        /* Now...surprise me */
        if(next_fn == NULL && virt != 0 && ret_addr == 0)
        {
            kprintf("restarting\n");
            virt = 0;
            fn = linked_list_first(&ctx->free_mem);
            from_slot = NULL;
            continue;
        }

        if(ret_addr == virt)
            break;

        fn = next_fn;
    }

    if( from_slot->length < len)
    {
        kprintf("Not From slot 0x%x - 0x%x 0x%x\n", 
                from_slot->base, 
                from_slot->length, 
                len);

        spinlock_unlock_int(&ctx->lock, int_status);
        return(0);
    }

    if(ret_addr == 0)
        ret_addr = from_slot->base;
    
    ret_addr = pagemgr_alloc(&ctx->pagemgr, ret_addr, len, attr);
    
    if(ret_addr == 0)
    {
        kprintf("PG_ALLOC_FAIL\n");
        spinlock_unlock_int(&ctx->lock, int_status);
        return(0);
    }

    

    if(vm_split_free_block(from_slot, ret_addr, len, &rem) > 0)
    {
        vm_put_free_mem(ctx, &rem);
    }      

    spinlock_unlock_int(&ctx->lock, int_status);

    return(ret_addr);
}

/* vm_free - free virtual memory */

int vm_free
(
    vm_ctx_t *ctx,
    virt_addr_t vaddr, 
    virt_size_t len
)
{
    virt_addr_t      virt = 0;
    vm_free_mem_t mm;
    int              status     = 0;
    int              int_status = 0;

    if(ctx == NULL)
        ctx = &kernel_ctx;

    if(len % PAGE_SIZE)
        len = ALIGN_UP(len, PAGE_SIZE);


    virt    = vaddr;
    mm.base = virt;
    mm.length = len;

    if(virt == 0)
        return(-1);
    
    spinlock_lock_int(&ctx->lock, &int_status);
    
    if(vm_is_free(ctx, virt, len))
    {
        spinlock_unlock_int(&ctx->lock, int_status);
        return(-1);
    }

    if(vm_is_reserved(ctx, virt, len))
    {
        spinlock_unlock_int(&ctx->lock, int_status);
        return(-1);
    }

    status = vm_merge_free_block(ctx, virt, len);

    if(status != 0)
        status = vm_put_free_mem(ctx, &mm);

    if(status == 0)
        status = pagemgr_free(&ctx->pagemgr, virt, len);

    spinlock_unlock_int(&ctx->lock, int_status);

    return(status);
}

/* vm_unmap - unmap virtual memory */
int vm_unmap
(
    vm_ctx_t *ctx,
    virt_addr_t vaddr, 
    virt_size_t len
)
{
    virt_addr_t virt = (virt_addr_t)vaddr;
    vm_free_mem_t mm;
    int status  = 0;
    int int_status = 0;

    if(len % PAGE_SIZE)
        len = ALIGN_UP(len, PAGE_SIZE);

    if(virt == 0)
        return(-1);

    if(ctx == NULL)
        ctx = &kernel_ctx;

    spinlock_lock_int(&ctx->lock, &int_status);

    if(vm_is_free(ctx, virt, len))
    {
        spinlock_unlock_int(&ctx->lock, int_status);
        return(-1);
    }
    if(vm_is_reserved(ctx, virt, len))
    {
        spinlock_unlock_int(&ctx->lock, int_status);
        return(-1);
    }

    status = vm_merge_free_block(ctx, virt, len);

    if(status != 0)
        status = vm_put_free_mem(ctx, &mm);

    if(status == 0)
        status = pagemgr_unmap(&ctx->pagemgr, virt, len);
    
    spinlock_unlock_int(&ctx->lock, int_status);

    return(status);
}


int vm_change_attrib
(
    vm_ctx_t *ctx,
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t    attr
)
{
    int status = 0;
    int int_status = 0;
    if(len == 0 || virt == 0)
        return(-1);

    if(ctx == NULL)
        ctx = &kernel_ctx;

    if(len % PAGE_SIZE)
        len = ALIGN_UP(len, PAGE_SIZE);

    spinlock_lock_int(&ctx->lock, &int_status);

    if(vm_is_reserved(ctx, virt, len))
    {
        spinlock_unlock_int(&ctx->lock, int_status);
        return(-1);
    }
    
    if(vm_is_free(ctx, virt, len))
    {
        spinlock_unlock_int(&ctx->lock, int_status);
        return(-1);
    }

    status = pagemgr_attr_change(&ctx->pagemgr, virt, len, attr);
    
    spinlock_unlock_int(&ctx->lock, int_status);

    return(status);
}

virt_addr_t vm_temp_identity_map
(
    vm_ctx_t *ctx,
    phys_addr_t phys, 
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t    attr
)
{
    if(ctx == NULL)
        ctx = &kernel_ctx;
    
    if(len % PAGE_SIZE)
        len = ALIGN_UP(len, PAGE_SIZE);

    return(pagemgr_map(&ctx->pagemgr,virt, phys, len, attr));
}

int vm_temp_identity_unmap
(
    vm_ctx_t *ctx,
    virt_addr_t vaddr, 
    virt_size_t len
)
{
    int status = 0;
    if(ctx == NULL)
        ctx = &kernel_ctx;


    if(len % PAGE_SIZE)
        len = ALIGN_UP(len, PAGE_SIZE);

    status = pagemgr_unmap(&ctx->pagemgr,(virt_addr_t)vaddr, len);

    return(status);
}
#endif