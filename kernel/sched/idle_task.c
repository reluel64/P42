#include <sched.h>
#include <linked_list.h>
#include <utils.h>
#include <liballoc.h>

static int32_t idle_task_enqueue
(
    struct sched_exec_unit *unit,
    struct sched_thread *th
)
{
    return(-1);
}

static int32_t idle_task_dequeue
(
    struct sched_exec_unit *unit,
    struct sched_thread *th
)
{
    return(-1);
}

static int32_t idle_task_pick_next_thread
(
    struct sched_exec_unit *unit,
    struct sched_thread **th
)
{
    *th = &unit->idle;
    return(0);
}

static int32_t idle_task_select_thread
(
    struct sched_exec_unit *unit,
    struct sched_thread *th
)
{
    th->cpu_left = 0;
    
    return(0);
}

static int32_t idle_task_put_prev_thread
(
    struct sched_exec_unit *unit,
    struct sched_thread *th
)
{
    return(0);
}

static int32_t idle_task_tick
(
    struct sched_exec_unit *unit
)
{
    struct sched_thread *th          = NULL;
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
    struct sched_exec_unit *unit
)
{
    return(0);
}



static struct sched_policy idle_task_policy = 
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
    .id               = sched_idle_task_policy,
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

