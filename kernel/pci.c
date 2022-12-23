#include <stdint.h>
#include <acpi.h>
#include <defs.h>
#include <vm.h>
#define CONFIG_ADDRESS (0xCF8)
#define CONFIG_DATA    (0xCFC)
#define CONFIG_SPACE_SIZE (256)
#define INVALID_VID               (0xffff)
#define INVALID_PID               (0xffff)
#define PCI_MCFG_CONF_SPACE_SIZE  (0x1000)
#define PCI_MAX_DEVICE            (256)
#define PCI_MAX_FUNCTION          (0x1000)
#define SLOTS_PER_BUS             (32)
#define FUNCTIONS_PER_DEVICE      (8)

#define HEADER_TYPE_NORMAL         (0)
#define HEADER_TYPE_BRIDGE         (1 << 0)
#define HEADER_TYPE_CARDBUS        (1 << 1)

#define HEADER_TYPE_MULTI_FUNCTION (1 << 7)
#define HEADER_TYPE_MASK           (0x7F)

/* BIST fields */
#define BIST_COMPLETION_CODE_MASK (0x0F)
#define BIST_START_MASK           (0x40)
#define BIST_CAPABLE_MASK         (0x80)

/* Command register */

#define CMD_REG_IO_SPACE           (1 <<  0)
#define CMD_REG_MEM_SPACE          (1 <<  1)
#define CMD_REG_BUS_MASTER         (1 <<  2)
#define CMD_REG_SPECIAL_CYCLES     (1 <<  3)
#define CMD_REG_MEM_WR_INV         (1 <<  4)
#define CMD_REG_VGA_SNOOP          (1 <<  5)
#define CMD_REG_PARITY_ERR_RESP    (1 <<  6)
#define CMD_REG_SERR_ENABLE        (1 <<  8)
#define CMD_REG_FAST_B2B_ENABLE    (1 <<  9)
#define CMD_REG_INT_DISABLE        (1 << 10)

#define PCI_PHYS_CONF(ecm_base,                           \
                      bus,                                \
                      ecam_start_bus,                     \
                      slot,                               \
                      function                            \
                     )                                    \
                     ((ecm_base)                        + \
                     (((bus) - (ecam_start_bus)) << 20) | \
                     (slot) << 15                       | \
                     (function) << 12)



typedef struct pci_header_common
{
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t command;
    uint16_t status;
    uint8_t revision_id;
    uint8_t prog_if;
    uint8_t subclass;
    uint8_t class_code;
    uint8_t cache_line_size;
    uint8_t latency_timer;
    uint8_t header_type;
    uint8_t bist;
}__attribute__((packed)) pci_header_common;

typedef struct pci_header_type0
{
    pci_header_common hdr;
    uint32_t          bar[6];
    uint32_t          card_cis_pointer;
    uint16_t          subsystem_vendor;
    uint16_t          subsystem_id;
    uint32_t          expansion_rom_address;
    uint8_t           capabilities_pointer;
    uint8_t           reserved[3];
    uint32_t          reserved_1;
    uint8_t           interrupt_line;
    uint8_t           interrupt_pin;
    uint8_t           min_grant;
    uint8_t           max_latency;
}__attribute__((packed)) pci_header_type0;

/* PCI-to-PCI bridge */
typedef struct pci_header_type1
{
    pci_header_common hdr;
    uint32_t          bar[2];
    uint8_t           primary_bus_number;
    uint8_t           secondary_bus_number;
    uint8_t           subordinate_bus_number;
    uint8_t           secondary_latency_timer;
    uint8_t           io_base;
    uint8_t           io_limit;
    uint16_t          secondary_status;
    uint16_t          memory_base;
    uint16_t          memory_limit;
    uint16_t          pref_mem_base;
    uint16_t          pref_mem_limit;
    uint32_t          pref_base_up;
    uint32_t          pref_limit_up;
    uint16_t          io_base_up;
    uint16_t          io_limit_up;
    uint8_t           capability_ptr;
    uint8_t           reserved[3];
    uint32_t          rom_base;
    uint8_t           int_line;
    uint8_t           int_pin;
    uint16_t          bridge_control;
}__attribute__((packed)) pci_header_type1;



int pci_enumerate
(
    void
)
{
    ACPI_STATUS          status           = 0;
    ACPI_TABLE_MCFG      *mcfg            = NULL;
    ACPI_MCFG_ALLOCATION *allocation      = NULL;
    ACPI_TABLE_HEADER    *hdr             = NULL;
    uint32_t             mcfg_alloc_count = 0;
    phys_addr_t          phys_conf        = 0;
    pci_header_common    *phc             = NULL;

    status = AcpiGetTable(ACPI_SIG_MCFG, 0, (ACPI_TABLE_HEADER**)&mcfg);

    if(status != AE_OK)
    {
        return(-1);
    }

    /* compute the amount of items in the ACPI_MCFG_ALLOCATION array */
    mcfg_alloc_count = (mcfg->Header.Length - sizeof(ACPI_TABLE_MCFG)) / 
                                              sizeof(ACPI_MCFG_ALLOCATION);

    /* get the address of ACPI_MCFG_ALLOCATION array */
    allocation = (ACPI_MCFG_ALLOCATION*)((uint8_t*)mcfg + 
                                        sizeof(ACPI_TABLE_MCFG));

    /* Go through the array */
    for(uint32_t i = 0; i < mcfg_alloc_count; i++)
    {
        /* go through each bus */
        for(uint32_t bus = allocation[i].StartBusNumber; 
                     bus < allocation[i].EndBusNumber;
                     bus++
           ) 
        {
            /* go through deices on the bus*/
            for(uint32_t slot = 0; slot < SLOTS_PER_BUS; slot++)
            {   
                /* go through the functions of each device */
                for(uint32_t fcn = 0; fcn < FUNCTIONS_PER_DEVICE; fcn++)
                {
                    phys_conf = PCI_PHYS_CONF(allocation[i].Address,
                                             bus,
                                             allocation[i].StartBusNumber,
                                             slot,
                                             fcn);

                    phc = (pci_header_common*)vm_map(NULL, 
                                                     VM_BASE_AUTO, 
                                                     PCI_MCFG_CONF_SPACE_SIZE,
                                                     phys_conf, 
                                                     0,
                                                     0);

                    /* failed to map? just skip */
                    if((virt_addr_t)phc == VM_INVALID_ADDRESS)
                    {
                        continue;
                    }
                   
                    /* Invalid data? skip */
                    if((phc->vendor_id == INVALID_VID)  && 
                       (phc->device_id == INVALID_PID))
                    {

                        vm_unmap(NULL, 
                                (virt_addr_t)phc, 
                                PCI_MCFG_CONF_SPACE_SIZE);

                        continue;
                    }

                    kprintf("PHC %x -> DID %x VID %x Class %x"\
                            "Subclass %x ProgIf %x " \
                            "Header Type %x\n", phc, phc->device_id, phc->vendor_id, 
                                                phc->class_code, phc->subclass, phc->prog_if, 
                                                phc->header_type);

                    vm_unmap(NULL, (virt_addr_t)phc, PCI_MCFG_CONF_SPACE_SIZE);

                }
            }
        }
    }

    AcpiPutTable((ACPI_TABLE_HEADER*)mcfg);
    kprintf("END CALL\n");
    return(0);
} 


int pci_enumerate_legacy
(
    void
)
{
    return(-1);
}