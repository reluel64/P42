#include <sched.h>
#include <linked_list.h>
#include <utils.h>
#include <liballoc.h>


#define BASIC_POLICY_MAX_UNITS 4096

typedef struct basic_policy_unit_t
{
    sched_exec_unit_t *unit;
    list_head_t threads;
}basic_policy_unit_t;

typedef struct basic_policy_t
{
    basic_policy_unit_t units[BASIC_POLICY_MAX_UNITS];
}basic_policy_t;

static basic_policy_t policy = {0};

static basic_policy_unit_t *basic_unit_get
(
    sched_exec_unit_t *unit
)
{
    cpu_t *cpu = NULL;
    basic_policy_unit_t *bpu = NULL;

    if(unit != NULL)
    {
        cpu = unit->cpu;
    }

    if(cpu != NULL)
    {
        if(cpu->cpu_id < BASIC_POLICY_MAX_UNITS)
        {
            bpu = &policy.units[cpu->cpu_id];
        }
    }

    return(bpu);
}

static int32_t basic_enqueue
(
    sched_exec_unit_t *unit,
    sched_thread_t *th
)
{
    basic_policy_unit_t *bpu = NULL;
    int32_t status = -1;


    bpu = basic_unit_get(unit);
    if(bpu != NULL && th != NULL)
    {
        linked_list_add_head(&bpu->threads, &th->sched_node);
        status = 0;
    }

    return(status);
}

static int32_t basic_dequeue
(
    sched_exec_unit_t *unit,
    sched_thread_t *th
)
{
    basic_policy_unit_t *bpu = NULL;
    int32_t status = -1;


    bpu = basic_unit_get(unit);

    if(bpu != NULL && th != NULL)
    {
        linked_list_remove(&bpu->threads, &th->sched_node);
        status = 0;
    }

    return(status);
}

static int32_t basic_pick_next_thread
(
    sched_exec_unit_t *unit,
    sched_thread_t **th
)
{
    list_node_t *n = NULL;
    basic_policy_unit_t *bpu = NULL;
    int32_t result = -1;

    bpu = basic_unit_get(unit);

    if((bpu != NULL) && (th != NULL))
    {
        n = linked_list_last(&bpu->threads);
        if(n != NULL)
        {
            *th = SCHED_NODE_TO_THREAD(n);
            result = 0;
        }

    }

    return(result);
}

static int32_t basic_select_thread
(
    sched_exec_unit_t *unit,
    sched_thread_t *th
)
{
    th->cpu_left = th->prio;
    return(0);
}

static int32_t basic_put_prev_thread
(
    sched_exec_unit_t *unit,
    sched_thread_t *th
)
{
    basic_policy_unit_t *bpu = NULL;
    int32_t result = -1;

    bpu = basic_unit_get(unit);

    if((bpu != NULL) && (th != NULL))
    {
        /* if the thread is sleeping, it is already removed from the queue 
         * by the scheduler code so we will not try to change the queue
         */
        if(th->flags & THREAD_READY)
        {
           // kprintf("Adding the thread to queue\n");
            /* remove thread from current position */
            linked_list_remove(&bpu->threads, &th->sched_node);

            /* put thread at the head of the queue */
            linked_list_add_head(&bpu->threads, &th->sched_node);

            result = 0;
        }
        
    }

    return(result);
}

static int32_t basic_tick
(
    sched_exec_unit_t *unit
)
{
    sched_thread_t *th          = NULL;
    int32_t status = 0;
    th = unit->current;

    if(th != NULL)
    {
        if(th->cpu_left > 0)
        {
            th->cpu_left--;
            status = 1;
        }
    }

   return(status);
}

static int basic_unit_init
(
    sched_exec_unit_t *unit
)
{
    basic_policy_unit_t *bpu = NULL;

    if(unit != NULL && unit->cpu != NULL)
    {
        bpu = &policy.units[unit->cpu->cpu_id];
        bpu->unit = unit;
        linked_list_init(&bpu->threads);
    }

    return(0);
}



static sched_policy_t basic_policy = 
{
    .node             = {.next = NULL, .prev = NULL},
    .dequeue          = basic_dequeue,
    .enqueue          = basic_enqueue,
    .tick             = basic_tick,
    .unit_init        = basic_unit_init,
    .pick_next        = basic_pick_next_thread,
    .select_thread    = basic_select_thread,
    .put_prev         = basic_put_prev_thread,
    .policy_name      = "basic",
    .id               = sched_basic_policy,
    .pv               = &policy
};

int basic_register
(
    void
)
{
    sched_policy_register(&basic_policy);
    return(0);
}

