#ifndef isr_h
#define isr_h

#include <stddef.h>
#include <defs.h>

typedef struct isr_info_t
{
    virt_addr_t iframe;
    uint32_t    cpu_id;
}isr_info_t;

typedef  int(*interrupt_handler_t)(void *pv, isr_info_t *inf);

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