#ifndef porth
#define porth

#include <stdint.h>

void __outb(uint16_t port, uint8_t val);
void __outw(uint16_t port, uint16_t val);
void __outd(uint32_t port, uint32_t val);

uint8_t __inb(uint16_t port);
uint16_t __inw(uint16_t port);
uint32_t __ind(uint32_t port);


#endif