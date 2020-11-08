#ifndef platformh
#define platformh
#include <gdt.h>
extern virt_addr_t kstack_base;
extern virt_addr_t kstack_top;

#define _BSP_STACK_TOP ((virt_addr_t)&kstack_top)
#define _BSP_STACK_BASE ((virt_addr_t)&kstack_base)

#define PLATFORM_CPU_NAME "x86_cpu"
#define CPU_TRAMPOLINE_LOCATION_START (0x8000)
#define PER_CPU_STACK_SIZE            (0x8000) /* 32 K */
#define START_AP_STACK_SIZE           (PAGE_SIZE) /* 4K */

typedef struct cpu_platform_t
{
    gdt_entry_t *gdt;
    tss64_entry_t *tss;
    virt_addr_t esp0;
}cpu_platform_t;

int pcpu_register(void);

#endif