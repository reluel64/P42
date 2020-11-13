/* PIT 8259 */


#include <port.h>
#include <devmgr.h>
#include <timer.h>
#include <isr.h>

#define PIT8254_TIMER "i8254"
#define COMMAND_PORT 0x43
#define CH0_PORT    0x40

#define INTERRUPT_INTERVAL 16

static int i = 0;
static int seconds = 0;


static int pit8254_irq_handler(void *dev, uint64_t ec)
{
    timer_update(INTERRUPT_INTERVAL);

    return(0);
}

static int pit8254_probe(device_t *dev)
{
    if(devmgr_dev_name_match(dev, PIT8254_TIMER) &&
      devmgr_dev_type_match(dev, TIMER_DEVICE_TYPE))
      return(0);

    return(-1);
}

static int pit8254_init(device_t *dev)
{
    uint8_t command = 0;
    uint16_t divider = (INTERRUPT_INTERVAL * 3579545ul) / 3000;
    command = 0b000110100;

    __outb(COMMAND_PORT, command);
    __outb(CH0_PORT, divider & 0xff);
    __outb(CH0_PORT, (divider >> 8) & 0xff);
}

static int pit8254_drv_init(driver_t *drv)
{
    device_t *dev = NULL;
    
    if(!devmgr_dev_create(&dev))
    {
        isr_install(pit8254_irq_handler, dev, 0x20, 0);
        devmgr_dev_name_set(dev, PIT8254_TIMER);
        devmgr_dev_type_set(dev, TIMER_DEVICE_TYPE);
        devmgr_dev_index_set(dev, 0);

        if(!devmgr_dev_add(dev, NULL))
        {
            isr_install(pit8254_irq_handler, dev, 0x20, 0);
        }
    }

    return(0);
}

static timer_api_t pit8254_api = 
{
    .arm_timer    = NULL,
    .disarm_timer = NULL
};

static driver_t pit8254 = 
{
    .drv_name = PIT8254_TIMER,
    .drv_type = TIMER_DEVICE_TYPE,
    .dev_probe = pit8254_probe,
    .dev_init =  pit8254_init,
    .drv_init =  pit8254_drv_init,
    .drv_api  = &pit8254_api
};

int pit8254_register(void)
{
    devmgr_drv_add(&pit8254);
    devmgr_drv_init(&pit8254);

    return(0);
}