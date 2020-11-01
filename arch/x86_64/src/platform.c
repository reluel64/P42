#include <cpu.h>
#include <isr.h>
#include <intc.h>
#include <pagemgr.h>
#include <apic.h>
#include <pic8259.h>
#include <devmgr.h>
#include <ioapic.h>
extern int pcpu_init(void);
extern int apic_register(void);
extern int pic8259_register(void);
extern int acpi_mem_mgr_on(void);
extern int ioapic_register(void);
extern int pit8254_register(void);

int platform_register(void)
{
    pcpu_init();
    return(0);
}

static int platform_setup_intc(void)
{
    apic_register();
    pic8259_register();
    ioapic_register();
    pit8254_register();
    return(0);
}

int isr_test(void)
{
    vga_print("TEST\n",0x7,-1);
    return(-1);
}
extern void test_interrupt(void);
int platform_init(void)
{

    __cli();
    if(isr_init() != 0)
        return(-1);
    
    if(pagemgr_install_handler() != 0)
        return(-1);

    acpi_mem_mgr_on();
    
    /* Initialize interrupt controllers */
    platform_setup_intc();
    
    cpu_init();
    __sti();
}
