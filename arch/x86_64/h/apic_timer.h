#ifndef apic_timerh
#define apic_timerh
#include <apic.h>
#include <spinlock.h>
#include <timer.h>
#include <devmgr.h>
#define APIC_TIMER_NAME "APIC_TIMER"


struct apic_timer
{
    struct device_node   dev_node;
    struct spinlock_rw   lock;
    uint32_t             calib_value;
    timer_tick_handler_t handler;
    void                *handler_data;
    struct time_spec     tm_res;
};


#endif