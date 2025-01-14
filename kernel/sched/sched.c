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

#define SCHED_IDLE_THREAD_STACK_SIZE    (PAGE_SIZE)

static struct list_head    threads       = LINKED_LIST_INIT;
static struct list_head    units         = LINKED_LIST_INIT;
static struct list_head    policies      = LINKED_LIST_INIT;
static struct spinlock_rw  units_lock    = SPINLOCK_RW_INIT;
static struct spinlock_rw  threads_lock  = SPINLOCK_RW_INIT;
static struct spinlock_rw  policies_lock = SPINLOCK_RW_INIT;


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
    const struct time_spec *step,
    const void        *isr_inf
);

static void sched_context_switch
(
    struct sched_thread *prev,
    struct sched_thread *next
);

static uint32_t sched_timer_wake_thread
(
    struct timer *tm,
    void *thread,
    const void *isr
);

static int32_t sched_select_thread
(
    struct sched_exec_unit *unit,
    struct sched_thread *th
);

static int32_t sched_put_prev
(
    struct sched_exec_unit *unit,
    struct sched_thread *th
);

static int32_t sched_enq
(
    struct sched_exec_unit *unit,
    struct sched_thread *th
);

static int32_t sched_deq
(
    struct sched_exec_unit *unit,
    struct sched_thread    *th
);

static uint8_t sched_preemption_enabled
(
    struct sched_exec_unit *unit
);


int basic_register
(
    void
);

int idle_task_register
(
    void
);

struct sched_policy *sched_get_policy_by_id
(
    enum sched_policy_id id
);

void sched_thread_entry_point
(
    struct sched_thread *th
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

int32_t sched_track_thread
(
    struct sched_thread *th
)
{
    uint8_t int_flag = 0;

    /* add thread to the global list */
    spinlock_write_lock_int(&threads_lock, &int_flag);
    linked_list_add_tail(&threads, &th->system_node);
    spinlock_write_unlock_int(&threads_lock, int_flag);

    return(0);
}


int32_t sched_untrack_thread
(
    struct sched_thread *th
)
{
    uint8_t int_flag = 0;

    /* remove thread to the global list */
    spinlock_write_lock_int(&threads_lock, &int_flag);
    linked_list_remove(&threads, &th->system_node);
    spinlock_write_unlock_int(&threads_lock, int_flag);

    return(0);
}

void sched_show_threads
(
    void
)
{
    uint8_t int_sts = 0;
    struct list_node *ln = NULL;
    struct sched_thread *th = NULL;

    spinlock_read_lock_int(&threads_lock, &int_sts);

    ln = linked_list_first(&threads);

    while(ln)
    {
        th = SYSTEM_NODE_TO_THREAD(ln);

        kprintf("THREAD %x\n", th);

        ln = linked_list_next(ln);
        
    }

    spinlock_read_unlock_int(&threads_lock, int_sts);
}

int32_t sched_pin_task_to_unit
(
    struct sched_exec_unit *unit,
    struct sched_thread *th
)
{
    int32_t status = 0;
    uint8_t int_status = 0;

    spinlock_lock_int(&unit->lock, &int_status);
    /* this thread belongs to this unit */
    th->unit = unit;
    linked_list_add_head(&unit->unit_threads, &th->unit_node);

    spinlock_unlock_int(&unit->lock, int_status);

    return(status);
}

int32_t sched_unpin_task_from_unit
(
    struct sched_exec_unit *unit,
    struct sched_thread *th
)
{
    int32_t status = 0;
    uint8_t int_status = 0;
    
    spinlock_lock_int(&unit->lock, &int_status);
    /* this thread belongs to this unit */
    th->unit = NULL;
    linked_list_remove(&unit->unit_threads, &th->unit_node);

    spinlock_unlock_int(&unit->lock, int_status);

    return(status);
}

int32_t sched_find_least_used_unit
(
    struct sched_exec_unit **unit
)
{
    uint8_t int_status = 0;
    struct list_node *ln = NULL;
    struct sched_exec_unit *least_busy = NULL;
    struct sched_exec_unit *cursor = NULL;
    
    spinlock_read_lock_int(&units_lock, &int_status);
    ln = linked_list_first(&units);

    while(ln)
    {
        cursor = (struct sched_exec_unit*)ln;

        if(least_busy == NULL)
        {
            least_busy = cursor;
        }
        else
        {
            if(linked_list_count(&cursor->unit_threads) < 
               linked_list_count(&least_busy->unit_threads))
            {
                least_busy = cursor;
            }
        }

        ln = linked_list_next(ln);
    }

    *unit = least_busy;
    spinlock_read_unlock_int(&units_lock, int_status);
    return(0);
}

int sched_start_thread
(
    struct sched_thread *th
)
{
    struct sched_exec_unit *unit = NULL;
    uint8_t int_status = 0;

    spinlock_lock_int(&th->lock, &int_status);
    
    /* get the most available unit */
    sched_find_least_used_unit(&unit);

    /* add the thread to the tracking list */
    sched_track_thread(th);

    /* pick the policy for the thread */
    th->policy = sched_get_policy_by_id(th != &unit->idle ? sched_basic_policy : 
                                                            sched_idle_task_policy);


    /* pin the thread to the unit */
    sched_pin_task_to_unit(unit, th);

    /* enqueue the thread to the policy so it will get executed */
    spinlock_lock(&unit->lock);
    sched_enq(unit, th);
    spinlock_unlock(&unit->lock);
    spinlock_unlock_int(&th->lock, int_status);
    return(0);
}

struct sched_thread *sched_thread_self
(
    void
)
{
    struct sched_exec_unit *unit = NULL;
    struct cpu             *cpu  = NULL;
    struct sched_thread    *self = NULL;
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
    struct sched_thread *th
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
    struct sched_thread *self = NULL;

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


void sched_disable_preempt(void)
{
    uint8_t int_status = 0;
    struct sched_exec_unit *unit = NULL;
    struct cpu *cpu = NULL;

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
        if(__atomic_load_n(&unit->preempt_lock, __ATOMIC_SEQ_CST) < UINT32_MAX)
        {
            __atomic_add_fetch(&unit->preempt_lock, 1, __ATOMIC_SEQ_CST);
        }
    }
    
    if(int_status)
    {
        cpu_int_unlock();
    }
}

void sched_enable_preempt(void)
{
    uint8_t int_status = 0;
    struct sched_exec_unit *unit = NULL;
    struct cpu *cpu = NULL;

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
        if(__atomic_load_n(&unit->preempt_lock, __ATOMIC_SEQ_CST) > 0)
        {
            __atomic_sub_fetch(&unit->preempt_lock, 1, __ATOMIC_SEQ_CST);
        }
    }
    
    if(int_status)
    {
        cpu_int_unlock();
    }
}

static uint8_t sched_preemption_enabled
(
    struct sched_exec_unit *unit
)
{
    return(__atomic_load_n(&unit->preempt_lock, __ATOMIC_SEQ_CST) == 0);
}

int32_t sched_policy_register
(
    struct sched_policy *p
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

struct sched_policy *sched_get_policy_by_id
(
    enum sched_policy_id id
)
{
    struct list_node *ln = NULL;
    struct sched_policy *policy = NULL;

    ln = linked_list_first(&policies);

    while(ln != NULL)
    {
        policy = (struct sched_policy*)ln;

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


/*
 * sched_init - initialize scheduler structures
 */ 

int sched_init(void)
{
    /* Set up RW spinlock to protect execution unit list */
    spinlock_rw_init(&units_lock);

    /* Initialize units list */
    linked_list_init(&units);
    
    /* register basic scheduling policy */
    basic_register();

    /* register idle task policy */
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
    struct device_node       *timer, 
    struct cpu          *cpu,
    struct sched_thread *post_unit_init
)
{
    struct sched_exec_unit *unit        = NULL;
    struct list_node       *node        = NULL;
    uint8_t           int_flag     = 0;
    uint8_t           use_tick_ipi = 0;
    struct timer_api       *funcs       = NULL;
    struct sched_policy    *policy      = NULL;

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
    unit = kcalloc(sizeof(struct sched_exec_unit), 1);

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
        policy = (struct sched_policy*)node;
    
        policy->unit_init(unit);

        node = linked_list_next(node);    
    }

    spinlock_read_unlock_int(&policies_lock, int_flag);

   

    /* Initialize the idle thread
     * as we will switch to it when we finish 
     * intializing the scheduler unit
     */ 
    kthread_create_static(&unit->idle,
                          "idle_thread",
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
    struct sched_thread *th
)
{
    uint8_t int_flag = 0;

    if(th != NULL)
    {
        spinlock_lock(&th->unit->lock);
        spinlock_lock_int(&th->lock, &int_flag);

        th->flags |= THREAD_READY;
       
        /* put the thread back to the policy */
        sched_enq(th->unit, th);

        spinlock_unlock(&th->unit->lock);

        spinlock_unlock_int(&th->lock, int_flag);
    }
}


static uint32_t sched_timer_wake_thread
(
    struct timer *timer,
    void *thread,
    const void *isr
)
{
//    kprintf("Waking up thread %x\n",thread);
    sched_wake_thread(thread);
    return(0);
}

void sched_sleep
(
    uint32_t delay
)
{
    uint8_t int_status = 0;
    struct timer tm;
    struct time_spec timeout ={.seconds = delay / 1000ull,
                          .nanosec = delay % 1000000ull
                         };
                         
    struct sched_thread *self = NULL;
    
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
        self->flags &= ~THREAD_READY;
    }

    /* don't bother to add the thread to the timer if delay is WAIT_FOREVER */
    if((delay != WAIT_FOREVER) && (delay != 0))
    {
        timer_enqeue_static(NULL, 
                            &timeout, 
                            sched_timer_wake_thread, 
                            self,
                            TIMER_ONESHOT, 
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
        if(timer_dequeue(NULL, &tm))
        {
            kprintf("TIMER NO LONGER IN QUEUE\n");
        }
    }
}


/* time tracking - called from the interrupt context */
static uint32_t sched_tick
(
    void              *pv_unit,
    const struct time_spec *step,
    const void        *isr_inf
)
{
    struct sched_exec_unit *unit         = NULL;
    struct sched_thread    *th           = NULL;
    uint8_t resched = 0;
    uint8_t preempt = 0;
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
            preempt = sched_preemption_enabled(unit);

            if(preempt)
            {
                th->flags |= THREAD_NEED_RESCHEDULE;
            }
        }
    }

    /* all good, unlock the unit */
    spinlock_unlock(&unit->lock);

    return(0);
    
}

void sched_thread_set_priority
(
    struct sched_thread *th, 
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
    struct sched_exec_unit *unit       = NULL;
    struct timer_api        *func      = NULL;
    unit = (struct sched_exec_unit*)pv;

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
        #if 0
        func = devmgr_dev_api_get(unit->timer_dev);

        if((func != NULL) && (func->disable != NULL))
        {
            func->disable(unit->timer_dev);
        }
        #endif
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
    struct sched_thread *prev,
    struct sched_thread *next
)
{
    if(prev != NULL)
    {
        prev->context_switches++;
    }

    context_switch(prev, next);
}

int sched_need_resched
(
    struct sched_exec_unit *unit
)
{
    struct sched_thread *th = NULL;
    int need_resched  = 1;
    uint8_t preemption = 0;
    /* if we have a thread, check if we have to do rescheduling */
    if(unit->current)
    {
        th = unit->current;
        need_resched = (th->flags & THREAD_NEED_RESCHEDULE) == 
                                    THREAD_NEED_RESCHEDULE;
                                    
    }

    preemption = sched_preemption_enabled(unit);

    if(preemption == 0)
    {
        need_resched = 0;
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
    struct sched_exec_unit *unit,
    struct sched_thread *th
)
{
    int32_t result = -1;
    
    if(th->policy != NULL && 
      (th->policy->select_thread != NULL))
    {
        result = th->policy->select_thread(unit, th);
    }

    return(result);
}


static int32_t sched_put_prev
(
    struct sched_exec_unit *unit,
    struct sched_thread *th
)
{
    int32_t status = -1;

    if((th->policy != NULL) && 
       (th->policy->put_prev != NULL))
    {
        status = th->policy->put_prev(unit, th);
    }


    return(status);
}

/* enqueue a thread on the policy */

static int32_t sched_enq
(
    struct sched_exec_unit *unit,
    struct sched_thread *th
)
{
    int32_t status = -1;

    if((th->policy != NULL) && 
       (th->policy->enqueue != NULL))
    {
        status = th->policy->enqueue(unit, th);
    }

    return(status);
}

/* Dequeue a thread from the policy */

static int32_t sched_deq
(
    struct sched_exec_unit *unit,
    struct sched_thread    *th
)
{
    int32_t status = -1;

    if((th->policy != NULL) && 
       (th->policy->dequeue != NULL))
    {
        status = th->policy->dequeue(unit, th);
    }

    return(status);
}

static int32_t sched_pick_next
(
    struct sched_exec_unit *unit,
    struct sched_thread **th
)
{
    struct sched_thread *next = NULL;
    struct list_node *pn = NULL;
    struct sched_policy *policy = NULL;
    uint8_t int_sts = 0;
    int32_t status = -1;

    spinlock_read_lock_int(&policies_lock, &int_sts);

    /* find a policy that has something ready t run */
    pn = linked_list_first(&policies);

    while(pn != NULL)
    {
        policy = (struct sched_policy*)pn;

        if(policy->pick_next != NULL)
        {
            status = policy->pick_next(unit, &next);

            if( status == 0)
           {
                break;
            }
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
    struct sched_exec_unit *unit,
    struct sched_thread *prev,
    struct sched_thread **next
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

static void  sched_block_thread
(
    struct sched_exec_unit *unit,
    struct sched_thread *th
)
{
    sched_deq(unit, th);   
}

static void sched_main(void)
{
    struct cpu             *cpu     = NULL;
    struct sched_exec_unit *unit    = NULL;
    struct sched_thread    *next_th = NULL;
    struct sched_thread    *prev_th = NULL;
    uint8_t           int_flag = 0;

    cpu    = cpu_current_get();
    unit   = cpu->sched;
    
    if(sched_preemption_enabled(unit))
    {
        /* Lock the execution unit */
        spinlock_lock_int(&unit->lock, &int_flag);

        prev_th = unit->current;

       if(prev_th != NULL)
        {
            if(~prev_th->flags & THREAD_READY)
            {
                sched_block_thread(unit, prev_th);
            }
        }

        sched_next_thread(unit, prev_th, &next_th);
    
        unit->current = next_th;

        /* Switch context */
        sched_context_switch(prev_th, next_th);

        /* Unlock unit */
        spinlock_unlock_int(&unit->lock, int_flag);
    }
}