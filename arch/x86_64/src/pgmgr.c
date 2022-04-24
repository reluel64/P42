/* Paging management 
 * This file contains code that would 
 * allow us to manage paging 
 * structures
 */


#include <stdint.h>
#include <paging.h>
#include <utils.h>
#include <pgmgr.h>
#include <isr.h>
#include <spinlock.h>
#include <vm.h>
#include <platform.h>
#include <intc.h>
#include <pfmgr.h>

typedef struct pgmgr_t
{
    virt_addr_t remap_tbl;
    uint8_t     pml5_support;
    uint8_t     nx_support;
    
    pat_t pat;
    isr_t fault_isr;
    isr_t inv_isr;
    uint8_t kernel_ctx_init;
}pgmgr_t;
#define PGMGR_DEBUG
/* for reference if in future we might want to also align the length */
#if 0
 ALIGN_UP((_base) - (ld)->base,(virt_size_t)1 <<                   \
                                           PGMGR_LEVEL_TO_SHIFT((_req_level)));  
#endif


/* locals */
static pgmgr_t pgmgr;
static pfmgr_t *pfmgr        = NULL;

static int         pgmgr_page_fault_handler
(
    void *pv, 
    isr_info_t *inf
);

static int         pgmgr_per_cpu_invl_handler
(
    void *pv, 
    isr_info_t *inf
);

static virt_addr_t _pgmgr_temp_map
(
    phys_addr_t phys, 
    uint16_t ix
);

static int _pgmgr_temp_unmap
(
    virt_addr_t vaddr
);

uint8_t pgmgr_pml5_support(void)
{
    return(pgmgr.pml5_support);
}

uint8_t pgmgr_nx_support(void)
{
    return(pgmgr.nx_support);
}

void pgmgr_boot_temp_map_init(void)
{
    virt_addr_t page_table = 0;
    virt_addr_t *pde = 0;
    virt_addr_t *remap_page = 0;
    
    page_table = (virt_addr_t)&KERNEL_VMA + 
                 (virt_addr_t)&BOOT_PAGING;

    pde = (virt_addr_t*)((virt_addr_t)&BOOT_PAGING + 0x3000 + 511 * 8);

    remap_page = (virt_addr_t*)
                 (page_table + 0x4000 + 512 * 511 * 8);

    /* make the first page in the table to point to the table itself */
    *remap_page = *pde;

    pgmgr.remap_tbl = BOOT_REMAP_TABLE_VADDR;   
}

static int pgmgr_check_nx(void)
{
    uint32_t eax = 0;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;

    eax = 0x80000001;

    __cpuid(&eax, &ebx, &ecx, &edx);

    return(!!(edx & (1 << 20)));
}

static void pgmgr_enable_nx(void)
{
    uint64_t reg_val = 0;

    reg_val = __rdmsr(0xC0000080);

    reg_val |= (1 << 11); /* enable NX */

    __wrmsr(0xC0000080, reg_val);
}

static int pgemgr_pml5_is_enabled(void)
{
    uint32_t eax = 0;
    uint32_t ebx = 0;
    uint32_t ecx = 0;
    uint32_t edx = 0;
    uint64_t cr4     = 0;
    int      enabled = 0;

    eax = 0x7;

    /* Check if PML5 is available */
    
    __cpuid(&eax, &ebx, &ecx, &edx);
    cr4 = __read_cr4();

    if((ecx & (1 << 16)) && ((cr4 & (1 << 12))))
        enabled = 1;

    /* Check if we have enabled it in the CPU */

    return(enabled);
}

static int pgmgr_alloc_pf_cb
(
    pfmgr_cb_data_t *cb_dat,
    void *pv
)
{
    phys_addr_t *pf = (phys_addr_t*)pv;
  
    if(*pf != 0)
        return(0);
    
    *pf = cb_dat->phys_base;
    cb_dat->used_bytes = PAGE_SIZE;

    return(0);
}

static int pgmgr_alloc_pf(phys_addr_t *pf)
{
    int ret = 0;

    ret = pfmgr->alloc(1, 0, pgmgr_alloc_pf_cb, pf);

    return(ret);
}

static int pgmgr_free_pf_cb
(
    pfmgr_cb_data_t *cb_dat,
    void *pv
)
{
    cb_dat->phys_base = *(phys_addr_t*)pv;
    cb_dat->used_bytes += PAGE_SIZE;

    return(0);
}

static int pgmgr_free_pf
(
    phys_addr_t addr
)
{
    return(pfmgr->dealloc(pgmgr_free_pf_cb, &addr));
}

static int pgmgr_attr_translate
(
    phys_addr_t *pte_mask, 
    uint32_t attr
)
{
    phys_addr_t pte = 0;

    if(attr & PGMGR_WRITABLE)
        pte |= PAGE_WRITABLE;
    
    if(attr & PGMGR_USER)
        pte |= PAGE_USER;
    
    if(!(attr & PGMGR_EXECUTABLE) && pgmgr.nx_support)
        pte |= PAGE_EXECUTE_DISABLE;
    
    /* Translate caching attributes */
    /*
        * PAT PCD PWT PAT Entry
        *  0   0   0    PAT0 -> WB
        *  0   0   1    PAT1 -> WT
        *  0   1   0    PAT2 -> UC-
        *  0   1   1    PAT3 -> UC
        *  1   0   0    PAT4 -> WC
        *  1   0   1    PAT5 -> WT
        *  1   1   0    PAT6 -> UC-
        *  1   1   1    PAT7 -> UC
        * 
        *  just to have a picture here
        * 
        * pat.fields.pa0 = PAT_WRITE_BACK;
        * pat.fields.pa1 = PAT_WRITE_THROUGH;
        * pat.fields.pa2 = PAT_UNCACHED;
        * pat.fields.pa3 = PAT_UNCACHEABLE;
        * pat.fields.pa4 = PAT_WRITE_COMBINING;
        * pat.fields.pa5 = PAT_WRITE_PROTECT;
        * pat.fields.pa6 = PAT_UNCACHED;
        * pat.fields.pa7 = PAT_UNCACHEABLE;
        */

    /* PAT 0 */
    if(attr & PGMGR_WRITE_BACK)
    {
        pte &= ~(PAGE_WRITE_THROUGH | 
                 PAGE_CACHE_DISABLE | 
                 PAGE_PAT);
    }
    /* PAT 1 */
    else if(attr & PGMGR_WRITE_THROUGH)
    {
        pte &= ~(PAGE_CACHE_DISABLE | 
                 PAGE_PAT);

        pte |= PAGE_WRITE_THROUGH;
    }
    /* PAT 2 */
    else if(attr & PGMGR_UNCACHEABLE)
    {
        pte &= ~(PAGE_WRITE_THROUGH |
                 PAGE_PAT);
        pte |= PAGE_CACHE_DISABLE;
    }
    /* PAT 3 */
    else if(attr & PGMGR_STRONG_UNCACHED)
    {
        pte |= (PAGE_WRITE_THROUGH | 
                PAGE_CACHE_DISABLE);
        pte &= ~PAGE_PAT;
    }
    /* PAT4 */
    else if(attr & PGMGR_WRITE_COMBINE)
    {
        pte |= PAGE_PAT;
        pte &= ~(PAGE_CACHE_DISABLE | 
                 PAGE_WRITE_THROUGH);
    }
    else if(attr & PGMGR_WRITE_PROTECT)
    {
        pte |= (PAGE_WRITE_THROUGH | PAGE_PAT);
        pte &= ~PAGE_CACHE_DISABLE;
    }
    /* By default do write-back */
    else
    {
        pte &= ~(PAGE_WRITE_THROUGH | 
                 PAGE_CACHE_DISABLE | 
                 PAGE_PAT);
    }

    *pte_mask = pte;

    return(0);
}

static void pgmgr_clear_pt
(
    pgmgr_ctx_t *ctx,
    phys_addr_t addr
)
{
    virt_addr_t vaddr = 0;

    vaddr = _pgmgr_temp_map(addr, 
                            PGMGR_CLEAR_PT_PAGE(ctx->max_level));

    if(vaddr != 0)
    {
        memset((void*)vaddr, 0, PAGE_SIZE);
        _pgmgr_temp_unmap(vaddr);
    }
}

static int pgmgr_level_is_empty
(
    virt_addr_t *level
)
{
    uint16_t i = 0;
    
    while(i < PGMGR_ENTRIES_PER_LEVEL)
    {
        if(level[i] != 0)
            break;
        
        i++;
    }

    return(i == PGMGR_ENTRIES_PER_LEVEL);
}

static int pgmgr_level_entry_is_empty
(
    pgmgr_ctx_t *ctx,
    phys_addr_t addr
)
{
    virt_addr_t *vaddr = NULL;
    int ret = -1;

    vaddr = (virt_addr_t*)_pgmgr_temp_map(addr, 
                                         PGMGR_LEVEL_ENTRY_PAGE(ctx->max_level));
    if(vaddr != NULL)
    {
        ret = pgmgr_level_is_empty(vaddr);
        _pgmgr_temp_unmap((virt_addr_t)vaddr);
    }
    
    return(ret);
}

static void pgmgr_iter_free_level
(
    pgmgr_iter_callback_data_t *iter_dat,
    pgmgr_level_data_t *ld,
    pfmgr_cb_data_t *pfmgr_dat,
    uint32_t op
)
{
    phys_addr_t addr = 0;
    virt_addr_t *up_level = NULL;
    uint8_t shift = 0;
    uint16_t entry = 0;

    ld->error  = PGMGR_ERR_OK;

    switch(op)
    {
        case PGMGR_CB_LEVEL_GO_DOWN:
        {
            if(~ld->level[iter_dat->entry] & PAGE_PRESENT)
            {
                /* If we got something that needs to be freed,
                 * and the underlying table is not available,
                 * just signal that we want to break instead of 
                 * going out with an error
                 */ 
                if(pfmgr_dat->used_bytes == 0)
                {
                    ld->cb_status |= PGMGR_CB_ERROR;
                    ld->error = PGMGR_ERR_TABLE_NOT_ALLOCATED;
                }
            }

            break;
        }

       case PGMGR_CB_NEXT_ENTRY:
       {
           /* usually the entry for the next_vaddr is bigger than entry for
            * vaddr. when is the other way around then we are going up but
            * there is a special case which happens if the last iteration
            * does not meet the condition to go up (and trigger a free for the
            * upper level) . In this special case we check if we are at the end
            * and if we are, we are forcing a level up
            */ 
           if(ld->offset + iter_dat->increment >= ld->length)
           {
               ld->cb_status |= PGMGR_CB_FORCE_GO_UP;
           }
           break;
       }

       case PGMGR_CB_LEVEL_GO_UP:           
       /* Intentionally fall through */
       case PGMGR_CB_DO_REQUEST:
       {   

           addr = PAGE_MASK_ADDRESS(ld->level[iter_dat->entry]);

           if(pgmgr_level_entry_is_empty(ld->ctx, addr) > 0)
           {
                /* first try */
                if(pfmgr_dat->used_bytes == 0)
                {
                    pfmgr_dat->used_bytes = PAGE_SIZE;
                    pfmgr_dat->phys_base = addr;
                    ld->level[iter_dat->entry] = 0;
                }
                /* add from right */
                else if(addr  ==
                       (pfmgr_dat->phys_base +
                        pfmgr_dat->used_bytes) )
                {
                    pfmgr_dat->used_bytes += PAGE_SIZE;
                    ld->level[iter_dat->entry] = 0;
                }
                /* add from left */
                else if(addr + PAGE_SIZE == pfmgr_dat->phys_base)
                {
                    pfmgr_dat->phys_base = addr;
                    pfmgr_dat->used_bytes += PAGE_SIZE;
                    ld->level[iter_dat->entry] = 0;
                }
                else
                {
                    /* we cannot add as the address is not contigous
                     * so we will send what we got so far and then
                     * try again
                     */
                    ld->cb_status |= PGMGR_CB_BREAK; 
                }
           }
           break;
       }
    }
}

static void pgmgr_iter_alloc_level
(
    pgmgr_iter_callback_data_t *iter_dat,
    pgmgr_level_data_t *ld,
    pfmgr_cb_data_t *pfmgr_dat,
    uint32_t op
)
{
    pgmgr_ctx_t *ctx = NULL;
    
    virt_size_t len = 0;
    virt_addr_t new_offset = 0;
    ctx       = ld->ctx;

    ld->error  = PGMGR_ERR_OK;

    switch(op)
    {
        case PGMGR_CB_LEVEL_GO_DOWN:
        /* fall through */
        case PGMGR_CB_DO_REQUEST:
        {
           // kprintf("Allocating %x\n",iter_dat->vaddr);
             /* check if the page table entry is present */
            if(~ld->level[iter_dat->entry] & PAGE_PRESENT)
            {
                ld->level[iter_dat->entry] = pfmgr_dat->phys_base  + 
                                             pfmgr_dat->used_bytes + 
                                             PAGE_PRESENT          + 
                                             PAGE_WRITABLE;

                /* Make sure that the underlying table is clean */
                pfmgr_dat->used_bytes += PAGE_SIZE;

                pgmgr_clear_pt(ctx, ld->level[iter_dat->entry]);
            } 
           
            break;
        }
        case PGMGR_CB_RES_CHECK:
        { 
            if(pfmgr_dat->avail_bytes <= pfmgr_dat->used_bytes)
            {
                ld->cb_status |= PGMGR_CB_BREAK;
            }
             
            break;
        }
    }
}

/* 
 * pgmgr_iter_free_page - frees / unmaps a page
 */

static void pgmgr_iter_free_page
(
    pgmgr_iter_callback_data_t *iter_dat,
    pgmgr_level_data_t *ld,
    pfmgr_cb_data_t *pfmgr_dat,
    uint32_t op
)
{
    phys_addr_t addr = 0;

    ld->error  = PGMGR_ERR_OK;

    switch(op)
    {
        case PGMGR_CB_LEVEL_GO_DOWN:
        {
            /* if we do not have a level how can we free a page which 
             * would belong to that level?
             * if we do not have a level, then this is an error
             */ 
            if(~ld->level[iter_dat->entry] & PAGE_PRESENT)
            {
                ld->cb_status |= PGMGR_CB_ERROR;
                ld->error = PGMGR_ERR_TABLE_NOT_ALLOCATED;
            }

            break;
        }
        
        case PGMGR_CB_DO_REQUEST:
        {
            addr = PAGE_MASK_ADDRESS(ld->level[iter_dat->entry]);
            
            if(ld->level[iter_dat->entry] & PAGE_PRESENT)
            {
                if(pfmgr_dat->used_bytes == 0)
                {
                    pfmgr_dat->used_bytes += PAGE_SIZE;
                    pfmgr_dat->phys_base = addr;
                    ld->level[iter_dat->entry] = 0;
                }
                else if(addr == 
                        (pfmgr_dat->phys_base +
                         pfmgr_dat->used_bytes))
                {
                    pfmgr_dat->used_bytes+=PAGE_SIZE;
                    ld->level[iter_dat->entry] = 0;
                }
                else if(addr + PAGE_SIZE == pfmgr_dat->phys_base)
                {
                    pfmgr_dat->phys_base = addr;
                    pfmgr_dat->used_bytes += PAGE_SIZE;
                    ld->level[iter_dat->entry] = 0;
                }
                else
                {
                    ld->cb_status |= PGMGR_CB_BREAK;
                }
            }

            break;
        }
    }
}

/* 
 * pgmgr_iter_alloc_page - allocates / maps a page
 */

static void pgmgr_iter_alloc_page
(
    pgmgr_iter_callback_data_t *iter_dat,
    pgmgr_level_data_t *ld,
    pfmgr_cb_data_t *pfmgr_dat,
    uint32_t op
)
{
    pgmgr_ctx_t        *ctx = NULL;

    ld->error  = PGMGR_ERR_OK;

    switch(op)
    {
        case PGMGR_CB_LEVEL_GO_DOWN:
        {
             /* No excuse here - no level, no page */
             if(~ld->level[iter_dat->entry] & PAGE_PRESENT)
             {
                 ld->error = PGMGR_ERR_TABLE_NOT_ALLOCATED;
                 ld->cb_status |= PGMGR_CB_ERROR;
             }

             break;
        }
        case PGMGR_CB_DO_REQUEST:
        {
            if(~ld->level[iter_dat->entry] & PAGE_PRESENT)
            {
                ld->level[iter_dat->entry] = (pfmgr_dat->phys_base + 
                                              pfmgr_dat->used_bytes) | 
                                              ld->attr_mask          | 
                                              PAGE_PRESENT;

                pfmgr_dat->used_bytes += PAGE_SIZE;
            }
            break;
        }
        case PGMGR_CB_RES_CHECK:
        {
            if(pfmgr_dat->avail_bytes <= pfmgr_dat->used_bytes)
                ld->cb_status |= PGMGR_CB_BREAK;
            break;
        }
    }
}

/*
 * pgmgr_iter_change_attribs - change page attributes
 */
static void pgmgr_iter_change_attribs
(
    pgmgr_iter_callback_data_t *iter_dat,
    pgmgr_level_data_t *ld,
    pfmgr_cb_data_t *pfmgr_dat,
    uint32_t op
)
{
    pgmgr_ctx_t        *ctx = NULL;

    ld->error  = PGMGR_ERR_OK;
    
    switch(op)
    {
        case PGMGR_CB_LEVEL_GO_DOWN:
             if(~ld->level[iter_dat->entry] & PAGE_PRESENT)
             {
                 ld->error = PGMGR_ERR_TABLE_NOT_ALLOCATED;
                 ld->cb_status |= PGMGR_CB_ERROR;
             }
             break;

        case PGMGR_CB_DO_REQUEST:
            if(ld->level[iter_dat->entry] & PAGE_PRESENT)
            {
                ld->level[iter_dat->entry] = PAGE_MASK_ADDRESS(
                                              ld->level[iter_dat->entry]) | 
                                              ld->attr_mask               |
                                              PAGE_PRESENT;
             
                pfmgr_dat->used_bytes += PAGE_SIZE;
            }
            break;
    }
}

/* pgmgr_iterate_levels - iterates through the paging levels and calls
 * the specified callback with different op codes so that the callback
 * can query or modify the callback
 */ 

static int pgmgr_iterate_levels
(
    pfmgr_cb_data_t *pfmgr_dat,
    void            *pv
)
{
    pgmgr_level_data_t *ld        = NULL;
    pgmgr_ctx_t       *ctx        = NULL;
    pgmgr_iter_callback_data_t it_dat = {
                                            .entry = 0,
                                            .increment = 0,
                                            .next_vaddr = 0,
                                            .shift = 0,
                                            .vaddr = 0,    
                                        };

    ld          = pv;
    ctx         = ld->ctx;

    if((ld->length  <= ld->offset))
    {
        return(0);
    }

    if(!ld->curr_level || !ld->level_phys)
    {
        ld->curr_level = ctx->max_level;
        ld->level_phys = ctx->pg_phys;
        ld->do_map     = 1;
    }
    
    while(ld->offset < ld->length )
    {
        ld->cb_status = 0;

        /* Check if there are resources available */
        ld->iter_cb(&it_dat,
                    ld,
                    pfmgr_dat,
                    PGMGR_CB_RES_CHECK);

        if(ld->cb_status  & PGMGR_CB_ERROR)
        {
            kprintf("%s %d\n",__FUNCTION__,__LINE__);
            return(-1);
        }
        else if(ld->cb_status & PGMGR_CB_BREAK)
        {
            break;
        }
        else if(ld->cb_status & PGMGR_CB_STOP)
        {
            return(0);
        }

        /* Map the page table */
        if(ld->do_map || ld->level == NULL)
        {
            ld->level = (virt_addr_t*) _pgmgr_temp_map(ld->level_phys, 
                                                       ld->curr_level);
            ld->do_map = 0;
        }

        it_dat.shift     = PGMGR_LEVEL_TO_SHIFT(ld->curr_level);
        it_dat.increment = (virt_size_t)1 << it_dat.shift;
        it_dat.vaddr     = ld->base + ld->offset, 
        it_dat.entry     = (it_dat.vaddr >> it_dat.shift) & PGMGR_MAX_TABLE_INDEX; 

        /* If we haven't reached the target level then we have to go
         * down by obtaining the address for the lower page table entry 
         * It the PAGE_TABLE_SIZE bit is set, we should not go any deeper
         * because this is the page we are looking for
         */
        if(~ld->level[it_dat.entry] & PAGE_TABLE_SIZE)
        {
            if(ld->curr_level > ld->req_level)
            {
                /* notify the callback the we have to go down 
                 * so it notifies if we can continue or not
                 */
                ld->iter_cb(&it_dat,
                             ld,
                             pfmgr_dat,
                             PGMGR_CB_LEVEL_GO_DOWN);

                if(ld->cb_status & PGMGR_CB_ERROR)
                {
                    return (-1);
                }
                
                else if(ld->cb_status & PGMGR_CB_BREAK)
                {
                    break;
                }
                else if(ld->cb_status & PGMGR_CB_STOP)
                {
                    return(0);
                }

                ld->level_phys = ld->level[it_dat.entry];
                ld->curr_level--;
                ld->do_map = 1;

                continue;
            }
        }
        else
        {
            kprintf("PGMGR: unable to handle page sizes > 4KB\n");
            while(1);
        }

        /* do the actual request - allocate/free/change attrs */
        ld->iter_cb(&it_dat, 
                    ld,
                    pfmgr_dat,
                    PGMGR_CB_DO_REQUEST);
 
        if(ld->cb_status & PGMGR_CB_ERROR)
        {
            return(-1);
        }
        else if(ld->cb_status & PGMGR_CB_BREAK)
        {
            break;
        }
        else if(ld->cb_status & PGMGR_CB_STOP)
        {
            return(0);
        }

        it_dat.next_vaddr = ld->base    + 
                            ld->offset  + 
                            it_dat.increment;

        /* Check the next entery */
        ld->iter_cb(&it_dat,
                    ld,
                    pfmgr_dat,
                    PGMGR_CB_NEXT_ENTRY);
        
        if(ld->cb_status & PGMGR_CB_ERROR)
        {
            return(-1);
        }
        else if(ld->cb_status & PGMGR_CB_BREAK)
        {
            break;
        }
        else if(ld->cb_status & PGMGR_CB_STOP)
        {
            return(0);
        }

        /* Check if we need to switch the level */
        if((((it_dat.next_vaddr >> it_dat.shift) & PGMGR_MAX_TABLE_INDEX) < 
             (it_dat.entry)) || 
            (ld->cb_status & PGMGR_CB_FORCE_GO_UP))
        {
            /* Calculate how much we need to go up */
            while(ld->curr_level < ctx->max_level)
            {           
                /* set up the level to point to the upper structure */     
                ld->curr_level++;

                /* calulcate the shift */
                it_dat.shift = PGMGR_LEVEL_TO_SHIFT(ld->curr_level);
                
                /* calculate the entry for the upper level */
                it_dat.entry = (it_dat.vaddr >> it_dat.shift) & 
                                PGMGR_MAX_TABLE_INDEX;

                /* save the physical address of the upper level */
                ld->level_phys = 
                             ((virt_addr_t*)pgmgr.remap_tbl)[ld->curr_level];

                /* save the upper level */
                ld->level = (virt_addr_t*) (pgmgr.remap_tbl + 
                                           (ld->curr_level  << PAGE_SIZE_SHIFT));

                /* Tell the callback that we are going up so it may
                 * be able to free level entries
                 */
                ld->iter_cb(&it_dat, 
                            ld,
                            pfmgr_dat,
                            PGMGR_CB_LEVEL_GO_UP);
                
                if(ld->cb_status & PGMGR_CB_ERROR)
                {
                    return(-1);
                }
                else if(ld->cb_status & PGMGR_CB_BREAK)
                {
                    break;
                }
                else if(ld->cb_status & PGMGR_CB_STOP)
                {
                    return(0);
                }

                /* If we might found the position, check if there 
                 * is a entry in the level. If it's not, then we will keep
                 * looking
                 */
                
                if(((it_dat.next_vaddr >> it_dat.shift) & PGMGR_MAX_TABLE_INDEX) >
                    ((it_dat.vaddr >> it_dat.shift) & PGMGR_MAX_TABLE_INDEX))
                {
                    it_dat.entry = (it_dat.next_vaddr >> it_dat.shift) & 
                                    PGMGR_MAX_TABLE_INDEX;
                    
                    if(ld->level[it_dat.entry] & PAGE_PRESENT)
                        break;
                }
            }
            /* Check if we've max level and if we did, 
             * begin from the top level 
             */
            if(ld->curr_level >= ctx->max_level)
            {
                ld->level = NULL;
                ld->level_phys = ctx->pg_phys;
                ld->curr_level = ctx->max_level;
                ld->do_map = 1;
            }
        }
        
        if(ld->cb_status & PGMGR_CB_BREAK)
            break;

       /* calculate the next offset */
       ld->offset += it_dat.increment;
    }
   
    /* All right, we're done */
    if(ld->offset >= ld->length)
    {
        return(0);
    }

    /* Keep going */
    return(1);
}

static int pgmgr_setup_remap_table
(
    pgmgr_ctx_t *ctx
)
{
    pgmgr_level_data_t lvl_dat;
    uint8_t curr_level = 0;
    virt_addr_t *level    = NULL;
    phys_addr_t addr      = 0;
    uint16_t entry        = 0;
    uint8_t shift         = 0;
    int status            = 0;

#ifdef PGMGR_DEBUG
    kprintf("----Setting up remapping table----\n");
#endif    
    PGMGR_FILL_LEVEL(&lvl_dat, 
                     ctx, 
                     REMAP_TABLE_VADDR, 
                     REMAP_TABLE_SIZE, 
                     2, 
                     0, 
                     pgmgr_iter_alloc_level);

    /* Allocate pages */
    status = pfmgr->alloc(0, 
                          ALLOC_CB_STOP, 
                          pgmgr_iterate_levels, 
                          &lvl_dat);

    if(status || lvl_dat.error)
    {
        kprintf("Failed to allocate the required " 
                "levels STATUS %x ERROR %x\n", status, lvl_dat.error);
        while(1);
    }

    addr = ctx->pg_phys;

    for(curr_level = ctx->max_level; curr_level > 0; curr_level--)
    {
        shift = PGMGR_LEVEL_TO_SHIFT(curr_level);
        entry = (REMAP_TABLE_VADDR >> shift) & PGMGR_MAX_TABLE_INDEX;

        level = (virt_addr_t*)_pgmgr_temp_map(addr, curr_level);
        kprintf("ADDR %x - LEVEL %x - %x\n", addr, level, level[entry]);

        /* If we reach the bottom level, map the first page from the level 
         * to the to the table from it belongs 
         */

        if(curr_level < PGMGR_MIN_PAGE_TABLE_LEVEL)
        {
            level[entry] = addr;
            break;
        }
        addr = level[entry];
    }

    pgmgr.remap_tbl = REMAP_TABLE_VADDR;

 #ifdef PGMGR_DEBUG
    kprintf("----Done setting up remapping table----\n");
#endif
    return(0);
}

int pgmgr_allocate_backend
(
    pgmgr_ctx_t *ctx,
    virt_addr_t vaddr,
    virt_size_t req_len,
    virt_size_t *out_len
)
{
    pgmgr_level_data_t ld;
    int status = 0;

    /* Create the tables */
    PGMGR_FILL_LEVEL(&ld, 
                     ctx, 
                     vaddr, 
                     req_len, 
                     2, 
                     0, 
                     pgmgr_iter_alloc_level);

    status = pfmgr->alloc(0, 
                          ALLOC_CB_STOP,
                          pgmgr_iterate_levels,
                          &ld);

    /* report how much did we actually built */
    if(out_len)
    {
        *out_len = ld.offset;
    }

    if(status < 0 || ld.error != PGMGR_ERR_OK)
    {
        kprintf("Backend build failed: Status %x Error %x\n",status, ld.error);
        return(-1);
    }

    return(0);
}

int pgmgr_release_backend
(
    pgmgr_ctx_t *ctx,
    virt_addr_t vaddr,
    virt_size_t req_len,
    virt_size_t      *out_len
)
{
    phys_addr_t attr_mask = 0;
    pgmgr_level_data_t ld;
    int status = 0;

    /* Release pages */
    PGMGR_FILL_LEVEL(&ld, 
                     ctx, 
                     vaddr, 
                     req_len, 
                     2, 
                     0, 
                     pgmgr_iter_free_level);

    status = pfmgr->dealloc(pgmgr_iterate_levels, &ld);

    /* report how much did we actually allocated */
    
    if(out_len)
    {
        *out_len = ld.offset;
    }

    if(status < 0 || ld.error != PGMGR_ERR_OK)
    {
        kprintf("Release failed: Status %x Error %x\n",status, ld.error);
        return(-1);
    }

    return(0);
}

int pgmgr_allocate_pages
(
    pgmgr_ctx_t *ctx,
    virt_addr_t vaddr,
    virt_size_t req_len,
    virt_size_t      *out_len,
    uint32_t    vm_attr
)
{
    phys_addr_t attr_mask = 0;
    pgmgr_level_data_t ld;
    int status = 0;

    pgmgr_attr_translate(&attr_mask, vm_attr);

    /* Allocate pages */
    PGMGR_FILL_LEVEL(&ld, 
                     ctx, 
                     vaddr, 
                     req_len, 
                     1, 
                     attr_mask, 
                     pgmgr_iter_alloc_page);

    status = pfmgr->alloc(0, 
                          ALLOC_CB_STOP,
                          pgmgr_iterate_levels,
                          &ld);

    /* report how much did we actually allocated */
    
    if(out_len)
    {
        *out_len = ld.offset;
    }

    if(status < 0 || ld.error != PGMGR_ERR_OK)
    {
        kprintf("Allocation failed: Status %x Error %x\n",status, ld.error);
        return(-1);
    }

    return(0);
}

int pgmgr_release_pages
(
    pgmgr_ctx_t *ctx,
    virt_addr_t vaddr,
    virt_size_t req_len,
    virt_size_t      *out_len
)
{
    phys_addr_t attr_mask = 0;
    pgmgr_level_data_t ld;
    int status = 0;

    /* Release pages */
    PGMGR_FILL_LEVEL(&ld, 
                     ctx, 
                     vaddr, 
                     req_len, 
                     1, 
                     0, 
                     pgmgr_iter_free_page);

    status = pfmgr->dealloc(pgmgr_iterate_levels, &ld);

    /* report how much did we actually allocated */
    
    if(out_len)
    {
        *out_len = ld.offset;
    }

    if(status < 0 || ld.error != PGMGR_ERR_OK)
    {
        kprintf("Release failed: Status %x Error %x\n",status, ld.error);
        return(-1);
    }

    return(0);
}

int pgmgr_map_pages
(
    pgmgr_ctx_t *ctx,
    virt_addr_t vaddr,
    virt_size_t req_len,
    virt_size_t      *out_len,
    uint32_t    vm_attr,
    phys_addr_t phys
)
{
    pgmgr_level_data_t ld;
    phys_addr_t attr_mask = 0;
    pfmgr_cb_data_t mem = {.avail_bytes = 0, .phys_base = 0, .used_bytes = 0};
    int status = 0;

    pgmgr_attr_translate(&attr_mask, vm_attr);

    /* Allocate pages */
    PGMGR_FILL_LEVEL(&ld, 
                     ctx, 
                     vaddr, 
                     req_len, 
                     1, 
                     attr_mask, 
                     pgmgr_iter_alloc_page);

    mem.phys_base   = phys;
    mem.avail_bytes = req_len;

    status = pgmgr_iterate_levels(&mem, &ld);

    if(out_len)
    {
        *out_len = ld.offset;
    }

    if(status < 0 || ld.error != PGMGR_ERR_OK)
    {
        kprintf("Mapping failed: Status %x Error %x\n",status, ld.error);
        return(-1);
    }

    return(0);
}

int pgmgr_unmap_pages
(
    pgmgr_ctx_t *ctx,
    virt_addr_t vaddr,
    virt_size_t req_len,
    virt_size_t *out_len
)
{
    pfmgr_cb_data_t mem = {.avail_bytes = 0, .phys_base = 0, .used_bytes = 0};
    pgmgr_level_data_t ld;
    int status = 0;

    /* Unmap pages */
    PGMGR_FILL_LEVEL(&ld, 
                     ctx, 
                     vaddr, 
                     req_len, 
                     1, 
                     0, 
                     pgmgr_iter_free_page);

    do
    {
        status = pgmgr_iterate_levels(&mem, &ld);
        mem.avail_bytes = 0;
        mem.phys_base   = 0;
        mem.used_bytes  = 0;

    }while((status > 0) && (ld.error == PGMGR_ERR_OK));

    /* report how much did we actually allocated */
    
    if(out_len)
    {
        *out_len = ld.offset;
    }

    if(status < 0 || ld.error != PGMGR_ERR_OK)
    {
        kprintf("Release failed: Status %x Error %x\n",status, ld.error);
        return(-1);
    }

    return(0);
}


int pgmgr_change_attrib
(
    pgmgr_ctx_t *ctx,
    virt_addr_t vaddr, 
    virt_size_t len, 
    uint32_t attr
)
{

    pgmgr_level_data_t ld;
    phys_addr_t attr_mask;
    pfmgr_cb_data_t cb_data = {.avail_bytes = 0,
                               .phys_base = 0,
                               .used_bytes = 0
                               };
    int status = 0;
    
    /* Clear the level data */
    memset(&ld, 0, sizeof(pgmgr_level_data_t));

    /* Translate the attributes */
    pgmgr_attr_translate(&attr_mask, attr);

    /* Fill the level data */
    PGMGR_FILL_LEVEL(&ld, 
                      ctx, 
                      vaddr, 
                      len, 
                      1, 
                      attr_mask, 
                      pgmgr_iter_change_attribs);

    cb_data.avail_bytes = len;
    /* Change the attributes */
    status = pgmgr_iterate_levels(&cb_data, &ld);


    if(status < 0 || ld.error)
        return(-1);

    return(0);
}

static int pgmgr_map_kernel(pgmgr_ctx_t *ctx)
{
    int status = 0;

    kprintf("Mapping kernel sections\n");
    
    status = pgmgr_allocate_backend(ctx, 
                                    _KERNEL_VMA,
                                    _KERNEL_VMA_END - _KERNEL_VMA,
                                    NULL);

    kprintf("BUILT BACKEND\n");

    /* Map code section */
    pgmgr_map_pages(ctx, (virt_addr_t)&_code, 
                         (virt_addr_t)&_code_end - (virt_addr_t)&_code, 
                         NULL,
                         PGMGR_EXECUTABLE,
                         (virt_addr_t)&_code - _KERNEL_VMA );
    
    /* Map data sections */
    pgmgr_map_pages(ctx, (virt_addr_t)&_data, 
                   (virt_addr_t)&_data_end -  (virt_addr_t)&_data, 
                   NULL,
                   PGMGR_WRITABLE,
                   (virt_addr_t)&_data - _KERNEL_VMA);

    pgmgr_map_pages(ctx, (virt_addr_t)&_rodata, 
                   (virt_addr_t)&_rodata_end - (virt_addr_t)&_rodata, 
                   NULL,
                   0,
                   (virt_addr_t)&_rodata - _KERNEL_VMA );

    pgmgr_map_pages(ctx, (virt_addr_t)&_bss, 
                   (virt_addr_t)&_bss_end - (virt_addr_t)&_bss, 
                   NULL,
                   PGMGR_WRITABLE,
                   (virt_addr_t)&_bss - _KERNEL_VMA);

    kprintf("Done mapping kernel sections\n");
    return(0);
}

int pgmgr_ctx_init
(
    pgmgr_ctx_t *ctx
)
{ 
    spinlock_init(&ctx->lock);
    
    if(pgmgr.pml5_support)
        ctx->max_level = 5;
    else
        ctx->max_level = 4;
    
    if(pgmgr_alloc_pf(&ctx->pg_phys))
    {
        kprintf("Failed to allocate page table root\n");
        while(1);
    } 

    pgmgr_clear_pt(ctx, ctx->pg_phys);

    return(0);
}

/* This should be called only once */

int pgmgr_kernel_ctx_init
(
    pgmgr_ctx_t *ctx
)
{
    /* check if we already initalized it */
    if(pgmgr.kernel_ctx_init)
    {
        return(-1);
    }
    kprintf("INITIALISING KERNEL CONTEXT\n");
    /* set up the context */
    pgmgr_ctx_init(ctx);

    /* map the kernel */
    pgmgr_map_kernel(ctx);

    /* create remapping table */
    pgmgr_setup_remap_table(ctx);

    /* If we support NX, enable it */
    if(pgmgr.nx_support)
        pgmgr_enable_nx();

    /* use the new page table */
    __write_cr3(ctx->pg_phys);

    /* Initializae per-CPU stuff */
    pgmgr_per_cpu_init();

    /* mark that we create the kernel context */
    pgmgr.kernel_ctx_init = 1;

    return(0);
}

int pgmgr_init(void)
{
    pat_t *pat = NULL;
    pat = &pgmgr.pat;

    kprintf("Initializing Page Manager\n");

    /* clear the pat memory */
    memset(pat, 0, sizeof(pat_t));

    pgmgr.pml5_support = pgemgr_pml5_is_enabled();
    pgmgr.nx_support   = pgmgr_check_nx();

    kprintf("PML5       %d\n", pgmgr.pml5_support);
    kprintf("NX support %d\n", pgmgr.nx_support);

    pfmgr = pfmgr_get();

    /* Populate PAT */
    pat->fields.pa0 = PAT_WRITE_BACK;
    pat->fields.pa1 = PAT_WRITE_THROUGH;
    pat->fields.pa2 = PAT_UNCACHED;
    pat->fields.pa3 = PAT_UNCACHEABLE;
    pat->fields.pa4 = PAT_WRITE_COMBINING;
    pat->fields.pa5 = PAT_WRITE_PROTECTED;
    pat->fields.pa6 = PAT_UNCACHED;
    pat->fields.pa7 = PAT_UNCACHEABLE;
    
    /* Install the interrupt handler for page faults */
    isr_install(pgmgr_page_fault_handler, 
                &pgmgr, 
                PLATFORM_PG_FAULT_VECTOR, 
                0,
                &pgmgr.fault_isr);

    /* install the interrupt handler for page invalidation */
    isr_install(pgmgr_per_cpu_invl_handler, 
                NULL, 
                PLATFORM_PG_INVALIDATE_VECTOR, 
                0,
                &pgmgr.inv_isr);
    
    pgmgr.kernel_ctx_init = 0;

    return(0);
}

static inline virt_addr_t _pgmgr_temp_map
(
    phys_addr_t phys, 
    uint16_t ix
)
{
    virt_addr_t *remap_tbl = (virt_addr_t*)pgmgr.remap_tbl;
    virt_addr_t remap_value = 0;

    /* ix is not allowd to be 0 - root of the remapping table 
     * or above 511 - max table 
     */

    if(ix > 511 || ix == 0)
    {
        kprintf("DIED @ %s %d\n",__FUNCTION__,__LINE__);
        return(-1);
    }

    phys = PAGE_MASK_ADDRESS(phys);
   
    if(phys % PAGE_SIZE)
    {
        kprintf("DIED @ %s %d\n",__FUNCTION__,__LINE__);
        return(0);
    }

    remap_tbl[ix] = phys | PAGE_PRESENT | PAGE_WRITABLE;

    remap_value = pgmgr.remap_tbl + (PAGE_SIZE * ix);

    __invlpg(remap_value);
    

    return(remap_value);
}


static int _pgmgr_temp_unmap
(
    virt_addr_t vaddr
)
{
    uint16_t ix = 0;
    virt_addr_t *remap_tbl = (virt_addr_t*)pgmgr.remap_tbl;

    if(vaddr % PAGE_SIZE || vaddr <= pgmgr.remap_tbl)
        return(-1);

    ix = (vaddr - pgmgr.remap_tbl) / PAGE_SIZE;

    /* remove address */
    remap_tbl[ix] = 0;

    /* invalidate entry */
    __invlpg(vaddr);

    return(0);
}


virt_addr_t pgmgr_temp_map
(
    phys_addr_t phys, 
    uint16_t ix
)
{
    if(ix < 510)
    {
        return(0);
    }

    return(_pgmgr_temp_map(phys, ix));
}

int pgmgr_temp_unmap
(
    virt_addr_t vaddr
)
{
    if(vaddr < pgmgr.remap_tbl + PAGE_SIZE * 510)
    {
        return(-1);
    }

    return(_pgmgr_temp_unmap(vaddr));
}


void pgmgr_invalidate
(
    pgmgr_ctx_t *ctx,
    virt_addr_t vaddr,
    virt_size_t len
)
{
    virt_addr_t cr3 = __read_cr3();

    if(cr3 == ctx->pg_phys)
    {
        if(len < PAGE_SIZE * PGMGR_UPDATE_ENTRIES_THRESHOLD)
        {
            for(virt_size_t i = 0; i < len; i+= PAGE_SIZE)
            {
                __invlpg(vaddr + i);
            }
        }
        else
        {
            __write_cr3(cr3);
        }
    }

     cpu_issue_ipi(IPI_DEST_ALL_NO_SELF, 0, IPI_INVLPG);
}

static inline void pgmgr_invalidate_all(void *pv, isr_info_t *inf)
{
   
    __write_cr3(__read_cr3());
   
}


static int pgmgr_page_fault_handler(void *pv, isr_info_t *inf)
{

    isr_frame_t *int_frame = 0;
    virt_addr_t fault_address = 0;
    virt_addr_t error_code = *(virt_addr_t*)(inf->iframe - sizeof(uint64_t));

    fault_address = __read_cr2();
    int_frame = (isr_frame_t*)inf->iframe;

    kprintf("CPU %d: ADDRESS 0x%x ERROR 0x%x IP 0x%x SS 0x%x RFLAGS 0x%x\n",
            inf->cpu_id,   
            fault_address, 
            error_code,    
            int_frame->rip,
            int_frame->ss,
            int_frame->rflags);


    //vm_fault_handler(NULL, fault_address, )
    while(1);

    return(0);
}

static int pgmgr_per_cpu_invl_handler
(
    void *pv, 
    isr_info_t *inf
)
{
    int status = 0;
 //kprintf("INVALIDATE  on CPU %x\n",inf->cpu_id);
    __write_cr3(__read_cr3());
    
    return(0);
}

int pgmgr_per_cpu_init(void)
{
    virt_addr_t cr0 = 0;
    virt_addr_t cr3 = 0;
    
    kprintf("Initializing per cpu pgmgr\n");

    /* enable write protect for read only pages in kernel mode */
    cr0 = __read_cr0();
    cr0 |= (1 << 16);
    
    /* enable cache and write back */
    cr0 &= ~((1 << 29) | (1 << 30));

    __write_cr0(cr0);

    __wbinvd();
    __wrmsr(PAT_MSR, pgmgr.pat.pat);
    
    /* Invalidate page table */
    cr3 = __read_cr3();

    __write_cr3(cr3);
    
    /* wirte back and invalidate */
    __wbinvd();

    return(0);
}