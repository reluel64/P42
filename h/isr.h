#ifndef isr_h
#define isr_h

#include <stddef.h>

typedef  int(*interrupt_handler_t)(void *pv, uint64_t error_code);

int isr_init(void);
int isr_install
(
    interrupt_handler_t ih, 
    void *pv, 
    uint16_t index, 
    uint8_t  eoi
);
int isr_uninstall
(
    interrupt_handler_t ih,
    void *pv,
    uint8_t eoi
);

#endif