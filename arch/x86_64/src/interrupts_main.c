/* Interrupt dispatcher */

#include <stdint.h>

extern uint64_t int_count;
extern void vga_print(uint8_t *buf, uint8_t color, uint64_t len);
void isr_handler(uint64_t index)
{
    if(index == 0)
    vga_print("Hello World",0x7,-1);
}

