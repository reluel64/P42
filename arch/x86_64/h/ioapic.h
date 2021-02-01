#ifndef ioapich
#define ioapich

#include <stdint.h>

#define IOAPIC_DRV_NAME "ioapic"

typedef struct ioapic_id_t
{
    uint32_t rsrvd: 24;
    uint32_t ioapic_id: 4;
    uint32_t rsrvd_2:4;

}__attribute__((packed))ioapic_id_t;

typedef struct ioapic_ver_t
{
    uint8_t apic_ver;
    uint8_t rsrvd;
    uint8_t max_redir;
    uint8_t rsrvd_2;
}__attribute__((packed)) ioapic_ver_t;

typedef struct ioapic_arb_t
{
    uint32_t rsrvd: 24;
    uint32_t ioapic_id:4;
    uint32_t rsrvd_2:4;

}__attribute__((packed)) ioapic_arb_t;

typedef struct ioredtbl_t
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

}__attribute__((packed)) ioredtbl_t;

typedef struct ioregsel_t
{
    uint32_t reg_address : 8;
    uint32_t rsrvd: 24;
    
}__attribute__((packed)) ioregsel_t;

typedef struct iowin_t
{
    uint32_t  reg_data;
}__attribute__((packed)) iowin_t;


typedef struct ioapic_t
{
    uint32_t             irq_base;
    uint32_t             id;
    phys_addr_t          phys_base;
    virt_addr_t          virt_base;
    ioredtbl_t           *redir_tbl;
    uint8_t              redir_tbl_count;
    volatile ioregsel_t  *ioregsel;
    volatile iowin_t     *iowin;
}ioapic_t;

#endif