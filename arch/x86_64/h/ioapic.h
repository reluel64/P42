#ifndef ioapich
#define ioapich

#include <stdint.h>

#define IOAPIC_DRV_NAME "ioapic"

struct __attribute__((packed)) ioapic_id
{
    uint32_t rsrvd: 24;
    uint32_t ioapic_id: 4;
    uint32_t rsrvd_2:4;

};

struct __attribute__((packed)) ioapic_version
{
    uint8_t apic_ver;
    uint8_t rsrvd;
    uint8_t max_redir;
    uint8_t rsrvd_2;
};

struct __attribute__((packed)) ioapic_arb
{
    uint32_t rsrvd: 24;
    uint32_t ioapic_id:4;
    uint32_t rsrvd_2:4;

};

struct __attribute__((packed))  ioredtbl
{
    uint8_t intvec;
    uint8_t delmod:3;
    uint8_t destmod:1;
    uint8_t delivs:1;
    uint8_t intpol:1;
    uint8_t remote_irr:1;
    uint8_t tr_mode:1;
    uint8_t int_mask:1;
    uint64_t rsrvd:39;
    uint8_t dest_field;

};

struct __attribute__((packed))  ioregsel
{
    uint32_t reg_address : 8;
    uint32_t rsrvd: 24;
};

struct __attribute__((packed)) iowin
{
    uint32_t  reg_data;
};


struct ioapic
{
    uint32_t             irq_base;
    uint32_t             id;
    phys_addr_t          phys_base;
    virt_addr_t          virt_base;
    struct ioredtbl           *redir_tbl;
    uint8_t              redir_tbl_count;
    volatile struct ioregsel  *ioregsel;
    volatile struct iowin     *iowin;
};

#endif