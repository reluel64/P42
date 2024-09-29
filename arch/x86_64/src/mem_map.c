/* utils to extract the memory map */
#include <stdint.h>
#include <memory_map.h>
#include <multiboot.h>
#include <stddef.h>
#include <acpi.h>
#include <utils.h>
#include <defs.h>



#undef  MEM_MAP_TEST_CALLBACK 
extern uint32_t mem_map_addr; /* address of the multiboot header */
extern uint32_t mem_map_sig; /* multiboot header presence */



static int numa_check_init = -1;

#if 0

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
    phys_size_t *next_pos
)
{
    ACPI_TABLE_HEADER      *hdr          = NULL;
    ACPI_SUBTABLE_HEADER   *sub_hdr      = NULL;
    ACPI_SRAT_MEM_AFFINITY *mem_aff      = NULL;
    ACPI_TABLE_SRAT        *srat         = NULL;
    int                     status       = -1;
    phys_size_t             e820_limit   = 0;
    phys_size_t             numa_limit = 0;

    memset(dom, 0, sizeof(memory_map_entry_t));

    if(next_pos == NULL)
        return(-1);

    if(AcpiGetTable(ACPI_SIG_SRAT, 0, (ACPI_TABLE_HEADER**)&srat))
    {
        kprintf("ERROR getting the table\n");
        while(1);
    }

    if(*next_pos == 0)
        *next_pos = sizeof(ACPI_TABLE_SRAT);

    for(phys_size_t srat_pos = *next_pos; 
                    srat_pos <  srat->Header.Length;
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
                dom->flags = mem_aff->Flags;
            }

            /* Retain the base but lower the length - this happens if the 
            * NUMA range spans accross multipe e820s
            */

            else
            {
                dom->base = mem_aff->BaseAddress;
                dom->length = ((e820->base + e820_limit) - mem_aff->BaseAddress)  + 1;
                dom->domain=  mem_aff->ProximityDomain;
                dom->type = e820->type;
                dom->flags = mem_aff->Flags;
            }
            *next_pos = srat_pos + sub_hdr->Length;
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
                dom->flags = mem_aff->Flags;
            }
            else
            {
                *dom = *e820;
                dom->domain = mem_aff->ProximityDomain;
                dom->length = ((mem_aff->BaseAddress + numa_limit) - e820->base)  + 1;
                dom->flags = mem_aff->Flags;
            }

            status = 0;
            *next_pos = srat_pos + sub_hdr->Length;
            break;           
        }
    }

    AcpiPutTable((ACPI_TABLE_HEADER*)srat);

    if(status == -1)
    {
        *next_pos = 0;
    }

    return(status);
}

#ifdef  MEM_MAP_TEST_CALLBACK
void mem_map_test_callback(memory_map_entry_t *map, void  *pv)
{
    
        kprintf("FINAL BASE 0x%x - LENGTH 0x%x - DOMAIN %d\n", map->base, map->length, map->domain);
}
#endif

static void mem_map_show_domains(void)
{
    ACPI_TABLE_HEADER      *hdr          = NULL;
    ACPI_SUBTABLE_HEADER   *sub_hdr      = NULL;
    ACPI_SRAT_MEM_AFFINITY *mem_aff      = NULL;
    ACPI_TABLE_SRAT        *srat         = NULL;

    if(AcpiGetTable(ACPI_SIG_SRAT, 0, (ACPI_TABLE_HEADER**)&srat))
    {
        kprintf("ERROR getting the table\n");
        while(1);
    }

     for(phys_size_t srat_pos = sizeof(ACPI_TABLE_SRAT);
                    srat_pos <  srat->Header.Length;
                    srat_pos += sub_hdr->Length
        )
    {

        sub_hdr = (ACPI_SUBTABLE_HEADER*)((uint8_t*)srat + srat_pos);

        if(sub_hdr->Type != ACPI_SRAT_TYPE_MEMORY_AFFINITY)
            continue;

        mem_aff = (ACPI_SRAT_MEM_AFFINITY*)sub_hdr;

        if(mem_aff->Length == 0)
            continue;

    kprintf("DOMAIN %d FLAGS 0x%x - BASE 0x%x - 0x%x\n",mem_aff->ProximityDomain,mem_aff->Flags, mem_aff->BaseAddress, mem_aff->Length);
    }

    AcpiPutTable((ACPI_TABLE_HEADER*)srat);

}
#endif

static void mem_map_show_e820(void)
{

    multiboot_info_t       *mb_info         = NULL;
    memory_map_entry_t      mem_entry;

    multiboot_memory_map_t *mb_mem_map      = NULL;
    uint64_t                map_length      = 0;


    if(mem_map_sig == MULTIBOOT_BOOTLOADER_MAGIC)
    {
        mb_info = (multiboot_info_t*)(uint64_t)mem_map_addr;
        map_length = mb_info->mmap_length;
 
        for(phys_size_t i = 0; i < map_length; )
        {
            mb_mem_map       = (multiboot_memory_map_t *)(mb_info->mmap_addr + i);
            mem_entry.base   = mb_mem_map->addr;
            mem_entry.length = mb_mem_map->len;
            mem_entry.type   = mb_mem_map->type;
        
            i += mb_mem_map->size+sizeof(mb_mem_map->size);
        
            kprintf("E820: 0x%x - 0x%x TYPE: %d\n",mem_entry.base, mem_entry.length, mem_entry.type);
        }
    }
}


int mem_map_iter
(
    void (*callback)(memory_map_entry_t *mmap,void *pv),
    void *pv
)
{
    multiboot_info_t       *mb_info         = NULL;
    memory_map_entry_t      mem_entry;
    memory_map_entry_t      dom_entry;
    multiboot_memory_map_t *mb_mem_map      = NULL;
    uint32_t                sig             = 0;
    uint64_t                mem_map         = 0;
    uint64_t                map_length      = 0;
    int                     has_numa        = 0;
    int                     numa_dom_status = 0;
    phys_size_t             next_pos        = 0;
    int                     in_domain       = 0;

#ifdef MEM_MAP_TEST_CALLBACK
    callback = mem_map_test_callback;
#endif

    //mem_map_numa_check_init();

    has_numa = (numa_check_init == 1);

    if(mem_map_sig == MULTIBOOT_BOOTLOADER_MAGIC)
    {
        mb_info = (multiboot_info_t*)(uint64_t)mem_map_addr;
        map_length = mb_info->mmap_length;
 
        if(!has_numa)
        {
#ifdef MEM_MAP_DEBUG
            kprintf("GETTING E820 MEMORY MAP WITHOUT NUMA\n");
            mem_map_show_e820();
#endif
            for(phys_size_t i = 0; i < map_length; )
            {
                mb_mem_map       = (multiboot_memory_map_t *)(mb_info->mmap_addr + i);
                mem_entry.base   = mb_mem_map->addr;
                mem_entry.length = mb_mem_map->len;
                mem_entry.type   = mb_mem_map->type;
                mem_entry.flags  = MEMORY_ENABLED;
                i += mb_mem_map->size+sizeof(mb_mem_map->size);
           
                callback(&mem_entry, pv); 
            }
        }
#if 0
        else
        {
            mem_map_show_e820();
            mem_map_show_domains();

            kprintf("GETTING E820 MEMORY MAP WITH NUMA\n");
            mb_info = (multiboot_info_t*)(uint64_t)mem_map_addr;
            map_length = mb_info->mmap_length;

            for(phys_size_t i = 0; i < map_length; )
            {
                memset(&mem_entry, 0, sizeof(memory_map_entry_t));

                mb_mem_map       = (multiboot_memory_map_t *)(mb_info->mmap_addr + i);
                mem_entry.base   = mb_mem_map->addr;
                mem_entry.length = mb_mem_map->len;
                mem_entry.type   = mb_mem_map->type;

                numa_dom_status = mem_map_get_numa_domain(&mem_entry, &dom_entry, &next_pos);
               
                if(!numa_dom_status)
                {
                    callback(&dom_entry, pv);
                    in_domain++;
                    continue;
                }
                else
                {
                    if(in_domain == 0)
                        callback(&mem_entry, pv);

                    in_domain = 0;
                }

                i += mb_mem_map->size + sizeof(mb_mem_map->size);

                /* Check if memory map entry is in domain */ 
            }          
            kprintf("DONE\n");

        }
#endif
    }
#ifdef MEM_MAP_TEST_CALLBACK
    while(1)
        halt();
#endif
}
