/* Polling serial */
#include <io.h>

#define PORT 0x3f8   /* COM1 */

void init_serial(void) 
{
   write_port_b(PORT + 1, 0x01);    // Disable all interrupts
   write_port_b(PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
   write_port_b(PORT + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
   write_port_b(PORT + 1, 0x00);    //                  (hi byte)
   write_port_b(PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
   write_port_b(PORT + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
   write_port_b(PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

static int is_transmit_empty() {
   return read_port_b(PORT + 5) & 0x20;
}
 
void write_serial(char a) {
   while (is_transmit_empty() == 0);
 
   write_port_b(PORT,a);
}
