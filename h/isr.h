#ifndef isr_h
#define isr_h

#include <stddef.h>
#include <defs.h>
typedef  int(*interrupt_handler_t)(void *pv, virt_addr_t iframe);

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