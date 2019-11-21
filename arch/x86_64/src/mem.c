/* PPhysical memory allocator */
#include <stdint.h>
#include <page.h>
#include <descriptors.h>
#include <multiboot.h>
#include <vga.h>
#include <stddef.h>

extern uint32_t mb_addr; /* address of the multiboot header */
extern uint32_t mb_present; /* multiboot header presence */
extern uint64_t KERNEL_LMA_END, KERNEL_LMA;
extern uint64_t KERNEL_VMA;
extern uint64_t read_cr3();
extern void write_cr3(uint64_t phys_addr);
extern void __invlpg(uint64_t address);
extern void flush_pages(void);
extern char * itoa(unsigned long value, char * str, int base);
extern uint64_t randomize(uint64_t seed);
extern uint64_t random_seed();
extern uint64_t EARLY_PAGE_MEMORY_AREA;

static uint64_t kernel_virtual_base = (uint64_t)&KERNEL_VMA;
static uint64_t phys_mm_phys_base;
static uint64_t phys_mm_virt_base;
static uint64_t phys_mm_header_count;
static uint64_t kernel_lma_start = (uint64_t)&KERNEL_LMA;
static uint64_t kernel_lma_end = (uint64_t)&KERNEL_LMA_END;
char buf[64];

typedef struct 
{
    uint64_t base;      /* physical base */
    uint64_t length;    /* physical length */   
    uint64_t avail;     /* 4KB chunks available */
    uint64_t bitmap_len;
    uint8_t  *bitmap_phys;   /* bitmap physical address */
}phys_mem_bitmap_hdr_t;

#define PAGE_SIZE (0x1000)
#define ALIGN(addr, alignment) ((addr) + ((((alignment) - (addr) % (alignment)) % (alignment))))
#define HEADER_AREA_LEN(x) ((x) * sizeof(phys_mem_bitmap_hdr_t))
#define EARLY_PHYS_TO_VIRT(x) ((uint64_t)(x) + (((uint64_t)(&KERNEL_VMA))))
#define EARLY_VIRT_TO_PHYS(x) ((uint64_t)(x) - (((uint64_t)(&KERNEL_VMA))))
/* calculate how many bytes we need to keep track
 * of the memory area specified by 'length'
 */
static uint64_t phys_mm_bitmap_size(uint64_t length)
{
    uint64_t page_count = length / PAGE_SIZE;

    /* no page count, no bitmap */
    if(page_count == 0)
        return(-1);

    return(page_count / 8);
}

static uint64_t phys_temp_map(uint64_t phys_addr)
{
    uint32_t early_pt_len = *(uint32_t*)
                        ((uint8_t*)&EARLY_PAGE_MEMORY_AREA + kernel_virtual_base);
                            
    uint64_t virt_addr =   kernel_virtual_base | (511 << 21 )|(510 << 12);
    
    /* mark the page as present and writeable */
    uint64_t  *virtPG = (uint64_t*)(kernel_lma_end + kernel_virtual_base + 0x3000 + 511 * 4096 + 510 * 8);

    virtPG[0] =  phys_addr            | 0x3;
    virtPG[1] =  phys_addr + 0x1000   | 0x3;
    flush_pages();

    return(virt_addr);
}


static uint64_t  phys_mm_get_random_area
(
uint64_t base,
uint64_t area_length,
uint64_t req_length,
uint64_t rand_interval
)
{
    uint64_t mem_map_pos = 0;
    uint64_t seed = random_seed();
    uint64_t mask = 0;
    uint64_t bit = 0;
    uint64_t address = 0;
    if(req_length > area_length)
    {
        return(-1);
    }
    
    /* compute mask */

    while(mask < rand_interval)
    {
        mask |= (0x1 << bit);
        bit++;
    }

    seed &= mask;

    while(1)
    {
        seed = randomize(seed);
        seed &= mask;
        address = (base + seed + req_length) ;
        address = ALIGN(address, PAGE_SIZE);

        if(address < (base + area_length))
        {
            break;
        }

    }
    return(ALIGN(base + seed, PAGE_SIZE));
}



/* init_phys_mm - initialize physical memory manager
 *
 * To initialize the physical memory manager we will
 * create headers for each range of available memory
 * and then we  will create a bitmap for that range.
 * The bitmap represents 4KB chunks which happen to be
 * the smallest page size on x86 architecture
 */



void init_phys_mm()
{
    vga_print("\nInitializing memory manager\n",0x7,-1);
    uint32_t mb_info_addr_location = *(uint32_t*)((uint8_t*)&mb_addr + kernel_virtual_base);
    multiboot_info_t *mb_info =  (multiboot_info_t*)(mb_info_addr_location + kernel_virtual_base);
    multiboot_memory_map_t *mem_map = (multiboot_memory_map_t*)((uint64_t)mb_info->mmap_addr + kernel_virtual_base);
    multiboot_memory_map_t *temp_mem_map = mem_map;
    uint64_t mem_map_end = (uint64_t)mem_map + mb_info->mmap_length;
    phys_mem_bitmap_hdr_t *hdr_ptr = NULL;
    phys_mem_bitmap_hdr_t temp_hdr = {0};
    
    uint64_t phys_mm_header_base = 0;
    uint64_t kernel_seg_len = 0;
    uint64_t req_len = 0;
    uint32_t mem_segments = 0;
    uint8_t  i = 0;
    uint64_t page_count = 0;
    uint64_t hdr_pos = 0;
    uint64_t hdr_page_pos = 0;
    uint64_t bitmap_pos = 0;

    uint64_t bmp_page_pos = 0;
    uint8_t *bitmap = 0;
    uint64_t temp_bitmap_phys;
    uint64_t phys_addr = 0;
    /* Get range count
     * we will use this to know how may headers to build */

    while((uint64_t)mem_map < mem_map_end)
    {
       if(mem_map->type == MULTIBOOT_MEMORY_AVAILABLE)
       {
        if((mem_map->addr <= (uint64_t)&KERNEL_LMA )&& 
           (mem_map->addr + mem_map->len > (uint64_t)&KERNEL_LMA_END))
           {
               kernel_seg_len = mem_map->len;
           }
           mem_segments++;
       }
       mem_map = (multiboot_memory_map_t*)((uint64_t) mem_map + mem_map->size + sizeof(mem_map->size));
    }

    /* restore mem_map value */
    mem_map = temp_mem_map;

    /* offset the phys mm header creation by one page */
    phys_mm_header_base = kernel_lma_end;
    phys_mm_header_base += *(uint32_t*)
                            ((uint8_t*)&EARLY_PAGE_MEMORY_AREA + kernel_virtual_base);
    
    phys_mm_header_base = ALIGN(phys_mm_header_base, PAGE_SIZE);

    req_len = ALIGN(HEADER_AREA_LEN(mem_segments), PAGE_SIZE) + 
              ALIGN(phys_mm_bitmap_size(kernel_seg_len),PAGE_SIZE);


    /* begin randomization of the header base - right after the kernel */
    phys_mm_header_base = phys_mm_get_random_area (phys_mm_header_base,
                                                   kernel_seg_len,
                                                   req_len,
                                                   0x1000000          /* 16MB of entropy */
                                                  );
  
    /* we got the base - it's time to build the headers */
    phys_mm_phys_base = phys_mm_header_base;
    phys_mm_header_count = mem_segments;

    /* map the physical header base to the first page */
    hdr_ptr = (phys_mem_bitmap_hdr_t*)phys_temp_map(phys_mm_phys_base);
    hdr_pos = 0;

    /* Begin creating the headers */
    while((uint64_t)mem_map < mem_map_end)
    {
       if(mem_map->type == MULTIBOOT_MEMORY_AVAILABLE)
       {
           if(hdr_page_pos >= PAGE_SIZE)
           {
               phys_mm_header_base += PAGE_SIZE;
               hdr_page_pos -= PAGE_SIZE;
               hdr_pos = 0;
               
               hdr_ptr = (phys_mem_bitmap_hdr_t*)(phys_temp_map(phys_mm_header_base) + hdr_page_pos);
           }
           
           hdr_ptr[hdr_pos].base = mem_map->addr;
           hdr_ptr[hdr_pos].length = mem_map->len;
           hdr_ptr[hdr_pos].avail = mem_map->len / PAGE_SIZE;
           hdr_ptr[hdr_pos].bitmap_len = ALIGN((mem_map->len / PAGE_SIZE) / 8, PAGE_SIZE);
           
            if((mem_map->addr <= kernel_lma_start) && 
               (mem_map->addr + mem_map->len > kernel_lma_end))
            {
              hdr_ptr[hdr_pos].bitmap_phys = ALIGN(HEADER_AREA_LEN(mem_segments), PAGE_SIZE) + phys_mm_header_base;
              hdr_ptr[hdr_pos].bitmap_phys = ALIGN((uint64_t)hdr_ptr[hdr_pos].bitmap_phys, PAGE_SIZE);
            }
           else
           {
               hdr_ptr[hdr_pos].bitmap_phys = ALIGN(mem_map->addr, PAGE_SIZE);
           }

           hdr_pos++;
           hdr_page_pos += sizeof(phys_mem_bitmap_hdr_t);
       }

       mem_map = (multiboot_memory_map_t*)((uint64_t) mem_map + mem_map->size + sizeof(mem_map->size));
       
    }
    /* reset our position */
    hdr_pos = 0;
    hdr_page_pos = 0;

    /* We have the headers - create the bitmaps 
     * To do this we will iterate through the headers
     * and use the information from each header to 
     * create its bitmap
     **/

    for(uint32_t hdr = 0; hdr < mem_segments; hdr++)
    {
        if (hdr_page_pos >= PAGE_SIZE)
        {
            phys_mm_header_base += PAGE_SIZE;
            hdr_page_pos -= PAGE_SIZE;
            hdr_ptr = (phys_mem_bitmap_hdr_t*)(phys_temp_map(phys_mm_header_base) + hdr_page_pos);
            hdr_pos = 0;
        }

        temp_hdr = hdr_ptr[hdr_pos];
        page_count = hdr_ptr[hdr_pos].avail;
        hdr_pos++;
        
        bmp_page_pos = 0;
        temp_bitmap_phys = (uint64_t)temp_hdr.bitmap_phys;

        bitmap = (uint8_t*)(phys_temp_map(temp_bitmap_phys));
       
        for(uint64_t i = 0; i < page_count; i++)
        {
            if (bmp_page_pos > PAGE_SIZE)
            {
                temp_bitmap_phys += PAGE_SIZE;
                bmp_page_pos     -= PAGE_SIZE;
                bitmap = (uint8_t*)(phys_temp_map(temp_bitmap_phys) + bmp_page_pos);
            }

            phys_addr = (i * PAGE_SIZE) + temp_hdr.base;
           
           if(i % 8 == 0)
           {
               bitmap[bmp_page_pos] = 0;
           }

            if((phys_addr >= kernel_lma_start) && 
               (phys_addr < kernel_lma_end))
            {
                bitmap[bmp_page_pos] |= (1 << (i % 8));
            }
            else if((phys_addr >= (uint64_t)temp_hdr.bitmap_phys) && 
                    (phys_addr < ((uint64_t)temp_hdr.bitmap_phys + temp_hdr.bitmap_len)))
            {
                bitmap[bmp_page_pos] |= (1 << (i % 8));
            }
            else if((phys_addr >= phys_mm_phys_base) && 
                    (phys_addr < phys_mm_phys_base + ALIGN(HEADER_AREA_LEN(mem_segments), PAGE_SIZE)))
            {
                bitmap[bmp_page_pos] |= (1 << (i % 8));
            }
            else
            {
                bitmap[bmp_page_pos] &= ~(1 << (i % 8));
            }

            /* check if it's time to advance the cursor */
            if((i + 1) % 8 == 0)
            {
                bmp_page_pos++;
            }
        }
   

        hdr_ptr = (phys_mem_bitmap_hdr_t*)(phys_temp_map(phys_mm_header_base));
        hdr_page_pos += sizeof(phys_mem_bitmap_hdr_t);
    }

}

void init_page_table(void)
{
    init_phys_mm();
    
//phys_temp_map(0);
   
#if 0
    vga_print("\n",0x7,-1);
    for(int i = 0; i<16; i++)
    {
    itoa(truePDT[i] + 0xFFFFFFFF80000000,buf,16);

    vga_print(buf,0x7,64);
    vga_print("\n",0x7,-1);
    }
    #endif
}
