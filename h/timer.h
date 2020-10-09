#ifndef timerh
#define timerh

#include <stdint.h>
#include <linked_list.h>
typedef  int (*timer_callback_t)(void);

typedef struct timer_t
{
    list_node_t node;
    char *name;
    int (*probe)(void);
    int (*init)(void);
    int (*arm)(uint32_t, timer_callback_t cb, void *cb_pv);
    int (*disarm)(void);
    int (*uninit)(void);
}timer_t;

#endif