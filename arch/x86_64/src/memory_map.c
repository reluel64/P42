/* utils to extract the memory map */
#include <stdint.h>
#include <memory_map.h>
#include <multiboot.h>
#include <stddef.h>

extern uint64_t phys_temp_map(uint64_t phys_addr);

extern uint32_t mem_map_addr; /* address of the multiboot header */
extern uint32_t mem_map_sig; /* multiboot header presence */

int mem_map_iter
(
    void (*callback)(memory_map_entry_t *mmap,void *pv),
    void *pv
)
{
    multiboot_info_t       *mb_info = NULL;
    memory_map_entry_t     mem_entry;
    multiboot_memory_map_t *mb_mem_map = NULL;
    uint32_t               signature = 0;
    uint64_t               mem_map = 0;
    uint64_t               map_length = 0;

    
    if(mem_map_sig == MULTIBOOT_BOOTLOADER_MAGIC)
    {
        mb_info = mem_map_addr;
        map_length = mb_info->mmap_length;

        for(uint64_t i = 0; i < map_length; )
        {
            mb_mem_map       = (multiboot_memory_map_t *)(mb_info->mmap_addr + i);
            mem_entry.base   = mb_mem_map->addr;
            mem_entry.length = mb_mem_map->len;
            mem_entry.type   = mb_mem_map->type;

            i += mb_mem_map->size+sizeof(mb_mem_map->size);

            callback(&mem_entry, pv); 
        }
    }
}