#include <scheduler.h>
#include <context.h>
#include <utils.h>

static int _thread_create
(
    void *out_th,
    void (*th_entry)(void *arg),
    void *arg,
    size_t stack_sz,
    uint32_t prio
)
{
    sched_thread_t *th = NULL;
    int             ret = 0;

    th = out_th;

    if(th_entry == NULL)
        return(-1);

    /* Clear memory */
    memset(th, 0, sizeof(sched_thread_t));


    th->prio = prio;
    th->pv   = arg;
    th->entry_point = th_entry;
    th->stack_sz = stack_sz;

   
    /* Initialize the platform context */
    ret = context_init(th);

    if(ret < 0)
        return(-1);


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

    return(_thread_create(out_th, 
                          th_entry, 
                          arg, 
                          stack_sz, 
                          prio));
}

int thread_create
(
    void **out_th,
    void (*th_entry)(void *arg),
    void *arg,
    size_t stack_sz,
    uint32_t prio
)
{
    if(out_th == NULL)
        return(-1);

    
}