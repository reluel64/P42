#include <vmmgr.h>
#include <liballoc.h>
#include <linked_list.h>
#include <scheduler.h>
#include <semaphore.h>

#define PEND_NODE_TO_THREAD(x) ((sched_thread_t*) \
                                ((uint8_t*)(x) -  \
                                offsetof(sched_thread_t, pend_node)))

semaphore_t *sem_create(uint32_t init_val)
{
    semaphore_t *sem = NULL;

    sem = kcalloc(1, sizeof(semaphore_t));

    if(sem == NULL)
        return(NULL);
    
    __atomic_store_n(&sem->count, init_val, __ATOMIC_RELEASE);
    linked_list_init(&sem->pendq);
    spinlock_init(&sem->lock);
    return(sem);
}

int sem_acquire(semaphore_t *sem)
{
    int int_state = 0;
    int th_int_state = 0;
    sched_thread_t *thread = NULL;

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
        
        /* Acquire spinlock for the thread */
         spinlock_lock_int(&thread->lock, &th_int_state);

         /* Mark the thread as blocked */
        __atomic_or_fetch(&thread->flags, THREAD_BLOCKED, __ATOMIC_ACQUIRE);

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
    }
    
    __atomic_sub_fetch(&sem->count, 1, __ATOMIC_ACQUIRE);
    
    spinlock_unlock_int(&sem->lock, int_state);

    return(0);
}

int sem_release(semaphore_t *sem)
{
    int int_state = 0;
    int unit_int_state = 0;

    sched_thread_t *thread = NULL;
    sched_exec_unit_t *unit = NULL;
    list_node_t    *pend_node = NULL;
    
    spinlock_lock_int(&sem->lock, &int_state);

    /* Get the first pending task */
    pend_node = linked_list_first(&sem->pendq);

    if(pend_node == NULL)
    {
        spinlock_unlock_int(&sem->lock, int_state);
        return(0);
    }

    thread = PEND_NODE_TO_THREAD(pend_node);
   
    spinlock_lock_int(&thread->lock, &unit_int_state);
    linked_list_remove(&sem->pendq, pend_node);
    
    __atomic_add_fetch(&sem->count, 1, __ATOMIC_RELAXED);
    spinlock_unlock_int(&thread->lock, unit_int_state);

    sched_unblock_thread(thread);

    spinlock_unlock_int(&sem->lock, int_state);

    return(0);
}