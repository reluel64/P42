/* utils to extract the memory map */
#include <stdint.h>
#include <memory_map.h>
#include <multiboot.h>
#include <stddef.h>
#include <acpi.h>
#include <utils.h>
#include <defs.h>

extern uint32_t mem_map_addr; /* address of the multiboot header */
extern uint32_t mem_map_sig; /* multiboot header presence */


#define ACPI_MAX_INIT_TABLES   16
static ACPI_TABLE_DESC      TableArray[ACPI_MAX_INIT_TABLES];
static int numa_check_init = -1;

/*
 * Checks if numa is supported and
 * fills the tables 
 */ 

static void mem_map_numa_check_init(void)
{
    ACPI_TABLE_HEADER  *hdr    = NULL;
    ACPI_STATUS        status = AE_OK;

    if(numa_check_init == -1)
    {
        memset(TableArray, 0, sizeof(TableArray));
        status = AcpiInitializeTables(TableArray, ACPI_MAX_INIT_TABLES, TRUE);

        if(status != AE_OK)
        {
            numa_check_init = 0;
            return;
        }

        status = AcpiGetTable(ACPI_SIG_SRAT, 0 ,&hdr);
        
        if(status == AE_OK)
        {
            numa_check_init = 1;
            AcpiPutTable(hdr);
        }
        else
            numa_check_init = 0;
    }
}

static int mem_map_get_numa_domain
(
    memory_map_entry_t *e820, 
    memory_map_entry_t *dom, 
    phys_size_t *last_pos
)
{
    ACPI_TABLE_HEADER      *hdr          = NULL;
    ACPI_SUBTABLE_HEADER   *sub_hdr      = NULL;
    ACPI_SRAT_MEM_AFFINITY *mem_aff      = NULL;
    ACPI_TABLE_SRAT        *srat         = NULL;
    int                     status       = -1;
    int                     touch_pos    = 0;
    phys_size_t             e820_limit   = 0;
    phys_size_t             numa_limit = 0;
    phys_size_t             temp_last_pos = 0;

    memset(dom, 0, sizeof(memory_map_entry_t));

    if(AcpiGetTable(ACPI_SIG_SRAT, 0, (ACPI_TABLE_HEADER**)&srat))
    {
        kprintf("ERROR getting the table\n");
        while(1);
    }

    if(last_pos != NULL)
        temp_last_pos = *last_pos;

    if(temp_last_pos == 0)
        temp_last_pos = sizeof(ACPI_TABLE_SRAT);

    for(phys_size_t srat_pos = temp_last_pos; 
                    srat_pos < srat->Header.Length; 
                    srat_pos += sub_hdr->Length
        )
        {
            sub_hdr = (ACPI_SUBTABLE_HEADER*)((uint8_t*)srat + srat_pos);

            if(sub_hdr->Type != ACPI_SRAT_TYPE_MEMORY_AFFINITY)
                continue;

            mem_aff = (ACPI_SRAT_MEM_AFFINITY*)sub_hdr;

            if(mem_aff->Length == 0)
                continue;
            
            /* use limits instead of lengths to avoid some nasty stuff */
            if(mem_aff->Length == 0)
                numa_limit = 1;
            else
                numa_limit = mem_aff->Length - 1;

            if(e820->length == 0)
                e820_limit = 1;
            else
                e820_limit = e820->length - 1;

            /* Check if NUMA entry is in E820 entry */

            if((e820->base <= mem_aff->BaseAddress) && 
                (e820->base + e820_limit >= mem_aff->BaseAddress))
            {
            /* The base is, but is the end? */
                if(e820->base + e820_limit >= mem_aff->BaseAddress + numa_limit)
                {
                    dom->base   = mem_aff->BaseAddress;
                    dom->length = mem_aff->Length;
                    dom->domain = mem_aff->ProximityDomain;
                    dom->type   = e820->type;
                }
            
                /* Retain the base but lower the length - this happens if the 
                 * NUMA range spans accross multipe e820
                 */

                else
                {
                    dom->base = mem_aff->BaseAddress;
                    dom->length = ((e820->base + e820_limit) - mem_aff->BaseAddress)  + 1;
                    dom->domain=  mem_aff->ProximityDomain;
                    dom->type = e820->type;
                }

                temp_last_pos = srat_pos + sub_hdr->Length;
                status = 0;
                break;
            }
        
            /* The E820 resides in the domain */
            else if(e820->base >= mem_aff->BaseAddress && 
                   (e820->base <= mem_aff->BaseAddress + numa_limit))
            {
                /* E820 is fully in domain */
                if(e820->base + e820_limit <= mem_aff->BaseAddress + numa_limit)
                {
                    *dom = *e820;
                    dom->domain = mem_aff->ProximityDomain;
                }
                else
                {
                    *dom = *e820;
                    dom->domain = mem_aff->ProximityDomain;
                    dom->length = ((mem_aff->BaseAddress + numa_limit) - e820->base)  + 1;
                }

                temp_last_pos = srat_pos + sub_hdr->Length;
                status = 0;
                break;           
            }
        }

    AcpiPutTable((ACPI_TABLE_HEADER*)srat);
    
    if(last_pos != NULL)
        *last_pos = temp_last_pos;
    
    return(status);
}

void test_callback(memory_map_entry_t *map, void  *pv)
{
    if(map->type != MEMORY_USABLE)
        kprintf("FINAL BASE 0x%x - LENGTH 0x%x - DOMAIN %d\n", map->base, map->length, map->domain);
}

int mem_map_iter
(
    void (*callback)(memory_map_entry_t *mmap,void *pv),
    void *pv
)
{
    multiboot_info_t       *mb_info      = NULL;
    memory_map_entry_t      mem_entry;
    memory_map_entry_t      dom_entry;
    multiboot_memory_map_t *mb_mem_map = NULL;
    uint32_t                sig        = 0;
    uint64_t                mem_map    = 0;
    uint64_t                map_length = 0;
    int                     has_numa   = 0;
    phys_size_t             last_pos   = 0;
    mem_map_numa_check_init();

    has_numa = (numa_check_init == 1);

    if(mem_map_sig == MULTIBOOT_BOOTLOADER_MAGIC)
    {
        mb_info = (multiboot_info_t*)(uint64_t)mem_map_addr;
        map_length = mb_info->mmap_length;
        
        if(!has_numa)
        {
            kprintf("GETTING E820 MEMORY MAP WITHOUT NUMA\n");
            for(phys_size_t i = 0; i < map_length; )
            {
                mb_mem_map       = (multiboot_memory_map_t *)(mb_info->mmap_addr + i);
                mem_entry.base   = mb_mem_map->addr;
                mem_entry.length = mb_mem_map->len;
                mem_entry.type   = mb_mem_map->type;
            
                i += mb_mem_map->size+sizeof(mb_mem_map->size);
           
                callback(&mem_entry, pv); 
            }
        }
        else
        {
            kprintf("GETTING E820 MEMORY MAP WITH NUMA\n");
            mb_info = (multiboot_info_t*)(uint64_t)mem_map_addr;
            map_length = mb_info->mmap_length;

#if 1
            for(phys_size_t i = 0; i < map_length; )
            {
                memset(&mem_entry, 0, sizeof(memory_map_entry_t));

                mb_mem_map       = (multiboot_memory_map_t *)(mb_info->mmap_addr + i);
                mem_entry.base   = mb_mem_map->addr;
                mem_entry.length = mb_mem_map->len;
                mem_entry.type   = mb_mem_map->type;
#if 1
                if(mem_map_get_numa_domain(&mem_entry, &dom_entry, &last_pos) == 0)
                {
                    callback(&dom_entry, pv);
                    continue;
                }
                else
                {
                    /* Check if entry is by any chance in domain */
                    if(mem_map_get_numa_domain(&mem_entry, &dom_entry, NULL) != 0)
                    {
                        dom_entry = mem_entry;
                        callback(&dom_entry, pv);
                    }

                    last_pos = 0;
                }
#else
                test_callback(&mem_entry, pv);
#endif
                i += mb_mem_map->size+sizeof(mb_mem_map->size);

                /* Check if memory map entry is in domain */
                  
#endif
            }          
            kprintf("DONE\n");

        }
    }
}
