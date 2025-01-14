#ifndef platformh
#define platformh
#include <gdt.h>
#include <cpu.h>
#include <isr.h>
#include <apic.h>
#include <apic_timer.h>
extern virt_addr_t kstack_base;
extern virt_addr_t kstack_top;

#define PAGE_SIZE (0x1000)
#define PAGE_SIZE_SHIFT (12)
#define _BSP_STACK_TOP ((virt_addr_t)&kstack_top)
#define _BSP_STACK_BASE ((virt_addr_t)&kstack_base)

#define PLATFORM_CPU_NAME "x86_cpu"
#define CPU_TRAMPOLINE_LOCATION_START (0x8000)

#define IDT_ENTRY_SIZE (sizeof(struct idt64_entry))
#define IDT_TABLE_COUNT (256)
#define IDT_TABLE_SIZE (IDT_ENTRY_SIZE * IDT_TABLE_COUNT)
#define IDT_ALLOC_SIZE (ALIGN_UP(IDT_TABLE_SIZE, PAGE_SIZE))
#define ISR_EC_MASK (0x27D00)
#define RESERVED_ISR_BEGIN (21)
#define RESERVED_ISR_END   (31)
#define MAX_ISR_HANDLERS       IDT_TABLE_COUNT

#define PLATFORM_AP_RETRIES       (1)
#define PLATFORM_AP_START_TIMEOUT (5000)
#define PLATFORM_AP_ALL_CPUS      (-1)
#define PLATFORM_SCHED_VECTOR          (240)
#define PLATFORM_PG_INVALIDATE_VECTOR  (239)
#define PLATFORM_LOCAL_TIMER_VECTOR    (238)
#define PLATFORM_PG_FAULT_VECTOR       (14)

#define IRQ(num)   (num) + 0x20

#define KMAIN_SYS_INIT_STACK_SIZE      (0x1000)

#define KERNEL_CODE_SEGMENT            (0x08)
#define KERNEL_DATA_SEGMENT            (0x10)
#define USER_CODE_SEGMENT              (0x18)
#define USER_DATA_SEGMENT              (0x20)

/* Interrupt Descriptor */
struct __attribute__((packed)) idt64_entry
{
    uint16_t  offset_1;
    uint16_t  seg_selector;
    uint8_t   ist : 3;
    uint8_t   zero: 5;
    uint8_t  type_attr;
    uint16_t  offset_2;
    uint32_t  offset_3;
    uint32_t  reserved;
};

struct __attribute__((packed)) idt64_ptr
{
    uint16_t limit;
    virt_addr_t addr;
};

struct platform_cpu
{
    struct cpu hdr;
    struct apic_device apic;
    struct apic_timer apic_tmr;
    struct gdt_entry *gdt;
    struct tss64_entry *tss;
};

struct platform_cpu_driver
{
    struct driver_node  drv_node;
    struct platform_cpu bsp_cpu;
    struct idt64_entry *idt;
    struct idt64_ptr    idt_ptr;
    struct isr ipi_isr;
};

struct __attribute__((packed)) isr_frame
{
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
};

extern void        __wrmsr(uint64_t reg, uint64_t val);
extern uint64_t    __rdmsr(uint64_t msr);
extern phys_addr_t __read_cr3(void);
extern void        __write_cr3(phys_addr_t );
extern virt_addr_t __stack_pointer(void);
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
extern void        __write_cr4(uint64_t cr4);
extern uint64_t    __read_cr0();
extern void        __write_cr0(uint64_t cr0);
extern uint64_t    __read_cr8();
extern void        __write_cr8(uint64_t cr8);
extern void        __cpuid
(
    uint32_t *eax,
    uint32_t *ebx,
    uint32_t *ecx,
    uint32_t *edx
);

extern void     __sti();
extern void     __cli();
extern uint8_t  __geti();
extern void     __lidt(struct idt64_ptr *);
extern void     __hlt();
extern void     __pause();



#define cpu_halt        __hlt
#define cpu_pause       __pause
#define cpu_int_lock    __cli
#define cpu_int_unlock  __sti
#define cpu_int_check   __geti


int platform_pre_init(void);
int platform_early_init(void);
int platform_init(void);
#endif