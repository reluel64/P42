#include <vmmgr.h>
#include <liballoc.h>
#include <linked_list.h>
#include <scheduler.h>
#include <mutex.h>

#define PEND_NODE_TO_THREAD(x) ((sched_thread_t*) \
                                ((uint8_t*)(x) -  \
                                offsetof(sched_thread_t, pend_node)))

mutex_t *mtx_create()
{
    mutex_t *mtx = NULL;

    mtx = kcalloc(1, sizeof(mutex_t));

    if(mtx == NULL)
        return(NULL);
    
    __atomic_store_n(&mtx->rlevel, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&mtx->owner, 0, __ATOMIC_RELEASE);
    linked_list_init(&mtx->pendq);
    spinlock_init(&mtx->lock);

    return(mtx);
}

int mtx_acquire(mutex_t *mtx, uint32_t wait_ms)
{
    int int_state = 0;
    int th_int_state = 0;
    void           *expected = NULL;
    sched_thread_t *thread = NULL;
    uint32_t block_flags = THREAD_BLOCKED;

    spinlock_lock_int(&mtx->lock, &int_state);
    thread = sched_thread_self();


    while(1)
    {


        expected = NULL;

        /* Try to become the owner of the mutex*/
        if(__atomic_compare_exchange_n(&mtx->owner, 
                                       &expected, 
                                       thread, 
                                       0, 
                                       __ATOMIC_ACQUIRE, 
                                       __ATOMIC_RELAXED
                                       ))
        {
            spinlock_unlock_int(&mtx->lock, int_state);
            return(0);
        }

        expected = thread;
        
        /* Check if we already own the mutex */
        if(__atomic_compare_exchange_n(&mtx->owner, 
                                       &expected, 
                                       thread, 
                                       0, 
                                       __ATOMIC_ACQUIRE, 
                                       __ATOMIC_RELAXED
                                       ))
        {
            spinlock_unlock_int(&mtx->lock, int_state);
            return(0);
        }

        /* if we the thread has the sleeping flag set,
         * then we already timed out so we will just exit 
         */

        if(block_flags & THREAD_SLEEPING)
        {
            /* remove the thread from the pendq */
            linked_list_remove(&mtx->pendq, &thread->pend_node);
            spinlock_unlock_int(&mtx->lock, int_state);
            return(-1);
        }

        if(wait_ms == NO_WAIT)
        {
            spinlock_unlock_int(&mtx->lock, int_state);
            return(-1);
        }

        spinlock_lock_int(&thread->lock, &th_int_state);

        /* we are going to sleep */

        if(wait_ms != WAIT_FOREVER)
        {
            block_flags |= THREAD_SLEEPING;
            thread->to_sleep = wait_ms;
            thread->slept = 0;
        }
         /* Mark the thread as blocked */
        
        __atomic_or_fetch(&thread->flags, block_flags, __ATOMIC_ACQUIRE);

        /* Add it to the mutex pend queue */

        linked_list_add_tail(&mtx->pendq, &thread->pend_node); 

        spinlock_unlock_int(&thread->lock, th_int_state);

        spinlock_unlock_int(&mtx->lock, int_state);

        sched_yield();

        spinlock_lock_int(&mtx->lock, &int_state);

    }

    return(0);
}

int mtx_release(mutex_t *mtx)
{
    int int_state = 0;
    int unit_int_state = 0;
    sched_thread_t *self = NULL;
    sched_thread_t *thread = NULL;
    sched_exec_unit_t *unit = NULL;
    list_node_t    *pend_node = NULL;
    void *expected = NULL;

    spinlock_lock_int(&mtx->lock, &int_state);

    self = sched_thread_self();
    
    expected = self;

    /* If we are not the owner, then get out */
    if(!__atomic_compare_exchange_n(&mtx->owner, 
                                       &expected, 
                                       0, 
                                       0, 
                                       __ATOMIC_ACQUIRE, 
                                       __ATOMIC_RELAXED
                                       ))
    {
        spinlock_unlock_int(&mtx->lock, int_state);
        return(-1);
    }

    /* Get the first pending task */
    pend_node = linked_list_first(&mtx->pendq);

    if(pend_node == NULL)
    {
        spinlock_unlock_int(&mtx->lock, int_state);
        return(0);
    }


    thread = PEND_NODE_TO_THREAD(pend_node);

    linked_list_remove(&mtx->pendq, pend_node);
    
    /* Set the new owner */
    __atomic_store_n(&mtx->owner, thread, __ATOMIC_RELEASE);
    
    spinlock_unlock_int(&thread->lock, unit_int_state);

    sched_unblock_thread(thread);

    spinlock_unlock_int(&mtx->lock, int_state);
    
    return(0);
}