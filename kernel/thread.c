#include <scheduler.h>
#include <context.h>
#include <utils.h>
#include <vm.h>
#include <liballoc.h>

static void thread_entry_point
(
    sched_thread_t *th
)
{
    
}

static int thread_setup
(
    void *out_th,
    void (*th_entry)(void *arg),
    void *arg,
    size_t stack_sz,
    uint32_t prio
)
{
    sched_thread_t *th = NULL;
    virt_addr_t    stack_origin = 0;
    virt_size_t    stack_size = 0;

    int             ret = 0;
    
    th = out_th;

    if(th_entry == NULL)
    {
        return(-1);
    }

    /* align the stack size to page size */
    stack_size = ALIGN_UP(stack_sz, PAGE_SIZE);

    /* thake into account the guard pages */
    stack_size += (PAGE_SIZE  << 1);

    stack_origin = vm_alloc(NULL, 
                            VM_BASE_AUTO,
                            stack_size,
                            0,
                            VM_ATTR_WRITABLE);

    if(stack_origin == VM_INVALID_ADDRESS)
    {
        return(-1);
    }

    memset((void*)stack_origin, 0, stack_size);

    /* mark the first guard page as read-only */
    vm_change_attr(NULL, 
                  stack_origin,
                  PAGE_SIZE, 0, 
                  VM_ATTR_WRITABLE, 
                  NULL);

    /* mark the last guard page as read only */
    vm_change_attr(NULL, 
                  stack_origin + stack_size - PAGE_SIZE,
                  PAGE_SIZE, 0, 
                  VM_ATTR_WRITABLE, 
                  NULL);


    /* clean up the stack */
    

    /* Clear memory */
    memset(th, 0, sizeof(sched_thread_t));

    th->prio         = prio;
    th->arg          = arg;
    th->entry_point  = th_entry;
    th->stack_sz     = stack_sz;
    th->flags        = THREAD_READY;

    /* we skip the guard page */
    th->stack_origin = stack_origin + PAGE_SIZE;
    
    /* Initialize the platform context */
    ret = context_init(th);

    if(ret < 0)
    {
        vm_free(NULL, stack_origin, stack_size);
        return(-1);
    }

    /* make the thread available to all cpus */
    memset(th->affinity, 0xff, sizeof(th->affinity));

    /* Initialize spinlock */
    spinlock_init(&th->lock);

    return(0);
}


int thread_start
(
    sched_thread_t *th
)
{
    sched_enqueue_thread(th);
}

int thread_create_static
(
    void *out_th,
    void (*th_entry)(void *arg),
    void *arg,
    size_t stack_sz,
    uint32_t prio
)
{
    if(out_th == NULL)
        return(-1);

    return(thread_setup(out_th, 
                          th_entry, 
                          arg, 
                          stack_sz, 
                          prio));
}

void *thread_create
(
    void (*th_entry)(void *arg),
    void *arg,
    size_t stack_sz,
    uint32_t prio
)
{
    sched_thread_t *th = NULL;
    int status = 0;

    th = kcalloc(sizeof(sched_thread_t), 1);

    if(th == NULL)
    {
        return(NULL);
    } 
    
    status = thread_setup(th, 
                          th_entry,
                          arg,
                          stack_sz,
                          prio);

    if(status < 0)
    {
        kfree(th);
        th = NULL;
    }

    return(th);
}