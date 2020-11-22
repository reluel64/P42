#ifndef apich
#define apich

#include <stddef.h>

#define APIC_DRIVER_NAME "APIC"
#define APIC_BASE_MSR (0x1B)


#define APIC_SVR_ENABLE_BIT (1 << 8)
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

#define APIC_ICR_LEVEL_DEASSERT (0x0)
#define APIC_ICR_LEVEL_ASSERT (0x1)

/* APIC ICR trigger mode */
#define APIC_ICR_TRIGGER_EDGE   (0x0)
#define APIC_ICR_TRIGGER_LEVEL     (0x1)

#define APIC_ICR_DELIVERY_STATUS_MASK (0x1000)

#define APIC_ID_SMT(x)     ((x)         & 0xF)
#define APIC_ID_CORE(x)    (((x) >> 4)  & 0xF)
#define APIC_ID_MODULE(x)  (((x) >> 8)  & 0xF)
#define APIC_ID_TILE(x)    (((x) >> 16) & 0xF)
#define APIC_ID_DIE(x)     (((x) >> 20) & 0xF)
#define APIC_ID_PACKAGE(x) (((x) >> 24) & 0xF)
#define APIC_ID_CLUSTER(x) (((x) >> 28) & 0xF)




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

typedef struct apic_instance_t
{
    list_node_t node;
    uint32_t apic_id;
    phys_addr_t paddr;
    volatile apic_reg_t *reg;
    
    uint8_t timer_fired;
}apic_instance_t;

typedef struct apic_device_t
{
    volatile apic_reg_t *reg;
    phys_addr_t paddr;
    virt_addr_t vaddr;
    uint32_t apic_id;
    uint8_t polarity;
    uint8_t trigger;
    uint8_t lint;
}apic_device_t ;

typedef struct apic_drv_private_t
{
    phys_addr_t paddr;
    virt_addr_t vaddr;
    volatile apic_reg_t *reg;
}apic_drv_private_t;




#endif