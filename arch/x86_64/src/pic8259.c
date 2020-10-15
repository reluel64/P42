#include <port.h>
#include <pic8259.h>
#include <acpi.h>
#include <devmgr.h>
#include <intc.h>

static int pic8259_probe(dev_t *dev)
{
    int has_pic             = 0;
    ACPI_STATUS             status  = AE_OK;
    ACPI_TABLE_MADT        *madt    = NULL;

    if(!devmgr_dev_name_match(dev, PIC8259_DRIVER_NAME))
        return(-1);

    status = AcpiGetTable(ACPI_SIG_MADT, 0, (ACPI_TABLE_HEADER**)&madt);

    if(ACPI_FAILURE(status))
    {
        return(-1);
    }

    has_pic = !!(madt->Flags & ACPI_MADT_PCAT_COMPAT);

    AcpiPutTable((ACPI_TABLE_HEADER*)madt);

    return(has_pic ? 0 : -1);
}

static int pic8259_drv_init(drv_t *drv)
{
    return(0);
}

static int pic8259_dev_init(dev_t *drv)
{
    return(0);
}


static int pic8259_disable(dev_t *dev)
{
    __outb(0xa1, 0xff);
    __outb(0x21, 0xff);
    return(0);
}

static intc_api_t pic_8259_api = 
{
    .enable = NULL,
    .disable = pic8259_disable,
    .send_ipi = NULL
};

static drv_t pic8259 = 
{
    .drv_name   = PIC8259_DRIVER_NAME,
    .dev_probe  = pic8259_probe,
    .dev_init   = pic8259_dev_init,
    .dev_uninit = NULL,
    .drv_type  = INTERRUPT_CONTROLLER,
    .drv_init   = pic8259_drv_init,
    .drv_uninit = NULL,
    .drv_api    = &pic_8259_api
};


int pic8259_register(void)
{
    devmgr_drv_add(&pic8259);
    devmgr_drv_init(&pic8259);
    return(0);
}