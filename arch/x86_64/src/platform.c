#include <cpu.h>
#include <isr.h>
#include <intc.h>
#include <vmmgr.h>
#include <apic.h>
#include <pic8259.h>
#include <devmgr.h>
#include <ioapic.h>
#include <platform.h>
#include <apic_timer.h>

extern int pcpu_init(void);
extern int apic_register(void);
extern int pic8259_register(void);
extern int acpi_mem_mgr_on(void);
extern int ioapic_register(void);
extern int pit8254_register(void);
extern int apic_timer_register(void);

int platform_early_init(void)
{
    /* Tell ACPI code that now the memory manager is up and running */
    acpi_mem_mgr_on();

    /* Register and initalize interrupt controllers */

    /* 
     * It's important that PIC8259 is initialized before
     * IOAPIC as the IOAPIC driver will disable pic8259 
     * during the initialization process
     */
    
    pic8259_register();
    ioapic_register();

    /* register and initialize PIT */
    
    pit8254_register();

    /* Register and initialize APIC */
    apic_register();
   
    /* Register and initialize APIC TIMER */
    apic_timer_register();
     
    vga_init();

    return(0);
}

int platform_init(void)
{
    device_t *ioapic = NULL;
    device_t *apic_timer = NULL;
#if 0
    /* Mask the PIT8254 */
    apic_timer = devmgr_dev_get_by_name(APIC_TIMER_NAME, 0);

    /* if we have the APIC timer, we should disable the PIT8254 */
    if(apic_timer != NULL)
    {
        ioapic = devmgr_dev_get_by_name(IOAPIC_DRV_NAME, 0);
        intc_mask_irq(ioapic, 0);
    }
#endif
}
