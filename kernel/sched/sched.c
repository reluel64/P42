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
#include <sched.h>
#include <context.h>
#include <thread.h>
#include <isr.h>
#include <platform.h>
#include <owner.h>

#define THREAD_NOT_RUNNABLE(x) (((x) & (THREAD_SLEEPING | THREAD_BLOCKED)))
#define SCHED_IDLE_THREAD_STACK_SIZE    (PAGE_SIZE)

static list_head_t    threads       = LINKED_LIST_INIT;
static list_head_t    units         = LINKED_LIST_INIT;
static list_head_t    policies      = LINKED_LIST_INIT;
static spinlock_rw_t  units_lock    = SPINLOCK_RW_INIT;
static spinlock_rw_t  threads_lock  = SPINLOCK_RW_INIT;
static spinlock_rw_t  policies_lock = SPINLOCK_RW_INIT;

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

static int32_t sched_select_thread
(
    sched_exec_unit_t *unit,
    sched_thread_t *th
);

static int32_t sched_put_prev
(
    sched_exec_unit_t *unit,
    sched_thread_t *th
);

static int32_t sched_enq
(
    sched_exec_unit_t *unit,
    sched_thread_t *th
);

static int32_t sched_deq
(
    sched_exec_unit_t *unit,
    sched_thread_t    *th
);


int basic_register
(
    void
);

int idle_task_register
(
    void
);

sched_policy_t *sched_get_policy_by_id
(
    sched_policy_id_t id
);

void sched_thread_entry_point
(
    sched_thread_t *th
)
{
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

int sched_create_thread
(
    sched_thread_t *th
)
{
    uint8_t int_flag = 0;

    /* add thread to the global list */
    spinlock_write_lock_int(&threads_lock, &int_flag);
    linked_list_add_tail(&threads, &th->system_node);
    spinlock_write_unlock_int(&threads_lock, int_flag);

    return(0);
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
    
    sched_create_thread(th);

    th->policy = sched_get_policy_by_id(th != &unit->idle ? sched_basic_policy : 
                                                            sched_idle_task_policy);

    /* Lock everything */
    spinlock_lock(&th->lock);
    spinlock_lock_int(&unit->lock, &int_flag);

    /* this thread belongs to this unit */
    th->unit = unit;
    linked_list_add_head(&unit->unit_threads, &th->unit_node);

    /* enqueue the thread to the policy so it will get executed */
    sched_enq(unit, th);

    /* Unlock everything */
    spinlock_unlock_int(&unit->lock, int_flag);
    spinlock_unlock(&th->lock);

    return(0);
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
    
    basic_register();

    idle_task_register();
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
    uint8_t           int_flag     = 0;
    uint8_t           use_tick_ipi = 0;
    timer_api_t       *funcs       = NULL;
    sched_policy_t    *policy      = NULL;


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

    /* Do per policy initalization */
    spinlock_read_lock_int(&policies_lock, &int_flag);

    node = linked_list_first(&policies);
 
    while(node != NULL)
    {
        policy = (sched_policy_t*)node;
    
        policy->unit_init(unit);

        node = linked_list_next(node);    
    }

    spinlock_read_unlock_int(&policies_lock, int_flag);

   

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
    unit->idle.policy = sched_get_policy_by_id(sched_idle_task_policy);
    unit->idle.flags |= THREAD_READY;

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

        /* put the thread back to the policy */
        sched_enq(th->unit, th);
        
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

        sched_deq(self->unit, self);
    }

    /* don't bother to add the thread to the timer if delay is WAIT_FOREVER */
    if((delay != WAIT_FOREVER) && (delay != 0))
    {
        timer_enqeue_static(NULL, 
                            &timeout, 
                            sched_timer_wake_thread, 
                            self, 
                            &tm);
    }

    spinlock_unlock_int(&self->lock, int_status);
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
    sched_thread_t    *th           = NULL;
    uint8_t resched = 0;
    unit = pv_unit;

    /* lock the unit */
    spinlock_lock(&unit->lock);

    th = unit->current;

    if(th != NULL)
    {
        if(th->policy != NULL)
        {
           
            if(th->policy->tick != NULL)
            {
        
                resched = (th->policy->tick(unit) == 0);
            }
        }

        if(resched)
        {
            th->flags |= THREAD_NEED_RESCHEDULE;
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


static int32_t sched_select_thread
(
    sched_exec_unit_t *unit,
    sched_thread_t *th
)
{
    int32_t result = -1;
    if(th->policy != NULL)
    {
        result = th->policy->select_thread(unit, th);
    }

    return(result);
}


static int32_t sched_put_prev
(
    sched_exec_unit_t *unit,
    sched_thread_t *th
)
{
    if(th->flags & THREAD_READY)
    {
        th->policy->put_prev(unit, th);
    }

    return(0);
}

/* enqueue a thread on the policy */

static int32_t sched_enq
(
    sched_exec_unit_t *unit,
    sched_thread_t *th
)
{
    if(th->policy != NULL)
    {
        
        th->policy->enqueue(unit, th);
    }
    return(0);
}

/* Dequeue a thread from the policy */

static int32_t sched_deq
(
    sched_exec_unit_t *unit,
    sched_thread_t    *th
)
{
    th->policy->dequeue(unit, th);
    return(0);
}

static int32_t sched_pick_next
(
    sched_exec_unit_t *unit,
    sched_thread_t **th
)
{
    sched_thread_t *next = NULL;
    list_node_t *pn = NULL;
    sched_policy_t *policy = NULL;
    uint8_t int_sts = 0;
    int32_t status = -1;

    spinlock_read_lock_int(&policies_lock, &int_sts);

    pn = linked_list_first(&policies);

    while(pn != NULL)
    {

        policy = (sched_policy_t*)pn;

        if(policy->pick_next(unit, &next) == 0)
        {
            status = 0;
            break;
        }

        pn = linked_list_next(pn);
    }

    spinlock_read_unlock_int(&policies_lock, int_sts);

    if(status == 0)
    {
        *th = next;
    }
    else
    {
        kprintf("no next thread\n");
        *th = NULL;
    }
    

    return(status);
}

static int32_t sched_next_thread
(
    sched_exec_unit_t *unit,
    sched_thread_t *prev,
    sched_thread_t **next
)
{
    int32_t status = -1;

    if(prev != NULL)
    {
        sched_put_prev(unit, prev);
    }

    /* pick the next thread */
    status = sched_pick_next(unit, next);

    if(status == 0)
    {
        /* we have a thread picked so we tell the policy that we select it */
        sched_select_thread(unit, *next);
    }

    return(status); 
}

static void sched_main(void)
{
    cpu_t             *cpu     = NULL;
    sched_exec_unit_t *unit    = NULL;
    sched_thread_t    *next_th = NULL;
    sched_thread_t    *prev_th = NULL;
    uint8_t           int_flag = 0;

    cpu    = cpu_current_get();
    unit   = cpu->sched;
    
    if(~unit->flags & UNIT_NO_PREEMPT)
    {
        /* Lock the execution unit */
        spinlock_lock_int(&unit->lock, &int_flag);

        prev_th = unit->current;

        sched_next_thread(unit, prev_th, &next_th);
    
        unit->current = next_th;

        /* Switch context */
        sched_context_switch(prev_th, next_th);

        /* Unlock unit */
        spinlock_unlock_int(&unit->lock, int_flag);
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

int32_t sched_policy_register
(
    sched_policy_t *p
)
{
    uint8_t int_status = 0;
    int32_t node_found = 0;
    int32_t ret = -1;

    spinlock_write_lock_int(&policies_lock, &int_status);

    node_found = linked_list_find_node(&policies, &p->node);

    if(node_found == -1)
    {
        linked_list_add_tail(&policies, &p->node);
        ret = 0;
    }

    spinlock_write_unlock_int(&policies_lock, int_status);

    return(ret);

}

sched_policy_t *sched_get_policy_by_id
(
    sched_policy_id_t id
)
{
    list_node_t *ln = NULL;
    sched_policy_t *policy = NULL;

    ln = linked_list_first(&policies);

    while(ln != NULL)
    {
        policy = (sched_policy_t*)ln;

        if(policy->id == id)
        {
            break;
        }
        else
        {
            policy = NULL;
        }

        ln = linked_list_next(ln);
    }

    return(policy);
}