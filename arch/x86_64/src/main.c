#include <stdint.h>
#include <multiboot.h>
#include <descriptors.h>
#include <io.h>
extern void load_gdt(void *gdt_ptr_addr);
extern void load_idt(void *itd_ptr_addr);
extern void interrupt_call(uint64_t int_ix);

char message[80];

uint64_t int_count = 0;


extern uint32_t mb_addr;
extern uint32_t mb_present;

extern void test_interrupt();
#define PORT 0x3f8   /* COM1 */

void io_wait()
{
    for(int i=0;i<1000000;i++)
    {
        read_port_b(0x80);
    }
}


void init_serial() {
   write_port_b(PORT + 1, 0x01);    // Disable all interrupts
   write_port_b(PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
   write_port_b(PORT + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
   write_port_b(PORT + 1, 0x00);    //                  (hi byte)
   write_port_b(PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
   write_port_b(PORT + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
   write_port_b(PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

int is_transmit_empty() {
   return read_port_b(PORT + 5) & 0x20;
}
 
void write_serial(char a) {
   while (is_transmit_empty() == 0);
 
   write_port_b(PORT,a);
}

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
  
      
           /* vga_print(message,0x7,-1);
            vga_print("\n",0,-1);
            */
        for(int i = 0; message[i]; i++)
        {
            write_serial(message[i]);
        }        

        write_serial('\n');
      // interrupt_call(32);
       for(int i = 0; i < 1000000;i++)
       {
           /*read_port_b(0x80);*/
       }
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
