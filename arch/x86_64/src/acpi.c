#include <acpi.h>
#include <vmmgr.h>
#include <utils.h>

typedef struct
{   
    rsdp_v2_t rsdp; /* since v2 is superseding v1, we declare the v2 here
                     * and in case we find a v1, just use the valid fields
                     */
    rsdt_t *rsdt;
    xsdt_t *xsdt;
    uint8_t init_ok;
}acpi_root_t;

typedef struct
{
    char  *sign;
    uint8_t sign_len;
    int (*callback)(void *tbl);
}acpi_handler_t;

static int acpi_apic_callback(void *pv);

static acpi_handler_t dispatch_tbl[] = 
{
    {"APIC", 4, acpi_apic_callback}
};

static acpi_root_t  acpi_root;




static void *acpi_map(phys_addr_t addr, phys_size_t size, uint32_t attr)
{
    phys_size_t align_addr = 0;
    phys_size_t diff = 0;
    virt_addr_t ret_addr = 0;

    align_addr = ALIGN_DOWN(addr, PAGE_SIZE);
    diff = addr - align_addr;

    /* compensate base difference */
    size += diff; 

    if(size % PAGE_SIZE)
        size = ALIGN_UP(size, PAGE_SIZE);

    ret_addr = (virt_addr_t) vmmgr_map(align_addr, 0, size, attr);
    
    ret_addr += diff;

    return((void*)ret_addr);

}

static int acpi_unmap(virt_addr_t addr, virt_size_t size)
{
    virt_addr_t align_addr = 0;
    virt_size_t diff = 0;
    int status = 0;

    align_addr = ALIGN_DOWN(addr, PAGE_SIZE);
    diff = addr - align_addr;

    /* compensate base difference */
    size += diff; 

    if(size % PAGE_SIZE)
        size = ALIGN_UP(size, PAGE_SIZE);

    status = (virt_addr_t) vmmgr_unmap((void*)align_addr, size);

    return(status);
}

int acpi_init(void)
{
    uint32_t     map_len  = (EBDA_END - EBDA_START) + 1;
    uint32_t     count    = map_len / sizeof(rdsp_srch_t);
    rdsp_srch_t *rdsp_ptr = NULL;
    rsdp_v1_t   *rsdp_v1  = NULL;
    rsdp_v2_t   *rsdp_v2  = NULL;
    uint8_t      copy_len = 0;
    virt_size_t  phys_map = 0;

    memset(&acpi_root, 0, sizeof(acpi_root_t));

    rdsp_ptr = acpi_map(EBDA_START, map_len, 0);

    for(uint32_t i = 0; i < count; i++)
    {
        if(memcmp(rdsp_ptr[i].sign, RSDP_SIGNATURE, 8) == 0)
        {
            switch(rdsp_ptr[i].revision)
            {
                case 1:
                    copy_len = sizeof(rsdp_v1_t);
                    break;

                case 2:
                    copy_len = sizeof(rsdp_v2_t);
                    break;

                default:
                    kprintf("Unknown Revision 0x%x\n",rdsp_ptr[i].revision);
                    return(-1);
            }

            memcpy(&acpi_root.rsdp, &rdsp_ptr[i], copy_len);
            break;
        }
    }

    /* Release virtual memory */
    acpi_unmap((virt_addr_t)rdsp_ptr, map_len);

    switch(acpi_root.rsdp.revision)
    {
        case 1:
            acpi_root.rsdt = acpi_map(acpi_root.rsdp.rsdt, PAGE_SIZE, 0);
            break;

        case 2:
            acpi_root.xsdt = acpi_map(acpi_root.rsdp.xsdt, PAGE_SIZE, 0);
            break;

        default:
            return(-1);        
    }

    /* Both failed? - get out then */
    if(acpi_root.rsdt == NULL && acpi_root.xsdt == NULL)
        return(-1);
    
    acpi_root.init_ok = 1;
    for(uint8_t i = 0; i < 3; i++)
    {
       // kprintf("XSDT length 0x%x\n",acpi_root.xsdt->entry[i]);

        description_header_t *dh = acpi_map(acpi_root.xsdt->entry[i], PAGE_SIZE,0);

       // kprintf("%s\n",dh->sign);

        acpi_unmap(dh, PAGE_SIZE);


    }
    return(0);
}

static int acpi_apic_callback(void *pv)
{
    madt_t *madt = (madt_t*)pv;
    uint32_t pos = sizeof(madt_t);
    uint8_t type = NULL;
    uint8_t len   = NULL;
    uint8_t *element = (uint8_t*)madt;

    while(pos < madt->hdr.length)
    {
        type = element [pos];
        len = element  [pos + 1];
       // kprintf("Type 0x%x Len 0x%x\n",type, len);
        
        if(type == PROCESSOR_LOCAL_APIC)
        {
            processor_lapic_t *lapic = (processor_lapic_t*) &element[pos];

            kprintf("CPU ID #%d CPU UID 0x%x Flags 0x%x\n",lapic->apic_id, lapic->acpi_cpu_uid, lapic->flags);
        }
        else if(type == IO_APIC)
        {
            io_apic_t *io_apic = (io_apic_t*) &element[pos];

            kprintf("APIC ID #%d APIC ADDR 0x%x GSIB 0x%x\n",io_apic->io_apic_id, io_apic->io_apic_address, io_apic->gsib);
        }
        else if(type == INTERRUPT_SOURCE_OVERRIDE)
        {
            intr_src_override_t *iso = (intr_src_override_t*) &element[pos];
            kprintf("BUS #%d SOURCE 0x%x GSI 0x%x FLAGS 0x%x\n",iso->bus, iso->source, iso->gsi, iso->flags);
        }
       

        pos += len;
    }
    
}

int acpi_read_tables()
{
    uint32_t entry_count = 0;
    phys_addr_t entry_address = 0;
    description_header_t *hdr = NULL;
    uint32_t table_len =  0;

    if(acpi_root.init_ok == 0)
        return(-1);

    if(acpi_root.rsdp.revision == 1)
        entry_count = (acpi_root.rsdt->hdr.length - offsetof(rsdt_t, entry)) / sizeof(uint32_t);
    else
        entry_count = (acpi_root.xsdt->hdr.length - offsetof(xsdt_t, entry)) / sizeof(uint64_t);
    
    for(uint32_t i = 0; i < entry_count; i++)
    {
        if(acpi_root.rsdp.revision == 1)
            entry_address = acpi_root.rsdt->entry[i];
        else
            entry_address = acpi_root.xsdt->entry[i];

        
        hdr = (description_header_t*)acpi_map(entry_address, PAGE_SIZE, 0);

       
        if(hdr != NULL && hdr->length > PAGE_SIZE)
        {
            table_len = hdr->length;
            acpi_unmap(hdr, PAGE_SIZE);
            hdr = (description_header_t*)acpi_map(entry_address, table_len, 0);
        }
    
        if(hdr == NULL)
            continue;

        /* Begin dispatching */

        for(uint32_t j = 0; j < sizeof(dispatch_tbl) / sizeof(acpi_handler_t); j++)
        {
            if(!memcmp(dispatch_tbl[j].sign, hdr->sign, dispatch_tbl[j].sign_len))
            {
                dispatch_tbl[j].callback(hdr);
            }
        }

    }
}