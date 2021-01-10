/* PIT 8259 */


#include <port.h>
#include <devmgr.h>
#include <timer.h>
#include <isr.h>
#include <spinlock.h>
#include <i8254.h>
#include <liballoc.h>
#include <platform.h>

#define COMMAND_PORT 0x43
#define CH0_PORT    0x40

#define INTERRUPT_INTERVAL_MS 1ull

typedef struct pit8254_dev_t
{
    list_head_t queue;
    spinlock_t  lock; 
    uint16_t divider;
}pit8254_dev_t;

static int counter = 0;

static inline void pit8254_rearm(pit8254_dev_t *pit)
{
    __outb(CH0_PORT, pit->divider & 0xff);
    __outb(CH0_PORT, (pit->divider >> 8) & 0xff);
}


static int pit8254_irq_handler(void *dev, isr_info_t *inf)
{
    pit8254_dev_t *pit_dev = NULL;
    uint16_t divider = 0;
    int int_status = 0;
    pit_dev = devmgr_dev_data_get(dev);

   
    spinlock_lock_int(&pit_dev->lock, &int_status);

    timer_update(&pit_dev->queue, INTERRUPT_INTERVAL_MS, inf);
    
    pit8254_rearm(pit_dev);
    
    spinlock_unlock_int(&pit_dev->lock, int_status);

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
    uint8_t        command = 0;
    pit8254_dev_t *pit_dev = NULL;

    command = 0b000110100;

    pit_dev = (pit8254_dev_t*)kcalloc(sizeof(pit8254_dev_t), 1);

    spinlock_init(&pit_dev->lock);
    linked_list_init(&pit_dev->queue);
    devmgr_dev_data_set(dev, pit_dev);
    
    pit_dev->divider = 1192;

    __outb(COMMAND_PORT, command);

    pit8254_rearm(pit_dev);

    return(0);
}

static int pit8254_drv_init(driver_t *drv)
{
    device_t *dev = NULL;
    pit8254_dev_t *pit_dev = NULL;

    if(!devmgr_dev_create(&dev))
    {
        devmgr_dev_name_set(dev, PIT8254_TIMER);
        devmgr_dev_type_set(dev, TIMER_DEVICE_TYPE);
        devmgr_dev_index_set(dev, 0);
        
        if(!devmgr_dev_add(dev, NULL))
        {
            isr_install(pit8254_irq_handler, dev, IRQ0, 0);
        }
    }

    return(0);
}

static int pit8254_arm_timer(device_t *dev, timer_t *tm)
{
    pit8254_dev_t *pit_dev    = NULL;
    int            int_status = 0;
    pit_dev = devmgr_dev_data_get(dev);

    spinlock_lock_int(&pit_dev->lock, &int_status);

    linked_list_add_tail(&pit_dev->queue, &tm->node);

    spinlock_unlock_int(&pit_dev->lock, int_status);

    return(0);
}

static timer_api_t pit8254_api = 
{
    .arm_timer    = pit8254_arm_timer,
    .disarm_timer = NULL
};

static driver_t pit8254 = 
{
    .drv_name  = PIT8254_TIMER,
    .drv_type  = TIMER_DEVICE_TYPE,
    .dev_probe = pit8254_probe,
    .dev_init  = pit8254_init,
    .drv_init  = pit8254_drv_init,
    .drv_api   = &pit8254_api
};

int pit8254_register(void)
{

    devmgr_drv_add(&pit8254);
    devmgr_drv_init(&pit8254);

    return(0);
}