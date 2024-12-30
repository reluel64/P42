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

struct isr_info
{
    virt_addr_t iframe;
    uint32_t    cpu_id;
    struct cpu *cpu;
};

typedef  int32_t (*interrupt_handler_t)(void *pv, struct isr_info *inf);

struct isr
{
    struct list_node node;
    interrupt_handler_t ih;
    void *pv;
    uint8_t allocated;
};

int isr_init
(
    void
);

struct isr *isr_install
(
    interrupt_handler_t ih, 
    void *pv, 
    uint16_t index, 
    uint8_t  eoi,
    struct isr *isr_slot
);

int isr_uninstall
(
    struct isr *isr,
    uint8_t eoi
);

#endif