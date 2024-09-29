#include <sched.h>
#include <liballoc.h>
#include <vm.h>
#include <utils.h>


extern vm_ctx_t      vm_kernel_ctx;

static list_head_t owners;
static spinlock_t lock = SPINLOCK_INIT;
static sched_owner_t kernel_owner;


int _owner_setup
(
    void *owner
)
{
    sched_owner_t *o = NULL;

    o = owner;

    linked_list_init(&o->threads);

    return(0);
}

int owner_kernel_init
(
    void
)
{
    uint8_t int_status = 0;

    sched_owner_t *o = &kernel_owner;
    
    memset(o, 0, sizeof(sched_owner_t));

    linked_list_init(&o->threads);

    o->user = 0;
    o->owner_id = 0;
    o->vm_ctx = &vm_kernel_ctx;
    
    spinlock_lock_int(&lock, &int_status);

    linked_list_add_head(&owners, &o->node);

    spinlock_unlock_int(&lock, int_status);

    return(0);
}

int owner_add_thread
(
    void *owner,
    void *thread
)
{
    int status = -1;
    uint8_t int_status = 0;
    sched_owner_t *ow = NULL;
    sched_thread_t *th = NULL;

    if((owner == NULL) || (thread == NULL))
    {
        return(status);
    }

    ow = owner;
    th = thread;
    
    spinlock_lock_int(&ow->th_lst_lock, &int_status);

    if(linked_list_find_node(&ow->threads, &th->owner_node) < 0)
    {
        linked_list_add_tail(&ow->threads, &th->owner_node);
        status = 0;
    }

    spinlock_unlock_int(&ow->th_lst_lock, int_status);

    return(status);

}

int owner_remove_thread
(
    void *owner,
    void *thread
)
{
    int status = -1;
    uint8_t int_status = 0;
    sched_owner_t *ow = NULL;
    sched_thread_t *th = NULL;

    if((owner == NULL) || (thread == NULL))
    {
        return(status);
    }

    ow = owner;
    th = thread;
    
    spinlock_lock_int(&ow->th_lst_lock, &int_status);

    if(linked_list_find_node(&ow->threads, &th->owner_node) == 0)
    {
        linked_list_remove(&ow->threads, &th->owner_node);
        status = 0;
    }

    spinlock_unlock_int(&ow->th_lst_lock, int_status);

    return(status);

}

sched_owner_t *owner_kernel_get
(
    void
)
{
    return(&kernel_owner);
}