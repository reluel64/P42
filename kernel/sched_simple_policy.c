/* 
 * Stupidly simple scheduler policy 
 * Part of P42
 */

#include <scheduler.h>
#include <utils.h>
#include <linked_list.h>
#include <spinlock.h>

static int simple_next_thread
(
    list_head_t *new_th_list,
    spinlock_t *new_th_lock,
    sched_exec_unit_t *unit,
    sched_thread_t **next
)
{
    list_node_t *th        = NULL;
    list_node_t *next_th   = NULL;
    sched_thread_t *thread = NULL;
    
    /* Check if we have threads that need first execution 
     * and if there are, add one of them to the ready queue
     */
    spinlock_lock_int(new_th_lock);

    th = linked_list_first(new_th_list);

    if(th)
    {
        linked_list_remove(new_th_list, th);
        linked_list_add_tail(&unit->ready_q, th);
    }

    spinlock_unlock_int(new_th_lock);

    /* Check blocked threads */

    th = linked_list_first(&unit->blocked_q);

    if(__atomic_fetch_and(&unit->flags, 
                          ~UNIT_THREADS_UNBLOCK, 
                         __ATOMIC_SEQ_CST) & 
                         UNIT_THREADS_UNBLOCK)

    {
        while(th)
        {
            next_th = linked_list_next(th);

            thread = NODE_TO_THREAD(th);

            if(!(__atomic_load_n(&thread->flags, __ATOMIC_SEQ_CST) & 
                THREAD_BLOCKED))
            {
                linked_list_remove(&unit->blocked_q, th);
                linked_list_add_tail(&unit->ready_q, th);
            }

            th = next_th;
        }
    }
    /* Check sleeping threads */

    if(__atomic_fetch_and(&unit->flags, 
                          ~UNIT_THREADS_WAKE,
                          __ATOMIC_SEQ_CST) & 
                          UNIT_THREADS_WAKE)
    {
        
        th = linked_list_first(&unit->sleep_q);

        while(th)
        {
            next_th = linked_list_next(th);

            thread = NODE_TO_THREAD(th);

            if(!(__atomic_load_n(&thread->flags, __ATOMIC_SEQ_CST) & 
                THREAD_SLEEPING))
            {

                linked_list_remove(&unit->sleep_q, th);
                linked_list_add_tail(&unit->ready_q, th);
            }

            th = next_th;
        }
    }

    
    th = linked_list_first(&unit->ready_q);

    if(th == NULL)
    {
        return(-1);
    }

    linked_list_remove(&unit->ready_q, th);

    *next = NODE_TO_THREAD(th);

    return(0);

}

static int simple_put_thread
(
    sched_thread_t *th
)
{
    sched_exec_unit_t *unit = NULL;
    list_head_t       *lh = NULL;
    uint32_t          state = 0;

    unit = th->unit;

    __atomic_and_fetch(&th->flags, ~THREAD_NEED_RESCHEDULE, __ATOMIC_SEQ_CST);

    /* The idle thread dioes not belong to any queue */
    if(&unit->idle == th)
        return(0);

   state = __atomic_load_n(&th->flags, __ATOMIC_SEQ_CST) & THREAD_STATE_MASK;

    switch(state)
    {
        case THREAD_BLOCKED:
            lh = &unit->blocked_q;
            break;

        case THREAD_SLEEPING:
            lh = &unit->sleep_q;
            break;

        case THREAD_READY:
            lh = &unit->ready_q;
            break;
        default:
            kprintf("%s %d\n",__FUNCTION__, __LINE__);
            while(1);
            break;
    }

    /* Add the thread in the corresponding queue */

    linked_list_add_tail(lh, &th->node);

    return(0);

}

static int simple_update_time
(
    sched_thread_t *th
)
{
    if(th->remain > 1)
    {
        th->remain--;
    }
    else
    {
        __atomic_or_fetch(&th->flags, 
                          THREAD_NEED_RESCHEDULE, 
                          __ATOMIC_SEQ_CST);

          th->remain = 255 - th->prio;
    }

    return(0);
}

static sched_policy_t policy = 
{
    .policy_name = "Simple",
    .next_thread = simple_next_thread,
    .put_thread  = simple_put_thread ,
    .update_time = simple_update_time,
    .load_balancing = NULL,
};


int sched_simple_register(sched_policy_t **p)
{
    *p = &policy;
    return(0);
}
