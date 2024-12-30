#ifndef schedh
#define schedh
#include <devmgr.h>
#include <cpu.h>
#include <timer.h>

#define THREAD_NAME_LENGTH      (64)

#define THREAD_READY            (1 << 0)
#define THREAD_RUNNING          (1 << 1)
#define THREAD_DEAD             (1 << 2)
#define THREAD_ALLOCATED        (1 << 3)
#define THREAD_NEED_RESCHEDULE  (1 << 4)
#define THREAD_INACTIVE         (1 << 5)

#define CPU_AFFINITY_VECTOR     (0x8)

#define SCHED_MAX_PRIORITY 255

#define SYSTEM_NODE_TO_THREAD(x) (struct sched_thread*) (((uint8_t*)(x)) -  \
                                 offsetof(struct sched_thread, system_node))

#define SCHED_NODE_TO_THREAD(x) (struct sched_thread*) (((uint8_t*)(x)) -  \
                                 offsetof(struct sched_thread, sched_node))

#define UNIT_NODE_TO_THREAD(x) (struct sched_thread*) (((uint8_t*)(x)) -  \
                                   offsetof(struct sched_thread, unit_node))

#define PEND_NODE_TO_THREAD(x) ((struct sched_thread*) ((uint8_t*)(x) -  \
                                offsetof(struct sched_thread, pend_node)))

#define OWNER_NODE_TO_THREAD(x) ((struct sched_thread*) (uint8_t*)(x) -  \
                                offsetof(struct sched_thread, owner_node)))

typedef unsigned char cpu_aff_t[CPU_AFFINITY_VECTOR] ;

struct sched_thread;
struct sched_exec_unit;

enum sched_policy_id
{
    sched_policy_default = 0,
    sched_idle_task_policy = 1,
    sched_basic_policy = 2
};

struct sched_policy
{

    struct list_node node;
    
    char *policy_name;
    enum sched_policy_id id;

    int32_t (*dequeue)
    (
        struct sched_exec_unit *unit,
        struct sched_thread    *th
    );

    int32_t (*enqueue)
    (
        struct sched_exec_unit *unit,
        struct sched_thread *th
    );

    int32_t (*pick_next)
    (
        struct sched_exec_unit *unit,
        struct sched_thread **th
    );

    int32_t (*select_thread)
    (
        struct sched_exec_unit *unit,
        struct sched_thread *th
    );

    int32_t (*put_prev)
    (
        struct sched_exec_unit *unit,
        struct sched_thread *th
    );

    int32_t (*tick)
    (
        struct sched_exec_unit *unit
    );

    int32_t (*unit_init)
    (
        struct sched_exec_unit *unit
    );

    void *pv;

};

struct sched_thread
{
    struct list_node        system_node;  /* system-wide node                              */
    struct list_node        unit_node;    /* unit wide-node                                */
    struct list_node        sched_node;   /* node in the queue                             */
    struct list_node        pend_node;    /* node for synchronization                      */
    struct list_node        owner_node;   /* node for the onwer                            */
    virt_addr_t        stack_origin; /* start of memory allocated for the stack       */
    virt_size_t        stack_sz;     /* stack size                                    */  
    uint32_t           flags;        /* thread flags                                  */
    uint32_t           id;           /* thread id                                     */
    uint32_t           cpu_left;     /* reamining time before task switch             */
    struct sched_owner      *owner;        /* owner of the thread - if kernel, owner = null */
    virt_addr_t        context;      /* thread context                                */

    uint16_t           prio;         /* priority                                      */
    void              *entry_point;  /* entry point of the thread                     */
    void              *arg;          /* parameter for the entry point of the thread   */
    struct sched_exec_unit *unit;         /* execution unit on which the thread is running */
    struct spinlock        lock;          /* lock to protect the structure members         */

    void              *rval;         /* return value                                  */
    cpu_aff_t         affinity;
    struct sched_policy    *policy;
    uint8_t           name[THREAD_NAME_LENGTH];

    uint64_t           context_switches;
};

struct sched_exec_unit
{
    struct list_node      node;      /* node in units list                          */
    struct cpu            *cpu;      /* cpu structure that is tied to the scheduler * 
                                 * execution unit                              */ 
    struct sched_thread *current;   /* current thread                             */
    struct sched_thread  idle;      /* our dearest idle task                      */
    struct list_head     dead_q;    /* queue of dead threads on the current CPU   */
    struct spinlock      lock;      /* lock to protect the queues                 */
    uint32_t        flags;     /* flags for the execution unit               */
    uint32_t        preempt_lock; /* preemption lock count */
    struct device_node       *timer_dev; /* timer device which is connected to this unit  */
    struct spinlock      wake_q_lock; /* lock for the wake queue */
    struct list_head     wake_q;    /* queue of threads that wait to be woken up  */
    struct list_head     unit_threads;
    struct isr           sched_isr;
};

struct sched_owner
{
    struct list_node node;
    struct list_head threads;
    uint32_t    owner_id;
    void       *vm_ctx;
    uint8_t    user;
    struct spinlock th_lst_lock;
};


int sched_unit_init
(
    struct device_node *timer, 
    struct cpu *cpu,
    struct sched_thread *post_unit_init
);

int sched_init
(
    void
);

int sched_start_thread
(
    struct sched_thread *th
);

int sched_need_resched
(
    struct sched_exec_unit *unit
);

struct sched_thread *sched_thread_self
(
    void
);

void sched_wake_thread
(
    struct sched_thread *th
);

void sched_yield
(
    void
);

void sched_sleep
(
    uint32_t delay
);

int sched_start_thread
(
    struct sched_thread *th
);

void sched_thread_entry_point
(
    struct sched_thread *th
);

void sched_thread_mark_dead
(
    struct sched_thread *th
);

void sched_thread_exit
(
    void *exit_val
);

void schedule
(
    void
);

int32_t  sched_policy_register
(
    struct sched_policy *p
);

void sched_show_threads
(
    void
);

#endif