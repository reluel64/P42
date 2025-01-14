/* page frame allocator 
 * Part of P42
 */

#include <linked_list.h>
#include <pfmgr.h>
#include <utils.h>
#include <memory_map.h>
#include <pgmgr.h>
#include <vm.h>

#define PFMGR_FOUND (0)
#define PFMGR_FOUND_MORE (1)
#define PFMGR_FOUND_NONE (-1)


struct pfmgr_init_data
{
    phys_addr_t busy_start;
    phys_size_t busy_len;
    phys_addr_t prev;
};

static struct pfmgr_base base;
static struct pfmgr pfmgr_interface;
static struct spinlock pfmgr_lock = SPINLOCK_INIT;

/* Tracking information = header + bitmap */
#define TRACK_LEN(x) (ALIGN_UP((sizeof(struct pfmgr_free_range)) + \
                               BITMAP_SIZE_FOR_AREA((x)), PAGE_SIZE))

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
    {
        limit = len - 1;
    }

    if(req_len >= 1)
    {
        req_limit = req_len - 1;
    }

    req_end = req_base + req_limit;
    end     = base     + limit;

    if(base >= req_base)
    {
        if((req_end <= end && req_end >= base) || (req_end >= end))
        {
            return(1);
        }
    }
    else if(req_end >= end)
    {
        if((base <= req_base && req_base <= end) || (base >= req_base))
        {
            return(1);
        }
    }
    else if(req_base >= base && req_end <= end)
    {
        return(1);
    }

    return(0);
}

static virt_addr_t pfmgr_early_map
(
    phys_addr_t addr
)
{
    phys_addr_t pad = 0;
    virt_addr_t vaddr = 0;

    pad = addr % PAGE_SIZE;
  
    /* map 8K */
    vaddr = pgmgr_temp_map(addr - pad, 510);
    if(vaddr != VM_INVALID_ADDRESS)
    {
        pgmgr_temp_map(addr - pad + PAGE_SIZE, 511);
        return(vaddr + pad);
    }

    return(VM_INVALID_ADDRESS);
}

/* pfmgr_early_clear_bitmap - clear bitmap area using early page tables */

static void pfmgr_early_clear_bitmap
(
    struct pfmgr_free_range *fmem, 
    phys_addr_t bmp_phys
)
{
    virt_addr_t *bmp = NULL;
    phys_size_t zlen = 0;
    phys_size_t pos  = 0;
    phys_size_t bmp_len = 0;
    
    bmp_len = fmem->hdr.struct_len - sizeof(struct pfmgr_free_range);

    while(pos < bmp_len)
    {
        zlen = min(bmp_len - pos, PAGE_SIZE);

        bmp = (virt_addr_t*)pfmgr_early_map(bmp_phys + pos);

        if(bmp != (virt_addr_t*)VM_INVALID_ADDRESS)
        {
            memset(bmp, 0, zlen);

            pos += zlen;
        }
        else
        {
            kprintf("INVALID ADDRESS\n");
        }
    }
}

/* pfmgr_early_mark_bitmap - mark bitmap using early page table */

static void pfmgr_early_mark_bitmap
(
    struct pfmgr_free_range *fmem, 
    phys_addr_t bmp_phys, 
    phys_addr_t addr, 
    phys_size_t len
)
{
    virt_addr_t *bmp    = NULL;
    phys_size_t bmp_off = 0;
    phys_size_t pf_ix   = 0;
    phys_size_t pf_pos  = 0;
    phys_size_t pos     = 0;

#ifdef PFMGR_EARLY_DEBUG
    kprintf("bmp_phys %x\n",bmp_phys);
#endif

    while(pos < len && fmem->avail_pf > 0)
    {
        pf_pos = ((addr + pos) - fmem->hdr.base) / PAGE_SIZE;
        pf_ix = pf_pos % PF_PER_ITEM;
        bmp_off = (pf_pos / PF_PER_ITEM) * sizeof(virt_addr_t);

        if(pf_ix == 0 || bmp == NULL)
        {
            bmp = (virt_addr_t*)pfmgr_early_map(bmp_phys + bmp_off);
        }

        if((virt_addr_t)bmp != VM_INVALID_ADDRESS)
        {
            bmp[0] |= ((virt_addr_t)1 << pf_ix);
            pos += PAGE_SIZE;
            fmem->avail_pf--;
        }
    }
}

/* pfmgr_early_init_free_callback - initialize free ranges using boot page tables */

static void pfmgr_init_free_callback
(
    struct memory_map_entry *e, 
    void *pv
)
{
    struct pfmgr_init_data       *init = NULL;
    struct pfmgr_free_range *freer = NULL;
    struct pfmgr_free_range local_freer;
    phys_addr_t        track_addr = 0;
    phys_addr_t        track_len = 0;

    init = pv;

    if(e->type != MEMORY_USABLE || 
      !(e->flags & MEMORY_ENABLED))
    {
        return;
    }

    memset(&local_freer, 0, sizeof(struct pfmgr_free_range));

    track_len = TRACK_LEN(e->length);
    track_addr = ALIGN_DOWN(e->base + (e->length - track_len), PAGE_SIZE);

    /* Link the previous entry with this one */
    if(init->prev)
    {
        freer = (struct pfmgr_free_range*)pfmgr_early_map(init->prev);
        if((virt_addr_t)freer != VM_INVALID_ADDRESS)
        {
            freer->hdr.next_range = track_addr;
        }
        else
        {
            kprintf("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
        }
        init->prev = 0;
    }
    
    if(base.physf_start == 0)
    {
        base.physf_start = track_addr;
    }

    local_freer.hdr.base      = e->base;
    local_freer.hdr.len       = e->length;
   // local_freer.hdr.domain_id = e->domain; 
    local_freer.hdr.type      = e->type;
    

    local_freer.hdr.struct_len = track_len;
    local_freer.total_pf       = BYTES_TO_PF(ALIGN_DOWN(e->length - track_len, 
                                             PAGE_SIZE));
    local_freer.avail_pf       = local_freer.total_pf;

#ifdef PFMGR_EARLY_DEBUG
    kprintf("%s -  RANGE START 0x%x LENGTH 0x%x END 0x%x\n",
            __FUNCTION__,
            e->base, 
            e->length, 
            e->base + e->length);


    kprintf("TRACKING START 0x%x LENGTH 0x%x\n",track_addr, track_len);
#endif

    pfmgr_early_clear_bitmap(&local_freer, 
                              track_addr + offsetof(struct pfmgr_free_range, bmp));

    if(pfmgr_in_range(e->base, e->length, _KERNEL_LMA,_KERNEL_IMAGE_LEN))
    {
        pfmgr_early_mark_bitmap(&local_freer,
                           track_addr + offsetof(struct pfmgr_free_range, bmp),
                           _KERNEL_LMA, 
                           _KERNEL_IMAGE_LEN);
#ifdef PFMGR_EARLY_DEBUG               
        kprintf("MARKED KERNEL %x -> %x\n",_KERNEL_LMA, _KERNEL_LMA_END);
#endif
    }

    if(pfmgr_in_range(e->base, e->length, base.physb_start, 
                      base.busyr.count * sizeof(struct pfmgr_busy_range)))
    {
        pfmgr_early_mark_bitmap(&local_freer,
                                track_addr + offsetof(struct pfmgr_free_range, bmp),
                                base.physb_start, 
                                base.busyr.count * sizeof(struct pfmgr_busy_range));
#ifdef PFMGR_EARLY_DEBUG
                           kprintf("MARKED BUSY RANGE\n");
#endif
    }

    pfmgr_early_mark_bitmap(&local_freer,
                       track_addr + offsetof(struct pfmgr_free_range, bmp),
                       track_addr, 
                       track_len);

#ifdef PFMGR_EARLY_DEBUG
    kprintf("MARKED FREE_RANGE\n");
#endif
    /* Commit to memory */
    freer = (struct pfmgr_free_range*)pfmgr_early_map(track_addr);
    
    if((virt_addr_t)freer != VM_INVALID_ADDRESS)
    {
        memcpy(freer, &local_freer, sizeof(struct pfmgr_free_range));
    }
    else
    {
        kprintf("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
    }

    base.freer.count++;
    init->prev = track_addr;
        
}

/* pfmgr_early_init_busy_callback - initialize busy ranges using boot page tables */

static void pfmgr_init_busy_callback
(
    struct memory_map_entry *e, 
    void *pv
)
{
    struct pfmgr_init_data *init = pv;    
    struct pfmgr_busy_range *busy = NULL;
    phys_addr_t  addr = 0;

    if(e->type == MEMORY_USABLE)
    {
        return;
    }

    addr = base.physb_start + 
           base.busyr.count * 
           sizeof(struct pfmgr_busy_range);

    
    /* Link the previous entry with this one */
    if(init->prev)
    {
        busy = (struct pfmgr_busy_range*)pfmgr_early_map(init->prev);

        if((virt_addr_t)busy != VM_INVALID_ADDRESS)
        {
           busy->hdr.next_range = addr;
        }
 
        init->prev = 0;
    }

    if(base.physb_start == 0)
    {
        base.physb_start = ALIGN_UP(_KERNEL_LMA_END, PAGE_SIZE);
    }

    busy = (struct pfmgr_busy_range*)pfmgr_early_map(base.physb_start + 
                                 base.busyr.count * 
                                 sizeof(struct pfmgr_busy_range));

    if((virt_addr_t)busy != VM_INVALID_ADDRESS)
    {
        memset(busy, 0, sizeof(struct pfmgr_busy_range));

        busy->hdr.base       = e->base;
        //busy->hdr.domain_id  = e->domain;
        busy->hdr.len        = e->length;
        busy->hdr.type       = e->type;
        busy->hdr.struct_len = sizeof(struct pfmgr_busy_range);
    }
    else
    {
        kprintf("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
    }
    init->prev = base.physb_start + 
                 base.busyr.count * 
                 sizeof(struct pfmgr_busy_range);

    base.busyr.count++;

#ifdef PFMGR_EARLY_DEBUG
    kprintf("%s -  RANGE START 0x%x LENGTH 0x%x END 0x%x\n",
            __FUNCTION__,
            e->base, 
            e->length, 
            e->base + e->length);
#endif
    
}

/* pfmgr_early_alloc_pf - early allocator for page frame using boot page tables */

int pfmgr_early_alloc_pf
(
    phys_addr_t start,
    phys_size_t pf, 
    uint8_t flags, 
    alloc_cb cb, 
    void *pv
)
{
    struct pfmgr_free_range *freer     = NULL;
    phys_addr_t freer_phys  = 0;
    phys_addr_t bmp_phys    = 0;
    virt_addr_t *bmp        = 0;
    phys_addr_t cb_phys     = 0;
    phys_size_t pf_pos      = 0;
    phys_size_t pf_ix       = 0;
    phys_size_t bmp_off     = 0;
    phys_size_t ret_pf      = 0;
    int         cb_sts      = 0;
    struct pfmgr_free_range local_freer;
    struct pfmgr_cb_data cb_dat = {.avail_bytes = 0,
                              .phys_base  = 0,
                              .used_bytes = 0
                             };

    spinlock_lock(&pfmgr_lock);

    freer_phys = base.physf_start;
   
    do
    {
        pf_pos = 0;
        bmp    = NULL;
        freer  = (struct pfmgr_free_range*)pfmgr_early_map(freer_phys);
        
        if((virt_addr_t)freer == VM_INVALID_ADDRESS)
        {
            spinlock_unlock(&pfmgr_lock);
            return(-1);
        }

        /* Get the physical address of the bitmap */
        bmp_phys = freer_phys + sizeof(struct pfmgr_free_range);
       
        /* save the structure locally */
        memcpy(&local_freer, freer, sizeof(struct pfmgr_free_range));
        
        /* U Can't Touch This */
        if(local_freer.hdr.base < LOW_MEMORY)
        {
            freer_phys = (phys_addr_t)local_freer.hdr.next_range;  
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
            {
                bmp = (virt_addr_t*)pfmgr_early_map(bmp_phys + bmp_off);

                if((virt_addr_t)freer == VM_INVALID_ADDRESS)
                {
                    kprintf("Failed to aquire bitmap\n");
                    spinlock_unlock(&pfmgr_lock);
                    return(-1);
                }

            }

            if((bmp[0] & ((virt_addr_t)1 << pf_ix)) == 0)
            {
                cb_dat.avail_bytes = PAGE_SIZE;
                cb_dat.phys_base = cb_phys;
                cb_dat.used_bytes = 0;
                cb_sts = cb(&cb_dat, pv);

                /* If we got an error, do not mark the bitmap */
                if(cb_sts < 0)
                {
                    break;
                }

                bmp[0] |= ((virt_addr_t)1 << pf_ix);

                local_freer.avail_pf--;
                ret_pf++;

                /* We're done, bail out */
                if(cb_sts == 0)
                {
                    break;
                }
            }

            pf_pos++;
        }

        /* Commit to memory */
        freer = (struct pfmgr_free_range*)pfmgr_early_map(freer_phys);
        if((virt_addr_t)freer != VM_INVALID_ADDRESS)
        {
            memcpy(freer, &local_freer, sizeof(struct pfmgr_free_range));
        }
        else
        {
            kprintf("Failed to commit memory\n");
            spinlock_unlock(&pfmgr_lock);
            return(-1);
        }

        /* advance */
        freer_phys = (phys_addr_t)local_freer.hdr.next_range;

        if(cb_sts == 0)
        {
            spinlock_unlock(&pfmgr_lock);
            return(0);
        }
        
    }while(freer_phys != 0);

    spinlock_unlock(&pfmgr_lock);

    return(-1);
}

/* pfmgr_lkup_bmp_for_free_pf - helper routine that looks for free pages 
 *
 * This routine will look in the bitmap represented by the struct pfmgr_free_range
 * and will return the requested amount of page frames specified in pf that starts
 * at start address
 * 
 */

static int pfmgr_lkup_bmp_for_free_pf
(
    struct pfmgr_free_range *freer,
    phys_addr_t        *start,
    phys_size_t        *pf,
    uint32_t           flags
)
{
    struct pfmgr_range_header *hdr = NULL;
    phys_addr_t start_addr    = 0;
    phys_size_t pf_ix         = 0;
    phys_size_t pf_pos        = 0;
    phys_size_t bmp_pos       = 0;
    
    phys_size_t mask          = 0;
    phys_size_t pf_ret        = 0;
    phys_size_t req_pf        = 0;
    phys_size_t mask_frames   = 0;
    int         status        = PFMGR_FOUND_NONE;
    uint8_t     stop          = 0;

    /* Check if there's anything interesting here */
    if(freer->avail_pf == 0)
    {
        return(status);
    }
    
    hdr        = &freer->hdr;
    start_addr = *start;
    req_pf     = *pf;
    
    if(!pfmgr_in_range(hdr->base, hdr->len, start_addr,0))
    {
        return(-1);
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
 
    pf_pos = BYTES_TO_PF(start_addr - hdr->base);

    while(pf_pos < freer->total_pf &&
          (req_pf > pf_ret) && 
          (pf_ret < freer->avail_pf) && !stop)
    {

        pf_ix       = POS_TO_IX(pf_pos);
        bmp_pos     = BMP_POS(pf_pos);
        mask        = ~(virt_addr_t)0;

        /* No more that PF_PER_ITEM */
        mask_frames = min(PF_PER_ITEM - pf_ix, PF_PER_ITEM);
        
        /* No more than required frames */
        mask_frames = min(mask_frames, req_pf - pf_ret);
        
        /* No more than available frames */
        mask_frames = min(mask_frames, freer->avail_pf - pf_ret);
        
        /* 
         * Check if we have PF_PER_ITEM from one shot 
         * Otherwise we must check every bit
         */ 

        if(mask_frames < PF_PER_ITEM)
        {
            mask = (1ull << mask_frames) - 1;
        }

        if((freer->bmp[bmp_pos] & mask) == 0)
        {
            if(pf_ret == 0)
            {
                start_addr = hdr->base + PF_TO_BYTES(pf_pos);
            }
            pf_ret += mask_frames;
            pf_pos += mask_frames;
        }
        else if(freer->bmp[bmp_pos] != ~(virt_addr_t)0)
        {
            /* Slower path - check each page frame */
            for(phys_size_t i = 0; i < mask_frames; i++)
            {
                /* check if we might go over the total pf */
                if((pf_pos >= freer->total_pf))
                {
                    break;
                }

                mask = ((virt_addr_t)1 << (pf_ix + i)); 

                if((freer->bmp[bmp_pos] & mask) == 0)
                {
                    if(pf_ret == 0)
                    {
                        start_addr = hdr->base + PF_TO_BYTES(pf_pos);
                    }   
                    pf_ret ++;
                }

                /* Stop looking if pf_ret > 0 */
                else if(pf_ret > 0)
                {
                    /* if we want contigous memory, we must reset the
                     * the returned frames to keep looking
                     */
                    if(flags & PHYS_ALLOC_CONTIG)
                    {
                        pf_ret = 0;
                    }
                    else
                    {
                       stop = 1;
                       break;
                    }
                }
                pf_pos++;
            }
        }
        else
        {
            pf_pos += PF_PER_ITEM;
        }
    }

    /* No page frame available */
    if(pf_ret == 0)
    {
        status = PFMGR_FOUND_NONE;
    }
    /* page frames available but not as required */
    else if(pf_ret < req_pf)
    {
        status = PFMGR_FOUND;
    }
    /* page frames available at least as required */
    else
    {
        status = PFMGR_FOUND_MORE;
    }

    *start = start_addr;
    *pf    = pf_ret;
 
    return(status);
}



/* pfmgr_lkup_bmp_for_free_pf - helper routine that looks for free pages 
 *
 * This routine will look in the bitmap represented by the struct pfmgr_free_range
 * and will return the requested amount of page frames specified in pf that starts
 * at start address
 * 
 */

/* pfmgr_mark_bmp - marks page frames entries as busy */

/* TODO: MAKE ALLOCATION EVEN FASTER */
static int pfmgr_mark_bmp
(
    struct pfmgr_free_range *freer, 
    phys_addr_t addr,
    phys_size_t pf
)
{
    phys_size_t bmp_pos     = 0;
    phys_size_t pf_pos      = 0;
    phys_size_t pf_ix       = 0;
    phys_size_t mask        = 0;
    phys_size_t mask_frames = 0;
    
    /* Calculate the starting position */
    pf_pos = BYTES_TO_PF(addr - freer->hdr.base);

    while((pf_pos < freer->total_pf) && 
          (freer->avail_pf > 0)      && 
          (pf > 0))
    {
        pf_ix       = POS_TO_IX(pf_pos);
        bmp_pos     = BMP_POS(pf_pos);
        mask        = ~(virt_size_t)0;;

        /* Do not mark more than PF_PER_ITEM at a time */
        mask_frames = min(PF_PER_ITEM - pf_ix, PF_PER_ITEM);

        /* Do not mark more than remaining frames */
        mask_frames = min(mask_frames, pf);

        /* Do not mark more than available frames */
        mask_frames = min(mask_frames, freer->avail_pf);

        if(mask_frames < PF_PER_ITEM)
        {
            mask = (1ull << mask_frames) - 1;
        }

        if((freer->bmp[bmp_pos] & mask) == 0)
        {
            freer->bmp[bmp_pos] |= mask;
            pf_pos          += mask_frames ;
            pf              -= mask_frames;
            freer->avail_pf -= mask_frames;
        }
        else
        {
            /* Slower path - check each page frame */
            for(phys_size_t i = 0; i < mask_frames; i++)
            {
                mask = ((virt_addr_t)1 << (pf_ix + i));

                if((freer->bmp[bmp_pos] & mask) == 0)
                {
                    freer->bmp[bmp_pos] |= mask;
                    pf--;
                    freer->avail_pf--;
                }
                else
                {
                    kprintf("FATAL: %s %d\n", __FUNCTION__,__LINE__);
                    while(1);
                    break;
                }
                pf_pos ++;
            }
        }
    }

#ifdef PFMGR_DEBUG
    if(pf > 0)
    {
        kprintf("PF %d pf_pos %d total_pf %d avail_pf %d\n", 
                 pf,pf_pos, 
                 freer->total_pf, 
                 freer->avail_pf);
    }
#endif
    return(pf > 0 ? -1 : 0);
}

/* pfmgr_clear_bmp - marks page frames entries as free */

/* TODO: MAKE FREEING EVEN FASTER */
static int pfmgr_clear_bmp
(
    struct pfmgr_free_range *freer, 
    phys_addr_t         addr,
    phys_size_t         pf
)
{
    phys_size_t bmp_pos     = 0;
    phys_size_t pf_pos      = 0;
    phys_size_t pf_ix       = 0;
    phys_size_t mask        = 0;
    phys_size_t mask_frames = 0;
    uint8_t     stop        = 0;
    
    pf_pos = BYTES_TO_PF(addr - freer->hdr.base);
 
    while(!stop                               && 
          (pf_pos < freer->total_pf)          && 
          (freer->avail_pf < freer->total_pf) &&
          (pf > 0))
    {
        pf_ix       = POS_TO_IX(pf_pos);
        bmp_pos     = BMP_POS(pf_pos);
        mask        =  ~(virt_size_t)0;

        /* Do not clear more than PF_PER_ITEM */
        mask_frames = min(PF_PER_ITEM - pf_ix, PF_PER_ITEM);

        /* Do not clear more than remaining page frames */
        mask_frames = min(mask_frames, pf);
        
        /* Do not clear more than remining frames up to total */
        mask_frames = min(mask_frames, freer->total_pf - freer->avail_pf);

        if(mask_frames < PF_PER_ITEM)
        {
            mask  = (1ull << mask_frames) - 1;
        }

        if((freer->bmp[bmp_pos] & mask)  == freer->bmp[bmp_pos])
        {   
            freer->bmp[bmp_pos] &= ~mask;
            pf_pos          += mask_frames ;
            pf              -= mask_frames;
            freer->avail_pf += mask_frames;
        }
        else
        {
            for(phys_size_t i = 0; i < mask_frames; i++)
            {
                mask = ((virt_addr_t)1 << (pf_ix + i));

                if((freer->bmp[bmp_pos] & mask))
                {
                    freer->bmp[bmp_pos] &= ~mask;
                    pf--;
                    freer->avail_pf++;
                }
                else
                {
                    stop = 1;
                    break;
                }
                pf_pos ++;
            }
        }
    }
  
    return(pf > 0 ? -1 : 0);
}

int pfmgr_show = 0;
/* pfmgr_alloc - allocates page frames */
#if 0
static int pfmgr_check_available_memory
(
    phys_size_t req_pf
)
{
    struct list_node *fnode = NULL;
    fnode = linked_list_first(&base.freer);

    while(fnode)
    {
        
        fnode = linked_list_next(fnode);
    }

    return(0);
}
#endif

static int _pfmgr_alloc
(
    phys_addr_t start,
    phys_size_t pf, 
    uint8_t     flags, 
    alloc_cb    cb, 
    void       *pv
)
{
    struct pfmgr_free_range *free_range = NULL;
    struct list_node        *fnode      = NULL;
    struct list_node        *next_fnode = NULL;
    struct pfmgr_cb_data    cb_dat = {
                                 .avail_bytes = 0,
                                 .phys_base   = 0,
                                 .used_bytes  = 0
                                };
    phys_addr_t         addr       = 0;
    phys_size_t         avail_pf   = 0;
    phys_size_t         req_pf     = 0;
    phys_size_t         used_pf    = 0;
    int                 lkup_sts   = 0;
    int                 cb_status  = 0;
    virt_addr_t         alloc_len  = 0;

    spinlock_lock(&pfmgr_lock);
    
    /* If we require the highest memory possible, we should
     * start from the end to the beginning
     */
    
    fnode = (flags & PHYS_ALLOC_HIGHEST) ?  
            linked_list_last(&base.freer) :
            linked_list_first(&base.freer);

    req_pf = pf;
    
    while(fnode)
    {
        free_range = (struct pfmgr_free_range*) fnode;

        if(req_pf == 0)
        {
            if(flags & PHYS_ALLOC_CB_STOP)
            {
                req_pf = free_range->avail_pf;
            }
            else
            {
                spinlock_unlock(&pfmgr_lock);
                return(-1);
            }
        }
        
        /* If we require the highest memory possible, we 
         * should traverse the list in reverse
         */
        next_fnode = (flags & PHYS_ALLOC_HIGHEST) ? 
                     linked_list_prev(fnode) :
                     linked_list_next(fnode);

        /* Don't do stuff in the lower memory */
        if(free_range->hdr.base  < LOW_MEMORY)
        {
            fnode = next_fnode;
            continue;
        }
        
        /* next_lkup should not be bigger than total_fp
         * if it happens otherwise, stop everything
         */
        if(free_range->next_lkup > free_range->total_pf)
        {
            kprintf("%s %d -> next_lkup (%x)> total_pf (%x)\n",
                    __FUNCTION__,
                    __LINE__ , 
                    free_range->next_lkup, 
                    free_range->total_pf);
            while(1);
        }
        else if (free_range->next_lkup == free_range->total_pf)
        {
            free_range->next_lkup = 0;
        }

        if(flags & PHYS_ALLOC_CONTIG)
        {
            /* Just a small check to see at least if we have the 
             * enough available frames. We will check later 
             * if they are contiguous
             */ 
            if(free_range->avail_pf < req_pf)
            {
                fnode = next_fnode;
                continue;
            }

            /* On contig allocations, we want to search the entire range so
             * we start the lookup from the beginning 
             */

            free_range->next_lkup = 0;
        }
        /* help the lookup a bit by starting the lookup from the previously
         * known to be free address
         */
        if(flags & PHYS_ALLOC_PREFERED_ADDR)
        {
            addr = start;
        }
        else
        {
            addr = free_range->hdr.base + 
                    PF_TO_BYTES(free_range->next_lkup);
        }
        avail_pf   = req_pf;   
        used_pf    = 0; 
#if 0
        kprintf("BASE 0x%x %d\n",free_range->hdr.base, free_range->avail_pf);
        kprintf("PRE_LKUP: 0x%x -> %d\n",addr, avail_pf);   
#endif
        /* Look for available page frames within range */
        lkup_sts   = pfmgr_lkup_bmp_for_free_pf(free_range, 
                                                &addr, 
                                                &avail_pf, 
                                                flags);
#if 0
       kprintf("PRE_POST: 0x%x -> 0x%x\n",addr, avail_pf);                      
#endif
        /* Check if we've got anything from the lookup */
        if(lkup_sts == PFMGR_FOUND_NONE)
        {
            /* Check if next_lkup > 0 and if it is, 
             * try again with it set to 0 
             * Also, if we preffer an address, try to satisfy only that
             */
            if(free_range->next_lkup > 0 && (~flags & PHYS_ALLOC_PREFERED_ADDR))
            {
                free_range->next_lkup = 0;
                addr                  = free_range->hdr.base;
                avail_pf              = req_pf;
                lkup_sts              = pfmgr_lkup_bmp_for_free_pf(free_range, 
                                                                  &addr, 
                                                                  &avail_pf,
                                                                  flags);
            }
            
            if(lkup_sts == PFMGR_FOUND_NONE)
            {
                fnode = next_fnode;
                continue;   
            }
        }
        
        /* Always update the next lookup */
        free_range->next_lkup = BYTES_TO_PF((addr - free_range->hdr.base)) +
                                avail_pf;
     
        /* Contiguous pages should be satisfied from one lookup */
        if((flags & PHYS_ALLOC_CONTIG) && lkup_sts < PFMGR_FOUND_MORE)
        {
            fnode = next_fnode;
            continue;
        }
    
        if(avail_pf > 0)
        {
            cb_dat.used_bytes  = 0;
            cb_dat.phys_base   = addr;
            cb_dat.avail_bytes = PF_TO_BYTES(avail_pf);
#ifdef PFMGR_DEBUG            
            kprintf("BASE %x AVAILABLE %x\n",cb_dat.phys_base, cb_dat.avail_bytes);
#endif
            cb_status = cb(&cb_dat, pv);
#ifdef PFMGR_DEBUG
            kprintf("ALLOC BASE 0x%x LEN 0x%x\n",addr, cb_dat.used_bytes);
#endif            
            /* 
             * If there is an error detected, we have to bail out
             */

            if(cb_status < 0)
            {
                spinlock_unlock(&pfmgr_lock);
                return(-1);
            }
            
            /* decrease the required pfs */
            alloc_len  += cb_dat.used_bytes;
            used_pf = BYTES_TO_PF(cb_dat.used_bytes);
            req_pf -= used_pf;
  
            if(pfmgr_mark_bmp(free_range, addr, used_pf))
            {
                kprintf("Failed to mark all bitmap\n");
                spinlock_unlock(&pfmgr_lock);
                return(-1);
            }

            free_range->next_lkup = BYTES_TO_PF (addr - free_range->hdr.base  + 
                                                 cb_dat.used_bytes);

            if(flags & PHYS_ALLOC_CB_STOP)
            {
                /* We're done here */
                if(cb_status == 0)
                {
#ifdef PFMGR_DEBUG
                    kprintf("ALLOC_LENGTH %d\n",alloc_len);
#endif  
                    spinlock_unlock(&pfmgr_lock);
                    return(0);
                }
            }
            else if(req_pf == 0)
            {
                break;
            }
        }
        
        /* If we got frames, try again on the same range */
        if(lkup_sts > PFMGR_FOUND_NONE)
        {
            continue;
        }
        fnode = next_fnode;
    }

    if(~flags & PHYS_ALLOC_CB_STOP)
    {
        spinlock_unlock(&pfmgr_lock);

        if(req_pf > 0)
        {
            return(-1);
        }
        else
        {
            return(0);
        }
    }

    spinlock_unlock(&pfmgr_lock);
    
    return(-1);
}

/* pfmgr_free - obtains the free range using addr and pf count*/

static int pfmgr_addr_to_free_range
(
    phys_addr_t addr, 
    phys_size_t pf,
    struct pfmgr_free_range **pfreer
)
{
    phys_size_t         length     = 0;
    struct list_node         *node      = NULL;
    struct pfmgr_free_range *freer      = NULL;

    length = PF_TO_BYTES(pf);

    node = linked_list_first(&base.freer);

    while(node)
    {
        freer = (struct pfmgr_free_range*)node;

        if(pfmgr_in_range(freer->hdr.base, 
                          freer->hdr.len,
                          addr, 
                          length))
        {
            *pfreer = freer;
            return(0);
        }

        node = linked_list_next(node);
    }

    return(-1);
}

/* pfmgr_free - frees page frames */

static int _pfmgr_free
(
    free_cb cb, 
    void *pv
)
{
    struct pfmgr_free_range *freer      = NULL;
    phys_addr_t addr               = 0;
    phys_size_t to_free_pf         = 0;
    int         again              = 0;
    int         err                = 0;
    phys_addr_t next_addr          = 0;
    struct pfmgr_cb_data cb_dat         = {.avail_bytes = 0, 
                                      .used_bytes  = 0,
                                      .phys_base   = 0
                                     };
    virt_size_t len = 0;

    spinlock_lock(&pfmgr_lock);
    
    do
    {
        cb_dat.avail_bytes  = 0;
        cb_dat.used_bytes   = 0, 
        cb_dat.phys_base    = 0;

        again = cb(&cb_dat, pv);

        to_free_pf = BYTES_TO_PF(cb_dat.used_bytes);
        addr       = cb_dat.phys_base;
        len       += cb_dat.used_bytes;

#if PFMGR_DEBUG        
        kprintf("FREED BASE 0x%x len 0x%x\n",addr,cb_dat.used_bytes);
#endif    
        if(again > 0 && cb_dat.used_bytes == 0)
        {
            kprintf("EMPTY_FOUND - skipping\n");
            continue;
        }

        if(again < 0)
        {
#ifdef PFMGR_DEBUG
            kprintf("ERROR FROM THE CALLBACK\n");
#endif
            break;
        }
        /* Get the range */
        if(pfmgr_addr_to_free_range(addr, 
                                    to_free_pf, 
                                    &freer) != 0)
        {
            kprintf("EXIT 0x%x - %d\n",addr, to_free_pf);
            /* WTF is this? */
            err = -1;
            break;
        }

        /* Clear free range */
        if(pfmgr_clear_bmp(freer, addr, to_free_pf) !=0)
        {
            kprintf("Could not clear all the requested frames\n");
            err = -1;
        }

        next_addr = BYTES_TO_PF(addr - freer->hdr.base);
     
        if(next_addr < freer->next_lkup)
        {
            freer->next_lkup = next_addr;
        }

    }while(again > 0);
    
#ifdef PFMGR_DEBUG
    kprintf("FREED_LENGTH %d\n",len);
#endif

    if(again < 0)
    {
        err = -1;
    }

    spinlock_unlock(&pfmgr_lock);
    
    return(err);
}

/* pfmgr_early_init - initializes tracking information */
void pfmgr_early_init(void)
{
    struct pfmgr_init_data init;

    spinlock_lock(&pfmgr_lock);

    memset(&init, 0, sizeof(struct pfmgr_init_data));
    mem_map_iter(pfmgr_init_busy_callback, &init);

    memset(&init, 0, sizeof(struct pfmgr_init_data));
    mem_map_iter(pfmgr_init_free_callback, &init);

    spinlock_unlock(&pfmgr_lock);

    memset(&pfmgr_interface, 0, sizeof(struct pfmgr));
    pfmgr_interface.alloc = pfmgr_early_alloc_pf;

    kprintf("KERNEL_BEGIN 0x%x KERNEL_END 0x%x\n",_KERNEL_LMA, _KERNEL_LMA_END);
}

/* pfmgr_init - initialize runtime structures for the page frame allocator */
int pfmgr_init(void)
{
    phys_addr_t     phys = 0;
    virt_size_t     size = 0;
    phys_addr_t     next_phys = 0;
    struct pfmgr_range_header *hdr = (struct pfmgr_range_header*)VM_INVALID_ADDRESS;


    linked_list_init(&base.freer);
    linked_list_init(&base.busyr);

    kprintf("Initializing Page Frame Manager\n");

    phys = base.physf_start;

    do
    {
        hdr = (struct pfmgr_range_header*)pfmgr_early_map(phys);
        
        if((virt_addr_t)hdr == VM_INVALID_ADDRESS)
        {
            kprintf("Failed to get the free range\n");
            return(-1);
        }

        size = (hdr->struct_len % PAGE_SIZE) ? 
                ALIGN_UP(hdr->struct_len, PAGE_SIZE) : 
                hdr->struct_len;
                
        hdr = (struct pfmgr_range_header*)vm_map(NULL,  
                                            VM_BASE_AUTO, 
                                            size,
                                            phys,
                                            0,
                                            VM_ATTR_WRITABLE);
        if((virt_addr_t)hdr == VM_INVALID_ADDRESS)
        {
            return(-1);
        }
        
        phys = hdr->next_range;

        linked_list_add_tail(&base.freer, &hdr->node);

    }while(phys != 0);

    phys = base.physb_start;
    next_phys = phys;
    hdr = (struct pfmgr_range_header*)VM_INVALID_ADDRESS;

    do
    {
        if((next_phys % PAGE_SIZE) == 0)
        {
            hdr = (struct pfmgr_range_header*)pfmgr_early_map(phys);
            
            if((virt_size_t)hdr == VM_INVALID_ADDRESS)
            {
                return(-1);
            }

            size = (hdr->struct_len % PAGE_SIZE) ? 
                   ALIGN_UP(hdr->struct_len, PAGE_SIZE) : 
                   hdr->struct_len;

            hdr = (struct pfmgr_range_header*)vm_map(NULL, 
                                                VM_BASE_AUTO, 
                                                size, 
                                                phys,
                                                0,
                                                VM_ATTR_WRITABLE);

            if((virt_size_t)hdr == VM_INVALID_ADDRESS)
            {
                return(-1);
            }
        }
        else if((virt_addr_t)hdr != VM_INVALID_ADDRESS)
        {
            /* advance manually */
            hdr = (struct pfmgr_range_header*)((uint8_t*)hdr + 
                  sizeof(struct pfmgr_busy_range));
        }

        if((virt_size_t)hdr != VM_INVALID_ADDRESS)
        {
            phys = hdr->next_range;
            next_phys = phys - base.physb_start;
            linked_list_add_tail(&base.busyr, &hdr->node);
        }

    }while(phys > 0);

    pfmgr_interface.alloc   = _pfmgr_alloc;
    pfmgr_interface.dealloc = _pfmgr_free;
  
    kprintf("Page frame manager is initialized\n");
 
    return(0);
}

int pfmgr_free
(
    free_cb cb,
    void *cb_pv
)
{
    int ret = -1;

    if(pfmgr_interface.dealloc != NULL)
    {
        ret = pfmgr_interface.dealloc(cb, cb_pv);
    }

    return(ret);
}

int pfmgr_alloc
(
    phys_addr_t start,
    phys_size_t pf, 
    uint8_t flags, 
    alloc_cb cb, 
    void *pv
)
{
    int ret = -1;

    if(pfmgr_interface.alloc != NULL)
    {
        ret = pfmgr_interface.alloc(start, pf, flags, cb, pv);
    }

    return(ret);
}

int pfmgr_show_free_memory
(
    void
)
{
    struct pfmgr_free_range *freer = NULL;
    phys_size_t free_mem = 0;
    phys_size_t total_mem = 0;
    int region = 0;

    
    kprintf("\nPhysical memory statistics:\n");
    freer = (struct pfmgr_free_range*)linked_list_first(&base.freer);
    
    while(freer)
    {

        kprintf("Region #%d: BASE 0x%x TOTAL 0x%x AVAILABLE 0x%x\n", region,
                freer->hdr.base, 
                PF_TO_BYTES(freer->total_pf), 
                PF_TO_BYTES(freer->avail_pf));
        free_mem += freer->avail_pf;
        total_mem += freer->total_pf;
        freer = (struct pfmgr_free_range*)linked_list_next(&freer->hdr.node);
        region++;
    }

    free_mem *= PAGE_SIZE;
    total_mem *= PAGE_SIZE;

    kprintf("FREE MEMORY %d USED %d TOTAL MEMORY %d\n",free_mem, 
                                                       total_mem - free_mem,  
                                                       total_mem);

    return(0);
}