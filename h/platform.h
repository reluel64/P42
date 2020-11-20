#ifndef platformh
#define platformh
#include <gdt.h>
#include <cpu.h>
extern virt_addr_t kstack_base;
extern virt_addr_t kstack_top;

#define _BSP_STACK_TOP ((virt_addr_t)&kstack_top)
#define _BSP_STACK_BASE ((virt_addr_t)&kstack_base)

#define PLATFORM_CPU_NAME "x86_cpu"
#define CPU_TRAMPOLINE_LOCATION_START (0x8000)
#define PER_CPU_STACK_SIZE            (0x8000) /* 32 K */


#define IDT_ENTRY_SIZE (sizeof(idt64_entry_t))
#define IDT_TABLE_COUNT (256)
#define IDT_TABLE_SIZE (IDT_ENTRY_SIZE * IDT_TABLE_COUNT)
#define ISR_EC_MASK (0x27D00)
#define RESERVED_ISR_BEGIN (21)
#define RESERVED_ISR_END   (31)
#define MAX_HANDLERS       (256)


#define PLATFORM_RESCHED_VECTOR (240)
#define PLATFORM_PG_INVALIDATE_VECTOR  (127)
#define PLATFORM_PG_FAULT_VECTOR       (14)

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
    virt_addr_t addr;

}__attribute__((packed)) idt64_ptr_t;

typedef struct cpu_platform_t
{
    gdt_entry_t *gdt;
    tss64_entry_t *tss;
    virt_addr_t esp0;

}cpu_platform_t;

typedef struct cpu_platfrom_driver_t
{
    uint8_t       cpu_name[64];
    idt64_entry_t *idt;
    idt64_ptr_t    idt_ptr;
}cpu_platform_driver_t;

typedef struct interrupt_frame_t
{
    uint64_t error_code;
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
}interrupt_frame_t;

int pcpu_register(cpu_api_t **api);

extern void        __wrmsr(uint64_t reg, uint64_t val);
extern uint64_t    __rdmsr(uint64_t msr);
extern phys_addr_t __read_cr3(void);
extern void        __write_cr3(phys_addr_t );
extern virt_addr_t __stack_pointer(void);
extern void        halt();
extern phys_addr_t __read_cr3(void);
extern void        __write_cr3(phys_addr_t phys_addr);
extern void        __invlpg(virt_addr_t address);
extern void        __enable_wp();
extern virt_addr_t __read_cr2();
extern void        __write_cr2(virt_addr_t cr2);
extern void        __wbinvd();
extern void        __wrmsr(uint64_t reg, uint64_t val);
extern uint64_t    __rdmsr(uint64_t msr);
extern uint64_t    __read_cr4();
extern void       __write_cr4(uint64_t cr4);
extern uint64_t    __read_cr0();
extern void       __write_cr0(uint64_t cr0);
extern void __cpuid
(
    uint32_t *eax,
    uint32_t *ebx,
    uint32_t *ecx,
    uint32_t *edx
);


#endif