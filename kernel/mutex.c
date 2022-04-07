#include <vm.h>
#include <liballoc.h>
#include <linked_list.h>
#include <scheduler.h>
#include <mutex.h>
#include <utils.h>

#define PEND_NODE_TO_THREAD(x) ((sched_thread_t*) \
                                ((uint8_t*)(x) -  \
                                offsetof(sched_thread_t, pend_node)))

mutex_t *mtx_init(mutex_t *mtx, int options)
{
    if(mtx == NULL)
        return(NULL);

    mtx->opts = options;

    __atomic_store_n(&mtx->rlevel, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&mtx->owner, 0, __ATOMIC_RELEASE);
    linked_list_init(&mtx->pendq);
    spinlock_init(&mtx->lock);

    return(mtx);
}


mutex_t *mtx_create(int options)
{
    mutex_t *mtx = NULL;
    mutex_t *ret_mtx = NULL;

    mtx = kcalloc(1, sizeof(mutex_t));

    if(mtx == NULL)
        return(NULL);

    ret_mtx = mtx_init(mtx, options);

    if(ret_mtx != mtx)
            kfree(mtx);

    return(ret_mtx);
}

int mtx_acquire(mutex_t *mtx, uint32_t wait_ms)
{
    int int_state = 0;
    int th_int_state = 0;
    void           *expected = NULL;
    sched_thread_t *thread = NULL;
    uint32_t block_flags = 0;

    spinlock_lock_int(&mtx->lock);
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
            /* set recursion level to 1 */
            __atomic_store_n(&mtx->rlevel, 1, __ATOMIC_RELEASE);

            spinlock_unlock_int(&mtx->lock);

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
            /* if we are already the owner, increase the recursion level */
            
            if(mtx->opts & MUTEX_RECUSRIVE)
                __atomic_add_fetch(&mtx->rlevel, 1, __ATOMIC_ACQUIRE);

            spinlock_unlock_int(&mtx->lock);
            return(0);
        }


        /* if we the thread has the sleeping flag set,
         * then we already timed out so we will just exit
         */

        if(block_flags == THREAD_SLEEPING)
        {
            /* remove the thread from the pendq */
            spinlock_unlock_int(&mtx->lock);
            return(-1);
        }

        if(wait_ms == NO_WAIT)
        {
            spinlock_unlock_int(&mtx->lock);
            return(-1);
        }

        thread->to_sleep = wait_ms;

        /* we are going to sleep */
        if(wait_ms != WAIT_FOREVER)
        {
            /* Timeout specified - sleep the thread */
            block_flags = THREAD_SLEEPING;
            sched_sleep_thread(thread, wait_ms);
        }
        else
        {
            /* Mark the thread as blocked */
            block_flags = THREAD_BLOCKED;
            sched_block_thread(thread);
        }

        /* Add it to the mutex pend queue */
        kprintf("BLOCKING %x\n",thread);
        linked_list_add_tail(&mtx->pendq, &thread->pend_node);

        spinlock_unlock_int(&mtx->lock);

        /* Catch the attention of the scheduler */
        sched_yield();

        spinlock_lock_int(&mtx->lock);
        linked_list_remove(&mtx->pendq, &thread->pend_node);
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

    spinlock_lock_int(&mtx->lock);

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
        spinlock_unlock_int(&mtx->lock);
        return(-1);
    }

    if(mtx->opts & MUTEX_RECUSRIVE)
    {
        /* reduce the recursion level */
        if(__atomic_sub_fetch(&mtx->rlevel, 1, __ATOMIC_RELEASE) > 0)
        {
            spinlock_unlock_int(&mtx->lock);
            return(0);
        }
    }

    /* Get the first pending task */
    pend_node = linked_list_first(&mtx->pendq);

    if(pend_node == NULL)
    {
        /* if we don't have a new owner, clear oursevles */
        __atomic_clear(&mtx->owner, __ATOMIC_RELEASE);
        spinlock_unlock_int(&mtx->lock);
        return(0);
    }

    thread = PEND_NODE_TO_THREAD(pend_node);

    /* Set the new owner */
    __atomic_store_n(&mtx->owner, thread, __ATOMIC_RELEASE);

    /* Wake up the thread */
    if(thread->flags & THREAD_BLOCKED)
        sched_unblock_thread(thread);
    else
        sched_wake_thread(thread);

    spinlock_unlock_int(&mtx->lock);

    return(0);
}
