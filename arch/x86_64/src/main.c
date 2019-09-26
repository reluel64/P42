#include <stdint.h>
#include <multiboot.h>
#include <descriptors.h>

extern void load_gdt(void *gdt_ptr_addr);
extern void load_idt(void *itd_ptr_addr);
extern void interrupt_call(uint64_t int_ix);

char message[80];

uint64_t int_count = 0;


extern uint32_t mb_addr;
extern uint32_t mb_present;

extern void disable_interrupts();
extern void enable_interrupts();;
extern void test_interrupt();

typedef  void(*interrupt_handler_t)(void);

void memset(void *ptr, int byte, uint64_t len)
{
    for(uint64_t i = 0; i < len; i++)
    ((uint8_t*)ptr)[i] = byte;
}

char * itoa(unsigned long value, char * str, int base)
{
     char * rc;
    char * ptr;
    char * low;
    // Check for supported base.
    if ( base < 2 || base > 36 )
    {
        *str = '\0';
        return str;
    }
    rc = ptr = str;
    // Set '-' for negative decimals.
    if ( value < 0 && base == 10 )
    {
        *ptr++ = '-';
    }
    // Remember where the numbers start.
    low = ptr;
    // The actual conversion.
    do
    {
        // Modulo is negative for negative value. This trick makes abs() unnecessary.
        *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"[35 + value % base];
        value /= base;
    } while ( value );
    // Terminating the string.
    *ptr-- = '\0';
    // Invert the numbers.
    while ( low < ptr )
    {
        char tmp = *low;
        *low++ = *ptr;
        *ptr-- = tmp;
    }
    return rc;
}

extern void vga_init(void);
extern void vga_print(uint8_t *buf, uint8_t color, uint64_t len);


#define FB_MEM (0xFFFFFFFF80000000 +  0xB8000)
extern void halt();
void kmain()
{
    multiboot_info_t *mb_info =  (multiboot_info_t*)((uint64_t)mb_addr + 0xFFFFFFFF80000000);
    multiboot_memory_map_t *mem_map = (multiboot_memory_map_t*)(mb_info->mmap_addr + 0xFFFFFFFF80000000);
    uint64_t mem_map_end = (uint64_t)mem_map + mb_info->mmap_length;
    uint16_t *vga_mem = (uint16_t*)FB_MEM;
    vga_init();

    setup_descriptors();
    load_descriptors();

    
   while(1)
   {
       memset(message,0,sizeof(message));
       itoa(int_count, message,10);
  
      
            vga_print(message,0x7,-1);
            vga_print("\n",0,-1);
        
       interrupt_call(32);
   }

    #if 0
    while((uint64_t)mem_map < mem_map_end)
    {
       if(mem_map->type == MULTIBOOT_MEMORY_AVAILABLE)
       {
        itoa(mem_map->len,0,16);
        for(int  i =0; message[i] != 0;i++)
        {
            p[b++] = message[i] | 0x7<<8;
        }

        b+=5;
        
        itoa(mem_map->addr,0,16);
        
        for(int  i =0; message[i] != 0;i++)
        {
        p[b++] = message[i] | 0x7<<8;
        }
  line++;
        b = (line * 80);
       }
        mem_map =(uint64_t) mem_map + mem_map->size + sizeof(mem_map->size);
      
    }
#endif
    /* hang the kernel */
    while(1);
}
