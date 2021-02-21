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

#define VMM_SLOT_SIZE (PAGE_SIZE)

#define VIRT_START_IN_BLOCK(virt, fblock)  ((fblock)->base <= (virt)) && \
      (((fblock)->base + (fblock)->length) >= ((virt)))

#define VIRT_FROM_FREE_BLOCK(virt, len, fblock) \
                                vmm_is_in_range((fblock)->base, \
                                (fblock)->length, \
                                (virt), (len))



#define ENTRY_TO_HEADER(entry)         (void*)(((virt_addr_t)(entry)) - \
                                              (((virt_addr_t)(entry)) %  \
                                              PAGE_SIZE ))



#define VMM_RSRVD_OFFSET    (0x0)
#define VMM_FREE_OFFSET     (PAGE_SIZE)

static vmmgr_ctx_t kernel_ctx;

static inline int vmmgr_split_free_block
(
    vmmgr_free_mem_t    *from,
    virt_addr_t          virt,
    virt_size_t          len,
    vmmgr_free_mem_t    *rem
);

static int vmmgr_add_reserved
(
    vmmgr_ctx_t *ctx, 
    vmmgr_rsrvd_mem_t *rsrvd
);


void vmmgr_list_entries()
{
    list_node_t *node = NULL;
    list_node_t *next_node = NULL;
    vmmgr_free_mem_t *fmem = NULL;
    vmmgr_rsrvd_mem_t *rsrvd  = NULL;
    vmmgr_free_mem_hdr_t *fh = NULL;
    vmmgr_rsrvd_mem_hdr_t *rh = NULL;

    kprintf("FREE_DESC_PER_PAGE %d\n",kernel_ctx.free_per_slot);
    kprintf("RSRV_DESC_PER_PAGE %d\n",kernel_ctx.rsrvd_per_slot);

    node = linked_list_first(&kernel_ctx.free_mem);

    kprintf("----LISTING FREE RANGES----\n");

    while(node)
    {
        fh = (vmmgr_free_mem_hdr_t*)node;

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
        rh = (vmmgr_rsrvd_mem_hdr_t*)node;

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

int vmmgr_init(void)
{
    vmmgr_rsrvd_mem_hdr_t *rh         = NULL;
    vmmgr_free_mem_hdr_t *fh          = NULL;

    virt_addr_t           vmmgr_base  = 0;
    virt_addr_t           vmmgr_max = 0;
    uint32_t              offset  = 0;

    vmmgr_max = cpu_virt_max();

    memset(&kernel_ctx, 0, sizeof(vmmgr_ctx_t));
    
    if(pagemgr_init(&kernel_ctx.pagemgr) == -1)
        return(-1);

    vmmgr_base = (~vmmgr_base) - (vmmgr_max >> 1);

    kprintf("Initializing Virtual Memory Manager BASE - 0x%x\n",vmmgr_base);

    kernel_ctx.vmmgr_base = vmmgr_base;

    linked_list_init(&kernel_ctx.free_mem);
    linked_list_init(&kernel_ctx.rsrvd_mem);
    spinlock_init(&kernel_ctx.lock);

    rh = (vmmgr_rsrvd_mem_hdr_t*)pagemgr_alloc(&kernel_ctx.pagemgr,
                                                kernel_ctx.vmmgr_base + 
                                                offset,
                                                VMM_SLOT_SIZE,
                                                PAGE_WRITABLE | PAGE_WRITE_THROUGH);

    offset += VMM_SLOT_SIZE;

    fh  = (vmmgr_free_mem_hdr_t*)pagemgr_alloc(&kernel_ctx.pagemgr,
                                                kernel_ctx.vmmgr_base + 
                                                offset,
                                                VMM_SLOT_SIZE,
                                                PAGE_WRITABLE | PAGE_WRITE_THROUGH);

    kprintf("rsrvd_start 0x%x\n", rh);
    kprintf("free_start 0x%x\n", fh);

    if(rh == NULL || fh == NULL)
        return(-1);

    memset(rh, 0, VMM_SLOT_SIZE);
    memset(fh, 0, VMM_SLOT_SIZE);
    
    linked_list_add_head(&kernel_ctx.rsrvd_mem, &rh->node);
    linked_list_add_head(&kernel_ctx.free_mem, &fh->node);

    /* How many free entries can we store per page */
    kernel_ctx.free_per_slot = (PAGE_SIZE - sizeof(vmmgr_free_mem_hdr_t)) /
                                              sizeof(vmmgr_free_mem_t);

    /* How many reserved entries can we store per page */
    kernel_ctx.rsrvd_per_slot = (PAGE_SIZE - sizeof(vmmgr_rsrvd_mem_hdr_t)) /
                                                sizeof(vmmgr_rsrvd_mem_t);

    /* Get the start of the array */

    fh->array[0].base = kernel_ctx.vmmgr_base;
    fh->array[0].length = (((uintptr_t)-1) - fh->array[0].base)+1;

    fh->avail = kernel_ctx.free_per_slot ;
    rh->avail = kernel_ctx.rsrvd_per_slot;
 
    /* Start reserving entries that 
     * must remain in virtual memory 
     * no matter what
     * */

    /* Reserve kernel image - only the higher half */
    vmmgr_reserve(&kernel_ctx,
                  _KERNEL_VMA     + _BOOTSTRAP_END,
                  _KERNEL_VMA_END - _KERNEL_VMA,
                   VMM_RES_KERNEL_IMAGE);

    /* Reserve remapping table */
    vmmgr_reserve(&kernel_ctx,
                  REMAP_TABLE_VADDR,
                  REMAP_TABLE_SIZE,
                  VMM_REMAP_TABLE);

    /* reserve head of tracking for free addresses */
    vmmgr_reserve(&kernel_ctx,
                  (virt_addr_t)fh,
                  PAGE_SIZE,
                  VMM_RES_FREE);

    /* reserve head of tracking for reserved addresses 
     * Yes, that some kind of a chicken-egg situation
     */
    vmmgr_reserve(&kernel_ctx,
                  (virt_addr_t)rh,
                  PAGE_SIZE,
                  VMM_RES_RSRVD);

    return(0);
}

/*
 * vmmgr_alloc_tracking - allocate tracking memory
 * Allocate tracking memory 
 * This routine is used by the Virtual Memory Manager
 * to allocate tracking memory when it runs out of it
 */ 

static virt_addr_t vmmgr_alloc_tracking(vmmgr_ctx_t *ctx)
{
    vmmgr_free_mem_hdr_t *fh    = NULL;
    vmmgr_free_mem_t *fdesc     = NULL;
    vmmgr_free_mem_t *from_slot = NULL;
    list_node_t *fn             = NULL;
    list_node_t *next_fn        = NULL;
    virt_addr_t addr =           0;

    /* Go thorugh the free entries */
    fn = linked_list_first(&ctx->free_mem);

    while(fn)
    {
        next_fn = linked_list_next(fn);

        fh = (vmmgr_free_mem_hdr_t*)fn;
        
        /* Check each free entry from the page 
         * We try to use a best fit approach to reduce fragmentation
         */

        for(uint16_t i = 0; i < ctx->free_per_slot; i++)
        {
            fdesc = &fh->array[i];

            if(from_slot == NULL)
                from_slot = fdesc;

            /* We need one page */
            if(from_slot->length < PAGE_SIZE && fdesc->length >= PAGE_SIZE)
                from_slot = fdesc;

            else if(from_slot->length > fdesc->length && fdesc->length >= PAGE_SIZE)
                from_slot = fdesc;
        }

        fn = next_fn;
    }

    if(from_slot == NULL)
        return(0);

    /* Check if the free descriptor allows us to allocate memory */
    if(from_slot->base == 0 || 
       from_slot->length == 0)
    {
       return(0);
    }

    addr = pagemgr_alloc(&ctx->pagemgr,
                          from_slot->base, 
                          VMM_SLOT_SIZE, 
                          PAGE_WRITABLE);

    if(addr == 0)
        return(0);

    from_slot->base   += PAGE_SIZE;
    from_slot->length -= PAGE_SIZE;

    memset((void*)addr, 0, VMM_SLOT_SIZE);

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

/* Check if a virtual address is reserved */
static int vmmgr_is_reserved
(
    vmmgr_ctx_t *ctx, 
    virt_addr_t virt, 
    virt_size_t len
)
{
    list_node_t *node      = NULL;
    list_node_t *next_node = NULL;
    vmmgr_rsrvd_mem_hdr_t *rh = NULL;

    node = linked_list_first(&ctx->rsrvd_mem);

    while(node)
    {
        next_node  = linked_list_next(node);

        rh = (vmmgr_rsrvd_mem_hdr_t*)node;

        for(uint16_t i = 0; i < ctx->rsrvd_per_slot; i++)
        {
            if(vmm_touches_range(rh->array[i].base, 
                                 rh->array[i].length, 
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

/* Check if the virtual address is free or is not mapped */
static int vmmgr_is_free
(
    vmmgr_ctx_t *ctx, 
    virt_addr_t virt, 
    virt_size_t len
)
{
    list_node_t      *node      = NULL;
    list_node_t      *next_node = NULL;
    vmmgr_free_mem_hdr_t *fh      = NULL;
    virt_size_t       diff      = 0;

    node = linked_list_first(&ctx->free_mem);

    while(node)
    {
        next_node  = linked_list_next(node);

        fh = (vmmgr_free_mem_hdr_t*)node;

        for(uint16_t i = 0; i < ctx->free_per_slot; i++)
        {
            /*  Start of interval is in range */
            if(vmm_touches_range(fh->array[i].base, 
                                 fh->array[i].length, 
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

/* Add tracking information to the reserved list */
static int vmmgr_add_reserved
(
    vmmgr_ctx_t *ctx, 
    virt_addr_t virt,
    virt_size_t len,
    uint32_t    type
)
{
    list_node_t           *rn        = NULL;
    list_node_t           *next_rn   = NULL;
    vmmgr_rsrvd_mem_hdr_t *rh        = NULL;
    vmmgr_rsrvd_mem_t     *rdesc     = NULL;
    vmmgr_rsrvd_mem_t     *candidate = NULL;
    vmmgr_rsrvd_mem_t      new_rsrvd = {0};
    uint8_t                done      = 0;

    rn = linked_list_first(&ctx->rsrvd_mem);

    /* Start finding a free slot */

    while(rn)
    {
        next_rn = linked_list_next(rn);
        rh = (vmmgr_rsrvd_mem_hdr_t*)rn;

        /* If this slot is already full,
         * then there are no available entries to
         * store the reserved data
     

        /* We have a reserved area which can hold 
         * the information that we want to store
         * so let's find the exact location in the page
         */ 

        if(rh->avail > 0)
        {
            for(uint16_t i = 0; i < ctx->rsrvd_per_slot; i++)
            {
                rdesc = &rh->array[i];

                    /* Let's see if this is our candidate slot
                    * to store the data 
                    * */
                if(rdesc->base == 0 &&
                   rdesc->length == 0)
                {
                    rdesc->base = virt;
                    rdesc->length = len;
                    rdesc->type = type;
                    rh->avail--;
                    return(0);
                }
                /* Check if the 'to reserve memory' is already in the list */
                else if(!memcmp(rsrvd, candidate, sizeof(vmmgr_rsrvd_mem_t)))
                {
                    return(-1);
                }
            }
        }
        
        if(next_rn == NULL)
        {
            next_rn = (list_node_t*)vmmgr_alloc_tracking(ctx);

            if(next_rn != NULL)
            {
                rh = (vmmgr_rsrvd_mem_hdr_t*)next_rn;

                /* the reserved slot is...reserved
                 * so we will subtract one from the available slots 
                 * */
                rh->avail           = ctx->rsrvd_per_slot - 1;
                rh->array[0].base   = (virt_addr_t)next_rn;
                rh->array[0].length = VMM_SLOT_SIZE;
                rh->array[0].type   = VMM_RES_RSRVD;
                linked_list_add_tail(&ctx->rsrvd_mem, &rh->node);
            }
        }

        rn = next_rn;
    }

    return(-1);
}

/* vmmgr_merge_free_block - merge adjacent memory blocks */

static int vmmgr_merge_free_block
(
    vmmgr_ctx_t *ctx, 
    virt_addr_t virt,
    virt_size_t len
)
{
    list_node_t          *fn      = NULL;
    list_node_t          *next_fn = NULL;
    vmmgr_free_mem_hdr_t *fh      = NULL;
    vmmgr_free_mem_t     *fdesc   = NULL;
    vmmgr_free_mem_t     *to_merge = NULL;
    vmmgr_free_mem_hdr_t *to_merge_hdr = NULL;
    int                   merged  = 0;

    fn = linked_list_first(&ctx->free_mem);

    while(fn)
    {
        merged = 0;
        next_fn = linked_list_next(fn);

        fh = (vmmgr_free_mem_hdr_t*)fn;

        for(uint16_t i = 0; i < ctx->free_per_slot; i++)
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
                    to_merge_hdr = ENTRY_TO_HEADER(to_merge);
                    to_merge_hdr->avail++;
                }

                to_merge = fdesc;
                virt = to_merge->base;
                len  = to_merge->length;
                /* Start again */
                next_fn = linked_list_first(&ctx->free_mem);
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
                    to_merge_hdr->avail++;
                }

                /* Start again */
                to_merge = fdesc;
                virt = to_merge->base;
                len  = to_merge->length;
                next_fn = linked_list_first(&ctx->free_mem);
                merged = 1;
                break;
            }
        }

        fn = next_fn;
    }

    if(merged)
        return(0);

    return(-1);
}

/* vmmgr_split_block - split a block 
 * and return the remaining block size
 * */

static inline int vmmgr_split_block
(
    virt_addr_t *from_virt,
    virt_size_t *from_len,
    const virt_addr_t virt,
    const virt_size_t len,
    virt_addr_t *rem_virt,
    virt_size_t *rem_len
)
{
    if(vmm_is_in_range(*from_virt, 
                       *from_len, 
                       virt, 
                       len))
    {
        *rem_virt = (virt + len) ;
        *rem_len  = ((*from_virt) + (*from_len))  - (*rem_virt);
        *from_len  = virt - (*from_virt);

        if((*rem_len) == 0)
        {
            (*rem_base) = 0;
            return(0);
        }

        if((*from_len) == 0)
        {
            *from_virt = *rem_virt;
            *from_len = *rem_len;

            (*rem_virt) = 0;
            (*rem_len) = 0;

            return(0);
        }

        return(1);
    }
    
    return(-1);
    
}

/* vmmgr_put_free_mem - place a free memory block in the free memory list 
 * This routine is similar in implementation with vmmgr_add_reserved
 * */

static int vmmgr_put_free_mem
(
    vmmgr_ctx_t *ctx, 
    virt_addr_t virt,
    virt_size_t len
)
{
    list_node_t          *fn       = NULL;
    list_node_t          *next_fn  = NULL;
    vmmgr_free_mem_hdr_t *fh       = NULL;
    vmmgr_free_mem_t     *fdesc    = NULL;

    fn = linked_list_first(&ctx->free_mem);

    while(fn)
    {
        next_fn = linked_list_next(fn);
        fh = (vmmgr_free_mem_hdr_t*)fn;

        if(fh->avail > 0)
        {
            for(uint16_t i = 0; i < ctx->free_per_slot; i++)
            {
                fdesc = &fh->array[i];

                if((fdesc->length == 0) && 
                   (fdesc->base   == 0))
                {
                    fdesc->base = virt;
                    fdesc->length = len;
                    fh->avail--;
                    return(0);
                }
            }
        }

        /* if next_fn is null, we don't have a slot 
         * to place the free entry so we will need to allocate a new one.
         * If we fail, well...bad luck :/
         */ 
        if(next_fn == NULL)
        {
            next_fn = (list_node_t*)vmmgr_alloc_tracking(ctx);

            if(next_fn != NULL)
            {
                fh = (vmmgr_free_mem_hdr_t*)next_fn;
                fh->avail = ctx->free_per_slot;

                linked_list_add_tail(&ctx->free_mem, &fh->node);

                vmmgr_add_reserved(ctx, (virt_addr_t)next_fn, 
                                        VMM_SLOT_SIZE, 
                                        VMM_RES_FREE);
            }
        }

        fn = next_fn;
    }

    return(-1);
}

/* vmmgr_reserve - reserve virtual memory */

int vmmgr_reserve
(
    vmmgr_ctx_t *ctx, 
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t type
)
{
    vmmgr_free_mem_hdr_t  *fh      = NULL;
    vmmgr_rsrvd_mem_hdr_t *rh      = NULL;
    list_node_t           *fn      = NULL;
    list_node_t           *next_fn = NULL;
    vmmgr_free_mem_t      *fdesc   = NULL;
    vmmgr_free_mem_t      rem;
    vmmgr_rsrvd_mem_t     rsrvd;
    int                   int_status = 0;
    int                   rc         = 0;

    if(ctx == NULL)
        ctx = &kernel_ctx;

    memset(&rem, 0, sizeof(vmmgr_free_mem_t));
    
    spinlock_lock_int(&ctx->lock, &int_status);

    fn           = linked_list_first(&ctx->free_mem);

    while(fn)
    {
        next_fn = linked_list_next(fn);

        fh = (vmmgr_free_mem_hdr_t*)fn;

        for(uint16_t i = 0; i < ctx->free_per_slot; i++)
        {
            fdesc = &fh->array[i];

            if(vmm_is_in_range(fdesc->base, fdesc->length, virt, len))
            {    
                rc = vmmgr_add_reserved(ctx, virt, len, type);

                if(vmmgr_split_free_block(&fh->array[i], virt, len, &rem) > 0)
                {
                    rc = vmmgr_put_free_mem(ctx, &rem);

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

    rc = vmmgr_add_reserved(ctx, &rsrvd);
    
    spinlock_unlock_int(&ctx->lock, int_status);

    return(rc);
}


static virt_addr_t vmmgr_acquire_mem
(
    vmmgr_ctx_t *ctx,
    virt_addr_t virt,
    virt_size_t len,
)
{
    list_node_t          *fn         = NULL;
    list_node_t          *next_fn    = NULL;
    vmmgr_free_mem_t     *fdesc      = NULL;
    vmmgr_free_mem_t     *from_slot  = NULL;
    vmmgr_free_mem_hdr_t *fh         = NULL;
    virt_addr_t           ret_addr   = 0;
    virt_addr_t           map_addr   = 0;
    virt_addr_t           rem_virt   = 0;
    virt_size_t           rem_len    = 0;
    int                   int_status = 0;
    int                   rc         = 0;
    int found  = 0;

    if(ctx == NULL)
        ctx = &kernel_ctx;

    if(len % PAGE_SIZE)
        len = ALIGN_UP(len, PAGE_SIZE);

    ret_addr = virt;

    fn = linked_list_first(&ctx->free_mem);

    while(fn)
    {
        next_fn = linked_list_next(fn);
        fh = (vmmgr_free_mem_hdr_t*)fn;


        if(fh->avail > 0)
        {
            for(uint16_t i  = 0; i < ctx->free_per_slot; i++)
            {
                fdesc = &fh->array[i];

                if(fdesc->length == 0)
                    continue;

                if(from_slot == NULL)
                    from_slot = fdesc;

                if(virt != 0)
                {
                    if(vmm_is_in_range(from_slot->base, 
                                        from_slot->length, 
                                        virt, 
                                        len))
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
        }   

        if(next_fn == NULL && virt != 0 && !found)
        {
            kprintf("RESTARTING\n");
            virt = 0;
            fn = linked_list_first(&ctx->free_mem);
            from_slot = NULL;
            continue;
        }

        if(found)
            break;

        fn = next_fn;
    }

    if(from_slot->length < len)
    {
        return(0);
    }

    if(vmmgr_split_block(&from_slot->base,
                         &from_slot->length,
                         ret_addr, 
                         len, 
                         &rem_virt,
                         &rem_len) > 0)
    {
        rc = vmmgr_put_free_mem(ctx, rem_virt, rem_len);

        /* if we cannot store the free entry, then we 
         * must revert the change to avoid loosing free memory
         */ 
        if(rc < 0)
        {
            if(vmmgr_merge_free_block(ctx, rem_virt, rem_len) < 0)
            {
                kprintf("ERROR while reversing the allocation\n");
                while(1);
            }
        }
    }

    return(0);
}
#if 0
/* vmmgr_map - map phyical address to virtual address */
virt_addr_t vmmgr_map
(
    vmmgr_ctx_t *ctx,
    phys_addr_t phys, 
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t    attr
)
{
    list_node_t          *fn         = NULL;
    list_node_t          *next_fn    = NULL;
    vmmgr_free_mem_t     *fdesc      = NULL;
    vmmgr_free_mem_t      rem;
    vmmgr_free_mem_t     *from_slot  = NULL;
    vmmgr_free_mem_hdr_t *fh         = NULL;
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
        fh = (vmmgr_free_mem_hdr_t*)fn;

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

    if(vmmgr_split_free_block(from_slot, ret_addr, len, &rem) > 0)
    {
        vmmgr_put_free_mem(ctx, &rem);
    }

    spinlock_unlock_int(&ctx->lock, int_status);

    return(ret_addr);
}

/* vmmgr_alloc - allocate virtual memory */
virt_addr_t vmmgr_alloc
(
    vmmgr_ctx_t *ctx,
    virt_addr_t virt, 
    virt_size_t len,
    uint32_t    attr
)
{
    list_node_t          *fn         = NULL;
    list_node_t          *next_fn    = NULL;
    virt_addr_t           ret_addr   = 0;
    virt_size_t           dist       = 0;
    vmmgr_free_mem_t     *fdesc      = NULL;
    vmmgr_free_mem_t      rem;
    vmmgr_free_mem_t     *from_slot  = NULL;
    vmmgr_free_mem_hdr_t *fh         = NULL;
    virt_addr_t           near_virt  = 0;
    int                   int_status = 0;

    if(ctx == NULL)
        ctx = &kernel_ctx;

    if(len % PAGE_SIZE)
        len = ALIGN_UP(len, PAGE_SIZE);

    memset(&rem, 0, sizeof(vmmgr_free_mem_t));

    spinlock_lock_int(&ctx->lock, &int_status);
    
    fn = linked_list_first(&ctx->free_mem);

    while(fn)
    {
        next_fn = linked_list_next(fn);
        fh = (vmmgr_free_mem_hdr_t*) fn;

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

    

    if(vmmgr_split_free_block(from_slot, ret_addr, len, &rem) > 0)
    {
        vmmgr_put_free_mem(ctx, &rem);
    }      

    spinlock_unlock_int(&ctx->lock, int_status);

    return(ret_addr);
}

/* vmmgr_free - free virtual memory */

int vmmgr_free
(
    vmmgr_ctx_t *ctx,
    virt_addr_t vaddr, 
    virt_size_t len
)
{
    virt_addr_t      virt = 0;
    vmmgr_free_mem_t mm;
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
    
    if(vmmgr_is_free(ctx, virt, len))
    {
        spinlock_unlock_int(&ctx->lock, int_status);
        return(-1);
    }

    if(vmmgr_is_reserved(ctx, virt, len))
    {
        spinlock_unlock_int(&ctx->lock, int_status);
        return(-1);
    }

    status = vmmgr_merge_free_block(ctx, virt, len);

    if(status != 0)
        status = vmmgr_put_free_mem(ctx, &mm);

    if(status == 0)
        status = pagemgr_free(&ctx->pagemgr, virt, len);

    spinlock_unlock_int(&ctx->lock, int_status);

    return(status);
}

/* vmmgr_unmap - unmap virtual memory */
int vmmgr_unmap
(
    vmmgr_ctx_t *ctx,
    virt_addr_t vaddr, 
    virt_size_t len
)
{
    virt_addr_t virt = (virt_addr_t)vaddr;
    vmmgr_free_mem_t mm;
    int status  = 0;
    int int_status = 0;

    if(len % PAGE_SIZE)
        len = ALIGN_UP(len, PAGE_SIZE);

    if(virt == 0)
        return(-1);

    if(ctx == NULL)
        ctx = &kernel_ctx;

    spinlock_lock_int(&ctx->lock, &int_status);

    if(vmmgr_is_free(ctx, virt, len))
    {
        spinlock_unlock_int(&ctx->lock, int_status);
        return(-1);
    }
    if(vmmgr_is_reserved(ctx, virt, len))
    {
        spinlock_unlock_int(&ctx->lock, int_status);
        return(-1);
    }

    status = vmmgr_merge_free_block(ctx, virt, len);

    if(status != 0)
        status = vmmgr_put_free_mem(ctx, &mm);

    if(status == 0)
        status = pagemgr_unmap(&ctx->pagemgr, virt, len);
    
    spinlock_unlock_int(&ctx->lock, int_status);

    return(status);
}


int vmmgr_change_attrib
(
    vmmgr_ctx_t *ctx,
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

    if(vmmgr_is_reserved(ctx, virt, len))
    {
        spinlock_unlock_int(&ctx->lock, int_status);
        return(-1);
    }
    
    if(vmmgr_is_free(ctx, virt, len))
    {
        spinlock_unlock_int(&ctx->lock, int_status);
        return(-1);
    }

    status = pagemgr_attr_change(&ctx->pagemgr, virt, len, attr);
    
    spinlock_unlock_int(&ctx->lock, int_status);

    return(status);
}

virt_addr_t vmmgr_temp_identity_map
(
    vmmgr_ctx_t *ctx,
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

int vmmgr_temp_identity_unmap
(
    vmmgr_ctx_t *ctx,
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
}#endif