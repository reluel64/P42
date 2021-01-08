#ifndef intch
#define intch

#include <devmgr.h>

#define INTERRUPT_CONTROLLER "intc"
#define IPI_INVLPG               (0x1)
#define IPI_INIT                 (0x2)
#define IPI_START_AP             (0x3)
#define IPI_RESCHED              (0x4)

/* Destination shorthand */
#define IPI_DEST_NO_SHORTHAND (0x0)
#define IPI_DEST_SELF         (0x1)
#define IPI_DEST_ALL          (0x2)
#define IPI_DEST_ALL_NO_SELF  (0x3)

/* destination mode */
#define IPI_DEST_MODE_PHYS        (0x0)
#define IPI_DEST_MODE_LOGICAL     (0x1)

/* level */
#define IPI_LEVEL_DEASSERT        (0x0)
#define IPI_LEVEL_ASSERT          (0x1)


#define IPI_TRIGGER_EDGE          (0x0)
#define IPI_TRIGGER_LEVEL          (0x1)

typedef struct ipi_packet_t
{
    uint8_t dest_mode;
    uint8_t dest;
    uint8_t type;
    uint8_t level;
    uint8_t vector;
    uint8_t trigger;
    uint32_t dest_cpu;
}ipi_packet_t;


typedef struct intc_api_t
{
    int  (*enable)          (device_t *);
    int  (*disable)         (device_t *);
    int  (*send_ipi)        (device_t *, ipi_packet_t *);
    int  (*mask)            (device_t *, int);
    int  (*unmask)            (device_t *, int);
}intc_api_t;

int intc_disable(device_t *dev);
int intc_enable(device_t *dev);
int intc_send_ipi(device_t *dev, ipi_packet_t *ipi);
#endif