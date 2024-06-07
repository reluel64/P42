#include <scheduler.h>
#include <linked_list.h>
#include <utils.h>
#include <liballoc.h>

typedef struct basic_policy_t
{
    list_head_t ready_q;
    list_head_t sleep_q;
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

   state = __atomic_load_n(&th->flags, __ATOMIC_SEQ_CST) & THREAD_STATE_MASK;

    switch(state)
    {
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

    if(lh != NULL)
    {
        linked_list_add_tail(lh, &th->sched_node);
    }

    return(0);
}

static int basic_tick
(
    sched_exec_unit_t *unit
)
{
    list_node_t    *node        = NULL;
    sched_thread_t *th          = NULL;
    basic_policy_t *policy      = NULL;
   
    th = unit->current;

    if(th != NULL)
    {
        if(th->cpu_left > 0)
        {
            th->cpu_left--;
        }
    }


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

