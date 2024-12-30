#include <sched.h>
#include <liballoc.h>
#include <vm.h>
#include <utils.h>


extern struct vm_ctx      vm_kernel_ctx;

static struct list_head owners;
static struct spinlock lock = SPINLOCK_INIT;
static struct sched_owner kernel_owner;


int _owner_setup
(
    void *owner
)
{
    struct sched_owner *o = NULL;

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

    struct sched_owner *o = &kernel_owner;
    
    memset(o, 0, sizeof(struct sched_owner));

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
    struct sched_owner *ow = NULL;
    struct sched_thread *th = NULL;

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
    struct sched_owner *ow = NULL;
    struct sched_thread *th = NULL;

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

struct sched_owner *owner_kernel_get
(
    void
)
{
    return(&kernel_owner);
}