#include <sched.h>
#include <context.h>
#include <platform.h>
#include <vm.h>
#include <utils.h>
#include <liballoc.h>

extern void __context_switch
(   
    virt_addr_t ctx_prev, 
    virt_addr_t ctx_next
);

static void context_user_setup
(
    struct sched_thread *th
)
{
    void *user_entry_pt = NULL;
    virt_addr_t *rsp0 = NULL;

    user_entry_pt = th->entry_point;
    rsp0 = (virt_addr_t*)((virt_addr_t*)th->context)[RSP0_INDEX];

    /* assemble stack to jump to user space */

    rsp0[0] = (virt_addr_t)user_entry_pt;
    rsp0[1] = ((virt_addr_t*)th->context)[CS_INDEX];
    rsp0[3] = ((virt_addr_t*)th->context)[RSP_INDEX];
    rsp0[4] = ((virt_addr_t*)th->context)[DS_INDEX];
}

int context_init
(
    struct sched_thread *th
)
{
    struct sched_owner *owner     = NULL;
    virt_addr_t   *context   = NULL;
    struct vm_ctx      *vm_ctx    = NULL;
    virt_addr_t   rsp0       = 0;
    /* virt_addr_t   *user_start = NULL; */

    owner = th->owner;

    if(owner == NULL)
    {
        return(-1);
    }

    vm_ctx = owner->vm_ctx;

    if(vm_ctx == NULL)
    {
        return(-1);
    }

    th->context = (virt_addr_t)kcalloc(1, CONTEXT_SIZE);

    if(th->context == 0)
    {
        return(-1);
    }
    
    /* allocate the RSP0 that will be used when switching 
     * from user mode to kernel mode
     */
    rsp0 = vm_alloc(vm_ctx, VM_BASE_AUTO, PAGE_SIZE, 0, VM_ATTR_WRITABLE);

    if(rsp0 == VM_INVALID_ADDRESS)
    {
        kfree((void*)th->context);
        return(-1);
    }

    memset((void*)rsp0, 0, PAGE_SIZE);

    /* Fill the context */
    context = (virt_addr_t*)th->context;

    /* every thread starts its execution from sched_thread_entry_point*/
    context[RIP_INDEX ]  = (virt_addr_t)sched_thread_entry_point;
    context[RSP_INDEX ]  = th->stack_origin + th->stack_sz;
    context[RBP_INDEX ]  = th->stack_origin + th->stack_sz;
    context[CR3_INDEX ]  = vm_ctx->pgmgr.pg_phys;
    context[TH_INDEX  ]  = (virt_addr_t)th;
    context[RSP0_INDEX]  = rsp0;


    if(owner->user)
    {
        context[CS_INDEX] = KERNEL_CODE_SEGMENT;
        context[DS_INDEX] = KERNEL_DATA_SEGMENT;

        context_user_setup(th);
    }
    else
    {
        context[CS_INDEX] = KERNEL_CODE_SEGMENT;
        context[DS_INDEX] = KERNEL_DATA_SEGMENT;
    }

    return(0);
}

int context_destroy
(
    struct sched_thread *th
)
{
    struct sched_owner *owner     = NULL;
    virt_addr_t   *context   = NULL;
    struct vm_ctx      *vm_ctx    = NULL;
    virt_size_t   rsp0       = 0;

    if(th == NULL)
    {
        return(-1);
    }

    context = (virt_addr_t*)th->context;

    if(context == NULL)
    {
        return(-1);
    }
    
    rsp0 = context[RSP0_INDEX];
    owner = th->owner;
    vm_ctx = owner->vm_ctx;

    if(rsp0 != VM_INVALID_ADDRESS)
    {
        /* sanitize the RSP0 stack */
        memset((void*) rsp0, 0, PAGE_SIZE);
        vm_free(vm_ctx, rsp0, PAGE_SIZE);
    }

    /* sanitize context memory */
    memset(context, 0, CONTEXT_SIZE);
    kfree(context);

    return(0);
}

void context_switch
(
    struct sched_thread *prev,
    struct sched_thread *next
)
{
    if(prev != NULL)
    {
        gdt_update_tss(next->unit->cpu, 
                      ((virt_addr_t*)next->context)[RSP0_INDEX]);
        __context_switch(prev->context, next->context);
    }
    else if(next != NULL)
    {
        gdt_update_tss(next->unit->cpu, 
                      ((virt_addr_t*)next->context)[RSP0_INDEX]);

        __context_switch(0, next->context);
    }
    else
    {
        kprintf("NOT SWITCHING TO/FROM ANYTHING\n");
    }
}