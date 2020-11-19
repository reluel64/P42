#include <linked_list.h>
#include <defs.h>
#include <spinlock.h>
#include <liballoc.h>
#include <utils.h>
#include <intc.h>
#include <isr.h>
#include <platform.h>

typedef struct thread_t
{
    list_node_t node;
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
    list_head_t threads;    /* queue of threads on the current CPU*/
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

static int sched_resched_isr(void *pv, uint64_t ec)
{
    return(0);
}

int sched_init(void)
{
    spinlock_init(&list_lock);
    linked_list_init(&new_threads);
    isr_install(sched_resched_isr, NULL, PLATFORM_RESCHED_VECTOR, 0);
    return(0);
}



static int sched_idle_loop(void)
{
    
}