/* PPhysical memory allocator */
#include <stdint.h>
#include <page.h>
#include <descriptors.h>
#include <multiboot.h>
#include <vga.h>

extern uint32_t mb_addr; /* address of the multiboot header */
extern uint32_t mb_present; /* multiboot header presence */
extern uint64_t KERNEL_LMA_END, KERNEL_LMA;
extern uint64_t PT0;
extern uint64_t KERNEL_VMA;
extern uint64_t read_cr3();
extern void write_cr3(uint64_t phys_addr);
extern void __invlpg(uint64_t address);
extern void flush_pages(void);
extern char * itoa(unsigned long value, char * str, int base);
extern uint64_t randomize(uint64_t seed);
extern uint64_t random_seed();
static uint64_t kernel_virtual_base = (uint64_t)&KERNEL_VMA;
static uint64_t phys_mm_phys_base;
static uint64_t phys_mm_virt_base;
static uint64_t phys_mm_header_count;

#define PAGE_SIZE (0x1000)
#define ALIGN(addr, alignment) ((addr) + ((((alignment) - (addr) % (alignment)) % (alignment))))

typedef struct 
{
    uint64_t base;      /* physical base */
    uint64_t length;    /* physical length */   
    uint64_t avail;     /* 4KB chunks available */
    uint8_t  *bitmap;   /* bitmap length */
}phys_mem_bitmap_hdr_t;

/* phys_temp_map - temporarily map physical memory 
 * We will always map the one page of physical
 * memory starting with KERNEL_VMA
 * Basically, KERNEL_VMA will be our
 */




static void phys_temp_map(uint64_t phys_addr)
{
    uint64_t *virtPT0 = (uint64_t*)((uint8_t*)&KERNEL_LMA_END  + kernel_virtual_base);

    /* mark the page as present and writeable */
    *virtPT0 = phys_addr | 0x3;

    flush_pages();
}


static uint64_t  phys_mm_get_random_area
(
uint64_t base,
uint64_t length,
uint64_t rand_interval
)
{
    char buf[64];
    uint64_t mem_map_pos = 0;
    uint64_t seed = random_seed();
    uint64_t mask = 0;
    uint64_t bit = 0;

    if(rand_interval > length)
    {
        return(-1);
    }
    
    /* compute mask */

    while(mask < rand_interval)
    {
        mask |= (0x1 << bit);
        bit++;
    }
    //mask >>= 1;
    seed &= mask;

    while(1)
    {
        seed = randomize(seed);
        seed &= mask;
        if((base + seed) < (base + rand_interval))
        {
            break;
        }

    }
    return(base + seed);
}

static void phys_mm_setup_bitmap
(
uint64_t phys_add,
uint64_t count
)
{
    
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
    char buf[64];
    uint32_t mb_info_addr_location = *(uint32_t*)((uint8_t*)&mb_addr + kernel_virtual_base);
    multiboot_info_t *mb_info =  (multiboot_info_t*)(mb_info_addr_location + kernel_virtual_base);
    multiboot_memory_map_t *mem_map = (multiboot_memory_map_t*)((uint64_t)mb_info->mmap_addr + kernel_virtual_base);
    multiboot_memory_map_t *temp_mem_map = mem_map;
    uint64_t mem_map_end = (uint64_t)mem_map + mb_info->mmap_length;
    uint32_t range_count = 0;
    
    uint64_t phys_mm_header_base;
    uint64_t kernel_phys_end = (((uint64_t)(&KERNEL_LMA_END)));
    uint64_t kernel_seg_len = 0;
    uint8_t  i = 0;

    /* we are not wrong
     * we will use the first page to map the physical
     * memory which needs to be modified
     */
    phys_mem_bitmap_hdr_t *hdr_ptr = (phys_mem_bitmap_hdr_t*)&KERNEL_VMA;

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
       }
       mem_map = (multiboot_memory_map_t*)((uint64_t) mem_map + mem_map->size + sizeof(mem_map->size));
    }

    /* restore mem_map value */
    mem_map = temp_mem_map;

    /* offset the phys mm header creation by one page */
    phys_mm_header_base = ALIGN(kernel_phys_end, 4096);
    
    /* begin randomization of the header base - right after the kernel */
    phys_mm_header_base = phys_mm_get_random_area (phys_mm_header_base,
                                                   kernel_seg_len,
                                                   0x1000000          /* 16MB of entropy */
                                                  );

    phys_mm_header_base = ALIGN(phys_mm_header_base, 0x1000);
  
    /* we got the base - it's time to build the headers */

    phys_mm_phys_base = phys_mm_header_base;
    phys_mm_header_count = PAGE_SIZE / sizeof(phys_mem_bitmap_hdr_t);

    /* map the physical header base to the first page */
    phys_temp_map(phys_mm_phys_base);

    /* Begin creating the header */
    while((uint64_t)mem_map < mem_map_end)
    {
       if(mem_map->type == MULTIBOOT_MEMORY_AVAILABLE)
       {
           hdr_ptr[i].base = mem_map->addr;
           hdr_ptr[i].length = mem_map->len;
           hdr_ptr[i].avail = mem_map->len / PAGE_SIZE;
           i++;
       }

       mem_map = (multiboot_memory_map_t*)((uint64_t) mem_map + mem_map->size + sizeof(mem_map->size));
    }

    /* We have the headers - create the bitmaps */

    
    itoa(hdr_ptr[0].avail,buf,10);
    vga_print("\n",0x7,-1);
    vga_print(buf,0x7,-1);
    vga_print("\n",0x7,-1);
    mem_map = temp_mem_map;
}

void init_page_table(void)
{
    init_phys_mm();
    
phys_temp_map(0);
   
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
