#ifndef intch
#define intch

#include <devmgr.h>

#define INTERRUPT_CONTROLLER "intc"

typedef struct ipi_packet_t
{
    uint8_t dest;
}ipi_packet_t;


typedef struct intc_api_t
{
    int  (*enable)          (dev_t *);
    int  (*disable)         (dev_t *);
    int  (*send_ipi)        (dev_t *, ipi_packet_t *);
}intc_api_t;

int intc_disable(dev_t *dev);
int intc_enable(dev_t *dev);
int intc_send_ipi(dev_t *dev, ipi_packet_t *ipi);
#endif