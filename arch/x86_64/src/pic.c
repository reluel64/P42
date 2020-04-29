#include <port.h>
#include <pic.h>

void pic_disable(void)
{
    __outb(0xa1, 0xff);
    __outb(0x21, 0xff);
}