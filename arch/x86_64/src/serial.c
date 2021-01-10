/* Polling serial */

#include <port.h>
#define PORT 0x3f8   /* COM1 */

void init_serial(void) 
{
   __outb(PORT + 1, 0x01);    // Disable all interrupts
   __outb(PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
   __outb(PORT + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
   __outb(PORT + 1, 0x00);    //                  (hi byte)
   __outb(PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
   __outb(PORT + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
   __outb(PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

static int is_transmit_empty() {
   return __inb(PORT + 5) & 0x20;
}
 
void write_serial(char a) {
   while (is_transmit_empty() == 0);
 
   __outb(PORT,a);
}
