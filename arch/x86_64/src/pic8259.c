#include <port.h>
#include <pic8259.h>
#include <acpi.h>
#include <devmgr.h>
#include <intc.h>
#include <isr.h>
#include <utils.h>

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

#define ICW1_IC4  (1 << 0)
#define ICW1_SNGL (1 << 1)
#define ICW1_ADI  (1 << 2)
#define ICW1_LTIM (1 << 3)
#define ICW1_PIC_INIT (1 << 4)

static isr_t pic8259_eoi = {0};

static int pic8259_eoi_isr
(
    void *dev, 
    isr_info_t *inf
)
{
    kprintf("%s %d %d\n",__FILE__,__FUNCTION__,__LINE__);
    __outb(PIC1_COMMAND,( 1<<5 ));
    __outb(PIC2_COMMAND,( 1<<5 ));
    return(0);
}

static int pic8259_probe
(
    device_t *dev
)
{
    int has_pic             = 0;
    ACPI_STATUS             status  = AE_OK;
    ACPI_TABLE_MADT        *madt    = NULL;

    if(!devmgr_dev_name_match(dev, PIC8259_DRIVER_NAME))
    {
        return(-1);
    }

    status = AcpiGetTable(ACPI_SIG_MADT, 0, (ACPI_TABLE_HEADER**)&madt);

    if(ACPI_FAILURE(status))
    {
        return(-1);
    }

    has_pic = !!(madt->Flags & ACPI_MADT_PCAT_COMPAT);

    AcpiPutTable((ACPI_TABLE_HEADER*)madt);

    return(has_pic ? 0 : -1);
}

static int pic8259_drv_init
(
    driver_t *drv
)
{
    device_t *dev = NULL;

    if(!devmgr_dev_create(&dev))
    {
        devmgr_dev_name_set(dev, PIC8259_DRIVER_NAME);
        devmgr_dev_type_set(dev, INTERRUPT_CONTROLLER);
        devmgr_dev_index_set(dev, 0);
        
        if(devmgr_dev_add(dev, NULL))
        {
            kprintf("%s %d failed to add device\n");
            return(-1);
        }
        else
        {
            isr_install(pic8259_eoi_isr,
                        dev,
                        0, 
                        1, 
                        &pic8259_eoi);
        }
    }

    return(0);
}

static int pic8259_dev_init
(
    device_t *drv
)
{

    __outb(PIC1_COMMAND, ICW1_IC4|ICW1_PIC_INIT);
    __outb(PIC2_COMMAND, ICW1_IC4|ICW1_PIC_INIT);

    __outb(PIC1_DATA, 0x20);
    __outb(PIC2_DATA, 0x28);

    __outb(PIC1_DATA, 0x4);
    __outb(PIC2_DATA, 0x2);

    __outb(PIC1_DATA, 0x1);
    __outb(PIC2_DATA, 0x1);

    return(0);
}

static int pic8259_disable
(
    device_t *dev
)
{
    __outb(0xa1, 0xff);
    __outb(0x21, 0xff);
    return(0);
}

static int pic8259_enable
(
    device_t *dev
)
{
  
    __outb(0xa1, 0);
    __outb(0x21, 0);
    return(0);
}

static intc_api_t pic_8259_api = 
{
    .enable   = pic8259_enable,
    .disable  = pic8259_disable,
    .send_ipi = NULL,
    .unmask   = NULL,
    .mask     = NULL
};

static driver_t pic8259 = 
{
    .drv_name   = PIC8259_DRIVER_NAME,
    .dev_probe  = pic8259_probe,
    .dev_init   = pic8259_dev_init,
    .dev_uninit = NULL,
    .drv_type   = INTERRUPT_CONTROLLER,
    .drv_init   = pic8259_drv_init,
    .drv_uninit = NULL,
    .drv_api    = &pic_8259_api
};


int pic8259_register
(
    void
)
{
    devmgr_drv_add(&pic8259);
    devmgr_drv_init(&pic8259);

    return(0);
}