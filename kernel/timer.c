
#include <linked_list.h>
#include <timer.h>
#include <spinlock.h>
#include <utils.h>
#include <liballoc.h>

void timer_update
(
    list_head_t *queue,
    uint32_t interval
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

        if(tm->ttime <= tm->ctime)
        {
            if(tm->handler)
            {
                tm->handler(tm->data);
            }

            if(tm->flags & TIMER_PERIODIC)
            {
                tm->ctime = 0;
            }
            else
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

void *timer_arm
(
    device_t *dev, 
    timer_handler_t cb, 
    void *data,
    uint32_t delay
)
{
    timer_api_t *api   = NULL;
    timer_t     *timer = NULL;
    int     int_status = 0;
    
    api = devmgr_dev_api_get(dev);

    if(api == NULL)
        return(NULL);
    
    timer = kcalloc(1, sizeof(timer_t));
    
    timer->handler = cb;
    timer->data = data;
    timer->ttime = delay;

    api->arm_timer(dev, timer);

    return(timer);
    
}

static int timer_sleep_callback(void *pv)
{
    kprintf("CPU %d\n",cpu_id_get());
    __sync_fetch_and_add((int*)pv, 1);
   
    return(1);
}

void timer_loop_delay(device_t *dev, uint32_t delay)
{
    timer_t *timer = NULL;
    int wait;
    wait = 0;
    timer = timer_arm(dev, timer_sleep_callback, (void*)&wait, delay);

    if(timer == NULL)
    {
        return;
    }

    while(!__sync_bool_compare_and_swap(&wait, 1, 0))
    {
        __pause();
    }

    kfree(timer);
}

int timer_periodic_install
(
    device_t *dev,
    timer_handler_t cb, 
    void *pv, 
    uint32_t period
)
{
    timer_t *tm = NULL;
    timer_api_t *api = NULL;

    tm = kcalloc(sizeof(timer_t), 1);
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