/* Virtual Memory Manager 
 * Part of P42 Kernel
 */

#include <paging.h>
#include <pagemgr.h>
#include <vmmgr.h>
#include <physmm.h>
#include <stddef.h>
#include <utils.h>

#define VIRT_FROM_FREE_BLOCK(virt, len, fblock) (((fblock)->base <= (virt)) && \
      (((fblock)->base + (fblock)->length)-1 >= ((virt) + (len)-1)))

#define VIRT_START_IN_BLOCK(virt, fblock)  ((fblock)->base <= (virt)) && \
      (((fblock)->base + (fblock)->length)-1 >= ((virt)))

#define NODE_TO_FREE_DESC(node) ((vmmgr_free_mem_t*)(((uint8_t*)(node)) + (sizeof(list_node_t))))
#define NODE_TO_RSRVD_DESC(node) ((vmmgr_rsrvd_mem_t*)(((uint8_t*)(node)) + (sizeof(list_node_t))))

extern uint64_t KERNEL_VMA;
extern uint64_t KERNEL_VMA_END;
extern uint64_t KERNEL_IMAGE_LEN;

static vmmgr_t vmem_mgr;
static pagemgr_t *pagemgr = NULL;

static inline int vmmgr_split_free_block
(
    vmmgr_free_mem_t *from, 
    uint64_t          virt, 
    uint64_t          len, 
    vmmgr_free_mem_t *rem
);

int vmmgr_reserve(uint64_t virt, uint64_t len, uint8_t type);
void vmmgr_list_entries()
{
    list_node_t *node = NULL;
    list_node_t *next_node = NULL;
    vmmgr_free_mem_t *fmem = NULL;
    vmmgr_rsrvd_mem_t *rsrvd  = NULL;


    node = linked_list_first(&vmem_mgr.free_mem);
    
    kprintf("----LISTING FREE RANGES----\n");

    while(node)
    {
        fmem = NODE_TO_FREE_DESC(node);

        next_node = linked_list_next(node);
        for(uint16_t i = 0; i < vmem_mgr.free_ent_per_page; i++)
        {
            if(fmem[i].base != 0  && fmem[i].length != 0)
                kprintf("BASE 0x%x LENGTH 0x%x\n",fmem[i].base,fmem[i].length);
        }

        node = next_node;
    }
#if 0
    node = linked_list_first(&vmem_mgr.rsrvd_mem);
    
    kprintf("----LISTING RESERVED RANGES----\n");

    while(node)
    {
        next_node = linked_list_next(node);
        rsrvd = NODE_TO_RSRVD_DESC(node);

        for(uint16_t i = 0; i < vmem_mgr.rsrvd_ent_per_page; i++)
        {
            if(rsrvd[i].base != 0  && rsrvd[i].length != 0)
                kprintf("BASE 0x%x LENGTH 0x%x\n",rsrvd[i].base,rsrvd[i].length);
        }

        node = next_node;
    }
#endif
}

int vmmgr_init(void)
{
    list_node_t          *node        = NULL;
    vmmgr_free_mem_t     *fmem        = NULL;
    uint8_t              *rsrvd_start = NULL;
    uint8_t              *free_start  = NULL;
    
    memset(&vmem_mgr, 0, sizeof(vmmgr_t));
    
    vmem_mgr.vmmgr_base = VMMGR_BASE;

    linked_list_init(&vmem_mgr.free_mem);
    linked_list_init(&vmem_mgr.rsrvd_mem);
    pagemgr     = pagemgr_get();

    rsrvd_start = (uint8_t*)pagemgr->alloc(vmem_mgr.vmmgr_base,
                                           PAGE_SIZE,
                                           PAGE_WRITABLE);

    free_start  = (uint8_t*)pagemgr->alloc(vmem_mgr.vmmgr_base + PAGE_SIZE,
                                                PAGE_SIZE,
                                                PAGE_WRITABLE);

    if(rsrvd_start == NULL || free_start == NULL)
        return(-1);

    kprintf("rsrvd_start 0x%x\n",rsrvd_start);
    kprintf("free_start 0x%x\n",free_start);

    memset(rsrvd_start, 0, sizeof(PAGE_SIZE));
    memset(free_start, 0, sizeof(PAGE_SIZE));

    node = (list_node_t*)rsrvd_start;
    linked_list_add_head(&vmem_mgr.rsrvd_mem, node);

    node = (list_node_t*)free_start;
    linked_list_add_head(&vmem_mgr.free_mem, node);

    vmem_mgr.free_ent_per_page = (PAGE_SIZE - sizeof(list_node_t)) / sizeof(vmmgr_free_mem_t);
    vmem_mgr.rsrvd_ent_per_page = (PAGE_SIZE - sizeof(list_node_t)) / sizeof(vmmgr_rsrvd_mem_t);

    /* Get the start of the array */
    fmem = NODE_TO_FREE_DESC(free_start);

    fmem[0].base = vmem_mgr.vmmgr_base;
    fmem[0].length = (UINT64_MAX - fmem->base)+1;

    vmmgr_reserve((uint64_t)&KERNEL_VMA,
                        ((uint64_t)&KERNEL_VMA_END) - ((uint64_t)&KERNEL_VMA),
                        VMM_RES_KERNEL_IMAGE);

    vmmgr_reserve(REMAP_TABLE_VADDR,
                        REMAP_TABLE_SIZE,
                        VMM_REMAP_TABLE);

    vmmgr_reserve((uint64_t)free_start,
                        PAGE_SIZE,
                        VMM_RESERVED);

    vmmgr_reserve((uint64_t)rsrvd_start,
                        PAGE_SIZE,
                        VMM_RESERVED);

    vmmgr_list_entries();
}


static int vmmgr_is_reserved(uint64_t virt, uint64_t len)
{
    list_node_t *node      = NULL;
    list_node_t *next_node = NULL;
    vmmgr_rsrvd_mem_t *rsrvd = NULL;

    node = linked_list_first(&vmem_mgr.rsrvd_mem);

    while(node)
    {
        next_node  = linked_list_next(node);

        rsrvd = NODE_TO_RSRVD_DESC(node);

        for(uint16_t i = 0; i < vmem_mgr.rsrvd_ent_per_page; i++)
        {
            if(rsrvd[i].base <= virt && virt <= rsrvd[i].length + rsrvd[i].base)
                return(1);
             
            else if(rsrvd[i].base <= virt + len && virt + len <= rsrvd[i].length + rsrvd[i].base)
                return(1);

            else if(rsrvd[i].base >= virt && virt + len >= rsrvd[i].length + rsrvd[i].base)
                return(1);
            
        }

        node = next_node;
    }

    return(0);
}


static int vmmgr_is_free(uint64_t virt, uint64_t len)
{
    list_node_t *node      = NULL;
    list_node_t *next_node = NULL;
    vmmgr_free_mem_t *freem = NULL;
    
    node = linked_list_first(&vmem_mgr.free_mem);

    while(node)
    {
        next_node  = linked_list_next(node);

        freem = NODE_TO_FREE_DESC(node);

        for(uint16_t i = 0; i < vmem_mgr.free_ent_per_page; i++)
        {
            /*  Start of interval is in range */
            if(freem[i].base <= virt && virt <= freem[i].length + freem[i].base)
            {
                kprintf("%s %d\n",__FUNCTION__,__LINE__);
                return(1);
            }
            /* End of interval is in range */
            else if(freem[i].base <= virt + len && virt + len <= freem[i].length + freem[i].base)
            {
                kprintf("%s %d\n",__FUNCTION__,__LINE__);
                return(1);
            }

            else if(freem[i].base >= virt && virt + len >= freem[i].length + freem[i].base)
            {
                kprintf("%s %d\n",__FUNCTION__,__LINE__);
                return(1);
            }
            
        }

        node = next_node;
    }

    return(0);
}

static int vmmgr_add_reserved(vmmgr_rsrvd_mem_t *rsrvd)
{
    list_node_t   *rsrvd_node      = NULL;
    list_node_t   *next_rsrvd_node = NULL;
    vmmgr_rsrvd_mem_t *rsrvd_cursor    = NULL;
    vmmgr_rsrvd_mem_t *rsrvd_candidate = NULL;
    uint8_t        done            = 0;

    rsrvd_node = linked_list_first(&vmem_mgr.rsrvd_mem);
    
    /* Start finding a free slot */

    while(rsrvd_node)
    {
        next_rsrvd_node = linked_list_next(rsrvd_node);
        rsrvd_cursor = NODE_TO_RSRVD_DESC(rsrvd_node);

        for(uint16_t i = 0; i < vmem_mgr.rsrvd_ent_per_page; i++)
        { 
            /* TODO: Support coalescing if the types are the same */
            if(rsrvd_candidate == NULL)
            {
                if(rsrvd_cursor[i].base == 0 && 
                    rsrvd_cursor[i].length == 0)
                {
                    rsrvd_candidate = &rsrvd_cursor[i];
                    done = 1;
                    break;
                }
            }
            else if(!memcmp(rsrvd, rsrvd_candidate, sizeof(vmmgr_rsrvd_mem_t)))
            {
                rsrvd_candidate = NULL;
                done = 1;
                break;
            }
        }

        if(done)
            break;

        rsrvd_node = next_rsrvd_node;
    }
    
    if(rsrvd_candidate)
        *rsrvd_candidate = *rsrvd;
    else
        return(-1);

    return(0);
}

static int vmmgr_merge_free_block(vmmgr_free_mem_t *fmem)
{
    list_node_t *fnode          = NULL;
    list_node_t *next_fnode     = NULL;
    vmmgr_free_mem_t *free_desc = NULL;
    int merged                  = 0;
    fnode = linked_list_first(&vmem_mgr.free_mem);

    while(fnode)
    {
        next_fnode = linked_list_next(fnode);
        free_desc = NODE_TO_FREE_DESC(fnode);

        for(uint16_t i = 0; i < vmem_mgr.free_ent_per_page; i++)
        {
            /* Merge from left */
            if(free_desc[i].base == fmem->base + fmem->length)
            {
                free_desc[i].base = fmem->base;
                free_desc[i].length+=fmem->length;
                
                if(merged == 1)
                {
                    fmem->base = 0;
                    fmem->length = 0;
                }
                
                fmem = &free_desc[i];
                fnode = linked_list_first(&vmem_mgr.free_mem);
                merged = 1;
                continue;
            }
            /* Merge from right */
            else if(free_desc[i].base + free_desc[i].length == fmem->base)
            {
                free_desc[i].length += fmem->length;
                fmem = &free_desc[i];

                if(merged == 1)
                {
                    fmem->base = 0;
                    fmem->length = 0;
                }
                
                fnode = linked_list_first(&vmem_mgr.free_mem);
                merged = 1;
                continue;
            }
        }
        
        fnode = next_fnode;
    }

    if(merged)
        return(0);

    return(-1);
}

static inline int vmmgr_split_free_block
(
    vmmgr_free_mem_t *from, 
    uint64_t          virt, 
    uint64_t          len, 
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

static int vmmgr_put_free_mem(vmmgr_free_mem_t *fmem)
{
    list_node_t *fnode          = NULL;
    list_node_t *next_fnode     = NULL;
    vmmgr_free_mem_t *free_desc = NULL;

    fnode = linked_list_first(&vmem_mgr.free_mem);

    while(fnode)
    {
        next_fnode = linked_list_next(fnode);
        free_desc = NODE_TO_FREE_DESC(fnode);

        for(uint16_t i = 0; i < vmem_mgr.free_ent_per_page; i++)
        {
            if(free_desc[i].length == 0)
            {
                memcpy(&free_desc[i], fmem, sizeof(vmmgr_free_mem_t));
                
                return(0);
            }
        }
        
        if(next_fnode == NULL)
        {
            kprintf("Allocate one page\n");
        }
        fnode = next_fnode;
    }
    return(-1);
}

int vmmgr_reserve(uint64_t virt, uint64_t len, uint8_t type)
{
    list_node_t      *free_node        = NULL;
    list_node_t      *rsrvd_node       = NULL;
    list_node_t      *next_free_node   = NULL;
    vmmgr_free_mem_t *free_desc        = NULL;
    vmmgr_free_mem_t  remaining;
    vmmgr_rsrvd_mem_t     rsrvd;

    memset(&remaining, 0, sizeof(vmmgr_free_mem_t));
    
    free_node    = linked_list_first(&vmem_mgr.free_mem);
    rsrvd.base   = virt;
    rsrvd.length = len;
    rsrvd.type   = type; 
    
    while(free_node)
    {
        next_free_node = linked_list_next(free_node);

        free_desc = NODE_TO_FREE_DESC(free_node);
        
        for(uint16_t fn = 0; fn < vmem_mgr.free_ent_per_page; fn++)
        {
            if(VIRT_FROM_FREE_BLOCK(virt, len, &free_desc[fn]))
            {
                vmmgr_split_free_block(&free_desc[fn], virt, len, &remaining);
                vmmgr_add_reserved(&rsrvd);

                if(remaining.base != 0)
                    vmmgr_put_free_mem(&remaining);

                return(0);
            }
        }   
        
        if(next_free_node == NULL)
        {
            kprintf("Not enough space!!!\n");
            break;
        }
        
        free_node = next_free_node;
    }

    return(-1);
}

void *vmmgr_map(uint64_t phys, uint64_t virt, uint64_t len, uint32_t attr)
{
    list_node_t      *free_node        = NULL;
    list_node_t      *next_free_node   = NULL;
    vmmgr_free_mem_t *free_desc        = NULL;
    vmmgr_free_mem_t  remaining;
    vmmgr_free_mem_t  *from_slot       = NULL;
    uint64_t          ret_addr         = virt;
    uint64_t          map_addr         = 0;
    
    len = ALIGN_UP(len, PAGE_SIZE);
    
    memset(&remaining, 0, sizeof(vmmgr_free_mem_t));

    free_node = linked_list_first(&vmem_mgr.free_mem);

    while(free_node)
    {
        next_free_node = linked_list_next(free_node);
        free_desc = NODE_TO_FREE_DESC(free_node);

        for(uint16_t i  = 0; i < vmem_mgr.free_ent_per_page; i++)
        {
            if(free_desc[i].length == 0)
                continue;

            if(from_slot == NULL)
                from_slot = free_desc + i;

                if(virt != 0)
                {
                    if(VIRT_FROM_FREE_BLOCK(virt, len, from_slot))
                    {
                        break;
                    }
                }
                else if(from_slot)
                {
                     /* Save the best-fit slot found so far */
                    
                    if(from_slot->length < len && free_desc[i].length >= len)
                    {
                        from_slot = free_desc + i;
                    }
                    else if((from_slot->length > free_desc[i].length) && 
                            (free_desc[i].length > len))
                    {
                        from_slot = free_desc + i;
                    }
                }
        }
        
        free_node = next_free_node;
    }
    
    if(from_slot->length < len)
    {
        return(NULL);
    }
    
    if(ret_addr == 0)
        ret_addr = from_slot->base;

    map_addr = pagemgr->map(ret_addr, phys, len, attr);

    if(map_addr == 0)
	{
		pagemgr->unmap(from_slot->base, len);
        return(NULL);
	}
    vmmgr_split_free_block(from_slot, ret_addr, len, &remaining);

    if(remaining.base != 0)
    {
        vmmgr_put_free_mem(&remaining);
    }

    return((void*)ret_addr);
}

void *vmmgr_alloc(uint64_t len, uint32_t attr)
{
    list_node_t      *free_node        = NULL;
    list_node_t      *next_free_node   = NULL;
    vmmgr_free_mem_t *free_desc        = NULL;
    vmmgr_free_mem_t  remaining;
    vmmgr_free_mem_t  *from_slot = NULL;
    uint64_t          ret_addr         = 0;

    len = ALIGN_UP(len, PAGE_SIZE);

    memset(&remaining, 0, sizeof(vmmgr_free_mem_t));

    free_node = linked_list_first(&vmem_mgr.free_mem);

    while(free_node)
    {
        next_free_node = linked_list_next(free_node);
        free_desc = NODE_TO_FREE_DESC(free_node);

        for(uint16_t i  = 0; i < vmem_mgr.free_ent_per_page; i++)
        {
            if(free_desc[i].length == 0)
                continue;

            if(from_slot == NULL)
                from_slot = free_desc + i;

            if(from_slot)
            {
                /* Save the best-fit slot found so far */
                    
                if(from_slot->length < len && free_desc[i].length >= len)
                {
                    from_slot = free_desc + i;
                }
                else if((from_slot->length > free_desc[i].length) && 
                        (free_desc[i].length > len))
                {
                    from_slot = free_desc + i;
                }
                else
                {
                    //from_slot = NULL;vmmgr_split_free_block
                }
            }
        }
        
        free_node = next_free_node;
    }

    if( from_slot->length < len)
    {
        return(NULL);
    }

    ret_addr = pagemgr->alloc(from_slot->base, len, attr);

    if(ret_addr == 0)
	{
        return(NULL);
	}
    else
    {
        kprintf("GOOD\n");
    }
    
	
    vmmgr_split_free_block(from_slot, ret_addr, len, &remaining);

    if(remaining.base != 0)
    {
        vmmgr_put_free_mem(&remaining);
    }

    return((void*)ret_addr);
}



int vmmgr_free(void *vaddr, uint64_t len)
{
    uint64_t virt = (uint64_t)vaddr;
    vmmgr_free_mem_t mm;
    mm.base = virt;
    mm.length = len;
    

    if(virt == 0)
        return(-1);

    pagemgr->dealloc(virt, len);

    if(vmmgr_merge_free_block(&mm) == 0)
        return(0);
    else
        vmmgr_put_free_mem(&mm);
    
    return(0);
#if 0
    if(vmmgr_is_free(virt, len))
    {
        kprintf("CANNOT FREE MEMORY\n");
    }
    if(vmmgr_is_reserved(virt,len))
    {
        kprintf("CANNOT FREE RSRVD MEMORY\n");
    }
#endif
}

int vmmgr_unmap(void *vaddr, uint64_t len)
{
    uint64_t virt = (uint64_t)vaddr;
    vmmgr_free_mem_t mm;
    mm.base = virt;
    mm.length = len;
    

    if(virt == 0)
        return(-1);
        
    pagemgr->unmap(virt, len);

    if(vmmgr_merge_free_block(&mm) == 0)
        return(0);
    else
        vmmgr_put_free_mem(&mm);
    
    return(0);
#if 0
    if(vmmgr_is_free(virt, len))
    {
        kprintf("CANNOT FREE MEMORY\n");
    }
    if(vmmgr_is_reserved(virt,len))
    {
        kprintf("CANNOT FREE RSRVD MEMORY\n");
    }
#endif
}


int vmmgr_change_attrib(uint64_t virt, uint64_t len, uint32_t attr)
{
    /* TODO: Check if range is not in freed regions */
    if(len == 0)
        return(-1);


    return(pagemgr->attr(virt, len, attr));
}