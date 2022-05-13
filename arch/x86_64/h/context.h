#ifndef contexth
#define contexth

/* These offsets must be kept in sync 
 * with those from context.asm
 */



/********************************/
static const int CONTEXT_START = __LINE__;
#define RAX_INDEX    (0x0000)
#define RBX_INDEX    (0x0001)
#define RCX_INDEX    (0x0002)
#define RDX_INDEX    (0x0003)
#define R8_INDEX     (0x0004)
#define R9_INDEX     (0x0005)
#define R10_INDEX    (0x0006)
#define R11_INDEX    (0x0007)
#define R12_INDEX    (0x0008)
#define R13_INDEX    (0x0009)
#define R14_INDEX    (0x000a)
#define R15_INDEX    (0x000b)
#define RSP_INDEX    (0x000c)
#define RBP_INDEX    (0x000d)
#define RFLAGS_INDEX (0x000e)
#define DS_INDEX     (0x000f)
#define CS_INDEX     (0x0010)
#define RIP_INDEX    (0x0011)
#define CR3_INDEX    (0x0012)
#define ARG_INDEX    (0x0013)
#define TH_INDEX     (0x0014)
#define RSP0_INDEX   (0x0015)
static const int CONTEXT_END  =__LINE__;



#define RAX_OFFSET    (RAX_INDEX << 3)
#define RBX_OFFSET    (RBX_INDEX << 3)
#define RCX_OFFSET    (RCX_INDEX << 3)
#define RDX_OFFSET    (RDX_INDEX << 3)
#define R8_OFFSET     (R8_INDEX << 3)
#define R9_OFFSET     (R9_INDEX << 3)
#define R10_OFFSET    (R10_INDEX << 3)
#define R11_OFFSET    (R11_INDEX << 3)
#define R12_OFFSET    (R12_INDEX << )
#define R13_OFFSET    (R13_INDEX << 3)
#define R14_OFFSET    (R14_INDEX << 3)
#define R15_OFFSET    (R15_INDEX << 3)
#define RSP_OFFSET    (RSP_INDEX << 3)
#define RBP_OFFSET    (RBP_INDEX << 3)
#define RFLAGS_OFFSET (RFLAGS_INDEX << 3)
#define DS_OFFSET     (DS_INDEX << 3)
#define CS_OFFSET     (CS_INDEX << 3)
#define RIP_OFFSET    (RIP_INDEX << 3)
#define CR3_OFFSET    (CR3_INDEX << 3)
#define ARG_OFFSET    (ARG_INDEX << 3)
#define TH_OFFSET     (TH_INDEX << 3)


#define CONTEXT_SIZE ((CONTEXT_END - CONTEXT_START - 2) << 3)



int context_init
(
    sched_thread_t *th
);

void context_switch
(
    sched_thread_t *prev,
    sched_thread_t *next
);
#endif