#include <stdint.h>
#include <devmgr.h>
#include <acpi.h>
#include <port.h>
#include <isr.h>
#include <platform.h>
#include <utils.h>
#include <semaphore.h>
#define IO_CONTROLLER "ioctrl"
#define I8042_DEV_NAME "i8042"
#define I8042_CMD_DISABLE_PORT1    (0xAD)
#define I8042_CMD_DISABLE_PORT2    (0xA7)
#define I8042_CMD_ENABLE_PORT1     (0xAE)
#define I8042_CMD_ENABLE_PORT2     (0xA8)
#define I8042_CMD_READ_BYTE0       (0x20)
#define I8042_CMD_WRITE_BYTE0      (0x60)
#define I8042_CMD_TEST_CTRL        (0xAA)
#define I8042_CMD_TEST_FIRST_PORT  (0xAB)
#define I8042_CMD_TEST_SECOND_PORT (0xA9)

#define I8042_DATA_PORT            (0x60)
#define I8042_CMD_STS_PORT         (0x64)

typedef struct
{
    isr_t kbd_isr;
    isr_t mse_isr;
}i8042_dev_t;
extern sem_t *kb_sem;

static int i8042_kbd_irq(void *pv, isr_info_t *isr_inf)
{
    __inb(I8042_DATA_PORT);
    sem_release(kb_sem);
  //  kprintf("%s %s %d\n", __FILE__,__FUNCTION__,__LINE__);
}

static int i8042_mse_irq(void *pv, isr_info_t *isr_inf)
{
    __inb(I8042_DATA_PORT);
    kprintf("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
}

static int i8042_dev_init(device_t *dev)
{
    static  i8042_dev_t i8042 = {0};
    uint8_t status_reg    = 0;
    uint8_t config_byte   = 0;
    uint8_t two_port_ctrl = 0;
    uint8_t ctrl_test     = 0;
    uint8_t port_test     = 0;

    devmgr_dev_data_set(dev, &i8042);

    /* install ISRs now to handle any potential arrival of data */
    isr_install(i8042_kbd_irq, dev, IRQ(1), 0, &i8042.kbd_isr);
    isr_install(i8042_mse_irq, dev, IRQ(12), 0, &i8042.mse_isr);
    /* flush the output buffer */

    do
    {
        __inb(I8042_DATA_PORT);
        status_reg = __inb(I8042_CMD_STS_PORT);

    }while(status_reg & 0x1);

    /* issue read the controller conf byte */
    
    __outb(I8042_CMD_STS_PORT, I8042_CMD_READ_BYTE0);

    do
    {
        status_reg = __inb(I8042_CMD_STS_PORT);

    }while((status_reg & 0x1) == 0);


    /* reat the controller conf byte */
    config_byte = __inb(I8042_DATA_PORT);

    config_byte = config_byte & ~((1 << 0) | (1 << 1) | (1 << 6));

    /* check if we have two ports */
    two_port_ctrl = (config_byte & (1 << 5) != 0);

    /* Issue test on the controller */
    __outb(I8042_CMD_STS_PORT, I8042_CMD_TEST_CTRL);

    do
    {
        status_reg = __inb(I8042_CMD_STS_PORT);

    }while((status_reg & 0x1) == 0);

    ctrl_test = __inb(I8042_DATA_PORT);

    if(ctrl_test != 0x55)
    {
        /* if the controller failed, remove the ISRs */
        isr_uninstall(&i8042.kbd_isr, 0);
        isr_uninstall(&i8042.mse_isr, 0);

        kprintf("CTRL test failed\n");
        return(-1);
    }

    /* test first port */
   __outb(I8042_CMD_STS_PORT, I8042_CMD_TEST_FIRST_PORT);

    do
    {
        status_reg = __inb(I8042_CMD_STS_PORT);

    }while((status_reg & 0x1) == 0);

    port_test = __inb(I8042_DATA_PORT);

    if(port_test == 0x0)
    {
        __outb(I8042_CMD_STS_PORT, I8042_CMD_ENABLE_PORT1);

        /* enable interupts  - get config byte*/
        __outb(I8042_CMD_STS_PORT, I8042_CMD_READ_BYTE0);

        /* check if data arrived*/
        do
        {
            status_reg = __inb(I8042_CMD_STS_PORT);

        }while((status_reg & 0x1) == 0);


        /* read the controller conf byte */
        config_byte = __inb(I8042_DATA_PORT);

        /* enable interrupt on first port*/
        config_byte |= (1 << 0);

        do
        {
            status_reg = __inb(I8042_CMD_STS_PORT);

        }while(status_reg & 0x2);

        __outb(I8042_CMD_STS_PORT, I8042_CMD_WRITE_BYTE0);
        __outb(I8042_DATA_PORT, config_byte);
    }

    if(two_port_ctrl)
    {
        /* test second port */
       __outb(I8042_CMD_STS_PORT, I8042_CMD_TEST_SECOND_PORT);

        do
        {
            status_reg = __inb(I8042_CMD_STS_PORT);

        }while((status_reg & 0x1) == 0);

        port_test = __inb(I8042_DATA_PORT);

        if(port_test == 0x0)
        {
            __outb(I8042_CMD_STS_PORT, I8042_CMD_ENABLE_PORT2);
        }

        /* enable interupts  - get config byte*/
        __outb(I8042_CMD_STS_PORT, I8042_CMD_READ_BYTE0);

        /* check if data arrived*/
        do
        {
            status_reg = __inb(I8042_CMD_STS_PORT);
            
        }while((status_reg & 0x1) == 0);


        /* read the controller conf byte */
        config_byte = __inb(I8042_DATA_PORT);

        /* enable interrupt on second port*/
        config_byte |= (1 << 1);

        do
        {
            status_reg = __inb(I8042_CMD_STS_PORT);

        }while(status_reg & 0x2);

        __outb(I8042_CMD_STS_PORT, I8042_CMD_WRITE_BYTE0);
        __outb(I8042_DATA_PORT, config_byte);
    }

    return(0);
}

static int i8042_probe(device_t *dev)
{
    int status = -1;
    ACPI_TABLE_FADT *fadt = NULL;

    if(devmgr_dev_name_match(dev, I8042_DEV_NAME) == 0)
    {
        
        return(-1);
    }

    status = AcpiGetTable(ACPI_SIG_MADT, 0, (ACPI_TABLE_HEADER**)&fadt);

    if(ACPI_FAILURE(status))
    {
        
        return(-1);
    }

    if(fadt != NULL)
    {
        if(fadt->BootFlags & ACPI_FADT_8042)
        {
            status = 0;
        }
    }

    AcpiPutTable((ACPI_TABLE_HEADER*)fadt);
    
    return(status);
}

static int i8042_drv_init(driver_t *drv)
{
    device_t *dev = NULL;

    devmgr_dev_create(&dev);
    devmgr_dev_name_set(dev, I8042_DEV_NAME);
    devmgr_dev_index_set(dev, 0);
    devmgr_dev_add(dev, NULL);
}

static driver_t i8042_drv = 
{
    .drv_name   = I8042_DEV_NAME,
    .dev_probe  = i8042_probe,
    .dev_init   = i8042_dev_init,
    .dev_uninit = NULL,
    .drv_type   = IO_CONTROLLER,
    .drv_init   = i8042_drv_init,
    .drv_uninit = NULL,
    .drv_api    = NULL
};

int i8042_register(void)
{
    devmgr_drv_add(&i8042_drv);
    devmgr_drv_init(&i8042_drv);
}