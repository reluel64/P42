#include <sched.h>
#include <linked_list.h>
#include <utils.h>
#include <liballoc.h>


#define BASIC_POLICY_MAX_UNITS 128

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

static int32_t basic_peek_next_thread
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
        *th = SCHED_NODE_TO_THREAD(n);
        result = 0;
    }

    return(result);
}

static int32_t basic_select_thread
(
    sched_exec_unit_t *unit,
    sched_thread_t *th
)
{

    
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
        linked_list_add_head(&bpu->threads, &th->sched_node);
        result = 0;
    }

    return(result);
}

static int basic_tick
(
    sched_exec_unit_t *unit
)
{
    sched_thread_t *th          = NULL;
   
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
    .peek_next        = basic_peek_next_thread,
    .select_thread    = basic_select_thread,
    .put_prev         = basic_put_prev_thread,
    .policy_name      = "basic",
    .pv               = &policy
};

int basic_register
(
    sched_policy_t *policy
)
{
    memset(&policy, 0, sizeof(basic_policy_t));
    sched_policy_register(&basic_policy);
    return(0);
}

