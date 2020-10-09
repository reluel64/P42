/* NUMA-aware page frame allocator 
 * Part of P42
 */

#include <linked_list.h>
#include <pfmgr.h>
#include <utils.h>
#include <memory_map.h>
#include <pagemgr.h>
#include <vmmgr.h>
typedef struct 
{
    phys_addr_t busy_start;
    phys_size_t busy_len;
    phys_addr_t prev;
}pfmgr_init_t;

static pfmgr_base_t base;
static pfmgr_t pfmgr_interface;

#define TRACK_LEN(x) (ALIGN_UP((sizeof(pfmgr_free_range_t)) + \
                               BITMAP_SIZE_FOR_AREA((x)), PAGE_SIZE))

#define BIT_SET(x) ((1ull) << x)
#define BIT_CLEAR(x) (~((1ull) << x))

/* pfmgr_in_range -  check if a a memory interval is in range */

static inline int pfmgr_in_range
(
    phys_addr_t base,
    phys_size_t len,
    phys_addr_t req_base,
    phys_size_t req_len
)
{
    phys_size_t limit     = 0;
    phys_size_t req_limit = 0;
    phys_size_t req_end   = 0;
    phys_size_t end       = 0;

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

/* pfmgr_in_range -  check if a a memory interval touches the range */

static inline int pfmgr_touches_range
(
    phys_addr_t base,
    phys_size_t len,
    phys_addr_t req_base,
    phys_size_t req_len
)
{
    phys_size_t limit     = 0;
    phys_size_t req_limit = 0;
    phys_size_t req_end   = 0;
    phys_size_t end       = 0;
    
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

/* pfmgr_early_clear_bitmap - clear bitmap area using early page tables */

static void pfmgr_early_clear_bitmap(pfmgr_free_range_t *fmem, phys_addr_t bmp_phys)
{
    virt_addr_t *bmp = NULL;
    phys_size_t zlen = 0;
    phys_size_t pos  = 0;
    phys_size_t bmp_len = fmem->hdr.struct_len - sizeof(pfmgr_free_range_t);

    while(pos < bmp_len)
    {
        zlen = min(bmp_len - pos, PAGE_SIZE);

        bmp = (virt_addr_t*)pagemgr_boot_temp_map(bmp_phys + pos);
        
        memset(bmp, 0, zlen);

        pos += zlen;
    }
}

/* pfmgr_early_mark_bitmap - mark bitmap using early page table */

static void pfmgr_early_mark_bitmap
(
    pfmgr_free_range_t *fmem, 
    phys_addr_t bmp_phys, 
    phys_addr_t addr, 
    phys_size_t len
)
{
    virt_addr_t *bmp = NULL;
    phys_size_t bmp_off  = 0;
    phys_size_t pf_ix = 0;
    phys_size_t pf_pos = 0;
    phys_size_t pos = 0;
    

    while(pos < len)
    {
        pf_pos = ((addr + pos) - fmem->hdr.base) / PAGE_SIZE;
        pf_ix = pf_pos % PF_PER_ITEM;
        bmp_off = (pf_pos / PF_PER_ITEM) * sizeof(virt_addr_t);

        if(pf_ix == 0 || bmp == NULL)
            bmp = (virt_addr_t*)pagemgr_boot_temp_map(bmp_phys + bmp_off);

        
        bmp[0] |= ((virt_addr_t)1 << pf_ix);
        pos += PAGE_SIZE;
        fmem->avail_pf--;
        
    }
}

/* pfmgr_early_init_free_callback - initialize free ranges using boot page tables */

static void pfmgr_init_free_callback(memory_map_entry_t *e, void *pv)
{
    pfmgr_init_t *init = pv;
    pfmgr_free_range_t *freer = NULL;
    pfmgr_free_range_t local_freer;
    phys_addr_t track_addr = 0;
    phys_addr_t track_len = 0;

    if(e->type != MEMORY_USABLE || 
      !(e->flags & MEMORY_ENABLED))
        return;

    memset(&local_freer, 0, sizeof(pfmgr_free_range_t));

    track_len = TRACK_LEN(e->length);
    track_addr = ALIGN_DOWN(e->base + (e->length - track_len), PAGE_SIZE);

    /* Link the previous entry with this one */
    if(init->prev)
    {
        freer = (pfmgr_free_range_t*)pagemgr_boot_temp_map(init->prev);
        freer->hdr.node.next = (list_node_t *)track_addr;
        init->prev = 0;
    }
    
    if(base.physf_start == 0)
        base.physf_start = track_addr;
    
    local_freer.hdr.base      = e->base;
    local_freer.hdr.len       = e->length;
    local_freer.hdr.domain_id = e->domain; 
    local_freer.hdr.type      = e->type;
    

    local_freer.hdr.struct_len = track_len;
    local_freer.total_pf       = e->length / PAGE_SIZE;
    local_freer.avail_pf       = local_freer.total_pf;

    kprintf("%s -  RANGE START 0x%x LENGTH 0x%x END 0x%x\n",__FUNCTION__,e->base, e->length, e->base + e->length);
    kprintf("TRACKING START 0x%x LENGTH 0x%x\n",track_addr, track_len);

    pfmgr_early_clear_bitmap(&local_freer, 
                              track_addr + sizeof(pfmgr_free_range_t));

    if(pfmgr_in_range(e->base, e->length, _KERNEL_LMA,_KERNEL_IMAGE_LEN))
    {
        pfmgr_early_mark_bitmap(&local_freer,
                           track_addr + sizeof(pfmgr_free_range_t),
                           _KERNEL_LMA, 
                           _KERNEL_IMAGE_LEN);
    }

    if(pfmgr_in_range(e->base, e->length, base.physb_start, 
                       base.busyr.count * sizeof(pfmgr_busy_range_t)))
    {
        pfmgr_early_mark_bitmap(&local_freer,
                           track_addr + sizeof(pfmgr_free_range_t),
                           base.physb_start, 
                           base.busyr.count * sizeof(pfmgr_busy_range_t));
    }

    pfmgr_early_mark_bitmap(&local_freer,
                       track_addr + sizeof(pfmgr_free_range_t),
                       track_addr, 
                       track_len);

    /* Commit to memory */
    freer = (pfmgr_free_range_t*)pagemgr_boot_temp_map(track_addr);
    memcpy(freer, &local_freer, sizeof(pfmgr_free_range_t));
    
    base.freer.count++;
    init->prev = track_addr;
        
}
/* pfmgr_early_init_busy_callback - initialize busy ranges using boot page tables */

static void pfmgr_init_busy_callback(memory_map_entry_t *e, void *pv)
{
    pfmgr_init_t *init = pv;    
    pfmgr_busy_range_t *busy = NULL;
    phys_addr_t  addr = 0;

    if(e->type == MEMORY_USABLE)
        return;
    
    addr = base.physb_start + 
           base.busyr.count * 
           sizeof(pfmgr_busy_range_t);

    /* Link the previous entry with this one */
    if(init->prev)
    {
        busy = (pfmgr_busy_range_t*)pagemgr_boot_temp_map(init->prev);
        busy->hdr.node.next = (list_node_t *)addr;
        init->prev = 0;
    }

    if(base.physb_start == 0)
        base.physb_start = ALIGN_UP(_KERNEL_LMA_END, PAGE_SIZE);

    busy = (pfmgr_busy_range_t*)pagemgr_boot_temp_map(base.physb_start + 
                                 base.busyr.count * 
                                 sizeof(pfmgr_busy_range_t));
    
    memset(busy, 0, sizeof(pfmgr_busy_range_t));

    busy->hdr.base       = e->base;
    busy->hdr.domain_id  = e->domain;
    busy->hdr.len        = e->length;
    busy->hdr.type       = e->type;
    busy->hdr.struct_len = sizeof(pfmgr_busy_range_t);

    init->prev = base.physb_start + 
                 base.busyr.count * 
                 sizeof(pfmgr_busy_range_t);

    base.busyr.count++;
    kprintf("%s -  RANGE START 0x%x LENGTH 0x%x END 0x%x\n",__FUNCTION__,e->base, e->length, e->base + e->length);
}

/* pfmgr_early_alloc_pf - early allocator for page frame using boot page tables */

int pfmgr_early_alloc_pf(phys_size_t pf, uint8_t flags, alloc_cb cb, void *pv)
{
    pfmgr_free_range_t *freer     = NULL;
    phys_addr_t freer_phys  = 0;
    phys_addr_t bmp_phys    = 0;
    virt_addr_t *bmp        = 0;
    phys_addr_t cb_phys     = 0;
    phys_size_t pf_pos      = 0;
    phys_size_t pf_ix       = 0;
    phys_size_t bmp_off     = 0;
    phys_size_t ret_pf      = 0;
    int         stop        = 0;
    pfmgr_free_range_t local_freer;

    freer_phys = base.physf_start;

    do
    {
        pf_pos = 0;
        bmp    = NULL;
        freer  = (pfmgr_free_range_t*)pagemgr_boot_temp_map(freer_phys);
        
        /* Get the physical address of the bitmap */
        bmp_phys = freer_phys + sizeof(pfmgr_free_range_t);

        /* save the structure locally */
        memcpy(&local_freer, freer, sizeof(pfmgr_free_range_t));

        /* U Can't Touch This */
        if(freer->hdr.base < LOW_MEMORY)
        {
            freer_phys = (phys_addr_t)local_freer.hdr.node.next;
            continue;
        }

        /* Find a free page and call the callback */
        while(pf_pos < local_freer.total_pf)
        {
            pf_ix = pf_pos % PF_PER_ITEM;
            bmp_off = (pf_pos / PF_PER_ITEM) * sizeof(virt_addr_t);

            cb_phys = local_freer.hdr.base + PAGE_SIZE * pf_pos;
            
            /* Skip ISA DMA - it also skips the boot page table */
            if(pfmgr_touches_range(0, 
                              ISA_DMA_MEMORY_LENGTH, 
                              cb_phys, 
                              PAGE_SIZE))
            {
                pf_pos++;
                continue;
            }

            if(pf_ix == 0 || bmp == NULL)
                bmp = (virt_addr_t*)pagemgr_boot_temp_map(bmp_phys + bmp_off);
            
            if((bmp[0] & ((virt_addr_t)1 << pf_ix)) == 0)
            {
                stop = !cb(cb_phys, 1, pv);

                if(stop == 1)
                    break;

                bmp[0] |= ((virt_addr_t)1 << pf_ix);

                local_freer.avail_pf--;
                ret_pf++;
            }

            pf_pos++;
        }

        /* Commit to memory */
        freer = (pfmgr_free_range_t*)pagemgr_boot_temp_map(freer_phys);
        memcpy(freer, &local_freer, sizeof(pfmgr_free_range_t));

        /* advance */
        freer_phys = (phys_addr_t)local_freer.hdr.node.next;

        if(stop)
        {
            return(0);
        }
        
    }while(freer_phys != 0);

    return(-1);
}

/* pfmgr_lkup_bmp_for_free_pf - helper routine that looks for free pages 
 *
 * This routine will look in the bitmap represented by the pfmgr_free_range_t
 * and will return the requested amount of page frames specified in pf that starts
 * at start address
 * 
 */

static int pfmgr_lkup_bmp_for_free_pf
(
    pfmgr_free_range_t *freer,
    phys_addr_t        *start,
    phys_size_t        *pf
)
{
    pfmgr_range_header_t *hdr = NULL;
    phys_addr_t pf_count   = 0;
    phys_addr_t start_addr = 0;
    phys_size_t pf_ix              = 0;
    phys_size_t pf_pos             = 0;
    phys_size_t bmp_pos            = 0;
    phys_size_t mask_frames        = 0;
    phys_size_t mask               = 0;
    phys_size_t pf_ret             = 0;
    phys_size_t req_pf             = 0;
    uint8_t     stop               = 0;
    int         status             = 0;

    /* Check if there's anything interesting here */
    if(freer->avail_pf == 0)
        return(-1);
    
    
    hdr        = &freer->hdr;
    start_addr = *start;
    req_pf     = *pf;

    if(!pfmgr_in_range(hdr->base, hdr->len, start_addr,0))
    {
        start_addr = hdr->base;
    }
    
    /* Skip ISA DMA */
    if(pfmgr_touches_range(0, 
                           ISA_DMA_MEMORY_LENGTH, 
                           start_addr,
                           req_pf * PAGE_SIZE))
    {
       
        /* Start after ISA DMA */
        start_addr = ISA_DMA_MEMORY_LENGTH;
    }

    pf_pos = (start_addr - hdr->base) / PAGE_SIZE;
    

    while((pf_pos < freer->total_pf) && (req_pf > 0))
    {
        pf_ix       = pf_pos % PF_PER_ITEM;
        bmp_pos     = pf_pos / PF_PER_ITEM;
        mask        = 0;
        mask_frames = min(PF_PER_ITEM - pf_ix, PF_PER_ITEM);
        mask_frames = min(mask_frames, req_pf);
        /* Compute the mask */

        if(mask_frames != PF_PER_ITEM)
        {
            for(phys_size_t i = 0; i < mask_frames; i++)
            {
                mask |= ((virt_addr_t)1 << (pf_ix + i));            
            }
        }
        else
        {
            mask = ~(virt_addr_t)0;
        }
#if 0
        if((1ull<<mask_frames + pf_ix) - (1ull << pf_ix) != mask)
        {
            kprintf("MASK_FRAMES %d - %d\n",mask_frames,pf_ix);
            kprintf("IT'sDIFFERENT 0x%x 0x%x\n", mask, (1ull<<mask_frames + pf_ix) - (1ull << pf_ix) );
            while(1);
        }
#endif
        if((freer->bmp[bmp_pos] & mask) == 0)
        {
            if(pf_ret == 0)
                start_addr = hdr->base + pf_pos * PAGE_SIZE;

            pf_ret += mask_frames;
            pf_pos += mask_frames;
            req_pf -= mask_frames;
        }
        else
        {
          
            for(phys_size_t i = 0; i < mask_frames; i++)
            {
                mask = ((virt_addr_t)1 << (pf_ix + i));     
                
                if((freer->bmp[bmp_pos] & mask) == 0)
                {
                    if(pf_ret == 0)
                    {
                        start_addr = hdr->base + pf_pos * PAGE_SIZE;
                    }
                    pf_ret ++;
                    pf_pos ++;
                    req_pf --;
                }
                else
                {
                    break;
                }
            }

            if(pf_ret != 0)
                break;
            
            pf_pos++;
        }
    }

    if(pf_ret != req_pf)
        status = -1;
    else
        status = 0;

    *start = start_addr;
    *pf    = pf_ret;

    return(status);

}

/* pfmgr_mark_bmp - marks page frames entries as busy */

/* TODO: MAKE ALLOCATION EVEN FASTER */
static int pfmgr_mark_bmp
(
    pfmgr_free_range_t *freer, 
    phys_addr_t addr,
    phys_size_t pf
)
{
    phys_size_t length      = 0;
    phys_size_t bmp_pos     = 0;
    phys_size_t pf_pos      = 0;
    phys_size_t pf_ix       = 0;
    phys_size_t mask        = 0;
    phys_size_t mask_frames = 0;

    /* Calculate the starting position */
    pf_pos = (addr - freer->hdr.base) / PAGE_SIZE;

    while((pf_pos < freer->total_pf) && 
          (freer->avail_pf > 0)      && 
          (pf > 0))
    {
        pf_ix   = pf_pos % PF_PER_ITEM;
        bmp_pos = pf_pos / PF_PER_ITEM;
        mask        = 0;
        mask_frames = min(PF_PER_ITEM - pf_ix, PF_PER_ITEM);
        mask_frames = min(mask_frames, pf);

        /* Compute the mask */

        if(mask_frames != PF_PER_ITEM)
        {
            for(phys_size_t i = 0; i < mask_frames; i++)
            {
                mask |= ((virt_size_t)1 << (i + pf_ix));
            }
        }
        else
        {
            mask = ~(virt_size_t)0;
        }


        if((freer->bmp[bmp_pos] & mask) == 0)
        {
            freer->bmp[bmp_pos] |= mask;
            pf_pos += mask_frames ;
            pf     -= mask_frames;
            freer->avail_pf-= mask_frames;
        }
        else
        {
            kprintf("%s %d: Wait, that's illegal\n",__FUNCTION__,__LINE__);
            break;
        }
    }

    if(pf > 0)
    {
        kprintf("PF %d pf_pos %d total_pf %d avail_pf %d\n", pf,pf_pos, freer->total_pf, freer->avail_pf);
    }

    return(pf > 0 ? -1 : 0);
}

/* pfmgr_clear_bmp - marks page frames entries as free */

/* TODO: MAKE FREEING EVEN FASTER */
static int pfmgr_clear_bmp
(
    pfmgr_free_range_t *freer, 
    phys_addr_t addr,
    phys_size_t pf
)
{
    phys_size_t length  = 0;
    phys_size_t bmp_pos = 0;
    phys_size_t pf_pos  = 0;
    phys_size_t pf_ix   = 0;
    phys_size_t mask    = 0;
    phys_size_t mask_frames = 0;

    pf_pos = (addr - freer->hdr.base) / PAGE_SIZE;

    while((pf_pos < freer->total_pf)          && 
          (freer->avail_pf < freer->total_pf) &&
          (pf > 0))
    {
        pf_ix   = pf_pos % PF_PER_ITEM;
        bmp_pos = pf_pos / PF_PER_ITEM;
        mask        = 0;
        mask_frames = min(PF_PER_ITEM - pf_ix, PF_PER_ITEM);
        mask_frames = min(mask_frames, pf);

        /* Compute the mask */

        if(mask_frames != PF_PER_ITEM)
        {
            for(phys_size_t i = 0; i < mask_frames; i++)
            {
                mask |= ((virt_size_t)1 << (i + pf_ix));
            }
        }
        else
        {
            mask = ~(virt_size_t)0;
        }
    

        if((freer->bmp[bmp_pos] & mask))
        {
            freer->bmp[bmp_pos] &= ~mask;
            pf_pos += mask_frames ;
            pf     -= mask_frames;
            freer->avail_pf += mask_frames;
        }
        else
        {
           kprintf("%s %d: Wait, that's illegal\n",__FUNCTION__,__LINE__);
           break;
        }
    }

    return(pf > 0 ? -1 : 0);
}

/* pfmgr_alloc - allocates page frames */

static int pfmgr_alloc(phys_size_t pf, uint8_t flags, alloc_cb cb, void *pv)
{
    pfmgr_free_range_t *freer      = NULL;
    pfmgr_free_range_t *next_freer = NULL;
    phys_addr_t addr               = 0;
    phys_size_t pf_ix              = 0;
    phys_size_t pf_pos             = 0;
    phys_size_t bmp_pos            = 0;
    phys_size_t avail_pf           = 0;
    phys_size_t req_pf             = 0;
    phys_size_t used_pf            = 0;
    int         lkup_sts           = 0;

    freer = (pfmgr_free_range_t*)linked_list_first(&base.freer);
    req_pf = pf;
    
    while(freer)
    {
        if(req_pf == 0)
        {
            if(flags & ALLOC_CB_STOP)
                req_pf = pf;
            else
                return(0);
        }
        
        next_freer = (pfmgr_free_range_t*)linked_list_next((list_node_t*)freer);
        
        if(freer->hdr.base  < LOW_MEMORY)
        {
            freer = next_freer;
            continue;
        }
        
        avail_pf   = req_pf;    
        lkup_sts   = pfmgr_lkup_bmp_for_free_pf(freer, &addr, &avail_pf);
        used_pf    = 0;

        /* Contiguous pages should be satisfied from one lookup */
        if((flags & ALLOC_CONTIG) && lkup_sts != 0)
        {
            addr += avail_pf * PAGE_SIZE;
            
            if(avail_pf == 0)
                freer = next_freer;

            continue;
        }

        if(avail_pf > 0)
        {
            used_pf = cb(addr, avail_pf, pv);
            
            if(used_pf == 0)
                return(0);

            /* decrease the required pfs */
            req_pf -= used_pf;
        
            if(pfmgr_mark_bmp(freer, addr, used_pf))
            {
                return(-1);
            }
        }

        /* Check if we got everything from this shot */
        if(lkup_sts == 0)
            break;

        /* advance the starting address - help the lookup */
        addr += used_pf * PAGE_SIZE;
        
        /* If we got frames, try again on the same range */
        if(avail_pf != 0)
            continue;
        
        freer = next_freer;
    }

    if(req_pf != 0)
        return(-1);

    return(0);
}

/* pfmgr_free - obtains the free range using addr and pf count*/

static int pfmgr_addr_to_free_range
(
    phys_addr_t addr, 
    phys_size_t pf,
    pfmgr_free_range_t **pfreer
)
{
    phys_size_t         length     = 0;
    pfmgr_free_range_t *freer      = NULL;
    pfmgr_free_range_t *next_freer = NULL;

    length = pf * PAGE_SIZE;

    freer = (pfmgr_free_range_t*)linked_list_first(&base.freer);

    while(freer)
    {
        next_freer = (pfmgr_free_range_t*)linked_list_next((list_node_t*)freer);

        if(pfmgr_in_range(freer->hdr.base, 
                          freer->hdr.len,
                          addr, 
                          length))
        {
            *pfreer = freer;
            return(0);
        }

        freer = next_freer;
    }

    return(-1);
}

/* pfmgr_free - frees page frames */

static int pfmgr_free(free_cb cb, void *pv)
{
    pfmgr_free_range_t *freer      = NULL;
    phys_addr_t addr               = 0;
    phys_size_t to_free_pf         = 0;
    int         again              = 0;
    int         err                = 0;

    do
    {
        addr = 0;
        to_free_pf = 0;
        again = cb(&addr, &to_free_pf, pv);
        
        /* Get the range */
        if(pfmgr_addr_to_free_range(addr, to_free_pf, &freer) != 0)
        {
            kprintf("EXIT 0x%x - %d\n",addr, to_free_pf);
            /* WTF is this? */
            return(-1);
        }
        /* Find free range */
       
        if(pfmgr_clear_bmp(freer, addr, to_free_pf) !=0)
        {
             kprintf("TO_FREE_PF 0x%x\n",addr);
            err = -1;
        }

    }while(again);
    
    return(err);
}
/* pfmgr_early_init - initializes tracking information */
void pfmgr_early_init(void)
{
    pfmgr_init_t init;

    memset(&init, 0, sizeof(pfmgr_init_t));
    mem_map_iter(pfmgr_init_busy_callback, &init);

    memset(&init, 0, sizeof(pfmgr_init_t));
    mem_map_iter(pfmgr_init_free_callback, &init);

    memset(&pfmgr_interface, 0, sizeof(pfmgr_t));
    pfmgr_interface.alloc = pfmgr_early_alloc_pf;

    kprintf("KERNEL_BEGIN 0x%x KERNEL_END 0x%x\n",_KERNEL_LMA, _KERNEL_LMA_END);
   
}

/* pfmgr_init - initialize runtime structures for the page frame allocator */
int pfmgr_init(void)
{
    phys_addr_t     phys = 0;
    phys_addr_t     next = 0;
    pfmgr_range_header_t *hdr = NULL;

    phys = base.physf_start;
    linked_list_init(&base.freer);
    kprintf("Initializing Page Frame Manager\n");

    do
    {
        hdr = (pfmgr_range_header_t*)pagemgr_boot_temp_map(phys);
        hdr = vmmgr_map(NULL, phys, 0, hdr->struct_len, VMM_ATTR_NO_CACHE |
                                                        VMM_ATTR_WRITABLE |
                                                        VMM_ATTR_WRITE_THROUGH);

        if(hdr == NULL)
            return(-1);

        phys = (phys_addr_t)hdr->node.next;

        linked_list_add_tail(&base.freer, &hdr->node);

    }while(phys != 0);

    phys = base.physb_start;
    hdr = NULL;
    linked_list_init(&base.busyr);

    do
    {
        if((phys == base.physb_start)                    || 
          (((phys_addr_t)hdr->node.next - base.physb_start % PAGE_SIZE) == 0))
        {
            hdr = (pfmgr_range_header_t*)pagemgr_boot_temp_map(phys);
            hdr = vmmgr_map(NULL, phys, 0, hdr->struct_len, VMM_ATTR_NO_CACHE |
                                                            VMM_ATTR_WRITABLE |
                                                            VMM_ATTR_WRITE_THROUGH);

            if(hdr == NULL)
                return(-1);
        }
        else
        {
            /* advance manually */
            hdr = (pfmgr_range_header_t*)((uint8_t*)hdr + sizeof(pfmgr_busy_range_t));
        }

      phys = (phys_addr_t)hdr->node.next;
      linked_list_add_tail(&base.busyr, &hdr->node);

    }while(phys != 0);

    pfmgr_interface.alloc   = pfmgr_alloc;
    pfmgr_interface.dealloc = pfmgr_free;

    return(0);
}

pfmgr_t *pfmgr_get(void)
{
    return(&pfmgr_interface);
}
