#ifndef apich
#define apich

#include <stddef.h>
#include <stdint.h>
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

/* APIC REGISTERS */
#define APIC_REGISTER_START                 (0x800)
#define LOCAL_APIC_ID_REGISTER              (0x802)
#define LOCAL_APIC_VERSION_REGISTER         (0x803)
#define TASK_PRIORITY_REGISTER              (0x808)
#define PROCESSOR_PRIORITY_REGISTER         (0x80a)
#define EOI_REGISTER                        (0x80B)
#define LOGICAL_DESTINATION_REGISTER        (0x80D)
#define SPURIOUS_INTERRUPT_VECTOR_REGISTER  (0x80F)
#define IN_SERVICE_REGISTER                 (0x810) /* 8 bytes */
#define TRIGGER_MODE_REGISTER               (0x818) /* 8 bytes */
#define INTERRUPT_REQUEST_REGISTER          (0x820) /* 8 bytes */
#define ERROR_STATUS_REGISTER               (0x828)
#define LVT_CMCI_REGISTER                   (0x82F)
#define INTERRUPT_COMMAND_REGISTER          (0x830)
#define LVT_TIMER_REGISTER                  (0x832)
#define LVT_THERMAL_SENSOR_REGISTER         (0x833)
#define LVT_PERFORMANCE_MONITORING_REGISTER (0x834)
#define LVT_INT0_REGISTER                   (0x835)
#define LVT_INT1_REGISTER                   (0x836)
#define LVT_ERROR_REGISTER                  (0x837)
#define INITIAL_COUNT_REGISTER              (0x838)
#define CURRENT_COUNT_REGISTER              (0x839)
#define DIVIDE_CONFIGURATION_REGISTER       (0x83E)
#define SELF_IPI_REGISTER                   (0x83F)
#define APIC_REGISTER_END                   (0x8FF)

struct apic_device
{
    phys_addr_t paddr;
    virt_addr_t vaddr;
    uint32_t apic_id;
    uint8_t polarity;
    uint8_t trigger;
    uint8_t lint;
};

struct apic_drv_private
{
    uint8_t     x2;
    phys_addr_t paddr;
    virt_addr_t vaddr;
    
    int (*apic_write)
    (
        virt_addr_t reg_base,
        uint32_t    reg,
        uint32_t    *data,
        uint32_t    cnt
    );

    int (*apic_read)
    (
        virt_addr_t reg_base,
        uint32_t    reg,
        uint32_t    *data,
        uint32_t    cnt
    );
};




#endif