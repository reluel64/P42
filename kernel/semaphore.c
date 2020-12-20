#include <vmmgr.h>
#include <liballoc.h>
#include <linked_list.h>
#include <scheduler.h>
#include <semaphore.h>


semb_t *sem_create(int init_val)
{
    semb_t *sem = NULL;

    sem = kcalloc(1, sizeof(semb_t));

    if(sem == NULL)
        return(NULL);
    
    sem->flag = 1;
    linked_list_init(&sem->pendq);
    spinlock_init(&sem->lock);
    return(sem);
}

static int sem_acquire(semb_t *sem)
{
    while(__sync_bool_compare_and_swap(&sem->flag, 1, 0))
    {
        return(0);
    }
}

int semb_wait(semb_t *sem)
{
    int int_state = 0;
    int th_int_state = 0;
    sched_thread_t *thread = NULL;

    spinlock_lock_interrupt(&sem->lock, &int_state);

    /* If we have the semaphore full, allow the task to continue 
     * Otherwise, we will need to block the task
     */
    if(__sync_bool_compare_and_swap(&sem->flag, 1, 0))
    {
        spinlock_unlock_interrupt(&sem->lock, int_state);
        return(0);
    }

    thread = sched_thread_self();

    spinlock_lock_interrupt(&thread->lock, &th_int_state);
    
    thread->flags = THREAD_BLOCKED;
    linked_list_add_tail(&sem->pendq, &thread->pend_node);
    spinlock_unlock_interrupt(&thread->lock, th_int_state);
    
    spinlock_unlock_interrupt(&sem->lock, int_state);

    /* We will be able to acquire the semaphore here */
    while(!__sync_bool_compare_and_swap(&sem->flag, 1, 0))
    {
        sched_yield();
    }

    return(0);
}

int semb_give(semb_t *sem)
{
    int int_state = 0;
    int unit_int_state = 0;

    sched_thread_t *thread = NULL;
    sched_exec_unit_t *unit = NULL;
    list_node_t    *pend_node = NULL;
    
    spinlock_lock_interrupt(&sem->lock, &int_state);

    pend_node = linked_list_first(&sem->pendq);
    
    thread = (sched_thread_t*)((uint8_t*)pend_node - offsetof(sched_thread_t, pend_node));
    
    __sync_bool_compare_and_swap(&sem->flag, 0, 1);

    sched_unblock_thread(thread);

    spinlock_unlock_interrupt(&sem->lock, int_state);

    return(0);
}