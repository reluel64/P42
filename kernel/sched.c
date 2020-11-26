#include <linked_list.h>
#include <defs.h>
#include <spinlock.h>
#include <liballoc.h>
#include <utils.h>
#include <intc.h>
#include <isr.h>
#include <platform.h>
#include <timer.h>

typedef struct thread_t
{
    list_node_t node;
    void       *owner;
    void       *context;
    uint32_t    id;
    uint16_t    prio;
    virt_addr_t stack;
    virt_size_t stack_sz;
    void       *entry_point;
}thread_t;

typedef struct shced_queue_t
{

}sched_queue_t;

typedef struct sched_exec_unit_t
{
    cpu_t *cpu;             
    list_head_t active_q;    /* queue of active threads on the current CPU*/
    list_head_t blocked_q;   /* queue of blocked threads */
    list_head_t sleep_q;     /* queue of sleeping threads */
    list_head_t dead_q;      /* queue of dead threads - for cleanup */
    thread_t *current;      /* current thread */
    spinlock_t lock;        /* lock to protect the queue */
}sched_exec_unit_t;


static list_head_t new_threads;
static spinlock_t  list_lock;


int shced_init_thread
(
    thread_t    *th,
    void        *entry_pt,
    virt_size_t stack_sz,
    uint32_t    prio
    
)
{
    if(th == NULL)
        return(-1);

    memset(th, 0, sizeof(thread_t));

    th->stack = (virt_addr_t)kcalloc(stack_sz, 1);

    if(!th->stack)
    {
        kfree(th);
        return(-1);
    }

    th->stack_sz = stack_sz;
    th->entry_point = entry_pt;
    th->prio        = prio;

    return(0);
}

int shced_start_thread(thread_t *th)
{
    int int_status = 0;

    spinlock_lock_interrupt(&list_lock, &int_status);

    linked_list_add_tail(&new_threads, &th->node);

    spinlock_unlock_interrupt(&list_lock, int_status);
    
    cpu_issue_ipi(IPI_DEST_ALL, 0, IPI_RESCHED);

    return(0);
}

static thread_t *sched_acquire_unblocked_thread
(
    sched_exec_unit_t *unit
)
{
    list_node_t *node = NULL;
    thread_t *th =  NULL;
    node = linked_list_first(&unit->blocked_q);

    while(node)
    {
        th = (thread_t*)node;

        
        node = linked_list_next(node);
    }

    return(NULL);
}

static int sched_resched_isr(void *pv, virt_addr_t iframe)
{
    sched_exec_unit_t *unit = pv;
    
    return 0;
    int int_status = 0;
    thread_t *next = NULL;
    
    spinlock_lock_interrupt(&unit->lock, &int_status);
    
    
    next = (thread_t*)linked_list_first(&unit->active_q);
    
    /* if we don't have any active threads, we must check
     * the other queues
     */ 
    if(next == NULL)
    {
        kprintf("Checking for blocked q");
        next = sched_acquire_unblocked_thread(unit);
        
    }

    kprintf("Rescheduling on %d\n", unit->cpu->cpu_id);
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

    unit = kcalloc(sizeof(sched_exec_unit_t), 1);
    kprintf("ENTER\n");
    if(unit == NULL)
    {
        return(-1);
    }

    /* assign scheduler to the cpu */
    cpu->sched = unit;

    /* tell the scheduler unit on which cpu it belongs */
    unit->cpu = cpu;

    timer_periodic_install(timer,
                           sched_resched_isr,
                           unit,
                           0);
                           
    //isr_install(sched_resched_isr, NULL, PLATFORM_RESCHED_VECTOR, 0);
}

static int sched_idle_loop(void)
{
    while(1)
    {
        cpu_halt();
    }
}