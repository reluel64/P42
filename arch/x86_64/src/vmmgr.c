/* Virtual Memory Manager */

#include <paging.h>
#include <pagemgr.h>
#include <vmmgr.h>
#include <physmm.h>
#include <stddef.h>


#define VIRT_FROM_FREE_BLOCK(virt, len, fblock) (((fblock)->base <= (virt)) && \
      (((fblock)->base + (fblock)->length) >= ((virt) + (len))))

extern uint64_t KERNEL_VMA;
extern uint64_t KERNEL_VMA_END;
extern uint64_t pagemgr_early_alloc(uint64_t vaddr, uint64_t len, uint16_t attr);



static vmmgr_t vmem_mgr;
static inline int vmmgr_split_free_block
(
    vmmgr_free_mem_t *from, 
    uint64_t          virt, 
    uint64_t          len, 
    vmmgr_free_mem_t *rem
);
uint64_t vmmgr_early_reserve(uint64_t virt, uint64_t len);

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
    
    rsrvd_start = (uint8_t*)pagemgr_early_alloc(vmem_mgr.vmmgr_base,PAGE_SIZE,0);

    free_start = (uint8_t*)pagemgr_early_alloc(vmem_mgr.vmmgr_base + PAGE_SIZE,
                                                PAGE_SIZE,0);


    memset(rsrvd_start, 0, sizeof(PAGE_SIZE));
    memset(free_start, 0, sizeof(PAGE_SIZE));

    node = (list_node_t*)rsrvd_start;
    linked_list_add_head(&vmem_mgr.rsrvd_mem, node);

    node = (list_node_t*)free_start;
    linked_list_add_head(&vmem_mgr.free_mem, node);

    vmem_mgr.free_ent_per_page = (PAGE_SIZE - sizeof(list_node_t)) / sizeof(vmmgr_free_mem_t);
    vmem_mgr.rsrvd_ent_per_page = (PAGE_SIZE - sizeof(list_node_t)) / sizeof(vmmgr_rsrvd_t);

    /* Get the start of the array */
    fmem = (vmmgr_free_mem_t*)(free_start + sizeof(list_node_t));

    fmem[0].base = vmem_mgr.vmmgr_base;
    fmem[0].length = (UINT64_MAX - fmem->base)+1;
        
    vmmgr_free_mem_t free = {0x1000000,0x1000000};
    vmmgr_free_mem_t rem  = {0};
    kprintf("FREE BASE 0x%x LENGTH 0x%x\n",free.base,free.length);
    vmmgr_split_free_block(&free,0x1EFF000,0x1000,&rem);

    kprintf("FREE BASE 0x%x LENGTH 0x%x\n",free.base,free.length);
    kprintf("REM BASE 0x%x LENGTH 0x%x\n",rem.base,rem.length);
    
   /* vmmgr_early_reserve(&KERNEL_VMA,((uint64_t)&KERNEL_VMA_END) - ((uint64_t)&KERNEL_VMA));*/
}


int vmmgr_add_reserved(vmmgr_rsrvd_t *rsrvd)
{
    list_node_t   *rsrvd_node      = NULL;
    list_node_t   *next_rsrvd_node = NULL;
    vmmgr_rsrvd_t *rsrvd_cursor    = NULL;
    vmmgr_rsrvd_t *rsrvd_candidate = NULL;
    uint8_t        done            = 0;

    rsrvd_node = linked_list_first(&vmem_mgr.rsrvd_mem);
    
    /* Start finding a free slot */

    while(rsrvd_node)
    {
        next_rsrvd_node = linked_list_next(rsrvd_node);
        rsrvd_cursor = (vmmgr_rsrvd_t*)(((uint8_t*)rsrvd_node) + sizeof(list_node_t));

        for(uint16_t i = 0; i < vmem_mgr.rsrvd_ent_per_page; i++)
        {
            
            /* TODO: Support coalescing if the types are the same */
            
            if(rsrvd_candidate == NULL   && 
               rsrvd_cursor[i].base == 0 && 
               rsrvd_cursor[i].length == 0)
            {
                rsrvd_candidate = &rsrvd_cursor[i];
                done = 1;
                break;
            }
            else if(!memcmp(rsrvd, &rsrvd_candidate[i], sizeof(vmmgr_rsrvd_t)))
            {
                rsrvd_candidate = NULL;
                done = 1;
                break;
            }
        }

        if(done == 1)
            break;

        rsrvd_node = next_rsrvd_node;
    }

    if(rsrvd_candidate)
        *rsrvd_candidate = *rsrvd;
    else
        return(-1);

    return(0);
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

        if(from->length == 0)
        {
            *from = *rem;
            rem->base = 0;
            rem->length = 0;
        }

        if(rem->length == 0)
        {
            rem->base = 0;
        }

        return(0);
    }
    else
    {
        return(-1);
    }
}

uint64_t vmmgr_early_reserve(uint64_t virt, uint64_t len)
{
    list_node_t      *free_node        = NULL;
    list_node_t      *rsrvd_node       = NULL;
    list_node_t      *next_free_node   = NULL;
    list_node_t      *next_rsrvd_node  = NULL;
    vmmgr_free_mem_t *free_desc        = NULL;
    vmmgr_free_mem_t  remaining;
    vmmgr_rsrvd_t    *rsrvd_desc       = NULL;
    uint8_t           found            = 0;

    memset(&remaining, 0, sizeof(vmmgr_free_mem_t));
    free_node   = linked_list_first(&vmem_mgr.free_mem);
    rsrvd_node  = linked_list_first(&vmem_mgr.rsrvd_mem);

    rsrvd_desc = ((uint8_t*)rsrvd_node) + sizeof(list_node_t);

    while(free_node)
    {
        next_free_node = linked_list_next(free_node);
        
        free_desc = ((uint8_t*)free_node) + sizeof(list_node_t);
        
        for(uint16_t fn = 0; fn < vmem_mgr.free_ent_per_page; fn++)
        {
            if(VIRT_FROM_FREE_BLOCK(virt, len,&free_desc[fn]))
               {
                   kprintf("We found something at 0x%0 len 0x%x\n",free_desc[fn].base, free_desc[fn].length);
                   found = 1;
                   break;
               }               
        }   
        
        if(found == 1)
            break;

        free_node = next_free_node;
    }
}
