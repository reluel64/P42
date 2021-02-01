#ifndef pcpuh
#define pcpuh

#include <defs.h>
#include <stdint.h>

typedef struct pcpu_regs_t
{
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rbp;
}__attribute__((packed))  pcpu_regs_t;

typedef struct pcpu_context_t
{
    uint64_t          dseg;
    uint64_t          addr_spc;
    pcpu_regs_t       regs;
    interrupt_frame_t iframe;
    
    /* below should go things that 
     * do not need to be popped from the stack 
     */
    
    uint64_t          esp0;
}__attribute__((packed))  pcpu_context_t;



#endif