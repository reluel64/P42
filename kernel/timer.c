
#include <linked_list.h>
#include <timer.h>
#include <spinlock.h>
#include <utils.h>
#include <liballoc.h>
#include <cpu.h>
void timer_update
(
    list_head_t *queue,
    uint32_t interval,
    virt_addr_t iframe
)
{
    timer_t *tm = NULL;
    list_node_t *node = NULL;
    list_node_t *next = NULL;
    int int_status = 0;
 
    node = linked_list_first(queue);
    
    while(node)
    {
        next = linked_list_next(node);
        tm = (timer_t*)node;
        
        if(tm->ttime <= tm->ctime + interval)
        {
            if(tm->handler)
            {
                tm->handler(tm->data, iframe);
                tm->ctime = 0;
            }

            if(!(tm->flags & TIMER_PERIODIC))
            {
                linked_list_remove(queue, node);
            }
        }
        else
        {
            tm->ctime += interval;
        }

        node = next;
    }
}

int timer_arm
(
    device_t *dev, 
    timer_t *tm,
    timer_handler_t cb, 
    void *data,
    uint32_t delay
)
{
    timer_api_t *api   = NULL;
    
    api = devmgr_dev_api_get(dev);

    if(api == NULL)
        return(-1);
    
    
    tm->handler = cb;
    tm->data = data;
    tm->ttime = delay;

    api->arm_timer(dev, tm);

    return(0);
    
}

static int timer_sleep_callback(void *pv)
{
    __atomic_store_n((int*)pv, 1, __ATOMIC_RELEASE);
   
    return(1);
}

int timer_loop_delay(device_t *dev, uint32_t delay)
{
    timer_t timer;
    int     wait   = 0;
    int     status = 0;
    
    memset(&timer, 0, sizeof(timer_t));
    
    status = timer_arm(dev, 
                      &timer, 
                      (timer_handler_t)timer_sleep_callback, 
                      (void*)&wait, 
                      delay);

    if(status < 0)
    {
        return(-1);
    }

    while(!__atomic_load_n(&wait, __ATOMIC_ACQUIRE))
    {
        cpu_pause();
    }

    return(0);
}



int timer_periodic_install
(
    device_t *dev,
    timer_t *tm,
    timer_handler_t cb, 
    void *pv, 
    uint32_t period
)
{
    timer_api_t *api = NULL;
    api = devmgr_dev_api_get(dev);

    if(tm == NULL)
    {
        kprintf("FAILED\n");
        return(-1);
    }
    tm->handler = cb;
    tm->data = pv;
    tm->ttime = period;

    tm->flags |= TIMER_PERIODIC;
 
    api->arm_timer(dev, tm);

    return(0);
}
