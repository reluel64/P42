#include <vm.h>
#include <liballoc.h>
#include <linked_list.h>
#include <scheduler.h>
#include <semaphore.h>

#define PEND_NODE_TO_THREAD(x) ((sched_thread_t*) \
                                ((uint8_t*)(x) -  \
                                offsetof(sched_thread_t, pend_node)))

sem_t *sem_init(sem_t *sem, uint32_t init_val, uint32_t max_count)
{

    if(sem == NULL)
        return(NULL);

    sem->max_count = max_count;

    __atomic_store_n(&sem->count, init_val, __ATOMIC_RELEASE);

    linked_list_init(&sem->pendq);

    spinlock_init(&sem->lock);

    return(sem);
}

sem_t *sem_create(uint32_t init_val, uint32_t max_count)
{
    sem_t *sem = NULL;

    sem = kcalloc(1, sizeof(sem_t));

    return(sem_init(sem, init_val, max_count));
}



int sem_acquire(sem_t *sem, uint32_t wait_ms)
{
    int int_state = 0;
    int th_int_state = 0;
    sched_thread_t *thread = NULL;
    uint32_t        block_flags = THREAD_BLOCKED;


    spinlock_lock_int(&sem->lock, &int_state);

    /* If we have the semaphore full, allow the task to continue 
     * Otherwise, we will need to block the task
     */
    
    if(__atomic_load_n(&sem->count, __ATOMIC_ACQUIRE) > 0)
    {
        __atomic_sub_fetch(&sem->count, 1, __ATOMIC_ACQUIRE);
        spinlock_unlock_int(&sem->lock, int_state);
        return(0);
    }

    thread = sched_thread_self();

    /* We will be able to acquire the semaphore here */
    while(__atomic_load_n(&sem->count, __ATOMIC_ACQUIRE) < 1)
    {
        
        if(block_flags & THREAD_SLEEPING)
        {
            /* remove the thread from the pendq */
            /*linked_list_remove(&sem->pendq, &thread->pend_node);*/
            spinlock_unlock_int(&sem->lock, int_state);
            return(-1);
        }

        if(wait_ms == NO_WAIT)
        {
            spinlock_unlock_int(&sem->lock, int_state);
            return(-1);
        }

        /* Acquire spinlock for the thread */
        spinlock_lock_int(&thread->lock, &th_int_state);

        if(wait_ms != WAIT_FOREVER)
        {
            block_flags |= THREAD_SLEEPING;
            thread->to_sleep = wait_ms;
            thread->slept = 0;
        }

         /* Mark the thread as blocked */
        __atomic_or_fetch(&thread->flags, block_flags, __ATOMIC_ACQUIRE);

        /* Add it to the semaphore pend queue */
        linked_list_add_tail(&sem->pendq, &thread->pend_node); 

        /* Release the spinlock of the thread */
        
        spinlock_unlock_int(&thread->lock, th_int_state);

        /* release the semaphore spinlock */
        spinlock_unlock_int(&sem->lock, int_state);

        /* suspend the thread */
        sched_yield();

        /* once the thread is woken up, it would lock again the semaphore */
        spinlock_lock_int(&sem->lock, &int_state);
        linked_list_remove(&sem->pendq, &thread->pend_node);
    }
    
    __atomic_sub_fetch(&sem->count, 1, __ATOMIC_ACQUIRE);
    
    spinlock_unlock_int(&sem->lock, int_state);

    return(0);
}

int sem_release(sem_t *sem)
{
    int int_state = 0;
    int unit_int_state = 0;

    sched_thread_t *thread = NULL;
    sched_exec_unit_t *unit = NULL;
    list_node_t    *pend_node = NULL;
    
    spinlock_lock_int(&sem->lock, &int_state);

    if(__atomic_load_n(&sem->count, __ATOMIC_ACQUIRE) < sem->max_count)
        __atomic_add_fetch(&sem->count, 1, __ATOMIC_RELEASE);
    

    /* Get the first pending task */
    pend_node = linked_list_first(&sem->pendq);

    if(pend_node == NULL)
    {
        spinlock_unlock_int(&sem->lock, int_state);
        return(0);
    }

    thread = PEND_NODE_TO_THREAD(pend_node);
   
    spinlock_lock_int(&thread->lock, &unit_int_state);
    
    
    spinlock_unlock_int(&thread->lock, unit_int_state);

    sched_unblock_thread(thread);

    spinlock_unlock_int(&sem->lock, int_state);

    return(0);
}