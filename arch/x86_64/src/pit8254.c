/* PIT 8259 */


#include <port.h>
#include <devmgr.h>
#include <timer.h>
#include <isr.h>
#include <spinlock.h>
#include <i8254.h>
#include <liballoc.h>
#include <platform.h>
#include <utils.h>

#define COMMAND_PORT 0x43
#define CH0_PORT    0x40


#define PIT8254_MODE_2        (1 << 2)
#define PIT8254_MODE_3       ((1 << 1) | (1 << 2))

#define PIT8254_ACCESS_LO_HI ((1 << 4) | (1 << 5))

#define MODE_MASK             (0b00001110)

#define PIT8254_FREQ             (1193182ull) /* HZ */

#define PIT8254_REQ_RESOLUTION   (1000ull)
#define PIT8254_DIVIDER          (PIT8254_FREQ / PIT8254_REQ_RESOLUTION)


struct pit8254_dev
{
    struct device_node  dev_node;
    struct spinlock_rw           lock; 
    uint16_t             divider;
    timer_tick_handler_t handler;
    void                *handler_data;
    struct isr                timer_isr;
    uint8_t              mode;
    struct time_spec          resolution;
};

static struct pit8254_dev _pit_dev = {0};

static int pit8254_irq_handler
(
    void *dev, 
    struct isr_info *inf
);

static int pit8254_rearm(struct device_node *dev);

static int pit8254_probe(struct device_node *dev)
{
    if(devmgr_dev_name_match(dev, PIT8254_TIMER) &&
      devmgr_dev_type_match(dev, TIMER_DEVICE_TYPE))
    {
        return(0);
    }
    
    return(-1);
}

static int pit8254_init(struct device_node *dev)
{
    struct pit8254_dev *pit_dev = NULL;
  
    pit_dev = (struct pit8254_dev*)dev;
    pit_dev->mode = PIT8254_MODE_2 | PIT8254_ACCESS_LO_HI;
    
    spinlock_rw_init(&pit_dev->lock);
    
    pit_dev->divider = 1193;
    pit_dev->resolution.nanosec = (1000000000 / PIT8254_FREQ) * pit_dev->divider ;
    
    kprintf("RESOLUTION %d DIVIDER %d\n",pit_dev->resolution.nanosec, pit_dev->divider);
    __outb(COMMAND_PORT, pit_dev->mode);


    pit8254_rearm(dev);

    return(0);
}

static int pit8254_drv_init(struct driver_node *drv)
{    

    if(!devmgr_device_node_init(&_pit_dev.dev_node))
    {
        devmgr_dev_name_set(&_pit_dev.dev_node, PIT8254_TIMER);
        devmgr_dev_type_set(&_pit_dev.dev_node, TIMER_DEVICE_TYPE);
        devmgr_dev_index_set(&_pit_dev.dev_node, 0);
        
        if(!devmgr_dev_add(&_pit_dev.dev_node, NULL))
        {

            isr_install(pit8254_irq_handler, 
                        &_pit_dev.dev_node, 
                        IRQ(0), 
                        0, 
                        &_pit_dev.timer_isr);
        }
    }

    return(0);
}

static int pit8254_rearm(struct device_node *dev)
{
    struct pit8254_dev *pit = NULL;
    
    pit = (struct pit8254_dev *)dev;

    __outb(CH0_PORT, pit->divider & 0xff);
    __outb(CH0_PORT, (pit->divider >> 8) & 0xff);
    
    return(0);
}

static int pit8254_irq_handler
(
    void *dev, 
    struct isr_info *inf
)
{
    struct pit8254_dev *pit_dev = NULL;
    
    pit_dev = (struct pit8254_dev *)dev;

    spinlock_read_lock(&pit_dev->lock);
    
    if(pit_dev->handler != NULL)
    {
        pit_dev->handler(pit_dev->handler_data, &pit_dev->resolution, inf);
    }

    spinlock_read_unlock(&pit_dev->lock);

    return(0);
}

static int pit8254_set_handler
(
    struct device_node             *dev,
    timer_tick_handler_t th,
    void                 *arg
)
{
    struct pit8254_dev *timer = NULL;
    uint8_t        int_flag = 0;

    timer = (struct pit8254_dev *)dev;

    spinlock_write_lock_int(&timer->lock, &int_flag);

    timer->handler      = th;
    timer->handler_data = arg;

    spinlock_write_unlock_int(&timer->lock, int_flag);

    return(0);
}

static int pit8254_get_handler
(
    struct device_node             *dev,
    timer_tick_handler_t *th,
    void                 **arg
)
{
    struct pit8254_dev *timer = NULL;
    uint8_t       int_status = 0;
    
    if(th == NULL || arg == NULL)
    {
        return(-1);
    }

    timer =(struct pit8254_dev *)dev;

    spinlock_read_lock_int(&timer->lock, &int_status);

    if(th != NULL)
    {
       *th  = timer->handler;
    }

    if(arg != NULL)
    {
        *arg = timer->handler_data;
    }

    spinlock_read_unlock_int(&timer->lock, int_status);

    return(0);
}

static int pit8254_set_timer
(
    struct device_node    *dev,
    struct time_spec *tm
)
{
    return(0);
}

static int pit8254_set_mode
(
    struct device_node *dev,
    uint8_t  mode
)
{
    return(0);
}

static struct timer_api pit8254_api = 
{
    .enable      = NULL,
    .disable     = NULL,
    .reset       = pit8254_rearm,
    .set_handler = pit8254_set_handler,
    .get_handler = pit8254_get_handler,
    .set_timer   = pit8254_set_timer,
    .set_mode    = pit8254_set_mode
};

static struct driver_node pit8254 = 
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