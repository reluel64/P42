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

static list_head_t new_threads;
static list_head_t units;
static spinlock_t  units_lock;
static spinlock_t  new_threads_lock;
static sched_owner_t kernel;

static void sched_idle_thread(void *pv);
static void schedule_main(void);
void schedule(void);

extern void __context_unit_start(void *tcb);
extern int sched_simple_register(sched_policy_t **p);

static uint32_t sched_update_thread_time
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
    kprintf("UNLOCKING %x\n",th->unit);
    spinlock_unlock_int(&th->unit->lock);

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
    /* Lock everything */
    spinlock_lock_int(&th->lock);
    spinlock_lock_int(&new_threads_lock);

    linked_list_add_tail(&new_threads, &th->node);

    /* Unlock everything */
    spinlock_unlock_int(&new_threads_lock);
    spinlock_unlock_int(&th->lock);
}

int sched_start_thread(sched_thread_t *th)
{
    int int_status = 0;

    spinlock_lock_int(&new_threads_lock);

    linked_list_add_tail(&new_threads, &th->node);

    spinlock_unlock_int(&new_threads_lock);

    schedule();

    return(0);
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
    /* Set up spinlock to protect global lists */
    spinlock_init(&new_threads_lock);

    /* Set up RW spinlock to protect execution unit list */
    spinlock_rw_init(&units_lock);

    /* Initialize new threads list */
    linked_list_init(&new_threads);

    /* Initialize units list */
    linked_list_init(&units);
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

    if(cpu == NULL)
    {
        kprintf("ERROR: CPU %x\n", cpu);
        return(-1);
    }
   
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

    /* Initialize the queues */
    linked_list_init(&unit->ready_q);
    linked_list_init(&unit->blocked_q);
    linked_list_init(&unit->sleep_q);
    linked_list_init(&unit->dead_q);

    /* Set up the spinlock for the unit */
    spinlock_init(&unit->lock);
    
    /* For now we have only one policy - a primitive one */
    sched_simple_register(&unit->policy);

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
        timer_dev_connect_cb(timer, sched_update_thread_time, unit);
        timer_dev_enable(timer); 
    }

    /* From now , we are entering the unit's idle routine */
    context_unit_start(&unit->idle);

    /* make the compiler happy  - we would never reach this*/
    return(0);

}

void sched_yield()
{
   schedule();
}

void sched_unblock_thread
(
    sched_thread_t *th
)
{
    spinlock_lock_int(&th->lock);

    __atomic_and_fetch(&th->flags, ~THREAD_BLOCKED, __ATOMIC_SEQ_CST);

    spinlock_unlock_int(&th->lock);
}

void sched_block_thread
(
    sched_thread_t *th
)
{
    spinlock_lock_int(&th->lock);

    __atomic_or_fetch(&th->flags, THREAD_BLOCKED, __ATOMIC_SEQ_CST);

    spinlock_unlock_int(&th->lock);
}

void sched_sleep_thread
(
    sched_thread_t *th
)
{
    spinlock_lock_int(&th->lock);

    __atomic_or_fetch(&th->flags, THREAD_SLEEPING, __ATOMIC_SEQ_CST);

    spinlock_unlock_int(&th->lock);
}

void sched_wake_thread
(
    sched_thread_t *th
)
{
    spinlock_lock_int(&th->lock);

    __atomic_and_fetch(&th->flags, ~THREAD_SLEEPING, __ATOMIC_SEQ_CST);

    spinlock_unlock_int(&th->lock);
}


static uint32_t sched_update_thread_time
(
    void *pv_unit,
    void *isr_inf
)
{
    sched_exec_unit_t *unit = NULL;
    sched_policy_t    *policy = NULL;
    sched_thread_t    *thread = NULL;

    unit = pv_unit;

    spinlock_lock_int(&unit->lock);

    policy = unit->policy;

    if(unit->current)
        thread = unit->current;
    else
        thread = &unit->idle;

    policy->update_time(thread);

    spinlock_unlock_int(&unit->lock);

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
    
    kprintf("Entered idle loop on %x\n", unit->cpu->cpu_id);

    while(1)
    {
        cpu_halt();

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

    if(unit->current)
        th = unit->current;
    else
        th = &unit->idle;
    
    /* Return a boolean (TRUE or FALSE) value - 
     * not the value of THREAD_NEED_RESCHEDULE 
     */
    return(!!(th->flags & THREAD_NEED_RESCHEDULE));
}

void schedule(void)
{
    schedule_main();
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
    {
        prev_th = unit->current;
        policy->put_thread(prev_th);    
    }
    else
    {
        prev_th = &unit->idle;
    }

    status = policy->next_thread(&new_threads, 
                                 &new_threads_lock, 
                                 unit, 
                                 &next_th);

    /* If we don't have a task to execute, go to the idle task */
    if(status != 0)
    {
        next_th = &unit->idle;
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
