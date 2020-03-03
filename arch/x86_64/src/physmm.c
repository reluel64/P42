/* Physical memory manager */
#include <stdint.h>
#include <paging.h>
#include <descriptors.h>
#include <multiboot.h>
#include <vga.h>
#include <physmm.h>
#include <stddef.h>
#include <memory_map.h>
#include <utils.h>
#include <pagemgr.h>
#include <vmmgr.h>

#define BITMAP_SIZE_FOR_AREA(x)  (((x) / PAGE_SIZE) / 8)
#define GIGA_BYTE(x) ((x) * 1024ull * 1024ull *1024ull)
#define MEGA_BYTE(x) ((x) * 1024ull *1024ull)
#define KILO_BYTE(x) ((x) * 1024ull)
#define IN_SEGMENT(x,base,len) (((x)>=(base) && (x)<=(base)+(len)))

#define DMA_MEMORY_RANGE MEGA_BYTE(16)

extern uint64_t KERNEL_LMA;
extern uint64_t KERNEL_LMA_END;
extern uint64_t KERNEL_VMA;
extern uint64_t KERNEL_VMA_END;
extern uint64_t BOOT_PAGING;
extern uint64_t BOOT_PAGING_END;
extern uint64_t BOOT_PAGING_LENGTH;


static uint64_t boot_paging     = (uint64_t)&BOOT_PAGING;
static uint64_t boot_paging_len = (uint64_t)&BOOT_PAGING_LENGTH;
static uint64_t boot_paging_end = (uint64_t)&BOOT_PAGING_END;

static uint64_t physmm_boot_alloc_pf();
static int physmm_boot_free_pf(uint64_t phys_addr);

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
 * +----------------------+
 * |       Array of       |
 * | phys_mm_avail_desc_t |
 * +----------------------+
 * |       Array of       |
 * |   phys_mm_region_t   |
 * +----------------------+ <- memory segment size - BITMAP_SIZE_FOR_AREA
 * |   Available memory   |
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

typedef struct _phys_mm_region_t phys_mm_region_t;

typedef struct 
{
    uint64_t           memory_size;
    uint32_t           rsrvd_regions;
    uint32_t           avail_regions;
    uint64_t           rgn_paddr;
    uint64_t           rgn_vaddr;
    uint64_t           desc_paddr;
    uint64_t           desc_vaddr;
    uint64_t           desc_len;
    uint64_t           rgn_len;
    uint64_t           rgn_area_len;
    uint64_t           desc_area_len;
    uint64_t           kernel_phys_base;
    uint64_t           kernel_phys_end;
    uint64_t           kernel_size;
    uint64_t           kernel_virt_base;
    uint64_t           kernel_virt_end;
    memory_map_entry_t kernel_segment;
}phys_mm_root_t;

typedef struct _phys_mm_region_t
{
    uint8_t type;
    uint64_t base;
    uint64_t length;
    uint64_t phys_pv;
    void *virt_pv;
}phys_mm_region_t;

typedef struct phys_mm_avail_desc_t
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

        /* let's handle the the kernel segment as it's special*/
        if(region.base == kernel_loc_map->base)
        {
            desc.bmp_phys_addr = ALIGN_DOWN(physmm_root.desc_area_len + 
                                            physmm_root.desc_paddr,
                                            PAGE_SIZE);
        }

        if((desc.bmp_phys_addr + desc.bmp_area_len) > 
            (region.base + region.length))
        {
            desc.bmp_phys_addr -= PAGE_SIZE;
        }
        kprintf("Segment End 0x%x\n",region.base+region.length);
        kprintf("Bitmap Start 0x%x BitmapEnd 0x%x\n",desc.bmp_phys_addr,desc.bmp_phys_addr+desc.bmp_area_len);

        /* start building the bitmap for this descriptor*/

        for(uint64_t pf = 0; pf < desc.pf_count;pf++)
        {
            
            pf_start = region.base + PAGE_SIZE * pf;
            pf_end   = pf_start + PAGE_SIZE;
           
            /* get 8 bytes */
            if((pf % 64) == 0)
            {
                 bmp_buf = (uint64_t*)pagemgr_boot_temp_map(desc.bmp_phys_addr + 
                                                    sizeof(uint64_t) * (pf / 64)
                                                   );
                (*bmp_buf) = 0;
            }

            /* mark the page frames as busy */
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
            }

            else if(pf_start >= physmm_root.desc_paddr && 
               pf_end <=  physmm_root.desc_paddr + physmm_root.desc_area_len)
            {
                (*bmp_buf) |= (1 << (pf % 64));
                desc.avail_pf--;
            }
            
            else if(pf_start >= desc.bmp_phys_addr && 
              pf_end <= ALIGN_UP(desc.bmp_phys_addr + desc.bmp_len, PAGE_SIZE))
            {
                (*bmp_buf) |= (1 << (pf % 64));
                desc.avail_pf--;
            }
            else if(pf_start >= boot_paging && pf_end<= boot_paging_end)
            {
                (*bmp_buf) |= (1 << (pf % 64));
                desc.avail_pf--;
            }
        }
        /* Save desc to memory */
        
        region.phys_pv = physmm_root.desc_paddr + (*desc_pos);
        mem_desc = (phys_mm_avail_desc_t*)pagemgr_boot_temp_map(region.phys_pv);

        memcpy(mem_desc, &desc, sizeof(phys_mm_avail_desc_t));

        (*desc_pos)+= sizeof(phys_mm_avail_desc_t);
    }

	/* save region to memory */

    mem_region = (phys_mm_region_t*)pagemgr_boot_temp_map(physmm_root.rgn_paddr + (*rgn_pos));

    memcpy(mem_region, &region, sizeof(phys_mm_region_t));

    (*rgn_pos)+= sizeof(phys_mm_region_t);

}

void physmm_early_init(void)
{
    kprintf("Initializing Early Physical Memory Manager\n");
    void *pv[2];
    uint64_t rgn = 0;
    uint64_t desc_pos = 0;
    uint64_t kseg_bmp_area_len = 0;
    uint64_t struct_space = 0;

    memset(&physmm_root,0,sizeof(physmm_root));
    
    physmm_root.kernel_phys_base = (uint64_t)&KERNEL_LMA;
    physmm_root.kernel_phys_end  = (uint64_t)&KERNEL_LMA_END;
    physmm_root.kernel_virt_base = (uint64_t)&KERNEL_VMA;
    physmm_root.kernel_virt_end  = (uint64_t)&KERNEL_VMA_END;
    physmm_root.kernel_size      = physmm_root.kernel_phys_end - 
                                   physmm_root.kernel_phys_base;

    mem_map_iter(mem_iter_fill_root, NULL);

    /* How many bytes does the area for regions occupy - PAGE multiple */
    physmm_root.desc_area_len = ALIGN_UP(physmm_root.desc_len, PAGE_SIZE);

    /* How many bytes does the area for descriptors occupy - PAGE multiple */
    physmm_root.rgn_area_len  = ALIGN_UP(physmm_root.rgn_len, PAGE_SIZE);

    kseg_bmp_area_len = BITMAP_SIZE_FOR_AREA(physmm_root.kernel_segment.length);

    struct_space = physmm_root.rgn_area_len                 +
                   physmm_root.desc_area_len                + 
                   ALIGN_UP(kseg_bmp_area_len, PAGE_SIZE);

    /* If we don't have space after the kernel
     * we will check if there is any space before it
     */ 

    if(physmm_root.kernel_phys_end + struct_space >=
       physmm_root.kernel_segment.base + physmm_root.kernel_segment.length)

    {
        physmm_root.rgn_paddr  = ALIGN_DOWN(physmm_root.kernel_phys_base - struct_space,PAGE_SIZE);

        if(physmm_root.rgn_paddr < DMA_MEMORY_RANGE)
        {
            kprintf("PANIC: Cannot use DMA reserved memory\n");

            /* at this point we should try to search another segment */
            while(1);
        }
    }
    else
    {
        physmm_root.rgn_paddr = ALIGN_DOWN(physmm_root.kernel_segment.base + 
                                           ALIGN_UP(physmm_root.kernel_segment.length - struct_space, PAGE_SIZE), 
                                           PAGE_SIZE);
    }

    physmm_root.desc_paddr = physmm_root.rgn_paddr  + 
                             physmm_root.rgn_area_len;
    

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

}

/* Allocate a page frame using the boot paging */

static uint64_t physmm_boot_alloc_pf()
{
    phys_mm_region_t region;
    phys_mm_avail_desc_t desc;
    uint64_t i = 0;
    uint64_t pf_pos = 0;
    uint64_t *bitmap = NULL;
    uint64_t phys_addr = 0;
    void *wbuf = NULL;

    while(i < physmm_root.rgn_len)
    {
        region = *(phys_mm_region_t*)pagemgr_boot_temp_map(physmm_root.rgn_paddr + i);


         /* do not touch low memory (< 1MB) */
        if(region.type != MEMORY_USABLE || 
          (region.base + region.length < MEGA_BYTE(1)))
        {
            i += sizeof(phys_mm_region_t);
            continue;
        }
        
        if(region.base < DMA_MEMORY_RANGE && region.base + region.length > DMA_MEMORY_RANGE)
            pf_pos = DMA_MEMORY_RANGE / PAGE_SIZE;
        else
            pf_pos = 0;

        desc   = *(phys_mm_avail_desc_t*)pagemgr_boot_temp_map(region.phys_pv);

        if(pf_pos >= desc.pf_count || desc.avail_pf == 0)
        {
           i+=sizeof(phys_mm_region_t);
           continue;
        }

        bitmap = (uint64_t*)pagemgr_boot_temp_map(desc.bmp_phys_addr + 
                                            sizeof(uint64_t) * (pf_pos / 64)
                                           );

        while(pf_pos < desc.pf_count)
        {
            if(pf_pos % 64)
            {
            bitmap = (uint64_t*)pagemgr_boot_temp_map(desc.bmp_phys_addr + 
                                                    sizeof(uint64_t) * (pf_pos / 64)
                                                );
            }
			
			/* mark the page frame as used */
            if(!((*bitmap) & (1 << (pf_pos % 64))))
            {
                phys_addr = region.base + pf_pos * PAGE_SIZE;
                (*bitmap) |= (1 << (pf_pos % 64));
                desc.avail_pf--;
                break;
            }

            pf_pos++;
        }

        if(phys_addr!= 0)
            break;
    }
    /* update the desciptor */
    wbuf = (void*)pagemgr_boot_temp_map(region.phys_pv);

    memcpy(wbuf, &desc, sizeof(desc));
    return(phys_addr);
}

/* Free a page frame using the boot paging */

static int physmm_boot_free_pf(uint64_t phys_addr)
{
    phys_mm_region_t     region;
    phys_mm_avail_desc_t desc;
    uint64_t             i      = 0;
    uint64_t             pf_pos = 0;
    uint64_t            *bitmap = NULL;
    void                *wbuf   = NULL;
    int                  status = 0;

    while(i < physmm_root.rgn_len)
    {
        region = *(phys_mm_region_t*)pagemgr_boot_temp_map(physmm_root.rgn_paddr + i);

        if(region.type != MEMORY_USABLE ||
        !((region.base < phys_addr && phys_addr < region.base+region.length)))
        {
            i+=sizeof(phys_mm_region_t);
            continue;
        }

        desc   = *(phys_mm_avail_desc_t*)pagemgr_boot_temp_map(region.phys_pv);

        pf_pos = (phys_addr - region.base) / PAGE_SIZE;

        bitmap = (uint64_t*)pagemgr_boot_temp_map(desc.bmp_phys_addr + 
                                            sizeof(uint64_t) * (pf_pos / 64)
                                            );
		
		/* clear the page frame */
        if((*bitmap) & (1 << (pf_pos % 64)))
            {
               (*bitmap) &= ~(1 << (pf_pos % 64)); 
               desc.avail_pf++;
               status = 0;
                break;
            }
            else
            {
                status = -1;
                break;
            }
        }
         
    /* update the desciptor */
    wbuf = (void*)pagemgr_boot_temp_map(region.phys_pv);

    memcpy(wbuf, &desc, sizeof(desc));
   
    return(status);
}

uint64_t physmm_early_alloc_pf(void)
{
    return(physmm_boot_alloc_pf());
}

int physmm_early_free_pf(uint64_t pf)
{
    return(physmm_boot_free_pf(pf));
}

void physmm_init(void)
{
    phys_mm_region_t     *rgn      = NULL;
    phys_mm_avail_desc_t *avdesc   = NULL;
    uint64_t          rgn_pos  = 0;
    uint64_t          desc_pos = 0;

    physmm_root.rgn_vaddr = (uint64_t)vmmgr_early_map(physmm_root.rgn_paddr,
                                            0,
                                            physmm_root.rgn_area_len,
                                            0);
   
    physmm_root.desc_vaddr = (uint64_t)vmmgr_early_map(physmm_root.desc_paddr,
                                            0,
                                            physmm_root.desc_area_len,
                                            0);
   
    /* Begin linking the available memory addresses */
 
    while(rgn_pos < physmm_root.rgn_len)
    {
        rgn = (phys_mm_region_t*)(physmm_root.rgn_vaddr + rgn_pos);
        
        kprintf("RGN 0x%x BASE 0x%x LEN 0x%x Type 0x%x\n",rgn,rgn->base,rgn->length, rgn->type);

        if(rgn->type == MEMORY_USABLE)
        {
            rgn->virt_pv = (void*)(physmm_root.desc_vaddr + 
                                   rgn->phys_pv           - 
                                   physmm_root.desc_paddr);
            avdesc = (phys_mm_avail_desc_t*)rgn->virt_pv;

            avdesc->bmp_virt_addr = (uint64_t)vmmgr_early_map(avdesc->bmp_phys_addr,
                                                    0,
                                                    avdesc->bmp_area_len,
                                                    0);

            kprintf("BMPVADDR 0x%x BMPLEN 0x%x\n",avdesc->bmp_virt_addr,avdesc->bmp_len);
                                                 
        }

        rgn_pos += sizeof(phys_mm_region_t);
    }
}

int physmm_alloc_pf(uint64_t length, uint8_t flags, alloc_cb cb)
{
    phys_mm_region_t     *rgn  = (phys_mm_region_t*)physmm_root.rgn_vaddr;
    phys_mm_avail_desc_t *desc;

    length = ALIGN_UP(length, PAGE_SIZE);

    
}