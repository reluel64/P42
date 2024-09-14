#include <vm.h>
#include <liballoc.h>
#include <linked_list.h>
#include <sched.h>
#include <mutex.h>
#include <utils.h>


mutex_t *mtx_init
(
    mutex_t *mtx, 
    int options
)
{
    if(mtx == NULL || 
      ((options & MUTEX_TASK_ORDER) == MUTEX_TASK_ORDER))
    {
        return(NULL);
    }
    
    mtx->opts = options;
    mtx->rlevel = 0;
    mtx->owner = 0;
    mtx->owner_prio = 0;
    
    linked_list_init(&mtx->pendq);
    spinlock_init(&mtx->lock);

    return(mtx);
}

mutex_t *mtx_create
(
    int options
)
{
    mutex_t *mtx = NULL;
    mutex_t *ret_mtx = NULL;

    mtx = kcalloc(1, sizeof(mutex_t));

    if(mtx == NULL)
    {
        return(NULL);
    }

    ret_mtx = mtx_init(mtx, options);

    if(ret_mtx != mtx)
    {
        kfree(mtx);
    }

    return(ret_mtx);
}

int mtx_acquire
(
    mutex_t *mtx, 
    uint32_t wait_ms
)
{
    uint8_t         int_state    = 0;
    void           *expected     = NULL;
    sched_thread_t *thread       = NULL;
    uint8_t         looped       = 0;
    list_node_t     *iter_node   = NULL;
    sched_thread_t  *iter_thread = NULL;

    if(mtx == NULL)
    {
        return(-1);
    }

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
                                       __ATOMIC_SEQ_CST,
                                       __ATOMIC_SEQ_CST
                                       ))
        {
            
            /* set recursion level to 1 */
            __atomic_store_n(&mtx->rlevel, 1, __ATOMIC_RELEASE);

            spinlock_unlock_int(&mtx->lock, int_state);

            return(0);
        }

        expected = thread;

        /* Check if we already own the mutex */
        if(__atomic_compare_exchange_n(&mtx->owner,
                                       &expected,
                                       thread,
                                       0,
                                       __ATOMIC_SEQ_CST,
                                       __ATOMIC_SEQ_CST
                                       ))
        {
            /* if we are already the owner, increase the recursion level */
            
            if(mtx->opts & MUTEX_RECUSRIVE)
            {
                __atomic_add_fetch(&mtx->rlevel, 1, __ATOMIC_SEQ_CST);
            }
            spinlock_unlock_int(&mtx->lock, int_state);
            return(0);
        }

        /* if we looped already, then we timed out in case we had a timer
         * therefore, just exit
         */
        if(looped > 0)
        {
            spinlock_unlock_int(&mtx->lock, int_state);
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
            spinlock_unlock_int(&mtx->lock, int_state);
            return(-1);
        }

        /* Add it to the mutex pend queue */
#ifdef MTX_DEBUG
        kprintf("BLOCKING %x\n",thread);
#endif

        /* insert the mutex in the queue based on either
         * FIFO or Priority
         */
        if(mtx->opts & MUTEX_FIFO)
        {
            linked_list_add_tail(&mtx->pendq, &thread->pend_node);
        }
        else
        {
            iter_node = linked_list_first(&mtx->pendq);
            
            while(iter_node)
            {
                iter_thread = PEND_NODE_TO_THREAD(iter_node);

                if(iter_thread->prio > thread->prio)
                {
                    linked_list_add_before(&mtx->pendq, 
                                           iter_node, 
                                           &thread->pend_node);
                    break;
                }

                iter_node = linked_list_next(iter_node);
            }

            if(iter_node == NULL)
            {
                linked_list_add_tail(&mtx->pendq, &thread->pend_node);
            }
        }

        spinlock_unlock_int(&mtx->lock, int_state);
        
        /* sleep */
        sched_sleep(wait_ms);

        spinlock_lock_int(&mtx->lock, &int_state);
        linked_list_remove(&mtx->pendq, &thread->pend_node);
    }

    return(0);
}

int mtx_release
(
    mutex_t *mtx
)
{
    uint8_t           int_state  = 0;
    sched_thread_t    *self      = NULL;
    sched_thread_t    *thread    = NULL;
    sched_exec_unit_t *unit      = NULL;
    list_node_t       *pend_node = NULL;
    void              *expected  = NULL;

    if(mtx == NULL)
    {
        return(-1);
    }

    spinlock_lock_int(&mtx->lock, &int_state);

    self = sched_thread_self();

    expected = self;

    /* If we are not the owner, then get out */
    if(!__atomic_compare_exchange_n(&mtx->owner,
                                       &expected,
                                       0,
                                       0,
                                       __ATOMIC_SEQ_CST,
                                       __ATOMIC_SEQ_CST
                                       ))
    {
        spinlock_unlock_int(&mtx->lock, int_state);
        return(-1);
    }

    if(mtx->opts & MUTEX_RECUSRIVE)
    {
        /* reduce the recursion level */
        if(__atomic_sub_fetch(&mtx->rlevel, 1, __ATOMIC_SEQ_CST) > 0)
        {
            spinlock_unlock_int(&mtx->lock, int_state);
            return(0);
        }
    }

    /* Get the first pending task */
    pend_node = linked_list_first(&mtx->pendq);

    if(pend_node == NULL)
    {
        /* if we don't have a new owner, clear oursevles */
        __atomic_clear(&mtx->owner, __ATOMIC_SEQ_CST);
        spinlock_unlock_int(&mtx->lock, int_state);
        return(0);
    }

    thread = PEND_NODE_TO_THREAD(pend_node);

    /* Set the new owner */
    __atomic_store_n(&mtx->owner, thread, __ATOMIC_SEQ_CST);

    sched_wake_thread(thread);

    spinlock_unlock_int(&mtx->lock, int_state);

    return(0);
}
