
#include <linked_list.h>
#include <timer.h>
#include <spinlock.h>
#include <utils.h>
#include <liballoc.h>

static list_head_t timers;
static spinlock_t lock;

void timer_update(void)
{
    timer_t *tm = NULL;
    list_node_t *node = NULL;
    list_node_t *next = NULL;

    spinlock_lock(&lock);
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
            tm->ctime++;

        node = next;
    }
    spinlock_unlock(&lock);
}

void *timer_arm
(
    dev_t *dev, 
    timer_handler_t *cb, 
    void *data,
    uint32_t delay
)
{
    timer_t *timer = NULL;

    timer = kcalloc(1, sizeof(timer_t));
    

    timer->handler = cb;
    timer->data = data;
    timer->ttime = delay;
    spinlock_lock(&lock);
    linked_list_add_head(&timers, &timer->node);
    spinlock_unlock(&lock);

    return(timer);
    
}

static int timer_sleep_callback(void *pv)
{
    spinlock_unlock((spinlock_t*)pv);
    return(1);
}

void timer_loop_delay(dev_t *dev, uint32_t delay)
{
    spinlock_t wait;
    timer_t *timer = NULL;
    spinlock_init(&wait);

    /* deliberately lock the spinlock */
    spinlock_lock(&wait);

    timer = timer_arm(dev, timer_sleep_callback, &wait, delay);

    spinlock_lock(&wait);

    kfree(timer);

}