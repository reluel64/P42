#include <vm.h>
#include <liballoc.h>
#include <linked_list.h>
#include <scheduler.h>
#include <semaphore.h>

sem_t *sem_init(sem_t *sem, uint32_t init_val, uint32_t max_count)
{
    if(sem == NULL)
    {
        return(NULL);
    }
    
    sem->max_count = max_count;

    __atomic_store_n(&sem->count, init_val, __ATOMIC_SEQ_CST);

    linked_list_init(&sem->pendq);

    spinlock_init(&sem->lock);

    return(sem);
}

sem_t *sem_create(uint32_t init_val, uint32_t max_count)
{
    sem_t *sem = NULL;
    sem_t *ret_sem = NULL;

    sem = kcalloc(1, sizeof(sem_t));

    ret_sem = sem_init(sem, init_val, max_count);

    /* check if we've failed to set up the semaphore */
    if(ret_sem != sem)
    {
        kfree(sem);
    }

    return(ret_sem);
}

int sem_acquire(sem_t *sem, uint32_t wait_ms)
{
    uint8_t         int_state = 0;
    sched_thread_t *thread = NULL;
    uint32_t        looped = 0;


    spinlock_lock_int(&sem->lock, &int_state);

    /* If we have the semaphore full, allow the task to continue 
     * Otherwise, we will need to block the task
     */
    
    if(__atomic_load_n(&sem->count, __ATOMIC_SEQ_CST) > 0)
    {
        __atomic_sub_fetch(&sem->count, 1, __ATOMIC_SEQ_CST);
        spinlock_unlock_int(&sem->lock, int_state);
        return(0);
    }

    thread = sched_thread_self();

    /* We will be able to acquire the semaphore here */
    while(__atomic_load_n(&sem->count, __ATOMIC_SEQ_CST) < 1)
    {
        /* if we looped already, then we timed out in case we had a timer
         * therefore, just exit
         */
        if(looped > 0)
        {
            spinlock_unlock_int(&sem->lock, int_state);
            return(-1);
        }

        /* if we are not waiting forever, set the looped flag so
         * we are  exiting on the next iteration
         */
        if(wait_ms != WAIT_FOREVER)
        {
            looped = 1;
        }

        if(wait_ms == NO_WAIT)
        {
            spinlock_unlock_int(&sem->lock, int_state);
            return(-1);
        }

        /* Add it to the semaphore pend queue */
        linked_list_add_tail(&sem->pendq, &thread->pend_node); 

        /* release the semaphore spinlock */
        spinlock_unlock_int(&sem->lock, int_state);

        /* sleep */
        sched_sleep(wait_ms);

        /* once the thread is woken up, it would lock again the semaphore */
        spinlock_lock_int(&sem->lock, &int_state);
        linked_list_remove(&sem->pendq, &thread->pend_node);
    }
    
    __atomic_sub_fetch(&sem->count, 1, __ATOMIC_SEQ_CST);
    
    spinlock_unlock_int(&sem->lock, int_state);

    return(0);
}

int sem_release(sem_t *sem)
{
    uint8_t           int_state  = 0;
    sched_thread_t    *thread    = NULL;
    sched_exec_unit_t *unit      = NULL;
    list_node_t       *pend_node = NULL;
    
    spinlock_lock_int(&sem->lock, &int_state);

    if(__atomic_load_n(&sem->count, __ATOMIC_SEQ_CST) < sem->max_count)
    {
        __atomic_add_fetch(&sem->count, 1, __ATOMIC_SEQ_CST);
    }

    /* Get the first pending task */
    pend_node = linked_list_first(&sem->pendq);

    if(pend_node == NULL)
    {
        spinlock_unlock_int(&sem->lock, int_state);
        return(0);
    }

    thread = PEND_NODE_TO_THREAD(pend_node);

    sched_wake_thread(thread);
    
    spinlock_unlock_int(&sem->lock, int_state);

    return(0);
}