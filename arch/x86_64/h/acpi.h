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

int acpi_init(void);
#endif