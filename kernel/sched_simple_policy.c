/* 
 * Stupidly simple scheduler policy 
 * Part of P42
 */

#include <scheduler.h>
#include <utils.h>
#include <linked_list.h>
#include <spinlock.h>
#include <liballoc.h>
#include <intc.h>

typedef struct simple_policy_unit_t
{
    sched_exec_unit_t *unit;        /* execution unit that holds this policy */

}simple_policy_unit_t;

static int simple_next_thread
(
    void *policy_data,
    sched_thread_t **next
)
{
    list_node_t *th        = NULL;
    list_node_t *next_th   = NULL;
    sched_thread_t *thread = NULL;
    simple_policy_unit_t  *policy_unit = NULL;
    sched_exec_unit_t     *unit = NULL;

    policy_unit = policy_data;
      
    unit = policy_unit->unit;

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

    thread = NODE_TO_THREAD(th);

    /* Mark the next thread as running */
    __atomic_or_fetch(&thread->flags, 
                      THREAD_RUNNING, 
                      __ATOMIC_SEQ_CST);

    *next = thread;

    return(0);

}

static int simple_put_thread
(
    void *policy_data,
    sched_thread_t *th
)
{
    list_head_t       *lh = NULL;
    uint32_t          state = 0;
    simple_policy_unit_t  *policy_unit = NULL;
    sched_exec_unit_t     *unit = NULL;

    if(th == NULL || policy_data == NULL)
    {
        return(-1);
    }

    policy_unit = policy_data;
    unit = policy_unit->unit;
    
    __atomic_and_fetch(&th->flags, ~THREAD_NEED_RESCHEDULE, __ATOMIC_SEQ_CST);

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

        case THREAD_NEW:
             __atomic_and_fetch(&th->flags, ~THREAD_NEW, __ATOMIC_SEQ_CST);
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

static int simple_init
(
    sched_exec_unit_t *unit
)
{
    simple_policy_unit_t *pd = NULL;
    
    if(unit == NULL)
    {
        return(-1);
    }

    if(unit->policy_data != NULL)
    {
        return(-1);
    }

    pd = kcalloc(1, sizeof(simple_policy_unit_t));
    
    if(pd == NULL)
    {
        return(-1);
    }

    pd->unit = unit;
    unit->policy_data = pd;

    return(0);
}
#if 0
static int simple_load_balancing
(
    void *policy_data,
    list_head_t    *units
)
{
    int                  status      = -1;
    list_node_t          *cursor = NULL;
    list_node_t          *th_tail = NULL;
    uint32_t             this_ready_in_q  = 0; 
    uint32_t             ready_in_q  = 0;
    simple_policy_unit_t *this_punit = NULL;
    simple_policy_unit_t *work_punit = NULL;
    sched_exec_unit_t    *this_unit  = NULL;
    sched_exec_unit_t    *work_unit  = NULL;
    sched_exec_unit_t    *balance_target = NULL;

    this_punit = policy_data;
    this_unit = this_punit->unit;
    
    cursor = linked_list_first(units);

    /* Find the unit with the least threads ready */
    while(cursor)
    {
        /* Skip ourselves */
        if(cursor == &this_unit->node)
        {
            cursor = linked_list_next(cursor);
            continue;
        }
        this_ready_in_q = linked_list_count(&this_punit->ready_q); 

        work_unit = (sched_exec_unit_t*)cursor;

        /* lock the unit we are looking in */
        spinlock_lock(&work_unit->lock);

        work_punit = work_unit->policy_data;

        ready_in_q = linked_list_count(&work_punit->ready_q);

        if(this_ready_in_q > ready_in_q && this_ready_in_q > 1)
        {
            th_tail = linked_list_last(&this_punit->ready_q);

            if(th_tail)
            {
                linked_list_remove(&this_punit->ready_q, th_tail);
                linked_list_add_tail(&work_punit->ready_q, th_tail);
                work_unit->flags |= UNIT_RESCHEDULE;

                cpu_issue_ipi(IPI_DEST_NO_SHORTHAND, 
                              work_unit->cpu->cpu_id, 
                              IPI_RESCHED);
            }
        }
        
        spinlock_unlock(&work_unit->lock);

        cursor = linked_list_next(cursor);
    }
    return(status);
}
#endif
static sched_policy_t policy = 
{
    .policy_name        = "Simple",
    .thread_dequeue     = simple_next_thread,
    .thread_enqueue     = simple_put_thread ,
    
    .init_policy        = simple_init,
    .thread_enqueue_new = simple_put_thread,
    /*.load_balancing     = simple_load_balancing,*/
};


int sched_simple_register(sched_policy_t **p)
{
    *p = &policy;
    return(0);
}

