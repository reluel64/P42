/* 
 * Descriptor definitions 
 */

#include <stdint.h>

#define MAX_IDTS (256)
#define MAX_GDTS (4)
#define KERNEL_CODE_SEGMENT (0x8)
#define KERNEL_DATA_SEGMENT (0x10)
#define USER_CODE_SEGMENT (0x18)
#define USER_DATA_SEGMENT (0x20)


typedef  void(*interrupt_handler_t)(void);

/* Global Descriptor */
typedef struct _gdt_entry
{
    uint16_t limit_low    ;
    uint16_t base_low     ;
    uint8_t  base_mid     ;
    uint8_t  flags        ;
    uint8_t  limit_hi : 4 ;
    uint16_t flags2:4;          /* also contains limit 19:16 */
    uint8_t  base_high  ;

}__attribute__((packed)) gdt_entry_t; 

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

/* Task State Segment */
typedef struct tss64
{
    uint32_t reserved0;
    uint32_t rsp0_low;
    uint32_t rsp0_high;
    uint32_t rsp1_low;
    uint32_t rsp1_high;
    uint32_t rsp2_low;
    uint32_t rsp2_high;
    uint32_t reserved28;
    uint32_t reserved32;
    uint32_t ist1_low;
    uint32_t ist1_high;
    uint32_t ist2_low;
    uint32_t ist2_high;
    uint32_t ist3_low;
    uint32_t ist3_high;
    uint32_t ist4_low;
    uint32_t ist4_high;
    uint32_t ist5_low;
    uint32_t ist5_high;
    uint32_t ist6_low;
    uint32_t ist6_high;
    uint32_t ist7_low;
    uint32_t ist7_high;
    uint32_t reserved92;
    uint32_t reserved96;
    uint16_t reserved115;
    uint16_t io_map;
    
}__attribute__((packed))  tss64_entry_t;

typedef struct gdt_ptr
{
    uint16_t len;
    uint64_t addr;

}__attribute__((packed)) gdt64_ptr_t;

typedef struct idt_ptr
{
    uint16_t len;
    uint64_t addr;

}__attribute__((packed)) idt64_ptr_t;


int idt_entry_add
(
    interrupt_handler_t ih,
    uint16_t type_attr,
    uint16_t selector,
    idt64_entry_t *idt_entry
);