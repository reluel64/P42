#ifndef isr_h
#define isr_h

#include <stddef.h>

typedef  void(*interrupt_handler_t)(void);
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
    uint16_t len;
    uint64_t addr;

}__attribute__((packed)) idt64_ptr_t;


int idt_entry_add
(
    interrupt_handler_t ih,
    uint8_t type_attr,
    uint8_t ist,
    uint16_t selector,
    idt64_entry_t *idt_entry
);

int init_isr(void);

#endif