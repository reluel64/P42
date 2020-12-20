#include <linked_list.h>
#include <defs.h>
#include <spinlock.h>
#include <liballoc.h>
#include <utils.h>
#include <intc.h>
#include <isr.h>
#include <timer.h>
#include <scheduler.h>
#include <vmmgr.h>
#include <intc.h>
#include <platform.h>

static list_head_t new_threads;
static spinlock_t  list_lock;
static int sched_idle_loop(void *pv);

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

    th->stack_sz = stack_sz;
    th->entry_point = entry_pt;
    th->prio        = prio;
    th->pv          = pv;
    th->context = cpu_ctx_init(th);
    spinlock_init(&th->lock);
    return(0);
}

int sched_start_thread(sched_thread_t *th)
{
    int int_status = 0;

    spinlock_lock_interrupt(&list_lock, &int_status);

    linked_list_add_tail(&new_threads, &th->node);

    spinlock_unlock_interrupt(&list_lock, int_status);
    
    cpu_issue_ipi(IPI_DEST_ALL, 0, IPI_RESCHED);

    return(0);
}


void sched_resched
(
    virt_addr_t       iframe, 
    sched_exec_unit_t *unit
)
{
    int                      int_status = 0;
    sched_thread_t          *next       = NULL;
    sched_thread_t          *current    = NULL;
    sched_thread_t          *new_thread = NULL;
    list_node_t             *node       = NULL;
    
    spinlock_lock_interrupt(&unit->lock, &int_status);
    current = unit->current;
    
    /* Check if we have new threads that await first
     * execution
     */ 
    spinlock_lock_interrupt(&list_lock, &int_status);
    
    new_thread = (sched_thread_t*)linked_list_first(&new_threads);

    if(new_thread != NULL)
    {
        linked_list_remove(&new_threads, &new_thread->node);
        linked_list_add_head(&unit->active_q, &new_thread->node);
    }

    spinlock_unlock_interrupt(&list_lock, int_status);

    /* Add the pre-empted task to the end of active queue list */
    if(current)
    {
        spinlock_lock_interrupt(&current->lock, &int_status);
        cpu_ctx_save(iframe,  current);

        /* do not add the idle task to the active_q */
        if(unit->current != unit->idle)
        {
            if(current->flags & THREAD_BLOCKED)
            {
                linked_list_add_tail(&unit->blocked_q, &current->node);
            }
            else
            {
                current->flags &= ~THREAD_RUNNING;
                current->flags |= THREAD_READY;
                linked_list_add_tail(&unit->active_q, &current->node);
            }
        }
        spinlock_unlock_interrupt(&current->lock, int_status);
    }

    next = (sched_thread_t*)linked_list_first(&unit->active_q);

    /* check if we have a task to run
     * if we don't, we will go into the idle task
     */

    if(next == NULL)
    {
        next = unit->idle;
        spinlock_lock_interrupt(&next->lock, &int_status);
    }
    else
    {
        spinlock_lock_interrupt(&next->lock, &int_status);
        linked_list_remove(&unit->active_q, &next->node);
    }
    
    unit->current = next;
    next->unit = unit;
    next->flags &= ~THREAD_READY;
    next->flags |= THREAD_RUNNING;
    
    cpu_ctx_restore(iframe, next);

    /* Unblock the thread structure */
    spinlock_unlock_interrupt(&next->lock, int_status);   
    
    /* Unblock the execution unit structure */
    spinlock_unlock_interrupt(&unit->lock, int_status);
}


void sched_unblock_thread(sched_thread_t *th)
{
    sched_exec_unit_t *unit = NULL;
    int int_status = 0;

    /* prevent thread from being migrated */
    spinlock_lock_interrupt(&th->lock, &int_status);
    
    unit = th->unit;
    
    spinlock_lock_interrupt(&unit->lock, &int_status);

    linked_list_remove(&unit->blocked_q, &th->node);
    linked_list_add_head(&unit->active_q, &th->node);

    th->flags &= ~THREAD_BLOCKED;

    spinlock_unlock_interrupt(&unit->lock, int_status);
    spinlock_unlock_interrupt(&th->lock, int_status);

}

void sched_block_thread(sched_thread_t *th)
{
    sched_exec_unit_t *unit = NULL;
    int int_status = 0;
    int th_int_status = 0;

    /* prevent thread from being migrated */
    spinlock_lock_interrupt(&th->lock, &th_int_status);
    
    unit = th->unit;
    
    spinlock_lock_interrupt(&unit->lock, &int_status);

    linked_list_remove(&unit->blocked_q, &th->node);
    linked_list_add_head(&unit->active_q, &th->node);

    spinlock_unlock_interrupt(&unit->lock, int_status);
    spinlock_unlock_interrupt(&th->lock, th_int_status);

}

static int sched_resched_isr(void *pv, virt_addr_t iframe)
{
    sched_exec_unit_t *unit      = NULL;
    cpu_t             *cpu       = NULL;
        
    unit = (sched_exec_unit_t*)pv;
    cpu = unit->cpu;

    if(cpu->cpu_id != cpu_id_get())
        return(0);

    sched_resched(iframe, unit);
    
    return(0);
}


int sched_init(void)
{
    spinlock_init(&list_lock);
    linked_list_init(&new_threads);

    return(0);
}

int sched_cpu_init(device_t *timer, cpu_t *cpu)
{
    sched_exec_unit_t *unit = NULL;
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
    
    sched_init_thread(unit->idle, sched_idle_loop, PAGE_SIZE, 0, unit);


    isr_install(sched_resched_isr, unit, PLATFORM_RESCHED_VECTOR, 0);
    
    timer_periodic_install(timer,
                           sched_resched_isr,
                           unit,
                           0);

    return(0);
 
}

int sched_yield()
{
    extern void __resched_interrupt(void);

 
    cpu_issue_ipi(IPI_DEST_SELF, 0, IPI_RESCHED);
}

sched_thread_t *sched_thread_self(void)
{
    cpu_t             *cpu = NULL;
    sched_exec_unit_t *unit = NULL;
    sched_thread_t    *th = NULL;
    int                int_status   = 0;

    cpu = cpu_current_get();
    
    unit = cpu->sched;

    /* No interrupts, no migration */
    spinlock_lock_interrupt(&unit->lock, &int_status);
    
    th = unit->current;

    spinlock_unlock_interrupt(&unit->lock, int_status);

    return(th);
}



static int sched_idle_loop(void *pv)
{
    kprintf("Entered idle loop\n");
    while(1)
    {
        cpu_halt();
    }
    return(0);
}