;Interrupt handling

global disable_interrupts
global enable_interrupts
global isr_entry
global load_idt
global load_gdt
global load_tss
global test_interrupt
extern dummy_interrupt

global write_port
global read_port
global show_ch

disable_interrupts:
    cli
    ret

enable_interrupts:
    sti
    ret


isr_entry:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rsp
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15


    cld

    call dummy_interrupt
    
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rsp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    iretq


load_idt:
    lidt[rdi]
    ret


load_gdt:
    lgdt[rdi]
    ret

load_tss:
    ltr di
    ret
    
test_interrupt:
    int 32
    ret


read_port:
	mov edx, [rsp + 8]
	in al, dx	
	ret

write_port:
	mov   edx, [rsp + 8]    
	mov   al, [rsp + 8 + 8]  
	out   dx, al  
	ret