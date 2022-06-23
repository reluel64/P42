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
#include <owner.h>

#define THREAD_NOT_RUNNABLE(x) (((x) & (THREAD_SLEEPING | THREAD_BLOCKED)))
#define SCHEDULER_TICK_MS 1

static list_head_t units;
static spinlock_t  units_lock;

static void sched_idle_thread
(
    void *pv
);

static void schedule_main
(
    void
);

static uint32_t sched_tick
(
    void *unit,
    void *isr_info
);

static int sched_dequeue_thread
(
    sched_exec_unit_t *unit,
    sched_thread_t   **next
);

static int sched_enqueue_thread
(
    sched_exec_unit_t *unit,
    sched_thread_t    *th
);

static void sched_context_switch
(
    sched_thread_t *prev,
    sched_thread_t *next
);

void sched_thread_entry_point
(
    sched_thread_t *th
)
{
    int             int_status   = 0;
    th_entry_point_t entry_point = NULL;
    uint8_t int_flag = 0;
    void *ret_val = NULL;

    entry_point = (th_entry_point_t)th->entry_point;
   
    /* during startup of the thread, unlock the scheduling unit 
     * on which we are running.
     * failing to do so will cause one thread to run on the locked unit
     * and will cause the unit to be unable to switch between threads
     */
    spinlock_unlock_int(&th->unit->lock, int_flag);
    cpu_int_unlock();

    if(entry_point != NULL)
    {
        ret_val = entry_point(th->arg);
    }

    /* The thread is now dead */
    spinlock_lock_int(&th->lock, &int_flag);

     if(~th->flags & THREAD_DEAD)
     {
         sched_thread_mark_dead(th);
         th->rval = ret_val;
     }
    
    spinlock_unlock_int(&th->lock, int_flag);
}

int sched_start_thread
(
    sched_thread_t *th
)
{
    cpu_t *cpu = NULL;
    sched_exec_unit_t *unit = NULL;
    uint8_t int_flag = 0;
    cpu = cpu_current_get();

    unit = cpu->sched;
    
    /* Lock everything */
    spinlock_lock(&th->lock);
    spinlock_lock_int(&unit->lock, &int_flag);

    /* this thread belongs to this unit */
    th->unit = unit;

    /* enqueue the thread */ 
    sched_enqueue_thread(unit, th);

    /* Unlock everything */
    spinlock_unlock_int(&unit->lock, int_flag);
    spinlock_unlock(&th->lock);
}

sched_thread_t *sched_thread_self(void)
{
    sched_exec_unit_t *unit = NULL;
    cpu_t             *cpu  = NULL;
    sched_thread_t    *self = NULL;
    uint8_t           iflag = 0;

    iflag = cpu_int_check();

    /* lock the interrupts to prevent rescheduling */
    cpu_int_lock();

    cpu = cpu_current_get();

    if(cpu == NULL)
    {
        if(iflag)
        {
            cpu_int_unlock();
        }
        return(NULL);
    }
    
    unit = cpu->sched;

    if(unit == NULL)
    {
        if(iflag)
        {
            cpu_int_unlock();
        }

        return(NULL);
    }

    /* lock the unit to extract the thread */
    spinlock_lock(&unit->lock);
    
    self = unit->current;

    /* unlock the unit */
    spinlock_unlock(&unit->lock);

    /* enable interrupts */
    if(iflag)
    {
        cpu_int_unlock();
    }
    
    return(self);
}

void sched_thread_mark_dead
(
    sched_thread_t *th
)
{
    if(th != NULL)
    {
        __atomic_or_fetch(&th->flags, THREAD_DEAD, __ATOMIC_ACQUIRE);
    }
}

void sched_thread_exit
(
    void *exit_val
)
{
    sched_thread_t *self = NULL;

    self = sched_thread_self();

    if(self != NULL)
    {
       if(~self->flags & THREAD_DEAD)
       {
           sched_thread_mark_dead(self);

           self->rval = exit_val;

           /* we're dead so we can release the cpu */
           sched_yield();
       }
    }
}

/*
 * sched_init - initialize scheduler structures
 */ 

int sched_init(void)
{
    /* Set up RW spinlock to protect execution unit list */
    spinlock_rw_init(&units_lock);

    /* Initialize units list */
    linked_list_init(&units);
    
    /* intialize the owner */

    owner_kernel_init();

    return(0);
}

/*
 * sched_cpu_init - initializes per CPU scheduler structures
 */

int sched_unit_init
(
    device_t *timer, 
    cpu_t *cpu,
    sched_thread_t *post_unit_init
)
{
    sched_exec_unit_t *unit       = NULL;
    list_node_t      *node = NULL;
    list_node_t      *next_node   = NULL;
    sched_thread_t   *pend_th     = NULL;
    uint8_t          int_flag     = 0;

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

    /* assign scheduler unit to the cpu */
    cpu->sched = unit;

    /* tell the scheduler unit on which cpu it belongs */
    unit->cpu = cpu;

    /* Set up the spinlock for the unit */
    spinlock_init(&unit->lock);
    
    /* initialize queues */
    linked_list_init(&unit->ready_q);
    linked_list_init(&unit->sleep_q);
    linked_list_init(&unit->blocked_q);

    /* Initialize the idle thread
     * as we will switch to it when we finish 
     * intializing the scheduler unit
     */ 
    kthread_create_static(&unit->idle, 
                         sched_idle_thread, 
                         unit, 
                         0x1000, 
                         255,
                         NULL);
    
    unit->idle.unit = unit;

    /* Add the unit to the list */
    spinlock_write_lock_int(&units_lock, &int_flag);

    linked_list_add_tail(&units, &unit->node);
    
    spinlock_write_unlock_int(&units_lock, int_flag);

    /* check if we have a thread to start at the end of the initalization */
    if(post_unit_init != NULL)
    {
        sched_start_thread(post_unit_init);
    }

    /* Link the execution unit to the timer provided by
     * platform's CPU driver
     */
    if(timer != NULL)
    {
        unit->timer_dev = timer;
        timer_dev_connect_cb(timer, sched_tick, unit);
        timer_dev_enable(timer); 
    }
    else
    {
        kprintf("No timer - will rely on resched IPIs only\n");
    }

    /* From now, we are entering the unit's idle routine */
    sched_context_switch(NULL, &unit->idle);

    /* make the compiler happy  - we would never reach this*/
    return(0);

}

void sched_yield(void)
{
   schedule();
}

/* sched_unblock_thread - unblock thread */

void sched_unblock_thread
(
    sched_thread_t *th
)
{
    uint8_t int_flag = 0;

    spinlock_lock_int(&th->lock, &int_flag);

    /* clear the blocked flag */
    __atomic_and_fetch(&th->flags, ~THREAD_BLOCKED, __ATOMIC_SEQ_CST);
    
    /* mark thread as ready */
    __atomic_or_fetch(&th->flags, (THREAD_READY), __ATOMIC_SEQ_CST);

    /* signal the unit that there are threads that need to be unblocked */
    __atomic_or_fetch(&th->unit->flags, 
                      UNIT_THREADS_UNBLOCK, 
                      __ATOMIC_SEQ_CST);

    spinlock_unlock_int(&th->lock, int_flag);
}

/* sched_block_thread -  blocks a thread */

void sched_block_thread
(
    sched_thread_t *th
)
{
    uint8_t int_flag = 0;

    if(th != NULL)
    {
        spinlock_lock_int(&th->lock, &int_flag);

        /* set the blocked thread */
        __atomic_or_fetch(&th->flags, THREAD_BLOCKED, __ATOMIC_SEQ_CST);

        /* thread not running and not ready */
        __atomic_and_fetch(&th->flags, 
                            ~(THREAD_RUNNING | THREAD_READY), 
                            __ATOMIC_SEQ_CST);

        spinlock_unlock_int(&th->lock, int_flag);
    }
}

void sched_sleep_thread
(
    sched_thread_t *th,
    uint32_t timeout
)
{
    uint8_t int_flag = 0;

    if(th != NULL)
    {
        if(timeout > 0)
        {
            spinlock_lock_int(&th->lock, &int_flag);

            th->to_sleep = timeout;
            th->slept = 0;
            
            /* mark thread as sleeping */
            __atomic_or_fetch(&th->flags, THREAD_SLEEPING, __ATOMIC_SEQ_CST);

            /* thread not running and not ready */
            __atomic_and_fetch(&th->flags, 
                                ~(THREAD_RUNNING | THREAD_READY), 
                                __ATOMIC_SEQ_CST);

            spinlock_unlock_int(&th->lock, int_flag);
        }
    }
}

void sched_wake_thread
(
    sched_thread_t *th
)
{
    uint8_t int_flag = 0;

    if(th != NULL)
    {
        spinlock_lock_int(&th->lock, &int_flag);

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
        
        spinlock_unlock_int(&th->lock, int_flag);
    }
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
static uint32_t sched_tick
(
    void *pv_unit,
    void *isr_inf
)
{
    sched_exec_unit_t *unit         = NULL;
    sched_policy_t    *policy       = NULL;
    uint8_t           int_flag      = 0;
    list_node_t       *node         = NULL;
    sched_thread_t    *th           = NULL;

    unit = pv_unit;

    /* lock the unit */
    spinlock_lock_int(&unit->lock, &int_flag);
    
    th = unit->current;

    /* Update the current thread */
    if(th != NULL)
    {
        if(th->remain > 1)
        {
            th->remain--;
        }
        else
        {
            __atomic_or_fetch(&th->flags, 
                              THREAD_NEED_RESCHEDULE, 
                              __ATOMIC_SEQ_CST);

            th->remain = 255 - th->prio;
        }
    }

    /* Update sleeping threads */
    node = linked_list_first(&unit->sleep_q);
    
    while(node)
    {
        
        th = (sched_thread_t*)node;

        /* If timeout has been reached, wake the thread */

        if(__atomic_load_n(&th->flags, __ATOMIC_SEQ_CST) & 
          THREAD_SLEEPING)
        {
            th->slept++;

            if(th->slept >= th->to_sleep)
            {            
                /* Wake up the thread */
                sched_wake_thread(th);
            }
        }
        node = linked_list_next(node);
    }    
    spinlock_unlock_int(&unit->lock, int_flag);

    return(0);
    
}

/******************************************************************************/
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

    unit = (sched_exec_unit_t*)pv;
   
    kprintf("Entered idle loop on %d UNIT ID 0x%x\n", unit->cpu->cpu_id, unit);

    /* Signal that the execution unit is up and running 
     * so that the BSP can continue waking up other cores
     */

    cpu_signal_on(unit->cpu->cpu_id);
#if 0
    /* When entering the idle loop, disable the timer
     * for the AP CPU as currently it does not have anything
     * to handle
     */ 

    if(unit->cpu->cpu_id > 0)
        timer_dev_disable(unit->timer_dev);
#endif

    while(1)
    {
    
        cpu_halt();
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
    
    /* if we have a thread, check if we have to do rescheduling */
    if(unit->current)
    {
        th = unit->current;
        need_resched = (th->flags & THREAD_NEED_RESCHEDULE) == 
                                    THREAD_NEED_RESCHEDULE;
    }
    
    return(need_resched);
}

static int scheduler_balance
(
    sched_exec_unit_t *unit
)
{
#if 0
    uint8_t int_flag = 0;
    sched_policy_t *policy = NULL;
    int expected = 0;

    /* lock the unit */
    spinlock_lock_int(&unit->lock, &int_flag);

    if(__atomic_compare_exchange_n(&in_balance, &expected, 1, 
                                   0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
   {

        policy = unit->policy;

        if(policy->load_balancing != NULL)
        {
            policy->load_balancing(unit->policy_data, &units);
        }

        __atomic_clear(&in_balance, __ATOMIC_SEQ_CST);
   }


   spinlock_unlock_int(&unit->lock, int_flag);
#endif
    return(0);
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
    {
        cpu_int_unlock();
    }
}

static int sched_dequeue_thread
(
    sched_exec_unit_t *unit,
    sched_thread_t   **next
)
{
    list_node_t *th        = NULL;
    list_node_t *next_th   = NULL;
    sched_thread_t *thread = NULL;

    /* Check blocked threads */
    th = linked_list_first(&unit->blocked_q);

    if(__atomic_fetch_and(&unit->flags, 
                          ~UNIT_THREADS_UNBLOCK, 
                         __ATOMIC_SEQ_CST) & 
                         UNIT_THREADS_UNBLOCK)
    {
        while(th)
        {
            next_th = linked_list_next(th);

            thread = NODE_TO_THREAD(th);

            if(!(__atomic_load_n(&thread->flags, __ATOMIC_SEQ_CST) & 
                THREAD_BLOCKED))
            {
                linked_list_remove(&unit->blocked_q, th);
                linked_list_add_tail(&unit->ready_q, th);
            }

            th = next_th;
        }
    }
    
    /* Check sleeping threads */

    if(__atomic_fetch_and(&unit->flags, 
                          ~UNIT_THREADS_WAKE,
                          __ATOMIC_SEQ_CST) & 
                          UNIT_THREADS_WAKE)
    {
        
        th = linked_list_first(&unit->sleep_q);

        while(th)
        {
            next_th = linked_list_next(th);

            thread = NODE_TO_THREAD(th);

            if(!(__atomic_load_n(&thread->flags, __ATOMIC_SEQ_CST) & 
                THREAD_SLEEPING))
            {
                linked_list_remove(&unit->sleep_q, th);
                linked_list_add_tail(&unit->ready_q, th);
            }

            th = next_th;
        }
    }

    th = linked_list_first(&unit->ready_q);

    if(th == NULL)
    {
        return(-1);
    }
    
    linked_list_remove(&unit->ready_q, th);

    thread = NODE_TO_THREAD(th);

    /* Mark the next thread as running */
    __atomic_or_fetch(&thread->flags, 
                      THREAD_RUNNING, 
                      __ATOMIC_SEQ_CST);

    *next = thread;

    return(0);

}

static int sched_enqueue_thread
(
    sched_exec_unit_t *unit,
    sched_thread_t    *th
)
{

    uint32_t state = 0;
    list_head_t *lh = NULL;

    if(th == NULL)
    {
        return(-1);
    }
    
    __atomic_and_fetch(&th->flags, ~THREAD_NEED_RESCHEDULE, __ATOMIC_SEQ_CST);

   state = __atomic_load_n(&th->flags, __ATOMIC_SEQ_CST) & THREAD_STATE_MASK;

    switch(state)
    {
        case THREAD_BLOCKED:
            lh = &unit->blocked_q;
            break;

        case THREAD_SLEEPING:
            lh = &unit->sleep_q;
            break;

        case THREAD_READY:
            lh = &unit->ready_q;
            break;

        default:
            kprintf("%s %d\n",__FUNCTION__, __LINE__);
            while(1);
            break;
    }

    /* Add the thread in the corresponding queue */

    linked_list_add_tail(lh, &th->sched_node);

    return(0);
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
    int               balance_sts = -1;
    uint8_t           int_flag = 0;


    cpu    = cpu_current_get();
    unit   = cpu->sched;
    //policy = unit->policy;
   
    /* Lock the execution unit */
    spinlock_lock_int(&unit->lock, &int_flag);
    
    if(unit->current != NULL)
        prev_th = unit->current;
    else
        prev_th = &unit->idle;

    
    /* Clear thread's running flag */
    __atomic_and_fetch(&prev_th->flags, 
                        ~THREAD_RUNNING, 
                        __ATOMIC_SEQ_CST);

    /* If we are not coming from the idle task, tell the policy
     * to handle the thread
     */
    if(unit->current != NULL)
    {       
        sched_enqueue_thread(unit, prev_th);
    }

    /* Ask the policy for the next thread */
    status = sched_dequeue_thread(unit, 
                                    &next_th);


    /* If we don't have a task to execute, go to the idle task */

    if(status < 0)
    {
        next_th = &unit->idle;
        unit->current = NULL;
    }
    else
    {
        unit->current = next_th;
    }

    /* Set thread's running flag */
    __atomic_or_fetch(&next_th->flags, 
                      THREAD_RUNNING, 
                      __ATOMIC_SEQ_CST);

    /* Switch context */
    sched_context_switch(prev_th, next_th);
    
    /* update the unit */
    cpu    = cpu_current_get();
    unit   = cpu->sched;
    next_th->unit = unit;

    /* Unlock unit */
    spinlock_unlock_int(&unit->lock, int_flag);

    /* try to do some balancing work */
    scheduler_balance(unit);
}
