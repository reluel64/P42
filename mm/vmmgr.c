/* Virtual Memory Manager
 * Part of P42 Kernel
 */

#include <paging.h>
#include <pagemgr.h>
#include <vmmgr.h>
#include <stddef.h>
#include <utils.h>


extern virt_size_t __max_linear_address(void);

#define VIRT_START_IN_BLOCK(virt, fblock)  ((fblock)->base <= (virt)) && \
      (((fblock)->base + (fblock)->length) >= ((virt)))

#define VIRT_FROM_FREE_BLOCK(virt, len, fblock) \
                                vmm_is_in_range((fblock)->base, \
                                (fblock)->length, \
                                (virt), (len))

#define NODE_TO_FREE_DESC(node) ((vmmgr_free_mem_t*)(((uint8_t*)(node)) + (sizeof(list_node_t))))
#define NODE_TO_RSRVD_DESC(node) ((vmmgr_rsrvd_mem_t*)(((uint8_t*)(node)) + (sizeof(list_node_t))))


static vmmgr_ctx_t vmmgr_kernel_ctx;


static inline int vmmgr_split_free_block
(
    vmmgr_free_mem_t    *from,
    virt_addr_t          virt,
    virt_size_t          len,
    vmmgr_free_mem_t    *rem
);
static int vmmgr_add_reserved(vmmgr_ctx_t *ctx, vmmgr_rsrvd_mem_t *rsrvd);


void vmmgr_list_entries()
{
    list_node_t *node = NULL;
    list_node_t *next_node = NULL;
    vmmgr_free_mem_t *fmem = NULL;
    vmmgr_rsrvd_mem_t *rsrvd  = NULL;
    vmmgr_free_mem_hdr_t *fh = NULL;
    vmmgr_rsrvd_mem_hdr_t *rh = NULL;

    kprintf("FREE_DESC_PER_PAGE %d\n",vmmgr_kernel_ctx.free_ent_per_page);
    kprintf("RSRV_DESC_PER_PAGE %d\n",vmmgr_kernel_ctx.rsrvd_ent_per_page);

    node = linked_list_first(&vmmgr_kernel_ctx.free_mem);

    kprintf("----LISTING FREE RANGES----\n");

    while(node)
    {
        fh = (vmmgr_free_mem_hdr_t*)node;

        next_node = linked_list_next(node);

        for(uint16_t i = 0; i < vmmgr_kernel_ctx.free_ent_per_page; i++)
        {
            fmem  = &fh->fmem[i];

            if(fmem->base != 0  && fmem->length != 0)
                kprintf("BASE 0x%x LENGTH 0x%x\n",fmem->base,fmem->length);
        }

        node = next_node;
    }
#if 1
    node = linked_list_first(&vmmgr_kernel_ctx.rsrvd_mem);

    kprintf("----LISTING RESERVED RANGES----\n");

    while(node)
    {
        next_node = linked_list_next(node);
        rh = (vmmgr_rsrvd_mem_hdr_t*)node;

        for(uint16_t i = 0; i < vmmgr_kernel_ctx.rsrvd_ent_per_page; i++)
        {
            rsrvd  = &rh->rsrvd[i];
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

    memset(&vmmgr_kernel_ctx, 0, sizeof(vmmgr_ctx_t));

    if(pagemgr_init(&vmmgr_kernel_ctx.pagemgr) == -1)
        return(-1);

    vmmgr_base = ~0ull - ((1ull << __max_linear_address() - 1) - 1);

    kprintf("Initializing Virtual Memory Manager BASE - 0x%x\n",vmmgr_base);

    vmmgr_kernel_ctx.vmmgr_base = vmmgr_base;

    linked_list_init(&vmmgr_kernel_ctx.free_mem);
    linked_list_init(&vmmgr_kernel_ctx.rsrvd_mem);

    rh = (vmmgr_rsrvd_mem_hdr_t*)pagemgr_alloc(&vmmgr_kernel_ctx.pagemgr,
                                           vmmgr_kernel_ctx.vmmgr_base,
                                           PAGE_SIZE,
                                           PAGE_WRITABLE);

    fh  = (vmmgr_free_mem_hdr_t*)pagemgr_alloc(&vmmgr_kernel_ctx.pagemgr,
                                                vmmgr_kernel_ctx.vmmgr_base + PAGE_SIZE,
                                                PAGE_SIZE,
                                                PAGE_WRITABLE);

    kprintf("rsrvd_start 0x%x\n",rh);
    kprintf("free_start 0x%x\n",fh);

    if(rh == NULL || fh == NULL)
        return(-1);

    memset(rh, 0, PAGE_SIZE);
    memset(fh, 0, PAGE_SIZE);
    
    linked_list_add_head(&vmmgr_kernel_ctx.rsrvd_mem, &rh->node);
    linked_list_add_head(&vmmgr_kernel_ctx.free_mem, &fh->node);

    vmmgr_kernel_ctx.free_ent_per_page = (PAGE_SIZE - sizeof(vmmgr_free_mem_hdr_t)) /
                                              sizeof(vmmgr_free_mem_t);

    vmmgr_kernel_ctx.rsrvd_ent_per_page = (PAGE_SIZE - sizeof(vmmgr_rsrvd_mem_hdr_t)) /
                                                sizeof(vmmgr_rsrvd_mem_t);

    /* Get the start of the array */


    fh->fmem[0].base = vmmgr_kernel_ctx.vmmgr_base;
    fh->fmem[0].length = (UINT64_MAX - fh->fmem->base)+1;

    fh->avail = vmmgr_kernel_ctx.free_ent_per_page ;
    rh->avail = vmmgr_kernel_ctx.rsrvd_ent_per_page;
 
    /* Start reserving entries that 
     * must remain in virtual memory 
     * no matter what
     * */

    /* Reserve kernel image - only the higher half */
    vmmgr_reserve(&vmmgr_kernel_ctx,
                  _KERNEL_VMA     + _BOOTSTRAP_END,
                  _KERNEL_VMA_END - _KERNEL_VMA,
                   VMM_RES_KERNEL_IMAGE);

    /* Reserve remapping table */
    vmmgr_reserve(&vmmgr_kernel_ctx,
                  REMAP_TABLE_VADDR,
                  REMAP_TABLE_SIZE,
                  VMM_REMAP_TABLE);

    /* reserve head of tracking for free addresses */
    vmmgr_reserve(&vmmgr_kernel_ctx,
                  (virt_addr_t)fh,
                  PAGE_SIZE,
                  VMM_RES_FREE);

    /* reserve head of tracking for reserved addresses 
     * Yes, that some kind of a chicken-egg situation
     */
    vmmgr_reserve(&vmmgr_kernel_ctx,
                  (virt_addr_t)rh,
                  PAGE_SIZE,
                  VMM_RES_RSRVD);

    
    return(0);
}


static void *vmmgr_alloc_tracking(vmmgr_ctx_t *ctx)
{
    vmmgr_free_mem_hdr_t *fh    = NULL;
    vmmgr_free_mem_t *fdesc     = NULL;
    vmmgr_free_mem_t *from_slot = NULL;
    list_node_t *fn             = NULL;
    list_node_t *next_fn        = NULL;
    virt_addr_t addr =           0;

    fn = linked_list_first(&ctx->free_mem);

    while(fn)
    {
        next_fn = linked_list_next(fn);

        fh = (vmmgr_free_mem_hdr_t*)fn;

        for(uint16_t i = 0; i < ctx->free_ent_per_page; i++)
        {
            fdesc = &fh->fmem[i];

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
        return(NULL);

    addr = pagemgr_alloc(&ctx->pagemgr,
                          from_slot->base, 
                          PAGE_SIZE, 
                          PAGE_WRITABLE);

    if(addr == 0)
        return(NULL);

    from_slot->base   += PAGE_SIZE;
    from_slot->length -= PAGE_SIZE;

    memset((void*)addr, 0, PAGE_SIZE);

    return((void*)addr);
}

/*
 * vmm_is_in_range - checks if a segment is in range of another segmen
 */

static inline int vmm_is_in_range
(
    virt_addr_t base,
    virt_size_t len,
    virt_addr_t req_base,
    virt_size_t req_len
)
{
    virt_size_t rem = 0;

    /* There's no chance that this is in range */
    if (req_base < base)
        return(0);

    rem = len - (req_base - base);

    /* If the remaining of the segment can
     * fit th request and if the remaining of the
     * segment is less or equal than the total length
     * we can say that the segment is in the range
     */
    if (rem >= req_len && len >= rem)
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
    virt_size_t rem = 0;

    if(req_base < base)
    {
        rem = base - req_base;

        if(rem >= req_len)
            return(0);

        else
            return(1);
    }
    else
    {
        rem = len - (req_base - base);

        if (rem >= req_len && len >= rem)
            return(1);
    }

    return(0);
}

static inline virt_addr_t vmmgr_near_addr
(
    virt_addr_t base,
    virt_size_t len,
    virt_addr_t req_base,
    virt_size_t req_len
)
{
    virt_size_t diff = 0;

    if(base > req_base)
    {
        diff = base - req_base;

        if(vmm_is_in_range(base,len - diff, req_base + diff, req_len))
            return(req_base + diff);

    }

    return(-1);
}


static int vmmgr_is_reserved(vmmgr_ctx_t *ctx, virt_addr_t virt, virt_size_t len)
{
    list_node_t *node      = NULL;
    list_node_t *next_node = NULL;
    vmmgr_rsrvd_mem_hdr_t *rh = NULL;

    node = linked_list_first(&ctx->rsrvd_mem);

    while(node)
    {
        next_node  = linked_list_next(node);

        rh = (vmmgr_rsrvd_mem_hdr_t*)node;

        for(uint16_t i = 0; i < ctx->rsrvd_ent_per_page; i++)
        {
            if(vmm_touches_range(rh->rsrvd[i].base, rh->rsrvd[i].length, virt, len))
                return(1);
        }

        node = next_node;
    }

    return(0);
}


static int vmmgr_is_free(vmmgr_ctx_t *ctx, virt_addr_t virt, virt_size_t len)
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

        for(uint16_t i = 0; i < ctx->free_ent_per_page; i++)
        {
            /*  Start of interval is in range */
            if(vmm_touches_range(fh->fmem[i].base, fh->fmem[i].length, virt, len))
                return(1);
        }

        node = next_node;
    }

    return(0);
}


static int vmmgr_add_reserved(vmmgr_ctx_t *ctx, vmmgr_rsrvd_mem_t *rsrvd)
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

        if(rh->avail == 0)
        {

            if(next_rn == NULL)
            {
                next_rn = vmmgr_alloc_tracking(ctx);

                if(next_rn != NULL)
                {
                    rh = (vmmgr_rsrvd_mem_hdr_t*)next_rn;
                    rh->avail = ctx->rsrvd_ent_per_page - 1;

                    /* First entry in the reserved section is the reserved page itself*/
                    rh->rsrvd[0].base   = (virt_addr_t)next_rn;
                    rh->rsrvd[0].length = PAGE_SIZE;
                    rh->rsrvd[0].type   = VMM_RES_RSRVD;
                    linked_list_add_tail(&ctx->rsrvd_mem, &rh->node);
                }

            }

            rn = next_rn;
            continue;
        }

        for(uint16_t i = 0; i < ctx->rsrvd_ent_per_page; i++)
        {
            /* TODO: Support coalescing if the types are the same */

            rdesc = &rh->rsrvd[i];

            if(candidate == NULL)
            {
                if(rdesc->base == 0 &&
                   rdesc->length == 0)
                {
                    candidate = rdesc;
                    done = 1;
                    rh->avail--;
                    break;
                }
            }
            else if(!memcmp(rsrvd, candidate, sizeof(vmmgr_rsrvd_mem_t)))
            {
                candidate = NULL;
                done = 1;
                break;
            }
        }

        if(done)
            break;

        if(next_rn == NULL)
        {
            next_rn = vmmgr_alloc_tracking(ctx);

            if(next_rn != NULL)
            {
                rh = (vmmgr_rsrvd_mem_hdr_t*)next_rn;
                rh->avail = ctx->rsrvd_ent_per_page - 1;
                rh->rsrvd[0].base   = (virt_addr_t)next_rn;
                rh->rsrvd[0].length = PAGE_SIZE;
                rh->rsrvd[0].type   = VMM_RES_RSRVD;
                linked_list_add_tail(&ctx->rsrvd_mem, &rh->node);
            }
        }

        rn = next_rn;
    }

    if(candidate)
        *candidate = *rsrvd;
    else
        return(-1);

    return(0);
}

static int vmmgr_merge_free_block(vmmgr_ctx_t *ctx, vmmgr_free_mem_t *fmem)
{
    list_node_t          *fn      = NULL;
    list_node_t          *next_fn = NULL;
    vmmgr_free_mem_hdr_t *fh      = NULL;
    vmmgr_free_mem_t     *fdesc   = NULL;
    int                   merged  = 0;

    fn = linked_list_first(&ctx->free_mem);

    while(fn)
    {
        next_fn = linked_list_next(fn);

        fh = (vmmgr_free_mem_hdr_t*)fn;

        for(uint16_t i = 0; i < ctx->free_ent_per_page; i++)
        {
            fdesc = &fh->fmem[i];

            /* Merge from left */
            if(fdesc->base == fmem->base + fmem->length)
            {
                fdesc->base    = fmem->base;
                fdesc->length += fmem->length;

                if(merged == 1)
                {
                    fmem->base = 0;
                    fmem->length = 0;
                    fh->avail++;
                }

                fmem = fdesc;
                /* Start again */
                next_fn = linked_list_first(&ctx->free_mem);
                merged = 1;
                break;
            }
            /* Merge from right */
            else if(fdesc->base + fdesc->length == fmem->base)
            {
                fdesc->length += fmem->length;
                fmem = fdesc;

                if(merged == 1)
                {
                    fmem->base = 0;
                    fmem->length = 0;
                    fh->avail++;
                }

                /* Start again */
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

static inline int vmmgr_split_free_block
(
    vmmgr_free_mem_t *from,
    virt_addr_t          virt,
    virt_size_t          len,
    vmmgr_free_mem_t *rem
)
{

    if(VIRT_FROM_FREE_BLOCK(virt, len, from))
    {
        rem->base    = (virt + len) ;
        rem->length  = (from->base + from->length)  - rem->base;
        from->length = virt - from->base;

        if(rem->length == 0)
        {
            rem->base = 0;
        }

        if(from->length == 0)
        {
            *from = *rem;
            rem->base = 0;
            rem->length = 0;
        }

        return(0);
    }
    else
    {
        return(-1);
    }
}

static int vmmgr_put_free_mem(vmmgr_ctx_t *ctx, vmmgr_free_mem_t *fmem)
{
    list_node_t          *fn       = NULL;
    list_node_t          *next_fn  = NULL;
    vmmgr_free_mem_hdr_t *fh       = NULL;
    vmmgr_free_mem_t     *fdesc    = NULL;
    vmmgr_rsrvd_mem_t    rsrvd;

    fn = linked_list_first(&ctx->free_mem);

    while(fn)
    {
        next_fn = linked_list_next(fn);
        fh = (vmmgr_free_mem_hdr_t*)fn;

        /* Nothing here, advance */

        if(fh->avail == 0)
        {
            if(next_fn == NULL)
            {
                next_fn = vmmgr_alloc_tracking(ctx);

                if(next_fn != NULL)
                {
                    memset(&rsrvd, 0, sizeof(vmmgr_rsrvd_mem_t));

                    fh = (vmmgr_free_mem_hdr_t*)next_fn;
                    fh->avail = ctx->free_ent_per_page;
                    linked_list_add_tail(&ctx->free_mem, &fh->node);

                    /* Reserve */
                    rsrvd.base = (virt_addr_t)next_fn;
                    rsrvd.length = PAGE_SIZE;
                    rsrvd.type = VMM_RES_FREE;

                    vmmgr_add_reserved(ctx, &rsrvd);
                }
            }

            fn = next_fn;
            continue;
        }

        for(uint16_t i = 0; i < ctx->free_ent_per_page; i++)
        {
            fdesc = &fh->fmem[i];

            if(fdesc->length == 0)
            {
                memcpy(fdesc, fmem, sizeof(vmmgr_free_mem_t));
                fh->avail--;
                return(0);
            }
        }

        if(next_fn == NULL)
        {
            next_fn = vmmgr_alloc_tracking(ctx);

            if(next_fn != NULL)
            {
                fh = (vmmgr_free_mem_hdr_t*)next_fn;
                fh->avail = ctx->free_ent_per_page;
                linked_list_add_tail(&ctx->free_mem, &fh->node);

                /* Reserve */
                rsrvd.base = (virt_addr_t)next_fn;
                rsrvd.length = PAGE_SIZE;
                rsrvd.type = VMM_RES_FREE;

                vmmgr_add_reserved(ctx,&rsrvd);
            }
        }

        fn = next_fn;
    }
    return(-1);
}

int vmmgr_reserve
(
    vmmgr_ctx_t *ctx, 
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t type
)
{
    vmmgr_free_mem_hdr_t *fh = NULL;
    vmmgr_rsrvd_mem_hdr_t *rh = NULL;

    list_node_t      *fn        = NULL;
    list_node_t      *next_fn   = NULL;
    vmmgr_free_mem_t *fdesc        = NULL;
    vmmgr_free_mem_t  rem;
    vmmgr_rsrvd_mem_t     rsrvd;

    if(ctx == NULL)
        ctx = &vmmgr_kernel_ctx;

    memset(&rem, 0, sizeof(vmmgr_free_mem_t));

    fn           = linked_list_first(&ctx->free_mem);
    rsrvd.base   = virt;
    rsrvd.length = len;
    rsrvd.type   = type;

    while(fn)
    {
        next_fn = linked_list_next(fn);

        fh = (vmmgr_free_mem_hdr_t*)fn;

        for(uint16_t i = 0; i < ctx->free_ent_per_page; i++)
        {
            if(VIRT_FROM_FREE_BLOCK(virt, len, &fh->fmem[i]))
            {
                vmmgr_split_free_block(&fh->fmem[i], virt, len, &rem);
                vmmgr_add_reserved(ctx, &rsrvd);

                if(rem.length != 0)
                    vmmgr_put_free_mem(ctx, &rem);

                return(0);
            }
        }

        if(next_fn == NULL)
        {
            kprintf("Not enough space!!!\n");
            break;
        }
 
        fn = next_fn;
    }

    vmmgr_add_reserved(ctx, &rsrvd);
    return(0);
}

void *vmmgr_map
(
    vmmgr_ctx_t *ctx,
    phys_addr_t phys, 
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t attr
)
{
    list_node_t          *fn        = NULL;
    list_node_t          *next_fn   = NULL;
    vmmgr_free_mem_t     *fdesc     = NULL;
    vmmgr_free_mem_t      rem;
    vmmgr_free_mem_t     *from_slot = NULL;
    vmmgr_free_mem_hdr_t *fh        = NULL;
    virt_addr_t           ret_addr  = virt;
    virt_addr_t           map_addr  = 0;

    if(ctx == NULL)
        ctx = &vmmgr_kernel_ctx;

    if(len % PAGE_SIZE)
        len = ALIGN_UP(len, PAGE_SIZE);

    memset(&rem, 0, sizeof(vmmgr_free_mem_t));

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

        for(uint16_t i  = 0; i < ctx->free_ent_per_page; i++)
        {
            fdesc = &fh->fmem[i];

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
        return(NULL);
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
        return(NULL);
	}

    vmmgr_split_free_block(from_slot, ret_addr, len, &rem);

    if(rem.base != 0)
    {
        vmmgr_put_free_mem(ctx, &rem);
    }

    return((void*)ret_addr);
}

void *vmmgr_alloc
(
    vmmgr_ctx_t *ctx,
    virt_addr_t virt, 
    virt_size_t len,
    uint32_t attr
)
{
    list_node_t          *fn        = NULL;
    list_node_t          *next_fn   = NULL;
    virt_addr_t           ret_addr  = 0;
    virt_size_t           dist      = 0;
    vmmgr_free_mem_t     *fdesc     = NULL;
    vmmgr_free_mem_t      rem;
    vmmgr_free_mem_t     *from_slot = NULL;
    vmmgr_free_mem_hdr_t *fh        = NULL;
    virt_addr_t           near_virt = 0;

    if(ctx == NULL)
        ctx = &vmmgr_kernel_ctx;

    if(len % PAGE_SIZE)
        len = ALIGN_UP(len, PAGE_SIZE);

    memset(&rem, 0, sizeof(vmmgr_free_mem_t));

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

        for(uint16_t i  = 0; i < ctx->free_ent_per_page; i++)
        {
            fdesc = &fh->fmem[i];

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

        /* In this case we will restart the loop */
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
        kprintf("Not From slot 0x%x - 0x%x 0x%x\n",from_slot->base,from_slot->length, len);
        return(NULL);
    }

    if(ret_addr == 0)
        ret_addr = from_slot->base;

    ret_addr = pagemgr_alloc(&ctx->pagemgr, ret_addr, len, attr);

    if(ret_addr == 0)
	{
        kprintf("PG_ALLOC_FAIL\n");
        return(NULL);
	}

    vmmgr_split_free_block(from_slot, ret_addr, len, &rem);

    if(rem.length != 0)
    {
        vmmgr_put_free_mem(ctx, &rem);
    }      

    return((void*)ret_addr);
}

int vmmgr_free
(
    vmmgr_ctx_t *ctx,
    void *vaddr, 
    virt_size_t len
)
{
    virt_addr_t virt = (virt_addr_t)vaddr;
    vmmgr_free_mem_t mm;
    int status  = 0;

    if(ctx == NULL)
        ctx = &vmmgr_kernel_ctx;

    if(len % PAGE_SIZE)
        len = ALIGN_UP(len, PAGE_SIZE);

    mm.base = virt;
    mm.length = len;

    if(virt == 0)
        return(-1);

    if(vmmgr_is_free(ctx, virt, len))
        return(-1);

    if(vmmgr_is_reserved(ctx, virt, len))
        return(-1);

    status = vmmgr_merge_free_block(ctx, &mm);

    if(status != 0)
        status = vmmgr_put_free_mem(ctx, &mm);

    if(status == 0)
        status = pagemgr_free(&ctx->pagemgr, virt, len);

    return(status);
}

int vmmgr_unmap
(
    vmmgr_ctx_t *ctx,
    void *vaddr, 
    virt_size_t len
)
{
    virt_addr_t virt = (virt_addr_t)vaddr;
    vmmgr_free_mem_t mm;
    int status  = 0;

    if(len % PAGE_SIZE)
        len = ALIGN_UP(len, PAGE_SIZE);

    mm.base = virt;
    mm.length = len;

    if(virt == 0)
        return(-1);

    if(ctx == NULL)
        ctx = &vmmgr_kernel_ctx;

    if(vmmgr_is_free(ctx, virt, len))
        return(-1);

    if(vmmgr_is_reserved(ctx, virt, len))
        return(-1);

    status = vmmgr_merge_free_block(ctx, &mm);

    if(status != 0)
        status = vmmgr_put_free_mem(ctx, &mm);

    if(status == 0)
        status = pagemgr_unmap(&ctx->pagemgr, virt, len);

    return(status);
}


int vmmgr_change_attrib
(
    vmmgr_ctx_t *ctx,
    virt_addr_t virt, 
    virt_size_t len, 
    uint32_t attr
)
{
    if(len == 0 || virt == 0)
        return(-1);

    if(ctx == NULL)
        ctx = &vmmgr_kernel_ctx;

    if(vmmgr_is_reserved(ctx, virt, len))
        return(-1);
    
    if(vmmgr_is_free(ctx, virt, len))
        return(-1);

    return(pagemgr_attr_change(&ctx->pagemgr, virt, len, attr));
}
