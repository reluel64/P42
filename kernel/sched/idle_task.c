#include <sched.h>
#include <linked_list.h>
#include <utils.h>
#include <liballoc.h>

static int32_t idle_task_enqueue
(
    sched_exec_unit_t *unit,
    sched_thread_t *th
)
{
    return(-1);
}

static int32_t idle_task_dequeue
(
    sched_exec_unit_t *unit,
    sched_thread_t *th
)
{
    return(-1);
}

static int32_t idle_task_pick_next_thread
(
    sched_exec_unit_t *unit,
    sched_thread_t **th
)
{
    *th = &unit->idle;
    return(0);
}

static int32_t idle_task_select_thread
(
    sched_exec_unit_t *unit,
    sched_thread_t *th
)
{
    th->cpu_left = 0;
    
    return(0);
}

static int32_t idle_task_put_prev_thread
(
    sched_exec_unit_t *unit,
    sched_thread_t *th
)
{
    return(0);
}

static int32_t idle_task_tick
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

static int idle_task_unit_init
(
    sched_exec_unit_t *unit
)
{
    return(0);
}



static sched_policy_t idle_task_policy = 
{
    .node             = {.next = NULL, .prev = NULL},
    .dequeue          = idle_task_dequeue,
    .enqueue          = idle_task_enqueue,
    .tick             = idle_task_tick,
    .unit_init        = idle_task_unit_init,
    .pick_next        = idle_task_pick_next_thread,
    .select_thread    = idle_task_select_thread,
    .put_prev         = idle_task_put_prev_thread,
    .policy_name      = "idle_task",
    .id      = sched_idle_task_policy,
    .pv               = NULL
};

int idle_task_register
(
    void
)
{
    sched_policy_register(&idle_task_policy);
    return(0);
}

