#ifndef isr_h
#define isr_h

#include <stddef.h>
#include <defs.h>
#include <isr.h>
#include <cpu.h>

#define ZERO_ISR_INIT {.node.prev = NULL, \
                       .node.next = NULL, \
                       .ih        = NULL, \
                       .pv        = NULL, \
                       .allocated = 0     \
                      }

typedef struct isr_info_t
{
    virt_addr_t iframe;
    uint32_t    cpu_id;
    cpu_t *cpu;
}isr_info_t;

typedef  int32_t (*interrupt_handler_t)(void *pv, isr_info_t *inf);

typedef struct isr_t
{
    list_node_t node;
    interrupt_handler_t ih;
    void *pv;
    uint8_t allocated;
}isr_t;

int isr_init
(
    void
);

isr_t *isr_install
(
    interrupt_handler_t ih, 
    void *pv, 
    uint16_t index, 
    uint8_t  eoi,
    isr_t *isr_slot
);

int isr_uninstall
(
    isr_t *isr,
    uint8_t eoi
);

#endif