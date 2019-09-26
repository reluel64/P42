/* Interrupt dispatcher */

#include <stdint.h>

extern uint64_t int_count;

void isr_handler(uint64_t index)
{
    if(index == 31)
    int_count++;
}

