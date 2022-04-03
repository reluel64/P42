/* 
 * Stupidly simple scheduler policy 
 * Part of P42
 */

#include <scheduler.h>
#include <utils.h>
#include <linked_list.h>
#include <spinlock.h>
#include <liballoc.h>

typedef struct simple_policy_unit_t
{
    sched_exec_unit_t *unit;        /* execution unit that holds this policy */
    list_head_t       ready_q;      /* queue of ready threads on the current CPU     */
    list_head_t       blocked_q;    /* queue of blocked threads                      */
    list_head_t       sleep_q;      /* queue of sleeping threads                     */
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
  //  kprintf("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
    /* Check blocked threads */
    th = linked_list_first(&policy_unit->blocked_q);

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
                linked_list_remove(&policy_unit->blocked_q, th);
                linked_list_add_tail(&policy_unit->ready_q, th);
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
        
        th = linked_list_first(&policy_unit->sleep_q);

        while(th)
        {
            next_th = linked_list_next(th);

            thread = NODE_TO_THREAD(th);

            if(!(__atomic_load_n(&thread->flags, __ATOMIC_SEQ_CST) & 
                THREAD_SLEEPING))
            {

                linked_list_remove(&policy_unit->sleep_q, th);
                linked_list_add_tail(&policy_unit->ready_q, th);
            }

            th = next_th;
        }
    }

    //    kprintf("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
    th = linked_list_first(&policy_unit->ready_q);

    if(th == NULL)
    {
          //  kprintf("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
        return(-1);
    }
    
    linked_list_remove(&policy_unit->ready_q, th);

    thread = NODE_TO_THREAD(th);

    /* Mark the next thread as running */
    __atomic_or_fetch(&thread->flags, 
                      THREAD_RUNNING, 
                      __ATOMIC_SEQ_CST);

    *next = thread;
   // kprintf("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
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

    if(th == NULL || policy_data == NULL)
    {
        kprintf("CANNOT PUT THREAD\n");
        return(-1);
    }
    kprintf("PUTTING THREAD\n");
    policy_unit = policy_data;
    
    __atomic_and_fetch(&th->flags, ~THREAD_NEED_RESCHEDULE, __ATOMIC_SEQ_CST);

   state = __atomic_load_n(&th->flags, __ATOMIC_SEQ_CST) & THREAD_STATE_MASK;

    switch(state)
    {
        case THREAD_BLOCKED:
            lh = &policy_unit->blocked_q;
            break;

        case THREAD_SLEEPING:
            lh = &policy_unit->sleep_q;
            break;

        case THREAD_READY:
            lh = &policy_unit->ready_q;
            break;

        case THREAD_NEW:
             __atomic_and_fetch(&th->flags, ~THREAD_NEW, __ATOMIC_SEQ_CST);
            lh = &policy_unit->ready_q;
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
    void *policy_data,
    sched_thread_t *th
)
{
    list_node_t          *node            = NULL;
    simple_policy_unit_t *policy_unit     = NULL;
    sched_thread_t       *sleeping_thread = NULL;

    policy_unit = policy_data;
    
    /* Update sleeping threads */
    node = linked_list_first(&policy_unit->sleep_q);
    
    while(node)
    {
        
        sleeping_thread = (sched_thread_t*)node;

        /* If timeout has been reached, wake the thread */

        if(__atomic_load_n(&sleeping_thread->flags, __ATOMIC_SEQ_CST) & 
          THREAD_SLEEPING)
        {
            sleeping_thread->slept++;

            if(sleeping_thread->slept >= sleeping_thread->to_sleep)
            {            
                /* Wake up the thread */
                sched_wake_thread(sleeping_thread);
            }
        }
        node = linked_list_next(node);
    }

    return(0);
}

static int sched_simple_init
(
    sched_exec_unit_t *unit
)
{
    simple_policy_unit_t *pd = NULL;
    
    if(unit == NULL)
    {
            kprintf("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
        return(-1);
    }

    if(unit->policy_data != NULL)
    {
            kprintf("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
        return(-1);
    }

    pd = kcalloc(1, sizeof(simple_policy_unit_t));
    
    if(pd == NULL)
    {
            kprintf("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
        return(-1);
    }
        kprintf("%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);
    pd->unit = unit;
    unit->policy_data = pd;

    return(0);
}

static sched_policy_t policy = 
{
    .policy_name = "Simple",
    .next_thread = simple_next_thread,
    .put_thread  = simple_put_thread ,
    .update_time = simple_update_time,
    .init_policy = sched_simple_init,
    .enqueue_new_thread = simple_put_thread,
    .load_balancing = NULL
};


int sched_simple_register(sched_policy_t **p)
{
    *p = &policy;
    return(0);
}

