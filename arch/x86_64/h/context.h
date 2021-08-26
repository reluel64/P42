#ifndef contexth
#define contexth

/* These offsets must be kept in sync 
 * with those from context.asm
 */

#define RAX_OFFSET    0x0000
#define RBX_OFFSET    0x0008
#define RCX_OFFSET    0x0010
#define RDX_OFFSET    0x0018
#define R8_OFFSET     0x0020
#define R9_OFFSET     0x0028
#define R10_OFFSET    0x0030
#define R11_OFFSET    0x0038
#define R12_OFFSET    0x0040
#define R13_OFFSET    0x0048
#define R14_OFFSET    0x0050
#define R15_OFFSET    0x0058
#define RSP_OFFSET    0x0060
#define RBP_OFFSET    0x0068
#define RFLAGS_OFFSET 0x0070
#define DS_OFFSET     0x0078
#define CS_OFFSET     0x0080
#define RIP_OFFSET    0x0088
#define CR3_OFFSET    0x0090


/********************************/

#define RAX_INDEX    (RAX_OFFSET >> 3)
#define RBX_INDEX    (RBX_OFFSET >> 3)
#define RCX_INDEX    (RCX_OFFSET >> 3)
#define RDX_INDEX    (RDX_OFFSET >> 3)
#define R8_INDEX     (R8_OFFSET >> 3)
#define R9_INDEX     (R9_OFFSET >> 3)
#define R10_INDEX    (R10_OFFSET >> 3)
#define R11_INDEX    (R11_OFFSET >> 3)
#define R12_INDEX    (R12_OFFSET >> 3)
#define R13_INDEX    (R13_OFFSET >> 3)
#define R14_INDEX    (R14_OFFSET >> 3)
#define R15_INDEX    (R15_OFFSET >> 3)
#define RSP_INDEX    (RSP_OFFSET >> 3)
#define RBP_INDEX    (RBP_OFFSET >> 3)
#define RFLAGS_INDEX (RFLAGS_OFFSET >> 3)
#define DS_INDEX     (DS_OFFSET >> 3)
#define CS_INDEX     (CS_OFFSET >> 3)
#define RIP_INDEX    (RIP_OFFSET >> 3)
#define CR3_INDEX    (CR3_OFFSET >> 3)


int context_init
(
    sched_thread_t *th
);

void context_save
(
    sched_thread_t *th
);

void context_load
(
    sched_thread_t *th
);

#endif