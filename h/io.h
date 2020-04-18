/* Port I/O */

#ifndef io_h
#define io_h

#include <stdint.h>

uint8_t read_port_b(uint16_t port);
uint16_t read_port_w(uint16_t port);
uint32_t read_port_dw(uint16_t port);
void write_port_b(uint16_t port, uint8_t value);
void write_port_w(uint16_t port, uint16_t value);
void write_port_dw(uint16_t port, uint32_t value);

#endif