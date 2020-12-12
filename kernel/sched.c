#include <linked_list.h>
#include <defs.h>
#include <spinlock.h>
#include <liballoc.h>
#include <utils.h>
#include <intc.h>
#include <isr.h>
#include <platform.h>
#include <timer.h>
#include <sched.h>
#include <vmmgr.h>

typedef struct sched_exec_unit_t
{
    cpu_t *cpu;             
    list_head_t active_q;    /* queue of active threads on the current CPU*/
    list_head_t blocked_q;   /* queue of blocked threads */
    list_head_t sleep_q;     /* queue of sleeping threads */
    list_head_t dead_q;      /* queue of dead threads - for cleanup */
    sched_thread_t *current;       /* current thread */
    sched_thread_t *idle;
    spinlock_t lock;         /* lock to protect the queue */

}sched_exec_unit_t;


static list_head_t new_threads;
static spinlock_t  list_lock;
static int sched_idle_loop(void);

extern  void *pcpu_initialize_context
(
    sched_thread_t *th,
    uint8_t is_user
);

void pcpu_prepare_context_switch(virt_addr_t iframe, void *ctx);
void pcpu_context_save(virt_addr_t iframe, void *context);

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
    th->context = pcpu_initialize_context(th, 0);

    return(0);
}

int sched_start_thread(sched_thread_t *th)
{
    int int_status = 0;

    spinlock_lock_interrupt(&list_lock, &int_status);

    linked_list_add_tail(&new_threads, &th->node);

    spinlock_unlock_interrupt(&list_lock, int_status);
    
    /*cpu_issue_ipi(IPI_DEST_ALL, 0, IPI_RESCHED);*/

    return(0);
}

#include <pcpu.h>
extern void __test_func(void);
 static pcpu_context_t *ctx = NULL;
static uint8_t *stack = NULL;


void sched_resched
(
    virt_addr_t iframe, 
    sched_exec_unit_t *unit
)
{
    int               int_status = 0;
    sched_thread_t          *next      = NULL;
    sched_thread_t          *current   = NULL;
    sched_thread_t          *new_thread = NULL;
    list_node_t       *node      = NULL;

    spinlock_lock_interrupt(&unit->lock, &int_status);

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
    if(unit->current)
    {
        pcpu_context_save(iframe,  unit->current->context);
        linked_list_add_tail(&unit->active_q, &unit->current->node);
    }

    next = (sched_thread_t*)linked_list_first(&unit->active_q);

    if(next != NULL)
    {
        linked_list_remove(&unit->active_q, &next->node);
        unit->current = next;
        pcpu_prepare_context_switch(iframe, next->context);
    }

   // kprintf("REscheduling %x CPU %d\n",next, unit->cpu->cpu_id);
    spinlock_unlock_interrupt(&unit->lock, int_status);


}
extern void pcpu_prepare_context_switch(virt_addr_t iframe, void *ctx);

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
#include <vmmgr.h>

int sched_cpu_init(device_t *timer, cpu_t *cpu)
{
    sched_exec_unit_t *unit = NULL;
    kprintf("Initializing Scheduler for CPU %d\n",cpu->cpu_id);
    unit = kcalloc(sizeof(sched_exec_unit_t), 1);

    if(unit == NULL)
    {
        return(-1);
    }

    
    /* assign scheduler to the cpu */
    cpu->sched = unit;

    /* tell the scheduler unit on which cpu it belongs */
    unit->cpu = cpu;

    linked_list_init(&unit->active_q);
    linked_list_init(&unit->blocked_q);
    linked_list_init(&unit->sleep_q);
    linked_list_init(&unit->dead_q);

    spinlock_init(&unit->lock);

    timer_periodic_install(timer,
                           sched_resched_isr,
                           unit,
                           0);

    unit->idle = kcalloc(sizeof(sched_thread_t), 1);
  //  isr_install(sched_resched_isr, unit, PLATFORM_RESCHED_VECTOR, 0);
 //   sched_init_thread(unit->idle, sched_idle_loop, PAGE_SIZE, 0);
   // sched_start_thread(unit->idle);

    return(0);
 
}

static int sched_idle_loop(void)
{
    kprintf("Entered idle loop\n");
    while(1)
    {
        cpu_halt();
    }
    return(0);
}