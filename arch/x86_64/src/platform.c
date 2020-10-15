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



int platform_register(void)
{

    pcpu_init();

    return(0);
}

static int platform_setup_intc(void)
{
    char *intc_drv[] = {APIC_DRIVER_NAME, PIC8259_DRIVER_NAME};
    int has_apic = 0;
    dev_t *dev = NULL;
    dev_t *temp_dev = NULL;

    apic_register();
    pic8259_register();
    ioapic_register();
  
    /* Initialize the interrupt controllers */
    for(int i = 0; i < sizeof(intc_drv) / sizeof(char*); i++)
    {
        dev = NULL;

        if(!devmgr_dev_create(&dev))
        {
            devmgr_dev_name_set(dev, intc_drv[i]);
            devmgr_dev_type_set(dev, INTERRUPT_CONTROLLER);

            if(devmgr_dev_add(dev, NULL))
            {
               kprintf("Failed to add %s\n", intc_drv[i]); 
            }            
        }
    }

    dev = devmgr_dev_get_by_name(PIC8259_DRIVER_NAME, 0);
    intc_disable(dev);
}

int platform_init(void)
{
    if(isr_init() != 0)
        return(-1);
    
    if(pagemgr_install_handler() != 0)
        return(-1);

    acpi_mem_mgr_on();

    /* Initialize interrupt controllers */
    platform_setup_intc();
#if 0
    for(int i = 0; i < sizeof(intc_names) / sizeof(char*); i++)
    {
        intc = intc_probe(intc_names[i]);
        
        if(intc != NULL)
        {
            if(intc_init(intc) != 0)
            {
                return(-1);
            }
            kprintf("USING PIC %s\n",intc_names[i]);
            break;
        }
        
    }

    if(intc == NULL)
    {
        return(-1);
    }
#endif
    cpu_init();
}
