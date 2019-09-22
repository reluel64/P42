#include <stdint.h>
#include "multiboot.h"
#include "descriptors.h"

extern void load_gdt(void *gdt_ptr_addr);
extern void load_idt(void *itd_ptr_addr);


static char message[80];

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


void dummy_interrupt(void)
{
 int_count++;
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

extern isr_entry(void);




#define FB_MEM (0xFFFFFFFF80000000 +  0xB8000)
extern void halt();
void kmain()
{
    multiboot_info_t *mb_info =  (multiboot_info_t*)((uint64_t)mb_addr + 0xFFFFFFFF80000000);
    multiboot_memory_map_t *mem_map = (multiboot_memory_map_t*)(mb_info->mmap_addr + 0xFFFFFFFF80000000);
    uint64_t mem_map_end = (uint64_t)mem_map + mb_info->mmap_length;
    uint16_t *vga_mem = (uint16_t*)FB_MEM;
    vga_init();

#if 0
    vga_write("Hello 1\n",6);
    vga_write("Hello 2\n",6);
    vga_write("Hello 3\n",6);
    vga_write("Hello 4\n",6);
    vga_write("Hello 5\n",6);
    vga_write("Hello 6\n",6);
    vga_write("Hello 7\n",6);
    vga_write("Hello 8\n",6);
    vga_write("Hello 9\n",6);
    vga_write("Hello 10\n",6);
    vga_write("Hello 11\n",6);
    vga_write("Hello 12\n",6);
    vga_write("Hello 13\n",6);
    vga_write("Hello 14\n",6);
    vga_write("Hello 15\n",6);
    vga_write("Hello 16\n",6);
    vga_write("Hello 17\n",6);
    vga_write("Hello 18\n",6);
    vga_write("Hello 19\n",6);
    vga_write("Hello 20\n",6);
    vga_write("Hello 21\n",6);
    vga_write("Hello 22\n",6);
    vga_write("Hello 23\n",6);
    vga_write("Hello 24\n",6);
    vga_write("Hello 25\n",6);
    vga_write("Hello 26\n",6);
    vga_write("Hello 27\n",6);
#endif 

    setup_descriptors();
    load_descriptors();

   while(1)
   {
       memset(message,0,sizeof(message));
       itoa(int_count, message,10);
  
        for(int i = 0; i < sizeof(message);i++)
        {
            vga_mem[i] = message[i] | 0x7 << 8;
        }
        test_interrupt();
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
