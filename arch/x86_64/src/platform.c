#include <cpu.h>
#include <isr.h>
#include <intc.h>
#include <vm.h>
#include <apic.h>
#include <pic8259.h>
#include <devmgr.h>
#include <ioapic.h>
#include <platform.h>
#include <apic_timer.h>
#include <acpi.h>
#include <serial.h>
#include <utils.h>
#include <vga.h>


extern int pcpu_init(void);
extern int apic_register(void);
extern int pic8259_register(void);
extern int acpi_mem_mgr_on(void);
extern int ioapic_register(void);
extern int pit8254_register(void);
extern int apic_timer_register(void);


/******************************************************************************
 *
 * Example ACPICA handler and handler installation
 *
 *****************************************************************************/

static void
NotifyHandler (
    ACPI_HANDLE                 Device,
    UINT32                      Value,
    void                        *Context)
{

    kprintf ("Received a notify 0x%x", Value);
}


static ACPI_STATUS
RegionInit (
    ACPI_HANDLE                 RegionHandle,
    UINT32                      Function,
    void                        *HandlerContext,
    void                        **RegionContext)
{

    if (Function == ACPI_REGION_DEACTIVATE)
    {
        *RegionContext = NULL;
    }
    else
    {
        *RegionContext = RegionHandle;
    }

    return (AE_OK);
}


static ACPI_STATUS
RegionHandler (
    UINT32                      Function,
    ACPI_PHYSICAL_ADDRESS       Address,
    UINT32                      BitWidth,
    UINT64                      *Value,
    void                        *HandlerContext,
    void                        *RegionContext)
{

    kprintf ("Received a region access");

    return (AE_OK);
}


static ACPI_STATUS
InstallHandlers (void)
{
    ACPI_STATUS             Status;


    /* Install global notify handler */

    Status = AcpiInstallNotifyHandler (ACPI_ROOT_OBJECT,
        ACPI_SYSTEM_NOTIFY, NotifyHandler, NULL);
    if (ACPI_FAILURE (Status))
    {
      
        return (Status);
    }

    Status = AcpiInstallAddressSpaceHandler (ACPI_ROOT_OBJECT,
        ACPI_ADR_SPACE_SYSTEM_MEMORY, RegionHandler, RegionInit, NULL);
    if (ACPI_FAILURE (Status))
    {
       
        return (Status);
    }

    return (AE_OK);
}



#define ACPI_MAX_INIT_TABLES    1024
static ACPI_TABLE_DESC      TableArray[ACPI_MAX_INIT_TABLES];


/*
 * This function would be called early in kernel initialization. After this
 * is called, all ACPI tables are available to the host.
 */
ACPI_STATUS
InitializeAcpiTables (
    void)
{
    ACPI_STATUS             Status;


    /* Initialize the ACPICA Table Manager and get all ACPI tables */

    Status = AcpiInitializeTables (NULL, ACPI_MAX_INIT_TABLES, TRUE);
    return (Status);
}


/*
 * This function would be called after the kernel is initialized and
 * dynamic/virtual memory is available. It completes the initialization of
 * the ACPICA subsystem.
 */
ACPI_STATUS
InitializeAcpi (
    void)
{
    ACPI_STATUS             Status;


    /* Initialize the ACPICA subsystem */

    Status = AcpiInitializeSubsystem ();
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    /* Copy the root table list to dynamic memory */

    Status = AcpiReallocateRootTable ();
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    /* Install local handlers */

    Status = InstallHandlers ();
    if (ACPI_FAILURE (Status))
    {
      
        return (Status);
    }

    /* Initialize the ACPI hardware */

    Status = AcpiEnableSubsystem (ACPI_FULL_INITIALIZATION);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    /* Create the ACPI namespace from ACPI tables */

    Status = AcpiLoadTables ();
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    /* Complete the ACPI namespace object initialization */

    Status = AcpiInitializeObjects (ACPI_FULL_INITIALIZATION);
    if (ACPI_FAILURE (Status))
    {
        return (Status);
    }

    return (AE_OK);
}


int platform_pre_init(void)
{
    int status = 0;

    /* init polling console */
    init_serial();

    /* initialize temporary mapping */
    pagemgr_boot_temp_map_init();

    return(status);
}

int platform_early_init(void)
{
    /* install ISR handlers for the page manager */
 
    if(pagemgr_install_handler())
        return(-1);

    /* Tell ACPI code that now the memory manager is up and running */
    acpi_mem_mgr_on();
    InitializeAcpiTables();
    InitializeAcpi();

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

    /*cpu_ap_start(-1, PLATFORM_AP_START_TIMEOUT);*/
#if 1
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

