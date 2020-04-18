#ifndef api_h
#define acpi_h
#include <stddef.h>
#include <stdint.h>

#define ACPI_REVISION_1 0x1
#define ACPI_REVISION_2 0x2

#define RSDP_SIGNATURE "RSD PTR " 
#define APIC_SIGNATURE "APIC"

#define EBDA_FROM_BDA 0x40E
#define EBDA_START 0xE0000
#define EBDA_END   0xFFFFF


/* Interrupt Controller Structure Types */
#define PROCESSOR_LOCAL_APIC       (0x00)
#define IO_APIC                    (0x01)
#define INTERRUPT_SOURCE_OVERRIDE  (0x02)
#define NMI_SOURCE                 (0x03)
#define LOCAL_APIC_NMI             (0x04)
#define LOCAL_APIC_ADDR_OVERRIDE   (0x05)
#define IO_SAPIC                   (0x06)
#define LOCAL_SAPIC                (0x07)
#define PLATFORM_INTERRUPT_SOURCES (0x08)
#define PROCESSOR_LOCAL_X2APIC     (0x09)
#define LOCAL_X2APIC_NMI           (0x0A)
#define GIC_CPU_INTERFACE          (0x0B) /* GICC */
#define GIC_DISTRIBUTOR            (0x0C) /* GICD */
#define GIC_MSI_FRAME              (0x0D)
#define GIC_REDISTRIBUTOR          (0x0E) /* GICR */
#define GIC_INTERRUPT_TRANSLATION  (0x0F) /* ITS */
#define RESERVED_BEGIN             (0x10)
#define RESERVED_END               (0x7F)
#define RESERVED_OEM_BEGIN         (0x80)
#define RESERVED_OEM_END           (0xFF)


/* Local APIC Flags */
#define LOCAL_APIC_FLAGS_ENABLED         (1 << 0)
#define LOCAL_APIC_FLAGS_ONMLINE_CAPABLE (1 << 1)

/* MPS INTI Flags */

#define POLARITY_BUS_DEFAULT       (0 << 0)
#define POLARITY_ACTIVE_HIGH       (1 << 0)
#define POLARITY_ACTIVE_LOW        ((1 << 0) | (1 << 1))

#define TRIGGER_MODE_BUS_DEFAULT   (0 << 0)
#define TRIGGER_MODE_EDGE          (1 << 0)
#define TRIGGER_MODE_LEVEL         ((1 << 0) | (1 << 1))

#define PLATFORM_INTERRUPT_PMI           (0x1)
#define PLATFORM_INTERRUPT_INIT          (0x2)
#define PLATFORM_INTERRUPT_CORRECTED     (0x3)

#define PLATFORM_INT_CPEI_OVERRIDE       (0x1)
// TODO: DEFINE SIGNATURES

typedef struct
{
    uint8_t sign[8];
    uint8_t checksum;
    uint8_t oemid[6];
    uint8_t revision;
}__attribute__((packed)) rdsp_srch_t;

typedef struct 
{
    uint8_t sign[8];
    uint8_t checksum;
    uint8_t oemid[6];
    uint8_t revision;
    uint32_t rsdt;
}__attribute__((packed)) rsdp_v1_t;

typedef struct 
{
    uint8_t sign[8];
    uint8_t checksum;
    uint8_t oemid[6];
    uint8_t revision;
    uint32_t rsdt;
    uint32_t length;
    uint64_t xsdt;
    uint8_t ext_checksum;
    uint8_t rsrvd[3];
}__attribute__((packed)) rsdp_v2_t;

typedef struct
{
    uint8_t sign[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    uint8_t oemid[6];
    uint8_t oem_tbl_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_rev;
}__attribute__((packed)) description_header_t;

typedef struct
{
    description_header_t hdr;
    uint32_t entry[1];
}__attribute__((packed)) rsdt_t;

typedef struct
{
    description_header_t hdr;
    uint64_t entry[1];
}__attribute__((packed)) xsdt_t;

typedef struct
{
    uint8_t  type;
    uint8_t  length;
    uint8_t  acpi_cpu_uid;
    uint8_t  apic_id;
    uint32_t flags;
}__attribute__((packed)) processor_lapic_t;

typedef struct
{
    uint8_t  type;
    uint8_t  length;
    uint8_t  io_apic_id;
    uint8_t  reserved;
    uint32_t io_apic_address;
    uint32_t gsib;             /* Global System Interrupt Base */
}__attribute__((packed)) io_apic_t;

typedef struct
{
    uint8_t  type;
    uint8_t  length;
    uint8_t  bus;
    uint8_t  source;    /* IRQ */
    uint32_t gsi;        /* Global System Interrupt */
    uint16_t flags;     /* MPS INTI Flags */
}__attribute__((packed)) intr_src_override_t;

typedef struct
{
    uint8_t type;
    uint8_t length;
    uint16_t flags; /* MPS INTI Flags */
    uint32_t gsi;
}__attribute__((packed)) nmi_source_t;

typedef struct
{
    uint8_t  type;
    uint8_t  length;
    uint8_t  acpi_cpu_uid;
    uint16_t flags;
    uint8_t  lapic_lint;
}__attribute__((packed)) lapic_nmi_t;

typedef struct
{
    uint8_t type;
    uint8_t length;
    uint16_t rsrvd;
    uint64_t lapic_address;
}__attribute__((packed)) lapic_addr_override_t;

typedef struct
{
    uint8_t  type;
    uint8_t  length;
    uint8_t  io_apic_id;
    uint8_t  reserved;
    uint32_t gsib;             /* Global System Interrupt Base */
    uint64_t io_apic_address;
}__attribute__((packed)) io_sapic_t;

typedef struct
{
    uint8_t type;
    uint8_t length;
    uint8_t apic_cpu_id;
    uint8_t local_sapic_id;
    uint8_t local_sapic_eid;
    uint8_t reserved[3];
    uint32_t flags;
    uint32_t acpi_cpu_uid;
    uint8_t acpi_cpu_uid_str[1];
}__attribute__((packed)) local_sapic_t;

typedef struct
{
    uint8_t type;
    uint8_t length;
    uint16_t flags; /* MPS INTI */
    uint8_t int_type;
    uint8_t cpu_id;
    uint8_t cpu_eid;
    uint8_t io_sapic_vector;
    uint32_t gsi; /* Global System Interrupt */
    uint32_t platform_int_flags;
}__attribute__((packed)) platform_int_src_t;

typedef struct
{
    description_header_t hdr;
    uint32_t             local_intr_ctlr_addr;
    uint32_t flags;
    
}madt_t;




int acpi_init(void);
int acpi_read_table(void);
#endif