#if 0
#include <linked_list.h>
#include <timer.h>
#include <spinlock.h>
#include <utils.h>

static list_head_t timer_list = {0};
static spinlock_t lock = {0};
static timer_t *active_tm = NULL;

int timer_register(timer_t *tm)
{
    linked_list_add_tail(&timer_list,&tm->node);
    return(0);
}
#error "TEST"
void *timer_probe(char *tm_name)
{
    timer_t *tm = NULL;

    tm = (timer_t*)linked_list_first(&timer_list);

    while(tm)
    {
        if(!strcmp(tm_name, tm->name))
        {
            if(!tm->probe())
            {
                active_tm = tm;
                return(0);
            }
        }
        tm = linked_list_next(&tm->node);
    }

    return(-1);
}

int timer_init(void *timer)
{
    timer_t *tm = NULL;
    int status = -1;

    if(timer == NULL)
        return(-1);

    spinlock_lock_interrupt(&lock);
    tm = timer;

    if(tm->init)
        status = tm->init();

    if(!status)
        active_tm = tm;
        
    spinlock_unlock_interrupt(&lock);
    return(status);
}

int timer_arm
(
    uint32_t val, 
    timer_callback_t *cb, 
    void *cb_pv
)
{
    int status = 0;

    spinlock_lock_interrupt(&lock);

    
    
    spinlock_unlock_interrupt(&lock);

    return(status);
}
#endif