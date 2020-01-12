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

extern uint32_t mem_map_addr; /* address of the multiboot header */
extern uint32_t mem_map_sig; /* multiboot header presence */
extern uint64_t KERNEL_LMA_END, KERNEL_LMA;
extern uint64_t KERNEL_VMA;
extern uint64_t BOOT_PAGING;
extern uint64_t BOOT_PAGING_LENGTH;
extern uint64_t read_cr3();
extern void write_cr3(uint64_t phys_addr);
extern void __invlpg(uint64_t address);
extern void flush_pages(void);
extern char * itoa(unsigned long value, char * str, int base);
extern uint64_t randomize(uint64_t seed);
extern uint64_t random_seed();
extern uint64_t EARLY_PAGE_MEMORY_AREA;

static uint64_t kernel_virtual_base = (uint64_t)&KERNEL_VMA;
static uint64_t phys_mm_header_count;
static uint64_t kernel_lma_start = (uint64_t)&KERNEL_LMA;
static uint64_t kernel_lma_end = (uint64_t)&KERNEL_LMA_END;
static uint64_t boot_paging = (uint64_t)&BOOT_PAGING;
static uint64_t boot_paging_len = (uint64_t)&BOOT_PAGING_LENGTH;


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
}phys_mm_root_t;

typedef struct
{
    uint8_t type;
    uint64_t base;
    uint64_t length;
    uint8_t *phys_pv;
    uint8_t *virt_pv;
}phys_mm_region_t;

typedef struct
{
    uint64_t pf_count;      /* number of 4KB page frames      */
    uint64_t avail_pf;      /* available page frames          */
    uint64_t bmp_phys_addr; /* physical address of the bitmap */ 
    uint64_t bmp_virt_addr; /* virtual address of the bitmap  */
    uint64_t bmp_len;       /* bitmap length in bytes         */
    uint64_t bmp_area_len;
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

/*
 * This routine maps 8KB of memory
 */
static uint64_t phys_temp_map(uint64_t phys_addr)
{
    uint16_t offset;
    uint64_t virt_addr =   kernel_virtual_base    | 
                           PDE_INDEX_TO_VIRT(511) | 
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
    flush_pages();
  
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

    if((ent->base < kernel_lma_start) && 
       (ent->base + ent->length > kernel_lma_end))
       {
           *((memory_map_entry_t*)pv) = *ent;
       }
}

static void mem_iter_build_structs(memory_map_entry_t *ent, void *pv)
{
    void                **ppv            = (void**)pv;
    size_t               *rgn_pos        = ppv[0];
    size_t               *desc_pos       = ppv[1];
    size_t                rgn_offset     = (*rgn_pos) * sizeof(phys_mm_region_t);
    uint64_t              pf_start       = 0;
    uint64_t              pf_end         = 0;
    uint64_t             *bmp_buf        = NULL;
    phys_mm_region_t     *region         = NULL;
    phys_mm_region_t      temp_region    = {0};
    phys_mm_avail_desc_t *desc           = NULL;
    phys_mm_avail_desc_t  temp_desc      = {0};
    memory_map_entry_t   *kernel_loc_map = ppv[3];
    uint8_t               bmp_after      = (uint8_t)ppv[2];

    region = (phys_mm_region_t*)phys_temp_map(physmm_root.rgn_paddr + rgn_offset);
    
    region->base    = ent->base;
    region->length  = ent->length;
    region->type    = ent->type;
    region->virt_pv = 0;
    region->phys_pv = 0;

    /* save the region as it is going to be unmapped */
    memcpy(&temp_region, region, sizeof(phys_mm_region_t));

    if(region->type == MEMORY_USABLE)
    {
        region->phys_pv = physmm_root.desc_paddr + (*desc_pos);
        desc            = (phys_mm_avail_desc_t*)phys_temp_map(region->phys_pv);
        desc->bmp_len   = BITMAP_SIZE_FOR_AREA(temp_region.length);
        desc->pf_count  = temp_region.length / PAGE_SIZE;
        desc->bmp_area_len = ALIGN_UP(desc->bmp_len, PAGE_SIZE);
        desc->bmp_phys_addr =  ALIGN_DOWN(temp_region.base + 
                                         (temp_region.length - desc->bmp_area_len),
                                         PAGE_SIZE);
        
        if(temp_region.base == kernel_loc_map->base)
        {
            if(bmp_after)
            {
               desc->bmp_phys_addr = physmm_root.desc_paddr + physmm_root.desc_area_len;
               kprintf("Bitmap Start %x bitmap len %x\n",desc->bmp_phys_addr, 
                                                         ALIGN_UP(desc->bmp_phys_addr + desc->bmp_len,PAGE_SIZE));
            }

            /* check if the bitmap would overlap the kernel */
            if(desc->bmp_phys_addr >= kernel_lma_start && desc->bmp_phys_addr <= kernel_lma_end)
            {
                kprintf("ERROR: Bitmap will overlap\n");
            }
        }

        /* Special case when the end subtraction does not result in a page
         * sized space.
         * In this case we subtract a page
         */ 
        else
        {
            if((desc->bmp_phys_addr + desc->bmp_area_len) > 
               (temp_region.base + temp_region.length))
            {
               desc->bmp_phys_addr -= PAGE_SIZE;
            }
        }
       
        /* save the descriptor */
        memcpy(&temp_desc, desc,sizeof(phys_mm_avail_desc_t));

        /* start building the bitmap for this descriptor*/

        for(uint64_t pf = 0; pf < temp_desc.pf_count;pf++)
        {
            pf_start = temp_region.base + PAGE_SIZE * pf;
            pf_end   = pf_start + PAGE_SIZE;
           
            /* get 8 bytes */
            if((pf % 64 == 0))
            {
                 bmp_buf = (uint64_t*)phys_temp_map(temp_desc.bmp_phys_addr + 
                                                    sizeof(uint64_t) * (pf / 64)
                                                   );
                (*bmp_buf) = 0;
            }

            /* mark the pages as busy */
            if(pf_start >= kernel_lma_start && pf_end <= kernel_lma_end)
            {
                (*bmp_buf) |= (1 << (pf % 64));
            }

            else if(pf_start >= physmm_root.rgn_paddr && 
               pf_end <= physmm_root.rgn_paddr + physmm_root.rgn_area_len)
            {
                (*bmp_buf) |= (1 << (pf % 64));
                /* kprintf("Reserving regions pfStart %x pfEnd %x\n",pf_start,pf_end); */
            }

            else if(pf_start >= physmm_root.desc_paddr && 
               pf_end <=  physmm_root.desc_paddr + physmm_root.desc_area_len)
            {
                (*bmp_buf) |= (1 << (pf % 64));
               /* kprintf("Reserving desc pfStart %x pfEnd %x\n",pf_start,pf_end); */
            }
            
            else if(pf_start >= temp_desc.bmp_phys_addr && 
              pf_end <= ALIGN_UP(temp_desc.bmp_phys_addr + temp_desc.bmp_len, PAGE_SIZE))
            {
                (*bmp_buf) |= (1 << (pf % 64));
                /* kprintf("Reserving bmp pfStart %x pfEnd %x\n",pf_start,pf_end);*/
            }
        }

        (*desc_pos)+= sizeof(phys_mm_avail_desc_t);
    }

    (*rgn_pos)++;
}

void init_phys_mm(void)
{
    void *pv[6];
    uint64_t rgn = 0;
    uint64_t desc_pos = 0;
    uint64_t rgn_len = 0;
    uint64_t mem_desc_len = 0;
    uint64_t kseg_bmp_area_len = 0;
    uint64_t struct_space = 0;
    uint8_t  place_before_kernel = 0; 
    memory_map_entry_t kernel_loc_map = {0};

    mem_map_iter(mem_iter_fill_root, &kernel_loc_map);

    /* How many bytes does the area for regions occupy - PAGE multiple */
    physmm_root.desc_area_len = ALIGN_UP(physmm_root.desc_len, PAGE_SIZE);

    /* How many bytes does the area for descriptors occupy - PAGE multiple */
    physmm_root.rgn_area_len = ALIGN_UP(physmm_root.rgn_len, PAGE_SIZE);


    physmm_root.rgn_paddr = ALIGN_UP(kernel_lma_end, PAGE_SIZE);

    physmm_root.desc_paddr = physmm_root.rgn_paddr  + 
                             physmm_root.rgn_area_len;

    kseg_bmp_area_len = BITMAP_SIZE_FOR_AREA(kernel_loc_map.length);

    struct_space = ALIGN_UP(physmm_root.desc_len, PAGE_SIZE) +
                   ALIGN_UP(physmm_root.rgn_len, PAGE_SIZE) + 
                   ALIGN_UP(kseg_bmp_area_len, PAGE_SIZE);


    /* If we don't have space after the kernel
     * we will check if there is any space before it
     */ 

    if(kernel_lma_end + struct_space > 
       kernel_loc_map.base + kernel_loc_map.length)
    {
        physmm_root.rgn_paddr  = ALIGN_DOWN(kernel_lma_start - struct_space,PAGE_SIZE);
        physmm_root.desc_paddr = physmm_root.rgn_paddr + physmm_root.rgn_area_len;
        place_before_kernel = 1;
    }

    if(place_before_kernel == 1)
    {
        /* in case the kernel is in the same 
         * segment as the boot paging structures,
         * then we will check if there is any risk
         * of overwriting it.
         */ 
       if(physmm_has_boot_paging(&kernel_loc_map))
       {
           if((kernel_lma_start - struct_space) <= 
              (boot_paging + boot_paging_len))
              {
                  return(-1);
              }
       } 

        /* if there is no space before the kernel in the segment,
         * we will exit for now - further implementation
         * should allow the strucutres to reside in another segment
         */ 
       if(kernel_lma_start - struct_space < kernel_loc_map.base)
            return(-1);

    }


    pv[0] = &rgn;
    pv[1] = &desc_pos;
    pv[2] = (void*)place_before_kernel;
    pv[3] = &kernel_loc_map;

    mem_map_iter(mem_iter_build_structs, pv);

#if 1
    kprintf("----Structure summary----\n");
    kprintf("Kernel start 0x%x end 0x%x\n",kernel_lma_start, kernel_lma_end);
    kprintf("Region start 0x%x end 0x%x\n", physmm_root.rgn_paddr, physmm_root.rgn_area_len);
    kprintf("Desc start 0x%x end 0x%x\n", physmm_root.desc_paddr, physmm_root.desc_area_len);
    kprintf("Structure size 0x%x\n",struct_space);
#endif
}

