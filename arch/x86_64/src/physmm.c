/* Physical memory manager */
#include <stdint.h>
#include <page.h>
#include <descriptors.h>
#include <multiboot.h>
#include <vga.h>
#include <physmm.h>
#include <stddef.h>
#include <memory_map.h>
#include <utils.h>

#define BITMAP_SIZE_FOR_AREA(x)  (((x) / PAGE_SIZE) / 8)
#define GIGA_BYTE(x) ((x) * 1024ull * 1024ull *1024ull)
#define MEGA_BYTE(x) ((x) * 1024ull *1024ull)
#define KILO_BYTE(x) ((x) * 1024ull)
#define IN_SEGMENT(x,base,len) (((x)>=(base) && (x)<=(base)+(len)))

extern uint64_t KERNEL_LMA;
extern uint64_t KERNEL_LMA_END;
extern uint64_t KERNEL_VMA;
extern uint64_t KERNEL_VMA_END;
extern uint64_t BOOT_PAGING;
extern uint64_t BOOT_PAGING_END;
extern uint64_t BOOT_PAGING_LENGTH;
extern uint64_t read_cr3();
extern void     write_cr3(uint64_t phys_addr);
extern void     __invlpg(uint64_t address);

static uint64_t boot_paging     = (uint64_t)&BOOT_PAGING;
static uint64_t boot_paging_len = (uint64_t)&BOOT_PAGING_LENGTH;
static uint64_t boot_paging_end = (uint64_t)&BOOT_PAGING_END;


/* Because diferent platforms can have
 * various reserved regions, each region 
 * is represented by a phys_mm_region_t
 * which contains the base address, length
 * and the physical and virtual addresses
 * to detailed information related to that
 * region.
 * 
 * For example, regions with memory that is 
 * available for use will have the phys_pv and
 * virt_pv point to a phys_mm_avail_desc_t 
 * structure which contain tracking information 
 * for the region specified by base and length.
 * 
 * Kernel memory segment layout (physical)
 * 
 * This map describes how the memory segment 
 * where the kernel image is filled.
 * 
 * +----------------------+ <- memory segment size
 * |   Tracking Bitmap    |
 * +----------------------+ <- memory segment size - BITMAP_SIZE_FOR_AREA
 * |   Available memory   |
 * +----------------------+
 * |       Array of       |
 * | phys_mm_avail_desc_t |
 * +----------------------+
 * |       Array of       |
 * |   phys_mm_region_t   |
 * +----------------------+ <- kernel_lma_end + page alignment
 * |     Kernel image     |
 * +----------------------+ <- kernel_lma_start
 * |     Memory below     |
 * |     kernel  image    |
 * +----------------------+ <- start of memory segment
 *
 * Available memory segment layout (physical)
 * 
 * If we don't host the kernel image in the memory segment,
 * the layout would look something like this
 * +----------------------+ <- memory segment size
 * |   Tracking Bitmap    |
 * +----------------------+ <- memory segment size - BITMAP_SIZE_FOR_AREA
 * |   Available memory   |
 * +----------------------+ <- start of memory segment
 */ 


typedef struct 
{
    uint64_t memory_size;
    uint32_t rsrvd_regions;
    uint32_t avail_regions;
    uint64_t rgn_paddr;
    uint64_t rgn_vaddr;
    uint64_t desc_paddr;
    uint64_t desc_vaddr;
    uint64_t desc_len;
    uint64_t rgn_len;
    uint64_t rgn_area_len;
    uint64_t desc_area_len;
    uint64_t kernel_phys_base;
    uint64_t kernel_phys_end;
    uint64_t kernel_size;
    uint64_t kernel_virt_base;
    uint64_t kernel_virt_end;
    memory_map_entry_t kernel_segment;
}phys_mm_root_t;

typedef struct
{
    uint8_t type;
    uint64_t base;
    uint64_t length;
    void *phys_pv;
    void *virt_pv;
}phys_mm_region_t;

typedef struct
{
    uint64_t pf_count;      /* number of 4KB page frames             */
    uint64_t avail_pf;      /* available page frames                 */
    uint64_t bmp_phys_addr; /* physical address of the bitmap        */ 
    uint64_t bmp_virt_addr; /* virtual address of the bitmap         */
    uint64_t bmp_len;       /* bitmap length in bytes                */
    uint64_t bmp_area_len;  /* bitmap length in bytes (page aligned) */    
}phys_mm_avail_desc_t;

static phys_mm_root_t physmm_root;

static int physmm_has_boot_paging
(
    memory_map_entry_t *mme
)
{
    if((mme->base < boot_paging) && 
       ((mme->length + mme->base) < (boot_paging + boot_paging_len)))
    {
        return(1);
    }

    return(0);
}

void physmm_build_init_pt();
/*
 * This routine maps 8KB of memory
 */
static uint64_t physmm_temp_map(uint64_t phys_addr)
{
    uint16_t offset;
    uint64_t virt_addr =   physmm_root.kernel_virt_base    | 
                           PDE_INDEX_TO_VIRT(511)          | 
                           PTE_INDEX_TO_VIRT(510);

    uint64_t boot_pt = &BOOT_PAGING;
    uint64_t  *page = NULL;
      
    offset = phys_addr % PAGE_SIZE;
    phys_addr -= offset;

    /* Get the last two pages from the last 512 GB */
    /* the page table resides at boot_pt + 0x3000 
     * each page table takes 4KB so we will skip 511 page tables
     * to get to the last page table
     * after this we skip 510 entries so we get the starting address of the
     * last two pages.
     */

    page = (uint64_t*)(boot_pt + 0x3000 + 511 * 4096 + 510 * 8);

    /* mark the page as present and writeable */
    page[0] =  phys_addr            | 0x3;
    page[1] =  phys_addr + 0x1000   | 0x3;

    /* reload the page table */
    __invlpg(virt_addr);
    __invlpg(virt_addr + PAGE_SIZE);
  
    return(virt_addr + offset);
}

static void mem_iter_fill_root(memory_map_entry_t *ent, void *pv)
{
    if(ent->type == MEMORY_USABLE)
    {
        physmm_root.avail_regions++;
        physmm_root.memory_size += ent->length;
        physmm_root.desc_len += sizeof(phys_mm_avail_desc_t);
    }
    else
    {
        physmm_root.rsrvd_regions++;
    }
    
    physmm_root.rgn_len += sizeof(phys_mm_region_t);

    if((ent->base < physmm_root.kernel_phys_base) && 
       (ent->base + ent->length > physmm_root.kernel_phys_end))
       {
           physmm_root.kernel_segment = *ent;
       }
}

/* Callback used to build the memory tracking
 * structures 
 */

static void mem_iter_build_structs(memory_map_entry_t *ent, void *pv)
{
    void                **ppv            = (void**)pv;
    size_t               *rgn_pos        = ppv[0];
    size_t               *desc_pos       = ppv[1];
    uint64_t              pf_start       = 0;
    uint64_t              pf_end         = 0;
    uint64_t             *bmp_buf        = NULL;
    phys_mm_region_t     *mem_region         = NULL;
    phys_mm_region_t      region    = {0};
    phys_mm_avail_desc_t *mem_desc           = NULL;
    phys_mm_avail_desc_t  desc      = {0};
    memory_map_entry_t   *kernel_loc_map = &physmm_root.kernel_segment;

    memset(&region, 0, sizeof(phys_mm_region_t));
    memset(&desc, 0, sizeof(phys_mm_avail_desc_t));
    
    region.base    = ent->base;
    region.length  = ent->length;
    region.type    = ent->type;

    if(region.type == MEMORY_USABLE)
    {
        region.phys_pv     = physmm_root.desc_paddr + (*desc_pos);
        desc.bmp_len       = BITMAP_SIZE_FOR_AREA(region.length);
        desc.pf_count      = region.length / PAGE_SIZE;
        desc.avail_pf      = desc.pf_count;
        desc.bmp_area_len  = ALIGN_UP(desc.bmp_len, PAGE_SIZE);
        desc.bmp_phys_addr = ALIGN_DOWN(region.base + 
                                         (region.length - desc.bmp_area_len),
                                         PAGE_SIZE);

        if((desc.bmp_phys_addr + desc.bmp_area_len) > 
            (region.base + region.length))
        {
            desc.bmp_phys_addr -= PAGE_SIZE;
        }
        

        /* start building the bitmap for this descriptor*/

        for(uint64_t pf = 0; pf < desc.pf_count;pf++)
        {
            
            pf_start = region.base + PAGE_SIZE * pf;
            pf_end   = pf_start + PAGE_SIZE;
           
            /* get 8 bytes */
            if((pf % 64 == 0))
            {
                 bmp_buf = (uint64_t*)physmm_temp_map(desc.bmp_phys_addr + 
                                                    sizeof(uint64_t) * (pf / 64)
                                                   );
                (*bmp_buf) = 0;
            }

            /* mark the pages as busy */
            if((pf_start >= physmm_root.kernel_phys_base) && 
               (pf_end <= physmm_root.kernel_phys_end))
            {
                (*bmp_buf) |= (1 << (pf % 64));
                desc.avail_pf--;
            }

            else if(pf_start >= physmm_root.rgn_paddr && 
               pf_end <= physmm_root.rgn_paddr + physmm_root.rgn_area_len)
            {
                (*bmp_buf) |= (1 << (pf % 64));
                desc.avail_pf--;
                /* kprintf("Reserving regions pfStart %x pfEnd %x\n",pf_start,pf_end); */
            }

            else if(pf_start >= physmm_root.desc_paddr && 
               pf_end <=  physmm_root.desc_paddr + physmm_root.desc_area_len)
            {
                (*bmp_buf) |= (1 << (pf % 64));
                desc.avail_pf--;
               /* kprintf("Reserving desc pfStart %x pfEnd %x\n",pf_start,pf_end); */
            }
            
            else if(pf_start >= desc.bmp_phys_addr && 
              pf_end <= ALIGN_UP(desc.bmp_phys_addr + desc.bmp_len, PAGE_SIZE))
            {
                (*bmp_buf) |= (1 << (pf % 64));
                desc.avail_pf--;
                /* kprintf("Reserving bmp pfStart %x pfEnd %x\n",pf_start,pf_end);*/
            }
        }
        /* Save desc to memory */
        
        region.phys_pv = physmm_root.desc_paddr + (*desc_pos);
        mem_desc = (phys_mm_avail_desc_t*)physmm_temp_map(region.phys_pv);

        memcpy(mem_desc, &desc,sizeof(phys_mm_avail_desc_t));

        (*desc_pos)+= sizeof(phys_mm_avail_desc_t);
    }
    
    mem_region = (phys_mm_region_t*)physmm_temp_map(physmm_root.rgn_paddr + (*rgn_pos));
    memcpy(mem_region, &region,sizeof(phys_mm_region_t));

    (*rgn_pos)+= sizeof(phys_mm_region_t);

}

void physmm_init(void)
{
    kprintf("Initializing Physical Memory Manager\n");
    void *pv[2];
    uint64_t rgn = 0;
    uint64_t desc_pos = 0;
    uint64_t kseg_bmp_area_len = 0;
    uint64_t struct_space = 0;

    memset(&physmm_root,0,sizeof(physmm_root));
    
    physmm_root.kernel_phys_base = &KERNEL_LMA;
    physmm_root.kernel_phys_end  = &KERNEL_LMA_END;
    physmm_root.kernel_virt_base = &KERNEL_VMA;
    physmm_root.kernel_virt_end  = &KERNEL_VMA_END;
    physmm_root.kernel_size      = physmm_root.kernel_phys_end - 
                                   physmm_root.kernel_phys_base;

    mem_map_iter(mem_iter_fill_root, NULL);

    /* How many bytes does the area for regions occupy - PAGE multiple */
    physmm_root.desc_area_len = ALIGN_UP(physmm_root.desc_len, PAGE_SIZE);

    /* How many bytes does the area for descriptors occupy - PAGE multiple */
    physmm_root.rgn_area_len = ALIGN_UP(physmm_root.rgn_len, PAGE_SIZE);


    physmm_root.rgn_paddr = ALIGN_UP(physmm_root.kernel_phys_end, PAGE_SIZE);

    physmm_root.desc_paddr = physmm_root.rgn_paddr  + 
                             physmm_root.rgn_area_len;

    kseg_bmp_area_len = BITMAP_SIZE_FOR_AREA(physmm_root.kernel_segment.length);

    struct_space = ALIGN_UP(physmm_root.desc_len, PAGE_SIZE) +
                   ALIGN_UP(physmm_root.rgn_len, PAGE_SIZE) + 
                   ALIGN_UP(kseg_bmp_area_len, PAGE_SIZE);

    /* If we don't have space after the kernel
     * we will check if there is any space before it
     */ 

    if(physmm_root.kernel_phys_end + struct_space > 
       physmm_root.kernel_segment.base + physmm_root.kernel_segment.length)

    {
        physmm_root.rgn_paddr  = ALIGN_DOWN(physmm_root.kernel_phys_end - struct_space,PAGE_SIZE);
        physmm_root.desc_paddr = physmm_root.rgn_paddr + physmm_root.rgn_area_len;
    }

    pv[0] = &rgn;
    pv[1] = &desc_pos;

    mem_map_iter(mem_iter_build_structs, pv);

#if 1
    kprintf("----Structure summary----\n");
    kprintf("Kernel start 0x%x end 0x%x\n",physmm_root.kernel_phys_base, physmm_root.kernel_phys_end);
    kprintf("Region start 0x%x end 0x%x\n", physmm_root.rgn_paddr, physmm_root.rgn_area_len);
    kprintf("Desc start 0x%x end 0x%x\n", physmm_root.desc_paddr, physmm_root.desc_area_len);
    kprintf("Structure size 0x%x\n",struct_space);
#endif
    physmm_build_init_pt();
}


void physmm_build_init_pt()
{
    uint64_t pml4_base  = 0;
    uint64_t pdpt_base  = 0;
    uint64_t pdt_base   = 0;
    uint64_t pt_base    = 0;
    uint64_t pdpt_count = 0;
    uint64_t pdt_count  = 0;
    uint64_t pt_count   = 0;
    uint64_t pg_mem     = 0;
    uint64_t full_map   = 0;
    
    phys_mm_region_t region;
    phys_mm_avail_desc_t desc;

    uint64_t mem_req = physmm_root.kernel_size  + 
                       physmm_root.rgn_area_len +
                       physmm_root.desc_area_len;


    /* Get the required length to map for bitmaps */
    for(uint64_t i = 0; i < physmm_root.rgn_len; i+= sizeof(phys_mm_region_t))
    {
        region = *(phys_mm_region_t*)physmm_temp_map(physmm_root.rgn_paddr + i);

        if(region.type != MEMORY_USABLE)
            continue;

        desc   = *(phys_mm_avail_desc_t*)physmm_temp_map(region.phys_pv);
        mem_req += desc.bmp_area_len;
    }
    mem_req = GIGA_BYTE(256);
    pg_mem  += PAGE_SIZE;
    mem_req += pg_mem;         

    mem_req = ALIGN_UP(mem_req,PAGE_SIZE);

    /* Calculate how many bytes we need
     * to store the paging structures 
     */

    for(pdpt_count = 0; ; pdpt_count++)
    {
        if(mem_req <= GIGA_BYTE(pdpt_count * 512))
            break;

        mem_req += PAGE_SIZE;
        pg_mem  += PAGE_SIZE;
    }

    for(pdt_count = 0; ; pdt_count++)
    {
        if(mem_req <= GIGA_BYTE(pdt_count))
            break;

        mem_req += PAGE_SIZE;
        pg_mem  += PAGE_SIZE;
    }

    for(pt_count = 0; ; pt_count++)
    {
        if(mem_req <= MEGA_BYTE(pt_count * 2))
            break;

        mem_req += PAGE_SIZE;
        pg_mem  += PAGE_SIZE;
    }

    /* Find an appropiate descriptor */
    for(uint64_t i = 0; i < physmm_root.rgn_len; i+= sizeof(phys_mm_region_t))
    {
        region = *(phys_mm_region_t*)physmm_temp_map(physmm_root.rgn_paddr + i);

        if(region.type != MEMORY_USABLE)
            continue;

        desc   = *(phys_mm_avail_desc_t*)physmm_temp_map(region.phys_pv);

        /* Skip memory below 1 MB */
        if(desc.avail_pf * PAGE_SIZE >= pg_mem && region.base >= 0x100000)
            break;
    }

   

    
    pml4_base = physmm_root.desc_paddr    + 
                physmm_root.desc_area_len;

    pdpt_base = pml4_base + PAGE_SIZE;
    pdt_base = pdpt_base + PAGE_SIZE * pdpt_count;
    pt_base = pdt_base + PAGE_SIZE * pdt_count;
    

    kprintf("PML4 0x%x\nPDPT 0x%x\nPDT 0x%x\nPT 0x%x\n",pml4_base,pdpt_base,pdt_base,pt_base);

}