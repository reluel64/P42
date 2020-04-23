#include <stdint.h>
#include <serial.h>
#include <utils.h>
#include <pfmgr.h>
#include <pagemgr.h>
#include <vmmgr.h>
#include <gdt.h>
#include <isr.h>
#include <liballoc.h>
#include <spinlock.h>
#include <acpi.h>
int apic_timer_start(uint32_t timeout);

uint8_t *apic_base ;extern void physmm_dump_bitmaps(void);
extern int vmmgr_change_attrib(virt_addr_t virt, virt_size_t len, uint32_t attr);
extern uint64_t start_ap_begin;
extern uint64_t start_ap_end;
extern int physf_early_init(void);
extern int physf_early_alloc_pf(phys_size_t pf, uint8_t flags, alloc_cb cb, void *pv);

extern int physf_init(void);
static int test_callback(phys_addr_t addr, phys_size_t count, void *pv)
{
    kprintf("Address 0x%x Count 0x%x\n",addr, count);
    return(0);
}

void kmain()
{
    /* Init polling console */
    init_serial();
    pagemgr_boot_temp_map_init();

    pfmgr_early_init();

    /* Initialize page manager*/
    if(pagemgr_init() != 0)
        return;

    /* Initialize Virtual Memory Manager */
    if(vmmgr_init() != 0)
        return;

    /* Initialize Page Frame Manager*/
    if(pfmgr_init() != 0)
        return;
        
    if(gdt_init() != 0)
        return;

    if(isr_init()!= 0)
        return;

    if(pagemgr_install_handler() != 0)
        return;


    vga_init();
    disable_pic();

    if(lapic_init())
        return;



uint64_t iter= 0;
#if 0
    for(uint64_t i = 1024*1024; i< 0x500000000;i+=1024*1024)
    {
        void *p = kmalloc(i);
        iter ++;
    
        if(p == NULL)
            break;
        kprintf("ALLOC_OK ITER  0x%x -> 0x%x\n",i, p);    
        //pg->dealloc(0xffff800001000000, i);
        kfree(p);
    }

     #endif   

#if 0
#define ACPI_MAX_INIT_TABLES   512
static ACPI_TABLE_DESC      TableArray[ACPI_MAX_INIT_TABLES];

memset(TableArray, 0, sizeof(TableArray));

ACPI_STATUS sts = AcpiInitializeTables(TableArray, ACPI_MAX_INIT_TABLES, TRUE);

uint8_t *hdr = NULL;
sts = AcpiGetTable(ACPI_SIG_SRAT,1,&hdr);
kprintf("STATUS %d\n",sts);


kprintf("TABLE_SIZE %d\n",sizeof(TableArray));

kprintf("TESTSATRING\n");
ACPI_TABLE_HEADER *phdr = hdr;
ACPI_SUBTABLE_HEADER *sub = hdr + sizeof(ACPI_TABLE_SRAT) + sizeof(ACPI_SRAT_CPU_AFFINITY) *4;
ACPI_SRAT_MEM_AFFINITY *mem = sub;

kprintf("LENGTH %d\n",sub->Type);
kprintf("TYPE %d DOMAIN %d BASE 0x%x LEN 0x%x\n",mem[0].Header.Type, mem[0].ProximityDomain, mem[0].BaseAddress, mem[0].Length);
kprintf("TYPE %d DOMAIN %d BASE 0x%x LEN 0x%x\n",mem[1].Header.Type, mem[1].ProximityDomain, mem[1].BaseAddress, mem[1].Length);
kprintf("TYPE %d DOMAIN %d BASE 0x%x LEN 0x%x\n",mem[2].Header.Type, mem[2].ProximityDomain, mem[2].BaseAddress, mem[2].Length);
kprintf("TYPE %d DOMAIN %d BASE 0x%x LEN 0x%x\n",mem[3].Header.Type, mem[3].ProximityDomain, mem[3].BaseAddress, mem[3].Length);
kprintf("TYPE %d DOMAIN %d BASE 0x%x LEN 0x%x\n",mem[4].Header.Type, mem[4].ProximityDomain, mem[4].BaseAddress, mem[4].Length);
kprintf("TYPE %d DOMAIN %d BASE 0x%x LEN 0x%x\n",mem[5].Header.Type, mem[5].ProximityDomain, mem[5].BaseAddress, mem[5].Length);
kprintf("TYPE %d DOMAIN %d BASE 0x%x LEN 0x%x\n",mem[6].Header.Type, mem[6].ProximityDomain, mem[6].BaseAddress, mem[6].Length);
kprintf("TYPE %d DOMAIN %d BASE 0x%x LEN 0x%x\n",mem[7].Header.Type, mem[7].ProximityDomain, mem[7].BaseAddress, mem[7].Length);


while(1);
sts = AcpiLoadTables();

kprintf("DONE\n");
//    extern int wake_cpu();

  //  wake_cpu();
  #endif
}

