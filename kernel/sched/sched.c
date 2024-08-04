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
#define SCHED_IDLE_THREAD_STACK_SIZE    (PAGE_SIZE)


static list_head_t units        = LINKED_LIST_INIT;
static spinlock_t  units_lock   = SPINLOCK_INIT;
static spinlock_t  threads_lock = SPINLOCK_INIT;
static list_head_t threads      = LINKED_LIST_INIT;

static void *sched_idle_thread
(
    void *pv
);

static void sched_main
(
    void
);

static uint32_t sched_tick
(
    void              *pv_unit,
    const time_spec_t *step,
    const void        *isr_inf
);

static void sched_context_switch
(
    sched_thread_t *prev,
    sched_thread_t *next
);

static uint32_t sched_timer_wake_thread
(
    void *thread,
    const void *isr
);

int basic_register
(
    sched_policy_t *policy
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
    kprintf("Thread ended\n");
    /* The thread is now dead */
    spinlock_lock_int(&th->lock, &int_flag);

     if(~th->flags & THREAD_DEAD)
     {
         sched_thread_mark_dead(th);
         th->rval = ret_val;
     }
    
    spinlock_unlock_int(&th->lock, int_flag);

    while(1)
    {
        cpu_pause();
    }
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
    unit->policy.enqueue(unit, th);

    /* Unlock everything */
    spinlock_unlock_int(&unit->lock, int_flag);
    spinlock_unlock(&th->lock);
}

sched_thread_t *sched_thread_self
(
    void
)
{
    sched_exec_unit_t *unit = NULL;
    cpu_t             *cpu  = NULL;
    sched_thread_t    *self = NULL;
    uint8_t           iflag = 0;

    iflag = cpu_int_check();

    if(iflag)
    {
        /* lock the interrupts to prevent rescheduling */
        cpu_int_lock();
    }
    
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
        th->flags |= THREAD_DEAD;
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
    device_t       *timer, 
    cpu_t          *cpu,
    sched_thread_t *post_unit_init
)
{
    sched_exec_unit_t *unit        = NULL;
    list_node_t       *node        = NULL;
    list_node_t       *next_node   = NULL;
    sched_thread_t    *pend_th     = NULL;
    uint8_t           int_flag     = 0;
    uint8_t           use_tick_ipi = 0;
    timer_api_t       *funcs       = NULL;


    kprintf("================================================\n");
    kprintf("Initializing Scheduler for CPU %d Interrupts %d\n",
            cpu->cpu_id, cpu_int_check());

    if(cpu == NULL)
    {
        kprintf("ERROR: CPU %x\n", cpu);
        return(-1);
    }
    
    if(timer == NULL)
    {
        use_tick_ipi = 1;
        kprintf("NO TIMER - unit cannot tick by itself\n");
    }


    funcs = devmgr_dev_api_get(timer);

    if(funcs == NULL || funcs->set_handler == NULL)
    {
        use_tick_ipi = 1;
        kprintf("no way to set the tick handler - no ticking by ourselves\n");
    }

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
    
    basic_register(&unit->policy);

    /* initialize instance of policy */
    unit->policy.init(unit);
    

    /* Initialize the idle thread
     * as we will switch to it when we finish 
     * intializing the scheduler unit
     */ 
    kthread_create_static(&unit->idle, 
                         sched_idle_thread, 
                         unit, 
                         SCHED_IDLE_THREAD_STACK_SIZE, 
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
    

    if(use_tick_ipi == 0)
    {
        unit->timer_dev = timer;
        funcs->set_handler(timer, sched_tick, unit);
        funcs->enable(timer);
    }
    else
    {
        kprintf("No timer - will rely on resched IPIs only\n");
    }

    unit->flags |= UNIT_START;

    /* Enter the scheduler's code */
    schedule();
    
    /* make the compiler happy  - we would never reach this*/
    return(0);

}

void sched_yield(void)
{
   schedule();
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

        /* clear the sleeping flag */
        th->flags &= ~THREAD_SLEEPING;

        /* thread is ready */
        th->flags |= THREAD_READY;


        /* notify unit that there are threads to be awaken */
       th->unit->flags |= UNIT_THREADS_WAKE;
        
        spinlock_unlock_int(&th->lock, int_flag);
    }
}


static uint32_t sched_timer_wake_thread
(
    void *thread,
    const void *isr
)
{
    sched_thread_t *th = thread;
    
    th->flags |= THREAD_WOKE_BY_TIMER;
    
    sched_wake_thread(thread);
    return(0);
}

void sched_sleep
(
    uint32_t delay
)
{
    uint8_t int_status = 0;
    timer_t tm;
    time_spec_t timeout ={.seconds = delay / 1000ull,
                          .nanosec = delay % 1000000ull
                         };
                         
    sched_thread_t *self = NULL;
    
    if(delay == 0)
    {
        kprintf("NO DELAY\n");
        return;
    }
    
    self = sched_thread_self();

    spinlock_lock_int(&self->lock, &int_status);

    /* mark thread as sleeping if we have any delay specified*/
    if(delay != 0)
    {
        self->flags |= THREAD_SLEEPING;

        /* thread not running and not ready */
        self->flags &= ~(THREAD_RUNNING | THREAD_READY);
    }

    spinlock_unlock_int(&self->lock, int_status);

    /* don't bother to add the thread to the timer if delay is WAIT_FOREVER */
    if((delay != WAIT_FOREVER) && (delay != 0))
    {
        timer_enqeue_static(NULL, 
                            &timeout, 
                            sched_timer_wake_thread, 
                            self, 
                            &tm);
    }

    /* ask the scheduler to put the thread to sleep */
    schedule();
    
    /* If the delay is wait forever, then we did not push anything so
     * don't look for a timer because there isn't one
     */
    if((delay != WAIT_FOREVER) && (delay != 0))
    {
        /* if we were woken up earlier, delete the timer from the timer queue*/
        if(~self->flags & THREAD_WOKE_BY_TIMER)
        {
            if(timer_dequeue(NULL, &tm))
            {
                kprintf("NOTHING TO DEQUEUE\n");
            }
        }   
        
        /* clear the woke by timer flag */
        self->flags &= ~THREAD_WOKE_BY_TIMER; 
    }
}


/* time tracking - called from the interrupt context */
static uint32_t sched_tick
(
    void              *pv_unit,
    const time_spec_t *step,
    const void        *isr_inf
)
{
    sched_exec_unit_t *unit         = NULL;
    sched_policy_t    *policy       = NULL;
    list_node_t       *node         = NULL;
    sched_thread_t    *th           = NULL;
    uint32_t          next_tick     = UINT32_MAX;

    unit = pv_unit;

    /* lock the unit */
    spinlock_lock(&unit->lock);
    
    th = unit->current;

    if(unit->policy.tick != NULL)
    {
        unit->policy.tick(unit);
    }

    /* reset the thread cpu quota*/
    if(th != NULL)
    {
        if(th->cpu_left == 0)
        {
            th->flags |= THREAD_NEED_RESCHEDULE;
            th->cpu_left = th->prio;
        }
    }


    /* all good, unlock the unit */
    spinlock_unlock(&unit->lock);

    return(0);
    
}

void sched_thread_set_priority
(
    sched_thread_t *th, 
    uint16_t prio
)
{

}

/******************************************************************************/
static void *sched_idle_thread
(
    void *pv
)
{
    sched_exec_unit_t *unit       = NULL;
    sched_thread_t    *th         = NULL;
    int                int_status = 0;
    list_node_t        *c         = NULL;
    list_node_t        *n         = NULL;
    timer_api_t        *func      = NULL;
    unit = (sched_exec_unit_t*)pv;

    kprintf("Entered idle loop on %d UNIT ID 0x%x\n", unit->cpu->cpu_id, unit);

    /* Signal that the execution unit is up and running 
     * so that the BSP can continue waking up other cores
     */

    cpu_signal_on(unit->cpu->cpu_id);
#if 1
    /* When entering the idle loop, disable the timer
     * for the AP CPU as currently it does not have anything
     * to handle
     */ 

    if(unit->cpu->cpu_id > 0)
    {
        func = devmgr_dev_api_get(unit->timer_dev);

        if((func != NULL) && (func->disable != NULL))
        {
            func->disable(unit->timer_dev);
        }
    }
#endif

    while(1)
    {
        cpu_halt();
    }

    return(NULL);
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
    return(0);
}

void schedule(void)
{       
    int int_status = 0;

    /* save interrupt status as we might need to 
     * restore it later
     */
    int_status = cpu_int_check();

    sched_main();

    if(!cpu_int_check() && int_status)
    {
        cpu_int_unlock();
    }
}

/* enqueue a thread */

static void sched_enq
(
    sched_exec_unit_t *unit,
    sched_thread_t **prev_th
)
{

    uint32_t state = 0;
    sched_thread_t *prev_thread = NULL;

    if(unit->current == NULL)
    {
        /* If the flag UNIT_START is set, then we do not
         * have a previous thread so we will just clear the flag.
         * Trying to set the prev_thread to the idle thread when the UNIT_START
         * is in place will have disastreous results as the context of the idle
         * task will be overwirtten right from the start with wrong values
         */
        if(unit->flags & UNIT_START)
        {
            unit->flags &= ~UNIT_START;
        }
        else
        {
            prev_thread = &unit->idle;
        }
    }
    else
    {
        prev_thread = unit->current;
    }

    if(prev_thread)
    {
        /* Clear thread's running flag */
        prev_thread->flags &= ~(THREAD_RUNNING | THREAD_NEED_RESCHEDULE);
    }

    if(unit->current)
    {
        prev_thread = unit->current;
    
        /* check if the thread is dead */
        if(prev_thread->flags & THREAD_DEAD)
        {
            linked_list_add_tail(&unit->dead_q, &prev_thread->sched_node);
            prev_thread = NULL;
        }
        else if(unit->policy.enqueue != NULL)
        {
            if(prev_th)
            {
                unit->policy.enqueue(unit, unit->current);
            }
        }
        else
        {
            kprintf("no enqueue function\n");
        }
    }

    *prev_th = prev_thread;
}

/* Dequeue a thread */

static void sched_deq
(
    sched_exec_unit_t *unit,
    sched_thread_t   **next_th
)
{
    int deq_status = 0;
    sched_thread_t *next_thread = NULL;

    deq_status = unit->policy.dequeue(unit, &next_thread);

    if(deq_status < 0)
    {
        unit->current = NULL;
        next_thread = &unit->idle;
    }
    else
    {
        unit->current = next_thread;
    }
       /* Set thread's running flag */
    next_thread->flags |= THREAD_RUNNING;

    *next_th = next_thread;
}

static void sched_main(void)
{
    cpu_t             *cpu     = NULL;
    sched_exec_unit_t *unit    = NULL;
    sched_thread_t    *next_th = NULL;
    sched_thread_t    *prev_th = NULL;
    sched_policy_t    *policy  = NULL;
    int               status = 0;
    int               balance_sts = -1;
    uint8_t           int_flag = 0;

    cpu    = cpu_current_get();
    unit   = cpu->sched;
    
    if(~unit->flags & UNIT_NO_PREEMPT)
    {

        /* Lock the execution unit */
        spinlock_lock_int(&unit->lock, &int_flag);

        /* Add the thread to the policy queue */

        sched_enq(unit, &prev_th);

        /* Ask the policy for the next thread */
        sched_deq(unit, &next_th);

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
}


void sched_disable_preempt(void)
{
    uint8_t int_status = 0;
    sched_exec_unit_t *unit = NULL;
    cpu_t *cpu = NULL;

    int_status = cpu_int_check();

    if(int_status)
    {
        cpu_int_lock();
    }

    cpu = cpu_current_get();

    if(cpu != NULL)
    {
        unit = cpu->sched;
    }

    if(unit != NULL)
    {
        unit->flags |= UNIT_NO_PREEMPT;
    }
    
    if(int_status)
    {
        cpu_int_unlock();
    }
}

void sched_enable_preempt(void)
{
    uint8_t int_status = 0;
    sched_exec_unit_t *unit = NULL;
    cpu_t *cpu = NULL;

    int_status = cpu_int_check();

    if(int_status)
    {
        cpu_int_lock();
    }
    cpu = cpu_current_get();

    if(cpu != NULL)
    {
        unit = cpu->sched;
    }

    if(unit != NULL)
    {
        unit->flags &= ~UNIT_NO_PREEMPT;
    }
    
    if(int_status)
    {
        cpu_int_unlock();
    }
}