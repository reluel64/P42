#ifndef intch
#define intch


typedef struct ipi_packet_t
{

}ipi_packet_t;


typedef struct intc_api_t
{
    int  (*enable)          (dev_t *);
    int  (*disable)         (dev_t *);
    int  (*send_ipi)        (dev_t *, ipi_packet_t *);
}intc_api_t;

#endif