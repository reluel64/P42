#ifndef apich
#define apich

#include <stddef.h>

typedef struct apic_reg_t
{
    uint32_t rsrvd_0    [4];
    uint32_t rsrvd_1    [4];
    uint32_t id         [4];
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
    uint32_t svr        [4];
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

}__attribute__((packed)) apic_reg_t;

typedef struct apic_t
{
    phys_addr_t paddr;
    apic_reg_t *reg;
    uint8_t x2apic;
}apic_t;


typedef struct apic_ipi_low_dword_t
{
    uint8_t vector;
    uint32_t delivery_mode:3;
    uint32_t dest_mode:1;
    uint32_t delivery_status:1;
    uint32_t rsrvd_1:1;
    uint32_t level:1;
    uint32_t trigger:1;
    uint32_t rsrvd_2:2;
    uint32_t dest_shortland:2;
    uint32_t rsrvd_3:12;

}__attribute__((packed)) apic_ipi_low_dword_t;

typedef struct apic_ipi_high_dword_t
{
    uint8_t reserved[3];
    uint8_t dest_field;

}__attribute__((packed)) apic_ipi_high_dword_t;

typedef struct apic_ipi_packet_t
{

    union
    {
        apic_ipi_low_dword_t low_bits;
        uint32_t low;
    };
    union
    {
        apic_ipi_high_dword_t high_bits;
        uint32_t high;
    };

}__attribute__((packed)) apic_ipi_packet_t;


#define APIC_SVR_ENABLE_BIT (1 << 7)
#define APIC_SVR_VEC_MASK(x)   (0xFF & (x))

#define APIC_LVT_INT_MASK       (1 << 16)
#define APIC_LVT_VECTOR_MASK(x)    (0xFF & (x))

/* APIC ICR delivery mode*/
#define APIC_ICR_DELIVERY_FIXED         (0b000)
#define APIC_ICR_DELIVERY_LOWEST        (0b001)
#define APIC_ICR_DELIVERY_SMI           (0b010)
#define APIC_ICR_DELIVERY_RESERVED      (0b011)
#define APIC_ICR_DELIVERY_NMI           (0b100)
#define APIC_ICR_DELIVERY_INIT          (0b101)
#define APIC_ICR_DELIVERY_INIT_DEASSERT (0b101)
#define APIC_ICR_DELIVERY_START         (0b110)

/* APIC ICR destination mode */
#define APIC_ICR_DEST_MODE_PHYSICAL    (0x0)
#define APIC_ICR_DEST_MODE_LOGICAL     (0x1)

/* APIC destination shortland */
#define APIC_ICR_DEST_SHORTLAND_NO              (0b00)
#define APIC_ICR_DEST_SHORTLAND_SELF            (0b01)
#define APIC_ICR_DEST_SHORTLAND_ALL_AND_SELF    (0b10)
#define APIC_ICR_DEST_SHORTLAND_ALL_NO_SELF     (0b11)

/* APIC ICR trigger mode */
#define APIC_ICR_TRIGGER_EDGE   (0x0)
#define APIC_ICR_TRIGGER_LEVEL     (0x1)


uint8_t apic_is_bsp(void);
uint32_t apic_id_get(void);
uint64_t apic_get_phys_addr(void);
int apic_cpu_init(cpu_entry_t *cpu);
int apic_send_ipi
(
    cpu_entry_t *cpu,
    apic_ipi_packet_t *ipi
);
#endif