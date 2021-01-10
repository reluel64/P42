#include <linked_list.h>
#include <defs.h>
#include <spinlock.h>
#include <liballoc.h>
#include <utils.h>
#include <intc.h>
#include <isr.h>
#include <timer.h>
#include <vmmgr.h>
#include <intc.h>
#include <platform.h>
#include <scheduler.h>

#define THREAD_NOT_RUNNABLE(x) (((x) & (THREAD_SLEEPING | THREAD_BLOCKED)))
#define SCHEDULER_TICK_MS 1
static list_head_t new_threads;
static list_head_t units;
static spinlock_t  units_lock;
static spinlock_t  list_lock;

static void sched_idle_loop(void *pv);


static void sched_thread_main(sched_thread_t *th)
{
    sched_thread_t *self = NULL;
    int int_status = 0;
    void *(*entry_point)(void *) = NULL;

    entry_point = th->entry_point;

    if(entry_point != NULL)
    {
        th->rval = entry_point(th->pv);
    }
    /* The thread is now dead */
    spinlock_lock_int(&th->lock, &int_status);

    __atomic_or_fetch(&th->flags, THREAD_DEAD, __ATOMIC_ACQUIRE);
    
    spinlock_unlock_int(&th->lock, int_status);
    
    while(1)
        sched_yield();
}

int sched_init_thread
(
    sched_thread_t    *th,
    void        *entry_pt,
    virt_size_t stack_sz,
    uint32_t    prio,
    void *pv

)
{
    if(th == NULL)
        return(-1);

    memset(th, 0, sizeof(sched_thread_t));

    th->stack = vmmgr_alloc(NULL, 0, stack_sz, VMM_ATTR_WRITABLE);

    if(!th->stack)
    {
        kfree(th);
        return(-1);
    }

    th->stack_sz    = stack_sz;
    th->entry_point = entry_pt;
    th->prio        = prio;
    th->pv          = pv;
    th->context     = cpu_ctx_init(th, sched_thread_main, th);

    spinlock_init(&th->lock);
    return(0);
}

int sched_start_thread(sched_thread_t *th)
{
    int int_status = 0;

    spinlock_lock_int(&list_lock, &int_status);

    linked_list_add_tail(&new_threads, &th->node);

    spinlock_unlock_int(&list_lock, int_status);

    cpu_issue_ipi(IPI_DEST_ALL, 0, IPI_RESCHED);

    return(0);
}

static void sched_wake_sleeping_threads
(
    sched_exec_unit_t *unit,
    uint32_t period
)
{
    list_node_t    *node      = NULL;
    list_node_t    *next_node = NULL;
    sched_thread_t *th        = NULL;

    node = linked_list_first(&unit->sleep_q);

    while(node)
    {
        next_node = linked_list_next(node);

        th = (sched_thread_t*)node;

        if(th->sleeped >= th->to_sleep)
        {
            linked_list_remove(&unit->sleep_q, &th->node);
            linked_list_add_tail(&unit->active_q, &th->node);
            __atomic_and_fetch(&th->flags, ~THREAD_SLEEPING, __ATOMIC_ACQUIRE);
        }

        th->sleeped += period;

        node = next_node;
    }
}

static inline void sched_unblock_threads
(
    sched_exec_unit_t *unit
)
{
    list_node_t    *node      = NULL;
    list_node_t    *next_node = NULL;
    sched_thread_t *th        = NULL;

    node = linked_list_first(&unit->blocked_q);

    while(node)
    {
        next_node = linked_list_next(node);

        th = (sched_thread_t*)node;

        if(!(th->flags & THREAD_BLOCKED))
        {
            linked_list_remove(&unit->blocked_q, &th->node);
            linked_list_add_tail(&unit->active_q, &th->node);
        }

        node = next_node;
    }
}

static inline void sched_preempt_thread
(
    sched_exec_unit_t *unit,
    sched_thread_t    *current,
    virt_addr_t        iframe
)
{
     cpu_ctx_save(iframe,  current);

    if(unit->current == unit->idle)
        return;

    if(unit->current == NULL)
        return;

    /* do not add the idle task to the active_q */
    if(current->flags & THREAD_BLOCKED)
    {
        linked_list_add_tail(&unit->blocked_q, &current->node);
    }
    else if(current->flags & THREAD_SLEEPING)
    {
        linked_list_add_tail(&unit->sleep_q, &current->node);
    }

    else if(current->flags & THREAD_DEAD)
    {
        linked_list_add_tail(&unit->dead_q, &current->node);
    }

    else
    {
        /* Add the pre-empted task to the end of active queue list */
        __atomic_and_fetch(&current->flags, 
                           ~THREAD_STATE_MASK,
                            __ATOMIC_ACQUIRE);
            
        /* Set the new status */
        __atomic_or_fetch(&current->flags, 
                            THREAD_READY, 
                            __ATOMIC_ACQUIRE);
            
        linked_list_add_tail(&unit->active_q, &current->node);
    }
   
}

/*
 * sched_switch_to_thread - switches execution to the thread 
 */

static inline void sched_switch_to_thread
(
    sched_exec_unit_t *unit,
    sched_thread_t    *th,
    virt_addr_t        iframe
)
{
    int             int_status = 0;
    sched_thread_t *next       = NULL;
    
    next = th;

    if(next == NULL)
        next = unit->idle;

    spinlock_lock_int(&next->lock, &int_status);
  
    if(next != unit->idle)
        linked_list_remove(&unit->active_q, &next->node);

    /* Make it current */
    unit->current = next;

    /* Make sure the unit is correct */
    next->unit = unit;

    /* recalculate the time slice */
    if(!next->remain)
        next->remain = SCHED_MAX_PRIORITY - next->prio;

    __atomic_and_fetch(&next->flags, 
                        ~THREAD_STATE_MASK,
                        __ATOMIC_ACQUIRE);

    /* make sure that the thread is in RUNNING state */
    __atomic_or_fetch(&next->flags, 
                    THREAD_RUNNING, 
                    __ATOMIC_ACQUIRE);

    cpu_ctx_restore(iframe, next);

    /* Unblock the thread structure */
    spinlock_unlock_int(&next->lock, int_status);
}

/* Scheduling routine
 * While scheduling we will take the following locks
 * 1) The execution unit lock to protect the queues
 * 2) The thread lock to protect the thread-specific data
 * 
 */ 

static void sched_resched
(
    virt_addr_t       iframe,
    sched_exec_unit_t *unit,
    uint32_t          period
)
{
    int                     preempt = 0;
    int                     int_status  = 0;
    sched_thread_t          *next       = NULL;
    sched_thread_t          *current    = NULL;
    sched_thread_t          *new_thread = NULL;
    list_node_t             *node       = NULL;

    spinlock_lock_int(&unit->lock, &int_status);
    current = unit->current;

    /* Check if we have new threads that await first
     * execution
     */
    spinlock_lock_int(&list_lock, &int_status);

    new_thread = (sched_thread_t*)linked_list_first(&new_threads);

    if(new_thread != NULL)
    {
        linked_list_remove(&new_threads, &new_thread->node);
        linked_list_add_head(&unit->active_q, &new_thread->node);
    }

    spinlock_unlock_int(&list_lock, int_status);

    /* If period is gt 0, then we have time progress and 
     * we can update the sleeping threads
     */ 

    if((period > 0) && (linked_list_count(&unit->sleep_q) > 0))
        sched_wake_sleeping_threads(unit, period);

    if(unit->current)
    {
        /* See if we need th pre-empt the thread */
        spinlock_lock_int(&current->lock, &int_status);

        if(current->remain > 0)
            current->remain--;

        if(!current->remain ||
            THREAD_NOT_RUNNABLE(current->flags))
        {
            preempt = 1;
        }        

        if(preempt)
        {

            sched_preempt_thread(unit, current, iframe);
        }

        spinlock_unlock_int(&current->lock, int_status);
    }
    /* In case current is NULL, we will set the preempt
     * flag to 1 so that we can force at least the idle
     * task to begin execution
     */  
    else
    {
        preempt = 1;
    }

    /* check if we have threads to unblock */
    if((__atomic_load_n(&unit->unblocked_th, __ATOMIC_ACQUIRE)) > 0)
    {
        __atomic_sub_fetch(&unit->unblocked_th, 1, __ATOMIC_ACQUIRE);
        sched_unblock_threads(unit);
    }

    /* We are pre-empting  */
    if(preempt)
    {
        next = (sched_thread_t*)linked_list_first(&unit->active_q);
        
        sched_switch_to_thread(unit, next, iframe);
    }

    /* Unblock the execution unit structure */
    spinlock_unlock_int(&unit->lock, int_status);

}


void sched_unblock_thread(sched_thread_t *th)
{
    sched_exec_unit_t *unit = NULL;
    cpu_t *cpu = NULL;
    int int_status = 0;

    /* prevent thread from being migrated */

    spinlock_lock_int(&th->lock, &int_status);
    
    unit = th->unit;
    cpu = unit->cpu;
    
    __atomic_and_fetch(&th->flags, ~THREAD_BLOCKED, __ATOMIC_ACQUIRE);
    __atomic_add_fetch(&unit->unblocked_th, 1, __ATOMIC_ACQUIRE);
    
    cpu_issue_ipi(IPI_DEST_NO_SHORTHAND, cpu->cpu_id, IPI_RESCHED);
    spinlock_unlock_int(&th->lock, int_status);
}


static int sched_timer_isr(void *pv, isr_info_t *inf)
{
    sched_exec_unit_t *unit      = NULL;
    cpu_t             *cpu       = NULL;

    unit = (sched_exec_unit_t*)pv;
    cpu = unit->cpu;

    if(cpu->cpu_id != inf->cpu_id)
        return(-1);

    sched_resched(inf->iframe, unit, SCHEDULER_TICK_MS);

    return(0);
}

static int sched_resched_isr(void *pv, isr_info_t *inf)
{
    sched_exec_unit_t *unit      = NULL;
    cpu_t             *cpu       = NULL;

    unit = (sched_exec_unit_t*)pv;
    cpu = unit->cpu;

    if(cpu->cpu_id != inf->cpu_id)
        return(-1);

    sched_resched(inf->iframe, unit, 0);

    return(0);
}

int sched_init(void)
{
    spinlock_init(&list_lock);
    spinlock_rw_init(&units_lock);
    linked_list_init(&new_threads);
    linked_list_init(&units);
    return(0);
}

int sched_cpu_init(device_t *timer, cpu_t *cpu)
{
    sched_exec_unit_t *unit = NULL;
    int int_status = 0;

    if(timer == NULL || cpu == NULL)
    {
        kprintf("ERROR: TIMER %x CPU %x\n", timer, cpu);
        return(-1);
    }

    kprintf("Initializing Scheduler for CPU %d\n",cpu->cpu_id);
    unit = kcalloc(sizeof(sched_exec_unit_t), 1);

    if(unit == NULL)
    {
        return(-1);
    }

    /* assign scheduler unit to the cpu */
    cpu->sched = unit;

    /* tell the scheduler unit on which cpu it belongs */
    unit->cpu = cpu;

    linked_list_init(&unit->active_q);
    linked_list_init(&unit->blocked_q);
    linked_list_init(&unit->sleep_q);
    linked_list_init(&unit->dead_q);

    spinlock_init(&unit->lock);

    unit->idle = kcalloc(sizeof(sched_thread_t), 1);
    
    /* Initialize the idle task
     * The scheduler will automatically start executing 
     * it in case there are no other tasks in the active_q
     */

    sched_init_thread(unit->idle, sched_idle_loop, PAGE_SIZE, 255, unit);

    /* Add the unit to the list */
    spinlock_write_lock_int(&units_lock, &int_status);
    linked_list_add_tail(&units, &unit->node);
    spinlock_write_unlock_int(&units_lock, int_status);


    isr_install(sched_resched_isr, unit, PLATFORM_RESCHED_VECTOR, 0);

    timer_periodic_install(timer,
                           &unit->tm,
                           sched_timer_isr,
                           unit,
                           SCHEDULER_TICK_MS);

    while(1)
    {
        cpu_halt();
    }

    /* make the compiler happy */
    return(0);

}

void sched_yield()
{
    cpu_resched();
}

sched_thread_t *sched_thread_self(void)
{
    cpu_t             *cpu = NULL;
    sched_exec_unit_t *unit = NULL;
    sched_thread_t    *th = NULL;
    int                int_status   = 0;
    int                istatus      = 0;

    istatus = cpu_int_check();
    
    if(istatus)
        cpu_int_lock();
    
    cpu = cpu_current_get();

    unit = cpu->sched;

    /* No interrupts, no migration */
    spinlock_lock_int(&unit->lock, &int_status);

    th = unit->current;

    spinlock_unlock_int(&unit->lock, int_status);
    
    if(istatus)
        cpu_int_unlock();
        
    return(th);
}


void sched_sleep(uint32_t delay)
{
    sched_thread_t *th = NULL;
    int            int_status = 0;

    th = sched_thread_self();

    spinlock_lock_int(&th->lock, &int_status);
    th->to_sleep = delay;
    th->sleeped = 0;
    __atomic_fetch_or(&th->flags, THREAD_SLEEPING, __ATOMIC_ACQUIRE);
    spinlock_unlock_int(&th->lock, int_status);

    cpu_resched();
}

static inline void sched_clean_thread(sched_thread_t *th)
{
    memset((void*)th->stack, 0, th->stack_sz);
    vmmgr_free(NULL, th->stack, th->stack_sz);
    cpu_ctx_destroy(th);

    if(th->flags & THREAD_ALLOCATED)
        kfree(th);
}

static void sched_idle_loop(void *pv)
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
        spinlock_lock_int(&unit->lock, &int_status);    

        c = linked_list_first(&unit->dead_q);

        while(c)
        {
            kprintf("Cleaning %x\n", c);
            
            n = linked_list_next(c);

            th = (sched_thread_t*)c;

            linked_list_remove(&unit->dead_q, c);
            
            sched_clean_thread(th);
            
            c = n;
        }

        spinlock_unlock_int(&unit->lock, int_status);
    }
}
