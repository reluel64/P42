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
    uint32_t sivr       [4];
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

uint8_t apic_is_bsp(void);
uint32_t apic_id_get(void);
uint64_t apic_get_phys_addr(void);
#endif