#include <stdint.h>
#include <cpu.h>
#include <platform.h>
#include <intc.h>
#include <utils.h>

struct cpu *cpu_current_get(void)
{
    struct device_node *dev = NULL;
    uint32_t cpu_id = 0;
    struct cpu    *cpu = NULL;
    int int_state = 0;

    int_state = cpu_int_check();

    if(int_state)
    {
        cpu_int_lock();
    }

    cpu_id = cpu_id_get();
    
    dev = devmgr_dev_get_by_name(PLATFORM_CPU_NAME, cpu_id);
    cpu = (struct cpu*)dev;

    if(int_state)
    {
        cpu_int_unlock();
    }
    
    return(cpu);
}


int32_t cpu_enqueue_call
(
    uint32_t cpu_id,
    int32_t (*ipi_handler)(void *pv),
    void *pv
)
{
    struct device_node *dev = NULL;
    struct cpu *cpu = NULL;
    struct platform_cpu *pcpu = NULL;
    uint8_t int_status = 0;
    struct ipi_exec_node *ipi_node = NULL;
    int32_t st = -1;
    struct cpu_api *api = NULL;

    dev = devmgr_dev_get_by_name(PLATFORM_CPU_NAME, cpu_id);
    pcpu = (struct platform_cpu*)dev;

    if(pcpu != NULL)
    {
        cpu = &pcpu->hdr;
    }

    if(cpu != NULL)
    {
        spinlock_lock_int(&cpu->ipi_cb_lock, &int_status);

        ipi_node = (struct ipi_exec_node*)
                   linked_list_get_first(&cpu->avail_ipi_cb_slots);

        if(ipi_node != NULL)
        {
            ipi_node->ipi_handler = ipi_handler;
            ipi_node->pv = pv;

            linked_list_add_tail(&cpu->ipi_cb_slots, &ipi_node->node);
            st = 0;
        }

        spinlock_unlock_int(&cpu->ipi_cb_lock, int_status);

        api = devmgr_dev_api_get(dev);

        if(api != NULL && api->send_ipi != NULL)
        {
            api->send_ipi(IPI_DEST_NO_SHORTHAND, cpu_id, PLATFORM_SCHED_VECTOR);
        }
    }

    return(st);
}

int32_t cpu_process_call_list
(
    void *pv,
    struct isr_info *inf
)
{
    struct cpu *cpu = inf->cpu;
    uint8_t int_status = 0;
    struct ipi_exec_node *ipi_node = NULL;
    int32_t (*ipi_handler)(void *pv) = NULL;
    void *ipi_pv = NULL;
    

    if(cpu != NULL)
    {        
        while(1)
        {
            spinlock_lock_int(&cpu->ipi_cb_lock, &int_status);
            ipi_node = (struct ipi_exec_node*)
                       linked_list_get_first(&cpu->ipi_cb_slots);
            
            
            if(ipi_node == NULL)
            {
                spinlock_unlock_int(&cpu->ipi_cb_lock, int_status);
                break;
            }

            linked_list_add_tail(&cpu->avail_ipi_cb_slots, &ipi_node->node);
            spinlock_unlock_int(&cpu->ipi_cb_lock, int_status);

            ipi_handler = ipi_node->ipi_handler;
            ipi_pv = ipi_node->pv;

            if(ipi_handler != NULL)
            {
                ipi_handler(ipi_pv);
            }


        }        
    }

    return(0);
}

int32_t cpu_init_generic
(
    struct cpu *cpu
    
)
{

    memset(&cpu->exec_slots, 0, sizeof(cpu->exec_slots));

    for(uint32_t i = 0; i < CPU_IPI_EXEC_NODE_COUNT; i++)
    {
        linked_list_add_tail(&cpu->avail_ipi_cb_slots, 
                             &cpu->exec_slots[i].node);
    }
}