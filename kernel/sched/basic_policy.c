#include <scheduler.h>
#include <linked_list.h>
#include <utils.h>
#include <liballoc.h>

typedef struct basic_policy_t
{
    list_head_t ready_q;
    list_head_t sleep_q;
    list_head_t block_q;
}basic_policy_t;


static int basic_deq_thread
(
    sched_exec_unit_t *unit,
    sched_thread_t   **next
)
{
    list_node_t *th        = NULL;
    list_node_t *next_th   = NULL;
    sched_thread_t *thread = NULL;
    basic_policy_t *policy = NULL;

    policy = unit->policy.pv;


    if(__atomic_fetch_and(&unit->flags, 
                          ~UNIT_THREADS_UNBLOCK, 
                         __ATOMIC_SEQ_CST) & 
                         UNIT_THREADS_UNBLOCK)
    {

        /* Check blocked threads */
        th = linked_list_first(&policy->block_q);

        while(th)
        {
            next_th = linked_list_next(th);

            thread = SCHED_NODE_TO_THREAD(th);

            if(!(__atomic_load_n(&thread->flags, __ATOMIC_SEQ_CST) & 
                THREAD_BLOCKED))
            {
                linked_list_remove(&policy->block_q, th);
                linked_list_add_tail(&policy->ready_q, th);
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
        
        th = linked_list_first(&policy->sleep_q);

        while(th)
        {
            next_th = linked_list_next(th);

            thread = SCHED_NODE_TO_THREAD(th);

            if(!(__atomic_load_n(&thread->flags, __ATOMIC_SEQ_CST) & 
                THREAD_SLEEPING))
            {
                linked_list_remove(&policy->sleep_q, th);
                linked_list_add_tail(&policy->ready_q, th);
            }

            th = next_th;
        }
    }

    th = linked_list_first(&policy->ready_q);

    if(th == NULL)
    {
        return(-1);
    }
    
    linked_list_remove(&policy->ready_q, th);

    thread = SCHED_NODE_TO_THREAD(th);

    /* Mark the next thread as running */
    __atomic_or_fetch(&thread->flags, 
                      THREAD_RUNNING, 
                      __ATOMIC_SEQ_CST);

    *next = thread;

    return(0);

}

static int basic_enq_thread
(
    sched_exec_unit_t *unit,
    sched_thread_t    *th
)
{

    uint32_t state = 0;
    list_head_t *lh = NULL;
    basic_policy_t *policy = NULL;

    policy = unit->policy.pv;
    
    __atomic_and_fetch(&th->flags, ~THREAD_NEED_RESCHEDULE, __ATOMIC_SEQ_CST);

   state = __atomic_load_n(&th->flags, __ATOMIC_SEQ_CST) & THREAD_STATE_MASK;

    switch(state)
    {
        case THREAD_BLOCKED:
            lh = &policy->block_q;
            break;

        case THREAD_SLEEPING:
            lh = &policy->sleep_q;
            break;

        case THREAD_READY:
            lh = &policy->ready_q;
            break;

        default:
            kprintf("%s %d STATE %d THREAD %x\n",__FUNCTION__, __LINE__, state,th);
            while(1);
            break;
    }

    /* Add the thread in the corresponding queue */

    linked_list_add_tail(lh, &th->sched_node);

    return(0);
}

static int basic_tick
(
    sched_exec_unit_t *unit,
    uint32_t          *next_sleep
)
{
    list_node_t    *node        = NULL;
    sched_thread_t *th          = NULL;
    basic_policy_t *policy      = NULL;
   
   return(0);
}

static int basic_init
(
    sched_exec_unit_t *unit
)
{
    basic_policy_t *bp = NULL;

    bp = kcalloc(sizeof(basic_policy_t), 1);

    if(bp == NULL)
    {
        return(-1);
    }

    /* initialize queues */
    linked_list_init(&bp->ready_q);
    linked_list_init(&bp->sleep_q);
    linked_list_init(&bp->block_q);

    unit->policy.pv = bp;

    return(0);
}

static sched_policy_t basic_policy = 
{
    .dequeue     = basic_deq_thread,
    .enqueue     = basic_enq_thread,
    .tick        = basic_tick,
    .init        = basic_init,
    .policy_name = "basic",
    .pv          = NULL
};

int basic_register
(
    sched_policy_t *policy
)
{
    memcpy(policy, &basic_policy, sizeof(sched_policy_t));
    return(0);
}

