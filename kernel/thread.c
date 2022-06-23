#include <scheduler.h>
#include <context.h>
#include <utils.h>
#include <vm.h>
#include <liballoc.h>
#include <thread.h>
#include <owner.h>

static int thread_setup
(
    void      *out_th,
    th_entry_point_t entry_pt,
    void      *arg,
    size_t    stack_sz,
    uint32_t  prio,
    cpu_aff_t *affiity,
    void      *owner
)
{
    sched_thread_t *th = NULL;
    sched_owner_t  *ow  = NULL;
    virt_addr_t    stack_origin = 0;
    virt_size_t    stack_size = 0;

    int             ret = 0;
    
    th = out_th;
    ow = owner;

    if(entry_pt == NULL)
    {
        return(-1);
    }

    /* align the stack size to page size */
    stack_size = ALIGN_UP(stack_sz, PAGE_SIZE);

    /* thake into account the guard pages */
    stack_size += (PAGE_SIZE  << 1);

    stack_origin = vm_alloc(ow->vm_ctx, 
                            VM_BASE_AUTO,
                            stack_size,
                            0,
                            VM_ATTR_WRITABLE);

    if(stack_origin == VM_INVALID_ADDRESS)
    {
        return(-1);
    }

    /* clean up the stack */
    memset((void*)stack_origin, 0, stack_size);

    /* mark the first guard page as read-only */
    vm_change_attr(ow->vm_ctx, 
                  stack_origin,
                  PAGE_SIZE, 0, 
                  VM_ATTR_WRITABLE, 
                  NULL);

    /* mark the last guard page as read only */
    vm_change_attr(ow->vm_ctx, 
                  stack_origin + stack_size - PAGE_SIZE,
                  PAGE_SIZE, 0, 
                  VM_ATTR_WRITABLE, 
                  NULL);

    /* Clear memory */
    memset(th, 0, sizeof(sched_thread_t));

    th->prio         = prio;
    th->arg          = arg;
    th->entry_point  = entry_pt;
    th->stack_sz     = stack_sz;
    th->flags        = THREAD_READY;
    th->owner        = owner;

    /* we skip the guard page */
    th->stack_origin = stack_origin + PAGE_SIZE;
    
    /* Initialize the platform context */
    ret = context_init(th);

    if(ret < 0)
    {
        vm_free(ow->vm_ctx, stack_origin, stack_size);
        return(-1);
    }

    if(affiity == NULL)
    {
        /* make the thread available to all cpus */
        memset(&th->affinity, 0xff, sizeof(cpu_aff_t));
    }
    else
    {
        memcpy(&th->affinity, affiity, sizeof(cpu_aff_t));
    }

    /* Add the thread to the owner */
    owner_add_thread(owner, out_th);

    /* Initialize spinlock */
    spinlock_init(&th->lock);

    return(0);
}


int thread_start
(
    sched_thread_t *th
)
{
    sched_start_thread(th);
}

int thread_create_static
(
    void *out_th,
    th_entry_point_t entry_pt,
    void *arg,
    size_t stack_sz,
    uint32_t prio,
    cpu_aff_t *affinity,
    void *owner
)
{
    int ret = -1;
    sched_thread_t *th = NULL;

    /* do some sanity checks */
    if((out_th   == NULL) || 
       (owner    == NULL) || 
       (entry_pt == NULL))
    {
        return(ret);
    }

    ret = thread_setup(out_th, 
                       entry_pt, 
                       arg, 
                       stack_sz, 
                       prio,
                       affinity,
                       owner);
    
    th = out_th;
}

void *thread_create
(
    th_entry_point_t entry_pt,
    void      *arg,
    size_t    stack_sz,
    uint32_t  prio,
    cpu_aff_t *affinity,
    void      *owner
)
{
    sched_thread_t *th = NULL;
    int status = 0;

    if((owner == NULL) || (entry_pt == NULL))
    {
        return(NULL);
    }

    th = kcalloc(sizeof(sched_thread_t), 1);

    if(th == NULL)
    {
        return(NULL);
    } 
    
    status = thread_setup(th, 
                          entry_pt,
                          arg,
                          stack_sz,
                          prio,
                          affinity,
                          owner);

    if(status < 0)
    {
        kfree(th);
        th = NULL;
    }

    return(th);
}


void *kthread_create
(
    th_entry_point_t entry_pt,
    void *arg,
    size_t    stack_sz,
    uint32_t  prio,
    cpu_aff_t *affinity
)
{
    sched_owner_t *ko = NULL;
    void *th_ret = NULL;

    ko = owner_kernel_get();

    th_ret = thread_create(entry_pt,
                           arg,
                           stack_sz,
                           prio,
                           affinity,
                           ko);

    return(th_ret);
}

int kthread_create_static
(
    void *out_th,
    th_entry_point_t entry_pt,
    void *arg,
    size_t    stack_sz,
    uint32_t  prio,
    cpu_aff_t *affinity
)
{
    int th_ret = 0;
    sched_owner_t *ko = NULL;
    
    ko = owner_kernel_get();

    th_ret = thread_create_static(out_th,
                                  entry_pt,
                                  arg,
                                  stack_sz,
                                  prio,
                                  affinity,
                                  ko);

    return(th_ret);
}