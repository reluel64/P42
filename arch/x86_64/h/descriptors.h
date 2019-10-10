/* 
 * Descriptor definitions 
 */

#ifndef descriptors_h
#define descriptors_h

#include <stdint.h>

#define MAX_INTERRUPTS (256)
#define MAX_GDT_ENTRIES (7)
#define KERNEL_CODE_SEGMENT (0x08)
#define KERNEL_DATA_SEGMENT (0x10)
#define USER_CODE_SEGMENT   (0x18)
#define USER_DATA_SEGMENT   (0x20)
#define TSS_SEGMENT         (0x28)

#define GDT_DATA_R    (0x0)    /* Read only                         */
#define GDT_DATA_RA   (0x1)    /* Read only, accessed               */
#define GDT_DATA_RW   (0x2)    /* Read / Write                      */
#define GDT_DATA_RWA  (0x3)    /* Read / Write, accessed            */
#define GDT_DATA_RD   (0x4)    /* Read only, expand-down            */
#define GDT_DATA_RDA  (0x5)    /* Read-Only, expand-down, accessed  */
#define GDT_DATA_RWD  (0x6)    /* Read/Write, expand-down           */
#define GDT_DATA_RWDA (0x7)    /* Read/Write, expand-down, accessed */

#define GDT_CODE_X    (0x8)   /* Execute-Only                       */
#define GDT_CODE_XA   (0x9)   /* Execute-Only, accessed             */
#define GDT_CODE_XR   (0xA)   /* Execute/Read                       */
#define GDT_CODE_XRA  (0xB)   /* Execute/Read, accessed             */
#define GDT_CODE_XC   (0xC)   /* Execute-Only, conforming           */
#define GDT_CODE_XCA  (0xD)   /* Execute-Only, conforming, accessed */
#define GDT_CODE_XRC  (0xE)   /* Execute/Read, conforming           */
#define GDT_CODE_XRCA (0xF)   /* Execute/Read, conforming, accessed */

#define GDT_SYSTEM_TSS (0x9)
#define GDT_SYSTEM_CALL_GATE     (0xC)
#define GDT_SYSTEM_INTERUPT_GATE (0xE)
#define GDT_SYSTEM_TRAP_GATE     (0xF)


#define GDT_DESC_TYPE_SYSTEM (0x0)
#define GDT_DESC_TYPE_CODE_DATA (0x1)

#define GDT_GRANULARITY_SET(x)   ((x) << 15)
#define GDT_OPERAND_SIZE_SET(x)  ((x) << 14)
#define GDT_LONG_SET(x)          ((x) << 13)
#define GDT_AVL_SET(x)           ((x) << 12)
#define GDT_PRESENT_SET(x)       ((x) << 7)
#define GDT_DPL_SET(x)           ((x) << 5)
#define GDT_DESC_TYPE_SET(x)     ((x) << 4) /* 0 - system 1 - code/data */
#define GDT_TYPE_SET(x)          ((x) << 0)


typedef  void(*interrupt_handler_t)(void);

/* Global Descriptor */
typedef struct _gdt_entry
{
    uint16_t limit_low     ;
    uint16_t base_low      ;
    uint8_t  base_mid      ;
    uint8_t  type       :4 ;
    uint8_t  desc_type  :1 ;
    uint8_t  dpl        :2 ;
    uint8_t  present    :1 ;
    uint8_t  limit_hi   :4 ;
    uint8_t  avl        :1 ;
    uint8_t  _long      :1 ;
    uint8_t  def_op_sz  :1 ;
    uint8_t  granularity:1 ;
    uint8_t  base_high     ;

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
    uint8_t type_attr,
    uint8_t ist,
    uint16_t selector,
    idt64_entry_t *idt_entry
);

int gdt_entry_encode
(
    uint64_t base, 
    uint32_t limit,
    uint32_t flags,
    gdt_entry_t *gdt_entry
);

int setup_descriptors(void);
int load_descriptors();

#endif