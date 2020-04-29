#include <vmmgr.h>
#include <stddef.h>
#include <linked_list.h>
#include <isr.h>
#include <utils.h>
#define LOCAL_APIC_ID                      (0x020)
#define LOCAL_APIC_VERSION                 (0x030)
#define TASK_PRIORITY_REGISTER             (0x080)
#define ARBITRATION_PRIORITY_REGISTER      (0x090)
#define PROCESSOR_PRIORITY_REGISTER        (0x0A0)
#define EOI_REGISTER                       (0x0B0)
#define REMOTE_READ_REGISTER               (0x0C0)
#define LOGICAL_DESTINATION_REGISTER       (0x0D0)
#define DESTINATION_FORMAT_REGISTER        (0x0E0)
#define SPURIOUS_INTERRUPT_REGISTER        (0x0F0)

#define IN_SERVICE_REGISTER_0_31           (0x100)
#define IN_SERVICE_REGISTER_32_63          (0x110)
#define IN_SERVICE_REGISTER_64_95          (0x120)
#define IN_SERVICE_REGISTER_96_127         (0x130)
#define IN_SERVICE_REGISTER_128_159        (0x140)
#define IN_SERVICE_REGISTER_160_191        (0x150)
#define IN_SERVICE_REGISTER_192_223        (0x160)
#define IN_SERVICE_REGISTER_224_244        (0x170)

#define TRIGGER_MODE_REGISTER_0_31         (0x180)
#define TRIGGER_MODE_REGISTER_32_63        (0x190)
#define TRIGGER_MODE_REGISTER_64_95        (0x1A0)
#define TRIGGER_MODE_REGISTER_96_127       (0x1B0)
#define TRIGGER_MODE_REGISTER_128_159      (0x1C0)
#define TRIGGER_MODE_REGISTER_160_191      (0x1D0)
#define TRIGGER_MODE_REGISTER_192_223      (0x1E0)
#define TRIGGER_MODE_REGISTER_224_255      (0x1F0)

#define INTERRUPT_REQUEST_REGISTER_0_31    (0x200)
#define INTERRUPT_REQUEST_REGISTER_63      (0x210)
#define INTERRUPT_REQUEST_REGISTER_64_95   (0x220)
#define INTERRUPT_REQUEST_REGISTER_127     (0x230)
#define INTERRUPT_REQUEST_REGISTER_159     (0x240)
#define INTERRUPT_REQUEST_REGISTER_191     (0x250)
#define INTERRUPT_REQUEST_REGISTER_223     (0x260)
#define INTERRUPT_REQUEST_REGISTER_224_244 (0x270)

#define ERROR_STATUS_REGISTER              (0x280)
#define LVT_CMCI_REGISTER                  (0x2F0)
#define INTERRUPT_COMMAND_REGISTER_0_31    (0x300)
#define INTERRUPT_COMMAND_REGISTER_32_64   (0x310)
#define LVT_TIMER_REGISTER                 (0x320)
#define LVT_THERMAL_SENSOR_REGISTER        (0x330)
#define LVT_PERF_MONITOR_REGISTER          (0x340)
#define LVT_LINT0_REGISTER                 (0x350)
#define LVT_LINT1_REGISTER                 (0x360)
#define LVT_LINT_ERROR_REGISTER            (0x370)

#define TIMER_INITIAL_COUNT_REGISTER       (0x380)
#define TIMER_CURRENT_COUNT_REGISTER       (0x390)
#define TIMER_DIVIDE_CONFIG_REGISTER       (0x3E0)


#define LVT_ERROR_VECTOR (239)
#define SPURIOUS_VECTOR (240)

extern uint64_t read_lapic_base(void);
extern void     write_lapic_base(uint64_t base);
static int lapic_timer_isr(void *pv, uint64_t error_code);
static int lapic_error_isr(void *pv, uint64_t error_code);
typedef struct
{
    uint32_t rsrvd_0    [4];
    uint32_t rsrvd_1    [4];
    uint32_t      id    [4];
    uint32_t version    [4];
    uint32_t rsrvd_2    [4];
    uint32_t rsrvd_3    [4];
    uint32_t rsrvd_4    [4];
    uint32_t rsrvd_5    [4];
    uint32_t tpr        [4];
    uint32_t apr        [4];
    uint32_t ppr        [4];
    uint32_t eoi        [4];
    uint32_t rrd        [4];
    uint32_t ldr        [4];
    uint32_t dfrr       [4];
    uint32_t sivr       [4];
    uint32_t isr_1      [32];
    uint32_t tmr        [32];
    uint32_t irr        [32];
    uint32_t esr        [4];
    uint32_t rsrvd_6    [24];
    uint32_t lvt_cmci   [4];
    uint32_t icr        [8];
    uint32_t lvt_timer  [4];
    uint32_t lvt_therm  [4];
    uint32_t lvt_perf   [4];
    uint32_t lvt_lint0  [4];
    uint32_t lvt_lint1  [4];
    uint32_t lvt_err  [4];
    uint32_t timer_icnt [4];
    uint32_t timer_ccnt [4];
    uint32_t rsrvd_7    [20];
    uint32_t timer_div  [4];
    uint32_t rsrvd_8    [4];

}__attribute__((packed)) lapic_register_t;

typedef struct
{
    list_node_t node;
    uint64_t  lapic_phys;
    uint64_t  lapic_virt;
}lapic_t;

typedef struct 
{
    list_head_t lapic_head;
    uint64_t bsp_lapic_phys;
    uint64_t bsp_lapic_virt;
}lapic_root_t;

static lapic_root_t lapic_root;

static int lapic_isr(void *pv, uint64_t error_code);
static int lapic_spurious_isr(void *pv, uint64_t error_code);

extern void _sti();
extern void _cli();

int lapic_init(void)
{
    lapic_register_t *reg = NULL;
    lapic_root.bsp_lapic_phys = read_lapic_base() & ~(uint64_t)0xFFF;
    lapic_root.bsp_lapic_virt = (uint64_t)vmmgr_map(NULL, lapic_root.bsp_lapic_phys,
                                        0, 
                                        sizeof(lapic_register_t),
                                        VMM_ATTR_NO_CACHE | 
                                        VMM_ATTR_WRITE_THROUGH |
                                        VMM_ATTR_WRITABLE);
                                        
    kprintf("APIC BASE 0x%x\n",lapic_root.bsp_lapic_phys);
    if(lapic_root.bsp_lapic_virt == 0)
        return(-1);

    reg = (lapic_register_t*)lapic_root.bsp_lapic_virt;

    /* disable APIC */
    reg->sivr[0] &= ~(1 << 8);

    /* Set interrupt handlers */
    isr_install(lapic_error_isr, NULL, LVT_ERROR_VECTOR);
    isr_install(lapic_spurious_isr, NULL, SPURIOUS_VECTOR);
    
    isr_install(lapic_timer_isr, &lapic_root, 32);

    /* Enable APIC and set spurious vector to 240 */
    reg->sivr[0] = ((uint32_t)(1 << 8) | (uint32_t)SPURIOUS_VECTOR);
    /* Set up the error vector */
    reg->lvt_err[0] &= 0xffffff00;
    reg->lvt_err[0] |= (uint32_t)LVT_ERROR_VECTOR;
    
    /* enable interrupts */
    _sti();
    return(0);
}

static int lapic_timer_isr(void *pv, uint64_t error_code)
{
    lapic_root_t *root = pv;
    lapic_register_t *reg = NULL;
    
    reg = (lapic_root_t*)root->bsp_lapic_virt;
    kprintf("%s\n",__FUNCTION__);
    reg->eoi[0] = 0x1;

    return(0);
}


static int lapic_isr(void *pv, uint64_t error_code)
{
    lapic_root_t *root = pv;
    lapic_register_t *reg = NULL;
    uint64_t apic_base = read_lapic_base() & ~(uint64_t)0xFFF;

    if(root->bsp_lapic_phys == apic_base)
    {
        reg = (lapic_register_t*)root->bsp_lapic_virt;
    }
    else
    {
        return(-1);
    }

    if(reg->esr[0] != 0)
    {
        kprintf("ERROR detected\n");
    }
    
    /* Send End-Of-Interrupt */
    reg->eoi[0] = 0x1;
   
    return(0);
}

static int lapic_error_isr(void *pv, uint64_t error_code)
{
    kprintf("ERROR DETECTED\n");
    while(1);
    return(0);
}

static int lapic_spurious_isr(void *pv, uint64_t error_code)
{
    return(0);
}

extern phys_addr_t start_ap_begin;
extern phys_addr_t start_ap_end;
extern phys_addr_t page_base;
extern uint8_t     cpu_on;
extern virt_addr_t gdt_base;
extern virt_addr_t gdt_base_get();

extern void        __wbinvd();
#if 0
int wake_cpu()
{
    pagemgr_t *pg = pagemgr_get();
    phys_size_t offset = (uint64_t)&page_base - (uint64_t)&start_ap_begin;
    phys_addr_t page_addr = page_manager_get_base();
    virt_addr_t gdt = gdt_base_get();
    uint8_t *map  = pg->map(0x8000,0x8000, PAGE_SIZE,VMM_ATTR_NO_CACHE |VMM_ATTR_WRITE_THROUGH |VMM_ATTR_WRITABLE|VMM_ATTR_EXECUTABLE);
    uint8_t *code = &start_ap_begin;
    
    lapic_register_t *reg = NULL;

    reg = (lapic_register_t*)lapic_root.bsp_lapic_virt;


 kprintf("Sending SIPI 0x%x\n", (uint64_t)&start_ap_end - (uint64_t)&start_ap_begin);
  
    reg->icr[0] = 0xc4500;
    memcpy(0x8000, code, (phys_size_t)&start_ap_end - (phys_size_t)&start_ap_begin);
    memcpy(0x8000 + offset, &page_addr, sizeof(phys_addr_t));
    
    
    offset = (uint64_t)&cpu_on - (uint64_t)&start_ap_begin;

    //kprintf("OFFSET 0x%x\n",offset);

    kprintf("CPU_ON 0x%x\n",map[offset]);

    for(uint64_t i=  0; i <256ull*1024*1024 && map[offset] == 0; i++);



       kprintf("Sending SIPI\n");
    reg->icr[0] = 0x000C4600| 0x8;


uint64_t i = 0;
for(i=  0; i <256ull*1024*1024 && map[offset] == 0; i++);

    if(map[offset])
    {
        kprintf("CPU ONLINE 0x%x\n", i);
        while(1);
    }

    kprintf("Sending SIPI\n");
reg->icr[0] = 0x000C4600| 0x8;

for(uint64_t i=  0; i <256ull*1024*1024; i++);
} 


int apic_timer_start(uint32_t timeout)
{
    lapic_register_t *reg = (lapic_register_t*)lapic_root.bsp_lapic_virt;

    reg->lvt_timer[0] = 32 | 1 << 17;
    reg->timer_div[0] = 0b1010;
    reg->timer_icnt[0] = timeout;
}

void apic_add_to_list(void)
{

}

#endif
