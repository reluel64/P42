#ifndef schedh
#define schedh
#include <devmgr.h>
#include <cpu.h>
#include <timer.h>


#define THREAD_READY            (1 << 0)
#define THREAD_RUNNING          (1 << 1)
#define THREAD_BLOCKED          (1 << 2)
#define THREAD_SLEEPING         (1 << 3)
#define THREAD_DEAD             (1 << 4)
#define THREAD_ALLOCATED        (1 << 5)
#define THREAD_NEED_RESCHEDULE  (1 << 6)
#define CPU_AFFINITY_VECTOR     (0x8)

#define UNIT_THREADS_WAKE        (1 << 0)
#define UNIT_THREADS_UNBLOCK     (1 << 1)
#define UNIT_RESCHEDULE          (1 << 2)

#define THREAD_STATE_MASK (THREAD_RUNNING | \
                          THREAD_READY    | \
                          THREAD_BLOCKED  | \
                          THREAD_SLEEPING)

#define SCHED_MAX_PRIORITY 255


#define NODE_TO_THREAD(x) (sched_thread_t*)(((uint8_t*)(x)) -  \
                          offsetof(sched_thread_t, sched_node))

#define PEND_NODE_TO_THREAD(x) ((sched_thread_t*) \
                                ((uint8_t*)(x) -  \
                                offsetof(sched_thread_t, pend_node)))

#define OWNER_NODE_TO_THREAD(x) ((sched_thread_t*) \
                                ((uint8_t*)(x) -  \
                                offsetof(sched_thread_t, owner_node)))

typedef struct sched_exec_unit_t sched_exec_unit_t;
typedef struct sched_thread_t    sched_thread_t;
typedef unsigned char cpu_aff_t[CPU_AFFINITY_VECTOR] ;

typedef struct sched_policy_t
{
    char *policy_name;

    int (*thread_dequeue)
    (
        void *policy_data,
        sched_thread_t    **next
    );

    int (*thread_enqueue)
    (
        void *policy_data,
        sched_thread_t *th
    );

    int (*thread_tick)
    (
        void *policy_data,
        sched_thread_t *th
    );

    int (*load_balancing)
    (
        void *policy_data,
        list_head_t    *units
    );

    int (*init_policy)
    (
        sched_exec_unit_t *unit
    );

    int (*thread_enqueue_new)
    (   
        void *policy_data,
        sched_thread_t *th
    );

}sched_policy_t;

typedef struct sched_thread_t
{
    
    list_node_t        sched_node;   /* node in the queue                             */
    list_node_t        pend_node;    /* node for synchronization                      */
    list_node_t        owner_node;   /* node for the onwer                            */
    virt_addr_t        stack_origin; /* start of memory allocated for the stack       */
    virt_size_t        stack_sz;     /* stack size                                    */  
    uint32_t           flags;        /* thread flags                                  */
    void              *owner;        /* owner of the thread - if kernel, owner = null */
    virt_addr_t        context;
    uint32_t           id;           /* thread id                                     */
    uint16_t           prio;         /* priority                                      */
    void              *entry_point;  /* entry point of the thread                     */
    void              *arg;          /* parameter for the entry point of the thread   */
    sched_exec_unit_t *unit;         /* execution unit on which the thread is running */
    spinlock_t        lock;          /* lock to protect the structure members         */
    uint32_t          slept;         /* sleeping cursor                               */
    uint32_t          to_sleep;      /* amount in ms to sleep                         */
    uint32_t          remain;        /* reamining time before task switch             */
    void              *rval;         /* return value                                  */
    cpu_aff_t         affinity;

}sched_thread_t;

typedef struct sched_exec_unit_t
{
    list_node_t      node;         /* node in units list */
    cpu_t            *cpu;         /* cpu structure that is tied to the scheduler 
                                    * execution unit*/ 
    sched_thread_t   *current;      /* current thread                                */
    sched_thread_t    idle;         /* our dearest idle task                         */
    list_head_t       ready_q;      /* queue of ready threads on the current CPU     */
    list_head_t       blocked_q;    /* queue of blocked threads                      */
    list_head_t       sleep_q;      /* queue of sleeping threads                     */
    spinlock_t        lock;         /* lock to protect the queues                    */
    uint32_t          flags;        /* flags for the execution unit                  */
    device_t         *timer_dev;    /* timer device which is connected to this unit  */
  //  sched_policy_t    *policy;
  //  void              *policy_data;
}sched_exec_unit_t;

typedef struct sched_owner_t
{
    list_node_t node;
    list_head_t threads;
    uint32_t    owner_id;
    void       *vm_ctx;
    uint8_t    user;
    spinlock_t th_lst_lock;
}sched_owner_t;


int sched_unit_init
(
    device_t *timer, 
    cpu_t *cpu,
    sched_thread_t *post_unit_init
);

int sched_init(void);
int sched_init_thread
(
    sched_thread_t    *th,
    void        *entry_pt,
    virt_size_t stack_sz,
    uint32_t    prio,
    void *pv
);

int sched_start_thread
(
    sched_thread_t *th
);

int sched_need_resched
(
    sched_exec_unit_t *unit
);

sched_thread_t *sched_thread_self
(
    void
);

void sched_unblock_thread
(
    sched_thread_t *th
);

void sched_block_thread
(
    sched_thread_t *th
);

void sched_unblock_thread
(
    sched_thread_t *th
);

void sched_sleep_thread
(
    sched_thread_t *th,
    uint32_t timeout
);

void sched_wake_thread
(
    sched_thread_t *th
);

void sched_yield(void);

void sched_sleep
(
    uint32_t delay
);

int sched_start_thread
(
    sched_thread_t *th
);

void sched_thread_entry_point
(
    sched_thread_t *th
);

void sched_thread_mark_dead
(
    sched_thread_t *th
);

void sched_thread_exit
(
    void *exit_val
);

void schedule(void);

#endif