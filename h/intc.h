#ifndef intch
#define intch

#include <devmgr.h>

#define INTERRUPT_CONTROLLER "intc"
#define IPI_INVLPG               (0x1)
#define IPI_INIT                 (0x2)
#define IPI_START_AP             (0x3)
#define IPI_SCHED                (0x4)

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

struct ipi_packet
{
    uint8_t dest_mode;
    uint8_t dest;
    uint8_t type;
    uint8_t level;
    uint8_t vector;
    uint8_t trigger;
    uint32_t dest_cpu;
};


struct intc_api
{
    int  (*enable)          (struct device_node *);
    int  (*disable)         (struct device_node *);
    int  (*send_ipi)        (struct device_node *, struct ipi_packet *);
    int  (*mask)            (struct device_node *, int);
    int  (*unmask)          (struct device_node *, int);
};

#endif