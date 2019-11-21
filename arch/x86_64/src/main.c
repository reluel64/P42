#include <stdint.h>
#include <vga.h>
#include <io.h>
extern void load_gdt(void *gdt_ptr_addr);
extern void load_idt(void *itd_ptr_addr);
extern void interrupt_call(uint64_t int_ix);

uint64_t int_count = 0;
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

void kmain()
{
    vga_init();

  //  setup_descriptors();
 //   load_descriptors();
    vga_print("Hello World",0x7,-1);
    init_phys_mm();
   //  init_page_table();
    
}
