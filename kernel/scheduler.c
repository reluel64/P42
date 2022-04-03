#include <linked_list.h>
#include <defs.h>
#include <spinlock.h>
#include <liballoc.h>
#include <utils.h>
#include <intc.h>
#include <isr.h>
#include <timer.h>
#include <vm.h>
#include <intc.h>
#include <platform.h>
#include <scheduler.h>
#include <context.h>
#include <thread.h>
#include <isr.h>
#include <platform.h>

#define THREAD_NOT_RUNNABLE(x) (((x) & (THREAD_SLEEPING | THREAD_BLOCKED)))
#define SCHEDULER_TICK_MS 1


static list_head_t units;
static spinlock_t  units_lock;
static sched_owner_t kernel;

/* used only when there is no sched unit in place */
static list_head_t   pre_policy_queue;;
static spinlock_t pre_policy_lock;


static void sched_idle_thread(void *pv);
static void schedule_main(void);


extern void __context_unit_start(void *tcb);
extern int sched_simple_register(sched_policy_t **p);

static uint32_t sched_update_time
(
    void *unit,
    void *isr_info
);


void sched_thread_entry_point
(
    sched_thread_t *th
)
{
    int             int_status   = 0;
    void *(*entry_point)(void *) = NULL;

    entry_point = th->entry_point;
   
    /* during startup of the thread, unlock the scheduling unit 
     * on which we are running.
     * failing to do so will cause one thread to run on the locked unit
     */
    spinlock_unlock_int(&th->unit->lock);
    cpu_int_unlock();

    if(entry_point != NULL)
    {
        th->rval = entry_point(th->arg);
    }

    /* The thread is now dead */
    spinlock_lock_int(&th->lock);

    __atomic_or_fetch(&th->flags, THREAD_DEAD, __ATOMIC_ACQUIRE);
    
    spinlock_unlock_int(&th->lock);
    
    while(1)
    {
        sched_yield();
    }
}


int sched_enqueue_thread
(
    sched_thread_t *th
)
{
    cpu_t *cpu = NULL;
    sched_exec_unit_t *unit = NULL;

    cpu = cpu_current_get();

    if((cpu == NULL) || cpu->sched == NULL)
    {
        spinlock_lock_int(&pre_policy_lock);
        linked_list_add_tail(&pre_policy_queue, &th->node);
        spinlock_unlock_int(&pre_policy_lock);
        return(-1);
    }

    unit = cpu->sched;
    
    /* Lock everything */
    spinlock_lock_int(&th->lock);
    spinlock_lock_int(&unit->lock);

    /* this thread belongs to this unit */
    th->unit = unit;

    /* Ask the policy to enqueue the thread */
    unit->policy->enqueue_new_thread(unit->policy_data, th);

    /* Unlock everything */
    spinlock_unlock_int(&unit->lock);
    spinlock_unlock_int(&th->lock);
}

sched_thread_t *sched_thread_self(void)
{
    sched_exec_unit_t *unit = NULL;
    cpu_t             *cpu  = NULL;
    sched_thread_t    *self = NULL;

    cpu = cpu_current_get();

    if(cpu == NULL)
        return(NULL);
    
    unit = cpu->sched;

    if(unit == NULL)
        return(NULL);

    spinlock_lock_int(&unit->lock);
    
    self = unit->current;

    spinlock_unlock_int(&unit->lock);

    return(self);
}

/*
 * sched_init - initialize scheduler structures
 */ 

int sched_init(void)
{
    /* Set up RW spinlock to protect execution unit list */
    spinlock_rw_init(&units_lock);
    spinlock_init(&pre_policy_lock);

    /* Initialize units list */
    linked_list_init(&units);
    linked_list_init(&pre_policy_queue);
    return(0);
}

/*
 * sched_cpu_init - initializes per CPU scheduler structures
 */

int sched_unit_init
(
    device_t *timer, 
    cpu_t *cpu
)
{
    sched_exec_unit_t *unit       = NULL;
    int                int_status = 0;
    list_node_t      *node = NULL;
    list_node_t      *next_node = NULL;
    sched_thread_t   *pend_th = NULL;
  
    if(cpu == NULL)
    {
        kprintf("ERROR: CPU %x\n", cpu);
        return(-1);
    }
    kprintf("================================================\n");
    kprintf("Initializing Scheduler for CPU %d\n",cpu->cpu_id);

    /* Allocate cleared memory for the unit */
    unit = kcalloc(sizeof(sched_exec_unit_t), 1);

    if(unit == NULL)
    {
        return(-1);
    }

    kprintf("UNIT %x\n",unit);

    /* assign scheduler unit to the cpu */
    cpu->sched = unit;

    /* tell the scheduler unit on which cpu it belongs */
    unit->cpu = cpu;

    /* Initialize the dead queue */
    linked_list_init(&unit->dead_q);

    /* Set up the spinlock for the unit */
    spinlock_init(&unit->lock);
    
    /* For now we have only one policy - a primitive one */
    sched_simple_register(&unit->policy);

    /* Initialize the policy */

    unit->policy->init_policy(unit);

    /* Initialize the idle thread
     * The scheduler will automatically start executing 
     * it in case there are no other threads in the ready_q
     */
    thread_create_static(&unit->idle, 
                         sched_idle_thread, 
                         unit, 
                         PAGE_SIZE, 
                         255);
    
    unit->idle.unit = unit;

    spinlock_lock_int(&pre_policy_lock);
    node = linked_list_first(&pre_policy_queue);

    while(node)
    {
        next_node = linked_list_next(node);
        
        pend_th = NODE_TO_THREAD(node);

        linked_list_remove(&pre_policy_queue, node);

        unit->policy->enqueue_new_thread(unit->policy_data, pend_th);

        node = next_node;
    }    

    spinlock_unlock_int(&pre_policy_lock);

    /* Add the unit to the list */
    spinlock_write_lock_int(&units_lock);

    linked_list_add_tail(&units, &unit->node);
    
    spinlock_write_unlock_int(&units_lock);

    /* Link the execution unit to the timer provided by
     * platform's CPU driver
     */
    if(timer != NULL)
    {
        unit->timer_dev = timer;
        timer_dev_connect_cb(timer, sched_update_time, unit);
        timer_dev_enable(timer); 
    }
    kprintf("%s UNLOCKED %d\n",__FUNCTION__, cpu_int_check());
    /* From now , we are entering the unit's idle routine */
    context_unit_start(&unit->idle);

    /* make the compiler happy  - we would never reach this*/
    return(0);

}

void sched_yield(void)
{
   schedule();
}

void sched_unblock_thread
(
    sched_thread_t *th
)
{
    spinlock_lock_int(&th->lock);

    /* clear the blocked flag */
    __atomic_and_fetch(&th->flags, ~THREAD_BLOCKED, __ATOMIC_SEQ_CST);
    
    /* mark thread as ready */
    __atomic_or_fetch(&th->flags, (THREAD_READY), __ATOMIC_SEQ_CST);

    /* signal the unit that there are threads that need to be unblocked */
    __atomic_or_fetch(&th->unit->flags, 
                      UNIT_THREADS_UNBLOCK, 
                      __ATOMIC_SEQ_CST);

    spinlock_unlock_int(&th->lock);
}

void sched_block_thread
(
    sched_thread_t *th
)
{
    spinlock_lock_int(&th->lock);

    /* set the blocked thread */
    __atomic_or_fetch(&th->flags, THREAD_BLOCKED, __ATOMIC_SEQ_CST);

    /* thread not running and not ready */
    __atomic_and_fetch(&th->flags, 
                        ~(THREAD_RUNNING | THREAD_READY), 
                        __ATOMIC_SEQ_CST);

    spinlock_unlock_int(&th->lock);
}

void sched_sleep_thread
(
    sched_thread_t *th,
    uint32_t timeout
)
{
    spinlock_lock_int(&th->lock);

    th->to_sleep = timeout;
    th->slept = 0;
    
    /* mark thread as sleeping */
    __atomic_or_fetch(&th->flags, THREAD_SLEEPING, __ATOMIC_SEQ_CST);

    /* thread not running and not ready */
    __atomic_and_fetch(&th->flags, 
                        ~(THREAD_RUNNING | THREAD_READY), 
                        __ATOMIC_SEQ_CST);

    spinlock_unlock_int(&th->lock);
}

void sched_wake_thread
(
    sched_thread_t *th
)
{
    spinlock_lock_int(&th->lock);

    th->slept = 0;
    th->to_sleep = 0;

    /* clear the sleeping flag */
    __atomic_and_fetch(&th->flags, ~THREAD_SLEEPING, __ATOMIC_SEQ_CST);

    /* thread is ready */
    __atomic_or_fetch(&th->flags, THREAD_READY, __ATOMIC_SEQ_CST);
    
    /* notify unit that there are threads to be awaken */
    __atomic_or_fetch(&th->unit->flags, 
                      UNIT_THREADS_WAKE, 
                      __ATOMIC_SEQ_CST);
    
    spinlock_unlock_int(&th->lock);

}

void sched_sleep
(
    uint32_t delay
)
{
    sched_thread_t *self = NULL;
    self = sched_thread_self();    
    sched_sleep_thread(self, delay);
    schedule();
}


/* time tracking - called from the interrupt context */
static uint32_t sched_update_time
(
    void *pv_unit,
    void *isr_inf
)
{
    sched_exec_unit_t *unit         = NULL;
    sched_policy_t    *policy       = NULL;
    sched_thread_t    *current       = NULL;
    unit = pv_unit;

    /* lock the unit */
    spinlock_lock_int(&unit->lock);

    policy = unit->policy;
    current = unit->current;
         
    /* Update the current thread that runs on the 
     * scheduler
     */    

    if(current != NULL)
    {
        if(current->remain > 1)
        {
            current->remain--;
        }
        else
        {
            __atomic_or_fetch(&current->flags, 
                              THREAD_NEED_RESCHEDULE, 
                              __ATOMIC_SEQ_CST);

            current->remain = 255 - current->prio;
        }
    }

    policy->update_time(unit->policy_data, unit->current);
    
    spinlock_unlock_int(&unit->lock);

    return(0);
    
}

/******************************************************************************/
extern  void cpu_signal_on(uint32_t id);
static void sched_idle_thread
(
    void *pv
)
{
    sched_exec_unit_t *unit       = NULL;
    sched_thread_t    *th         = NULL;
    int                int_status = 0;
    list_node_t        *c         = NULL;
    list_node_t        *n         = NULL;

    int timer_enabled = 1;
    unit = (sched_exec_unit_t*)pv;
   
    kprintf("Entered idle loop on %d\n", unit->cpu->cpu_id);

    /* Signal that the execution unit is up and running 
     * so that the BSP can continue waking up other threads
     */
    
    cpu_signal_on(unit->cpu->cpu_id);

    if(unit->cpu->cpu_id > 0)
        timer_dev_disable(unit->timer_dev);

    while(1)
    {

        cpu_halt();

#if 0
        /* Begin cleaning the dead threads */
        spinlock_lock_int(&unit->lock);    

        c = linked_list_first(&unit->dead_q);

        while(c)
        {
            kprintf("Cleaning %x\n", c);
            
            n = linked_list_next(c);

            th = (sched_thread_t*)c;

            linked_list_remove(&unit->dead_q, c);
            
           /* sched_clean_thread(th);*/
            
            c = n;
        }

        spinlock_unlock_int(&unit->lock);
#endif
    }

}

static void sched_context_switch
(
    sched_thread_t *prev,
    sched_thread_t *next
)
{
    context_switch(prev, next);
}

int sched_need_resched
(
    sched_exec_unit_t *unit
)
{
    sched_thread_t *th = NULL;
    int need_resched  = 1;

    if(unit->current)
    {
        th = unit->current;
        need_resched = (th->flags & THREAD_NEED_RESCHEDULE) == 
                        THREAD_NEED_RESCHEDULE;
    }
    
    return(need_resched);
}

void schedule(void)
{       
    int int_status = 0;

    /* save interrupt status as we might need to 
     * restore it later
     */
    int_status = cpu_int_check();

    schedule_main();

    if(!cpu_int_check() && int_status)
        cpu_int_unlock();
}

static void schedule_main(void)
{
    cpu_t             *cpu     = NULL;
    list_node_t       *new_th  = NULL;
    list_node_t       *next    = NULL;
    sched_exec_unit_t *unit    = NULL;
    sched_thread_t    *next_th = NULL;
    sched_thread_t    *prev_th = NULL;
    sched_policy_t    *policy  = NULL;
    int               status = 0;

    cpu    = cpu_current_get();
    unit   = cpu->sched;
    policy = unit->policy;

    /* Lock the execution unit */
    spinlock_lock_int(&unit->lock);
    
    if(unit->current != NULL)
        prev_th = unit->current;
    else
        prev_th = &unit->idle;

    /* Clear thread's running flag */
    __atomic_and_fetch(&prev_th->flags, 
                        ~THREAD_RUNNING, 
                        __ATOMIC_SEQ_CST);

    if(unit->current != NULL)
    {
        policy->put_thread(unit->policy_data, prev_th);    
    }


    status = policy->next_thread(unit->policy_data, 
                                 &next_th);

    /* If we don't have a task to execute, go to the idle task */
    if(status != 0)
    {
        next_th = &unit->idle;
        unit->current = NULL;
    }
    else
    {
        next_th->unit = unit;
        unit->current = next_th;
    }

    /* Switch context */
    sched_context_switch(prev_th, next_th);

    /* Unlock unit */
    spinlock_unlock_int(&unit->lock);
}
