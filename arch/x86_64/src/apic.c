#include <vmmgr.h>
#include <stddef.h>
#include <linked_list.h>
#include <isr.h>
#include <utils.h>
#include <apic.h>

#define LVT_ERROR_VECTOR (239)
#define SPURIOUS_VECTOR (240)

extern uint64_t __read_apic_base(void);
extern void     __write_apic_base(uint64_t base);
extern uint8_t  __check_x2apic(void);
extern uint64_t  __max_physical_address();

static int lapic_timer_isr(void *pv, uint64_t error_code);
static int lapic_error_isr(void *pv, uint64_t error_code);



static int lapic_isr(void *pv, uint64_t error_code);
static int lapic_spurious_isr(void *pv, uint64_t error_code);




#if 0
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


uint32_t apic_id_get(void)
{
    uint32_t apic_id = 0;
    apic_reg_t *reg = NULL;
    uint64_t apic_phys_base = 0;
    uint32_t map_size = 0;

    apic_phys_base = __read_apic_base();
    map_size  = ALIGN_UP(sizeof(apic_reg_t), PAGE_SIZE);

    reg = vmmgr_map(NULL,apic_phys_base, 
                    0, map_size, 
                    VMM_ATTR_NO_CACHE);

    if(reg != NULL)
    {
        apic_id = reg->id[0] >> 24;
        vmmgr_unmap(NULL, (void*)reg, map_size);
        return(apic_id);
    }

    return(-1);
}

uint8_t apic_is_bsp(void)
{
    uint64_t msr_val = 0;

    msr_val = __read_apic_base();

    msr_val = ((msr_val >> 8) & 0x1);

    return(msr_val);
}

uint64_t apic_get_phys_addr(void)
{
    uint64_t apic_base = 0;
    uint64_t max_phys_mask = 0;

    apic_base = __read_apic_base();
    max_phys_mask =  __max_physical_address();

    max_phys_mask -= 1;
    max_phys_mask =  ((1ull << max_phys_mask) - 1);

    apic_base = apic_base & ~(uint64_t)0xFFF;
    apic_base = apic_base & max_phys_mask;

   return(apic_base);
}