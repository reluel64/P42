#ifndef isr_h
#define isr_h

#include <stddef.h>

typedef  int(*interrupt_handler_t)(void *pv, uint64_t error_code);

/* Interrupt Descriptor */
typedef struct _idt_entry
{
    uint16_t  offset_1;
    uint16_t  seg_selector;
    uint8_t   ist : 3;
    uint8_t   zero: 5;
    uint8_t  type_attr;
    uint16_t  offset_2;
    uint32_t  offset_3;
    uint32_t  reserved;
}__attribute__((packed)) idt64_entry_t;


typedef struct idt_ptr
{
    uint16_t limit;
    uint64_t addr;

}__attribute__((packed)) idt64_ptr_t;


int isr_init(void);
int isr_install(interrupt_handler_t ih, void *pv, uint16_t index);
int isr_uninstall(interrupt_handler_t ih);
void isr_per_cpu_init(void);
#endif