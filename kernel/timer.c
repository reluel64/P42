
#include <linked_list.h>
#include <timer.h>
#include <spinlock.h>
#include <utils.h>
#include <liballoc.h>

static list_head_t timers;
static spinlock_t lock;


static int fired = 0;

void timer_update(uint32_t interval)
{
    timer_t *tm = NULL;
    list_node_t *node = NULL;
    list_node_t *next = NULL;
    int int_status = 0;
    spinlock_lock_interrupt(&lock, &int_status);
    node = linked_list_first(&timers);
    
    while(node)
    {
        next = linked_list_next(node);
        tm = (timer_t*)node;

        if(tm->ttime <= tm->ctime)
        {
            if(tm->handler(tm->data))
            {
                linked_list_remove(&timers, &tm->node);
            }
            tm->ctime = 0;
        }
        else
            tm->ctime += interval;

        node = next;
    }
   
    spinlock_unlock_interrupt(&lock, int_status);
}

void *timer_arm
(
    device_t *dev, 
    timer_handler_t cb, 
    void *data,
    uint32_t delay
)
{
    timer_t *timer = NULL;
    int     int_status = 0;
    timer = kcalloc(1, sizeof(timer_t));
    
    timer->handler = cb;
    timer->data = data;
    timer->ttime = delay;

    spinlock_lock_interrupt(&lock, &int_status);

    linked_list_add_tail(&timers, &timer->node);

    spinlock_unlock_interrupt(&lock, int_status);
    return(timer);
    
}

static int timer_sleep_callback(void *pv)
{
    __sync_fetch_and_add((int*)pv, 1);
    return(1);
}

void timer_loop_delay(device_t *dev, uint32_t delay)
{
    timer_t *timer = NULL;
    int wait;
    wait = 0;

    timer = timer_arm(dev, timer_sleep_callback, (void*)&wait, delay);

    while(!__sync_bool_compare_and_swap(&wait, 1, 0))
    {
        __pause();
    }

    kfree(timer);
}