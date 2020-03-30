/* Physical memory manager */
#include <stdint.h>
#include <paging.h>
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
#define ISA_DMA_MEMORY_END MEGA_BYTE(16)
#define ISA_DMA_MEMORY_BEGIN MEGA_BYTE(1)
#define REGION_COUNT(x) ((x) / sizeof(phys_mm_region_t))
#define PAGES_TO_BYTES(x) ((x) * PAGE_SIZE)
#define BYTES_TO_PAGES(x) ((x) / PAGE_SIZE)

static phys_addr_t boot_paging     = (phys_addr_t)&BOOT_PAGING;
static size_t boot_paging_len = (size_t)&BOOT_PAGING_LENGTH;
static phys_addr_t boot_paging_end = (phys_addr_t)&BOOT_PAGING_END;

static int physmm_boot_alloc_pf
(
    phys_size_t  count, 
    uint8_t flags, 
    alloc_cb cb, 
    void *pv
);

static int physmm_alloc_pf
(
    phys_size_t pages, 
    uint8_t flags, 
    alloc_cb reqcb, 
    void *pv
);

static int physmm_free_pf(free_cb cb, void *pv);

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

typedef struct 
{
    phys_size_t           memory_size;
    uint32_t              rsrvd_regions;
    uint32_t              avail_regions;
    phys_addr_t           rgn_paddr;
    phys_addr_t           rgn_vaddr;
    phys_addr_t           desc_paddr;
    phys_addr_t           desc_vaddr;
    phys_size_t           desc_len;
    phys_size_t           rgn_len;
    phys_size_t           rgn_area_len;
    phys_size_t           desc_area_len;
    phys_addr_t           kernel_phys_base;
    phys_addr_t           kernel_phys_end;
    phys_size_t           kernel_size;
    phys_addr_t           kernel_virt_base;
    phys_addr_t           kernel_virt_end;
    memory_map_entry_t    kernel_segment;
}phys_mm_root_t;

typedef struct _phys_mm_region_t
{
    uint8_t  type;          /* type of the memory region         */
    phys_addr_t base;          /* starting physical address         */
    phys_size_t length;        /* length                            */
    phys_addr_t phys_pv;       /* physical address of detailed info */
    void    *virt_pv;       /* virtual address of detaled info   */
}phys_mm_region_t;

typedef struct phys_mm_avail_desc_t
{
    phys_size_t pf_count;      /* number of 4KB page frames             */
    phys_size_t avail_pf;      /* available page frames                 */
    phys_addr_t bmp_phys_addr; /* physical address of the bitmap        */
    virt_addr_t *bmp;          /* virtual address of the bitmap         */
    phys_size_t bmp_len;       /* bitmap length in bytes                */
    phys_size_t bmp_area_len;  /* bitmap length in bytes (page aligned) */
    phys_size_t avail_pos;
}phys_mm_avail_desc_t;

static phys_mm_root_t physmm_root;
static physmm_t physmm_if;

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
    void               **ppv            = (void**)pv;
    phys_size_t         *rgn_pos        = ppv[0];
    phys_size_t         *desc_pos       = ppv[1];
    phys_addr_t          pf_start       = 0;
    phys_addr_t          pf_end         = 0;
    phys_addr_t         *bmp            = NULL;
    phys_mm_region_t     *mem_region     = NULL;
    phys_mm_region_t      region         = {0};
    phys_mm_avail_desc_t *mem_desc       = NULL;
    phys_mm_avail_desc_t  desc           = {0};
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

        for(phys_size_t pf = 0; pf < desc.pf_count;pf++)
        {
            
            pf_start = region.base + PAGE_SIZE * pf;
            pf_end   = pf_start + PAGE_SIZE;
           
            /* get 8 bytes */
            if((pf % PF_PER_ITEM) == 0)
            {
                 bmp = (phys_addr_t*)pagemgr_boot_temp_map(desc.bmp_phys_addr + 
                                                        sizeof(phys_addr_t) * (pf / PF_PER_ITEM)
                                                       );
                                     
                (*bmp) = (phys_addr_t)0;
            }

            /* mark the page frames as busy */
            if((pf_start >= physmm_root.kernel_phys_base) && 
               (pf_end <= physmm_root.kernel_phys_end))
            {
                (*bmp) |= (1 << (pf % PF_PER_ITEM));
                desc.avail_pf--;
            }

            else if(pf_start >= physmm_root.rgn_paddr && 
               pf_end <= physmm_root.rgn_paddr + physmm_root.rgn_area_len)
            {
                (*bmp) |= (1 << (pf % PF_PER_ITEM));
                desc.avail_pf--;
            }

            else if(pf_start >= physmm_root.desc_paddr && 
               pf_end <=  physmm_root.desc_paddr + physmm_root.desc_area_len)
            {
                (*bmp) |= (1 << (pf % PF_PER_ITEM));
                desc.avail_pf--;
            }
            
            else if(pf_start >= desc.bmp_phys_addr && 
              pf_end <= desc.bmp_phys_addr + desc.bmp_area_len)
            {
                (*bmp) |= (1 << (pf % PF_PER_ITEM));
                desc.avail_pf--;
            }
            else if(pf_start >= boot_paging && pf_end <= boot_paging_end)
            {
                (*bmp) |= (1 << (pf % PF_PER_ITEM));
                desc.avail_pf--;
            }
    
        }
        /* Save desc to memory */
        
        region.phys_pv = physmm_root.desc_paddr + (*desc_pos);
        mem_desc = (phys_mm_avail_desc_t*)pagemgr_boot_temp_map(region.phys_pv);

        memcpy(mem_desc, &desc, sizeof(phys_mm_avail_desc_t));

        (*desc_pos) += sizeof(phys_mm_avail_desc_t);
    }
    
    /* save region to memory */

    mem_region = (phys_mm_region_t*)pagemgr_boot_temp_map(physmm_root.rgn_paddr + (*rgn_pos));

    memcpy(mem_region, &region, sizeof(phys_mm_region_t));

    (*rgn_pos) += sizeof(phys_mm_region_t);

}

void physmm_early_init(void)
{
    void *pv[2];
    phys_size_t rgn = 0;
    phys_size_t desc_pos = 0;
    phys_size_t kseg_bmp_area_len = 0;
    phys_size_t struct_space = 0;

    kprintf("Initializing Early Physical Memory Manager\n");
    memset(&physmm_root,0,sizeof(physmm_root));
    
    physmm_root.kernel_phys_base = (phys_size_t)&KERNEL_LMA;
    physmm_root.kernel_phys_end  = (phys_size_t)&KERNEL_LMA_END;
    physmm_root.kernel_virt_base = (phys_size_t)&KERNEL_VMA;
    physmm_root.kernel_virt_end  = (phys_size_t)&KERNEL_VMA_END;
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

        if(physmm_root.rgn_paddr < ISA_DMA_MEMORY_END && 
           physmm_root.rgn_paddr >= ISA_DMA_MEMORY_END)
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
    memset(&physmm_if, 0, sizeof(physmm_t));
    physmm_if.alloc = physmm_boot_alloc_pf;
}

/* Allocate a page frame using the boot paging */

static int physmm_boot_alloc_pf
(
    phys_size_t count, 
    uint8_t flags, 
    alloc_cb cb, 
    void *pv
)
{
    phys_mm_region_t     region;
    phys_mm_avail_desc_t desc;
    phys_size_t          i         = 0;
    phys_size_t          pf_pos    = 0;
    phys_addr_t         *bmp       = NULL;
    phys_addr_t          phys_addr = 0;
    phys_size_t          marked_pf = 0;
    void                *wbuf      = NULL;
    int                  status    = 0;
    int                  stop      = 0;

    while(i < physmm_root.rgn_len)
    {
        region = *(phys_mm_region_t*)pagemgr_boot_temp_map(physmm_root.rgn_paddr + i);


         /* do not touch low memory (< 1MB) */
        if(region.type != MEMORY_USABLE || 
          (region.base + region.length <= MEGA_BYTE(1)))
        {
            i += sizeof(phys_mm_region_t);
            continue;
        }
        
        if(region.base <=ISA_DMA_MEMORY_BEGIN && 
           region.base + region.length > ISA_DMA_MEMORY_END)
            pf_pos = (ISA_DMA_MEMORY_END) / PAGE_SIZE;
        else
            pf_pos = 0;

        desc   = *(phys_mm_avail_desc_t*)pagemgr_boot_temp_map(region.phys_pv);

        if(pf_pos >= desc.pf_count || desc.avail_pf == 0)
        {
           i+=sizeof(phys_mm_region_t);
           continue;
        }

        bmp = (phys_addr_t*)pagemgr_boot_temp_map(desc.bmp_phys_addr + 
                                               sizeof(phys_addr_t) * (pf_pos / PF_PER_ITEM)
                                              );

        while(pf_pos < desc.pf_count)
        {

            if((pf_pos % PF_PER_ITEM) == 0)
            {
            bmp = (phys_addr_t*)pagemgr_boot_temp_map(desc.bmp_phys_addr + 
                                                    sizeof(phys_addr_t) * (pf_pos / PF_PER_ITEM)
                                                );
            }
            
            /* mark the page frame as used */
            if((~bmp[0]) & (1 << (pf_pos % PF_PER_ITEM)))
            {
                phys_addr = region.base + pf_pos * PAGE_SIZE;
                if(!cb(phys_addr, 1, pv))
                {
                    stop = 1;
                    break;
                }
                
               
                bmp[0] |= (1 << (pf_pos % PF_PER_ITEM));
                desc.avail_pf--;
                marked_pf++;
            }

            pf_pos++;
        }

        if(marked_pf >= count || stop)
            break;

    }
    /* update the desciptor */
    wbuf = (void*)pagemgr_boot_temp_map(region.phys_pv);

    memcpy(wbuf, &desc, sizeof(desc));
    
    if(marked_pf < count)
        status = -1;
    
    return(status);
}


int physmm_init(void)
{
    phys_mm_region_t     *rgn      = NULL;
    phys_mm_avail_desc_t *avdesc   = NULL;
    phys_size_t              rgn_pos  = 0;
    phys_size_t              desc_pos = 0;
    phys_size_t              pf_pos   = 0;
    phys_size_t              bmp_pos  = 0;

    kprintf("Initializing Physical Memory Manager\n");

    physmm_root.rgn_vaddr = (phys_addr_t)vmmgr_map(physmm_root.rgn_paddr,
                                            0,
                                            physmm_root.rgn_area_len,
                                            VMM_ATTR_WRITABLE);
   
    physmm_root.desc_vaddr = (phys_addr_t)vmmgr_map(physmm_root.desc_paddr,
                                            0,
                                            physmm_root.desc_area_len,
                                            VMM_ATTR_WRITABLE);

    if(physmm_root.desc_vaddr == 0 ||
       physmm_root.rgn_vaddr  == 0)
    {
        return(-1);
    }

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

            avdesc->bmp = (phys_addr_t*)vmmgr_map(avdesc->bmp_phys_addr,
                                                    0,
                                                    avdesc->bmp_area_len,
                                                    VMM_ATTR_WRITABLE);

            if(avdesc->bmp == NULL)
                return(-1);
           
            /* Clear the bitmap that was reserved for boot paging */

            if(rgn->base <= boot_paging && 
               rgn->base + rgn->length >= boot_paging_end) 
            {
                for(phys_size_t i = boot_paging; i < boot_paging_end; i+=PAGE_SIZE)
                {
                    pf_pos = (i - rgn->base) / PAGE_SIZE;
                    bmp_pos = pf_pos / PF_PER_ITEM;

                    if(avdesc->bmp[bmp_pos]  & ((phys_addr_t)1 << (pf_pos % PF_PER_ITEM)))
                    {
                        avdesc->bmp[bmp_pos]  &= ~((phys_addr_t)1 << (pf_pos % PF_PER_ITEM));
                    }
                }
            }

            kprintf("BMPVADDR 0x%x BMPLEN 0x%x\n",avdesc->bmp,avdesc->bmp_len);
        }

        rgn_pos += sizeof(phys_mm_region_t);
    }

    physmm_if.alloc   = physmm_alloc_pf;
    physmm_if.dealloc = physmm_free_pf;
    return(0);
}

/* 
 * physmm_has_contig
 * --------------------------
 * Checks if a descriptor has contiguous
 * pages and if it does, it 
 * sets contig_start to the beginning of the 
 * page set
 */

static int physmm_has_contig
(
    phys_mm_region_t *rgn,
    phys_size_t       pages,
    phys_addr_t      *contig_start
)
{
    phys_size_t  pf_pos           = 0;
    phys_size_t  contig_pages     = 0;
    phys_size_t  bmp_pos          = 0;
    phys_size_t  index            = 0;
    phys_mm_avail_desc_t *desc = NULL;

    if(rgn->type != MEMORY_USABLE)
        return(-1);

    desc          = rgn->virt_pv;

    *contig_start = 0;

    while(pf_pos < desc->pf_count && contig_pages < pages)
    {   
        if((rgn->base + PAGE_SIZE * pf_pos >= ISA_DMA_MEMORY_BEGIN &&
           rgn->base + PAGE_SIZE * pf_pos < ISA_DMA_MEMORY_END))
           {
            pf_pos++;
            continue;
           }

        if(pf_pos % PF_PER_ITEM)
        {
            /* We found a busy pf - reset the counter */
        
            if(desc->bmp[bmp_pos] & (1 << (pf_pos % PF_PER_ITEM)))
            {
                contig_pages = 0;
            }
            else
            {
                if(contig_pages == 0)
                    (*contig_start) = pf_pos;

                contig_pages++;
            }
            pf_pos++;
        }
        else
        {
            if(desc->bmp[bmp_pos] == 0)
            {
                contig_pages   += PF_PER_ITEM;
                (*contig_start) = pf_pos;
            }

            pf_pos += PF_PER_ITEM;
        }

        /* Advance to next element from bitmap */
        if((pf_pos % PF_PER_ITEM) == 0)
            bmp_pos++;
    }

    if(contig_pages < pages)
        return(-1);

    return(0);
}

/* 
 * physmm_find_highest_mem
 * --------------------------
 * Finds the highest available memory
 */


static phys_mm_region_t *physmm_find_highest_mem
(
    phys_size_t  pages
)
{
    phys_mm_region_t     *rgn        = (phys_mm_region_t*)physmm_root.rgn_vaddr;
    phys_mm_avail_desc_t *desc       = NULL;
    phys_size_t              regions    = 0;
    phys_size_t              page_count = 0 ;

    regions = REGION_COUNT(physmm_root.rgn_len);

    for(phys_size_t  i = regions; i > 0 ; i--)
    {
        if(rgn[i - 1].type != MEMORY_USABLE)
            continue;

        if(rgn[i - 1].base  < ISA_DMA_MEMORY_BEGIN)
            continue;

        desc = rgn[i - 1].virt_pv;
        page_count += desc->avail_pf;

        if(page_count >= pages);
            return(rgn + i - 1);
    }

    return(NULL);
}

static phys_mm_region_t *physmm_find_contig
(
    phys_size_t  pages, 
    uint8_t   flags,
    phys_addr_t *contig
)
{
    phys_mm_region_t     *rgn       = (phys_mm_region_t*)physmm_root.rgn_vaddr;
    phys_mm_region_t     *temp_rgn  = NULL;
    phys_size_t              regions   = 0;

    regions = REGION_COUNT(physmm_root.rgn_len);

    for(phys_size_t i = 0; i < regions; i++)
    {
        if(rgn[i].type != MEMORY_USABLE)
            continue;
        
        if(physmm_has_contig(rgn + i, pages, contig) == 0)
            return(rgn + i);
    }

    return(NULL);
}

/* 
 * physmm_alloc_pf
 * --------------------------
 * Allocates page frame(s)
 */

static int physmm_alloc_pf
(
    phys_size_t pages, 
    uint8_t flags, 
    alloc_cb reqcb, 
    void *pv
)
{
    phys_mm_region_t     *rgn         = (phys_mm_region_t*)physmm_root.rgn_vaddr;;
    phys_mm_avail_desc_t *desc        = NULL;
    phys_size_t           marked_pg   = 0;
    phys_size_t           pf_ix       = 0;
    phys_size_t           rgn_ix      = 0;
    phys_size_t           regions     = 0;
    phys_size_t           bmp_ix      = 0;
    phys_addr_t           phys        = 0;
    phys_size_t           pf_count    = 0;
    phys_size_t           mask        = 0;
    phys_size_t           used_pf     = 0;
    uint8_t               pf_pos      = 0;
    uint8_t               stop        = 0;

    if(flags & ALLOC_HIGHEST && (~flags) & ALLOC_CONTIG)
    {
       rgn = physmm_find_highest_mem(pages);

        if(rgn == NULL)
            return(-1);
    }
    else if (flags & ALLOC_CONTIG && (~flags) & ALLOC_HIGHEST)
    {
        rgn = physmm_find_contig(pages, flags, &pf_ix);

        if(rgn == NULL)
            return(-1);
    }

    if(flags & ALLOC_CB_STOP)
        pages = (phys_size_t)~0;

    rgn_ix  = REGION_COUNT((phys_addr_t)rgn - physmm_root.rgn_vaddr);
    regions = REGION_COUNT(physmm_root.rgn_len);
    rgn     = (phys_mm_region_t*)physmm_root.rgn_vaddr;

    for(phys_size_t i = rgn_ix; i < regions; i++)
    {
        if(rgn[i].type != MEMORY_USABLE)
            continue;
        
        desc   = rgn[i].virt_pv;
       // pf_ix  = desc->avail_pos;
        //kprintf("AVAIL_POS %d\n",desc->avail_pos);
        while(pf_ix     < desc->pf_count && 
              marked_pg < pages          && 
              0         < desc->avail_pf )
        {
            phys       = rgn[i].base + pf_ix * PAGE_SIZE;
            bmp_ix = pf_ix / PF_PER_ITEM;
            pf_pos = pf_ix % PF_PER_ITEM;

            if(((~flags) & ALLOC_ISA_DMA))
            {
                if(phys >= ISA_DMA_MEMORY_BEGIN && 
                   phys < ISA_DMA_MEMORY_END)
                {
                    pf_ix = ISA_DMA_MEMORY_END / PAGE_SIZE;
                    continue;
                }
            }
            else
            {
                if(phys >= ISA_DMA_MEMORY_END)
                {
                    stop = 1;
                    break;
                }
                else if(phys < ISA_DMA_MEMORY_BEGIN)
                    break;
            }

            if(phys < LOW_MEMORY)
            {
                pf_ix++;
                continue;
            }

            if(pf_pos == 0)
            {
                /* It's a new set - check if it's free */
                if(desc->bmp[bmp_ix] == (phys_addr_t)0)
                {           
                         
                    /* It looks free, let's see if we can 
                     * mark the entire set as busy
                     */ 
                    
                    pf_count = min(pages - marked_pg, PF_PER_ITEM);

                    /* Call the callback */
                    used_pf = reqcb(phys, pf_count, pv);

                    if(used_pf == 0)
                    {
                        stop = 1;
                        break;
                    }
                    
                    used_pf = min(used_pf, PF_PER_ITEM);
                    mask    = 0;

                    for(phys_size_t i = 0; i< used_pf; i++)
                        mask |= (phys_size_t)1 << i;

                    /* mark the set */
                    desc->bmp[bmp_ix] = mask;

                    /* advance marked page frames */
                    marked_pg  += used_pf;

                    /* advance page frame index */
                    pf_ix      += used_pf;
                    desc->avail_pos = pf_ix;

                    /* update available PFs */
                    if(desc->avail_pf >= used_pf)
                        desc->avail_pf-= used_pf;
                    else
                        desc->avail_pf = 0;

                    continue;
                }
            }

            mask = ((phys_size_t)1 << pf_pos);

            /* This is the slowest path to allocate stuff */
            if((~(desc->bmp[bmp_ix])) & mask)
            {
                /* Only one PF here */
                pf_count = 1;
             
                /* call the callback */
                used_pf = reqcb(phys, pf_count, pv);

                if(used_pf == 0)
                {
                    stop = 1;
                    break;
                }

               // kprintf("ADDR 0x%x\n",phys);
                /* Mark the bit */

                desc->bmp[bmp_ix] |= mask;

                /* decrease available PFs */
                desc->avail_pf--;

                /* advance marked page frames */
                marked_pg++;
                desc->avail_pos = pf_ix + 1;
            }

            pf_ix++;

        }

        /* Once the condition is met, 
         * just break out
         */
        if(marked_pg >= pages || stop)
            break;
    }
    
   // kprintf("REQP %d MP %d\n",pages, marked_pg);
   // kprintf("ALLOC_COUNT %d\n",marked_pg);
    if(marked_pg < pages && ((~flags) & ALLOC_CB_STOP))
        return(-1);

    return(0);
}

static inline int physmm_is_in_range
(
    phys_addr_t base,
    phys_size_t len,
    phys_addr_t req_base,
    phys_size_t req_len
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


static int physmm_free_pf(free_cb cb, void *pv)
{
    phys_size_t           pf_ix      = 0;
    phys_size_t           bmp_ix      = 0;
    phys_size_t           pf_count    = 0;
    phys_addr_t           phys        = 0;
    phys_size_t           pf_count_ix = 0;
    phys_addr_t           paddr       = 0;
    phys_size_t           mask        = 1;
    phys_size_t           regions     = 0;
    uint8_t               pf_pos      = 0;
    phys_mm_region_t     *rgn         = NULL;
    phys_mm_avail_desc_t *desc        = NULL;
    int                   keep_going  = 0;

    regions = REGION_COUNT(physmm_root.rgn_len);
    
    do
    {
        pf_count = 0;
        phys = 0;
        keep_going = cb(&phys, &pf_count, pv);

        
        if(pf_count == 0 && keep_going == 0)
            break;

        rgn = (phys_mm_region_t*)physmm_root.rgn_vaddr;
        
        for(phys_size_t i = 0; i < regions; i++)
        {
            if(rgn[i].type != MEMORY_USABLE)
                continue;

            if(!physmm_is_in_range(rgn[i].base, rgn[i].length, phys, pf_count * PAGE_SIZE))
                    continue;
            
            desc = rgn[i].virt_pv;
            
            pf_count_ix = 0;

            while(pf_count_ix < pf_count)
            {

                paddr = phys + pf_count_ix * PAGE_SIZE;
                pf_ix = (paddr - rgn[i].base) / PAGE_SIZE;
                bmp_ix = pf_ix / PF_PER_ITEM;
                pf_pos = (uint8_t)(pf_ix % PF_PER_ITEM);
                mask = 0;

               // kprintf("PHYS 0x%x PADDR 0x%x\n", phys, paddr);
#if 1
                if(pf_pos == 0)
                {
                    if(pf_count_ix + PF_PER_ITEM < pf_count)
                    {
                        desc->bmp[bmp_ix] = mask;
                        pf_count_ix += PF_PER_ITEM;
                        desc->avail_pf += PF_PER_ITEM;
                        if(pf_ix < desc->avail_pos)
                            desc->avail_pos = pf_ix;
                        continue;
                    }
                }
#endif
                mask = ((phys_size_t) 1 << pf_pos);

                if(desc->bmp[bmp_ix] & mask)
                {
                    desc->bmp[bmp_ix] &= ~mask;
                    desc->avail_pf++;

                    if(pf_ix < desc->avail_pos)
                            desc->avail_pos = pf_ix;
                }
                else
                {
                   kprintf("ALREADY FREE 0x%x\n",phys);
                   
                }

                pf_count_ix++;

                /* If we reach this, we're screwed */
                if(desc->avail_pf > desc->pf_count)
                {
                    
                    kprintf("ERROR - avail pf is above total pf\n");
                    return(-1);
                }
            }          
        }

    }while(keep_going);

    return(0);
}


void physmm_dump_bitmaps(void)
{
    phys_mm_region_t *region  = NULL;
    phys_mm_avail_desc_t *desc = NULL;
    uint64_t regions = 0;
    if(physmm_root.rgn_vaddr == NULL)
        return;
    regions = REGION_COUNT(physmm_root.rgn_len);
    region = (phys_mm_region_t*)physmm_root.rgn_vaddr;

    for(uint64_t rgn_ix = 0; rgn_ix < regions; rgn_ix++)
    {

        if(region[rgn_ix].type != MEMORY_USABLE)
            continue;

        desc = region[rgn_ix].virt_pv;

        kprintf("REGION 0x%x\n",region[rgn_ix].base);

        for(uint64_t bmp_ix = 0; bmp_ix < desc->bmp_len / sizeof(phys_size_t);bmp_ix++)
        {
            if(desc->bmp[bmp_ix] != 0)
                kprintf("BITMAP_IX %d -> 0x%x\n",bmp_ix, desc->bmp[bmp_ix]);
        }
    }
}

physmm_t *physmm_get(void)
{
    return(&physmm_if);
}

int test(uint64_t addr, uint64_t length, void *pv)
{
    static int allocs = 0;
    allocs+=length;
    //kprintf("PHYS %x LEN %d\n",addr,length);
    //kprintf("ALLOCS %d\n", allocs);
    return(length);
    
}

int physmm_test(void)
{ 
   
   int ret = physmm_alloc_pf((1024ull*1024ull*1024ull*16ull)/4096,0,(alloc_cb)test,NULL);
    kprintf("RET %x\n",ret);
    
}
