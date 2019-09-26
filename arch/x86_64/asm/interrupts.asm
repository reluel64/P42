;Interrupt handling


global disable_interrupts
global enable_interrupts
global isr_entry
global load_idt
global load_gdt
global load_tss
global test_interrupt
global interrupt_call
global isr_handlers_fill
extern isr_handler

%macro isr_def 1

isr_entry_point%1:
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
    mov rdi, %1
    call isr_handler
    
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
%endmacro

; We are lazy - ask the assembler to create all the 256 irqs */
%assign i 0 
    %rep 256
        isr_def i
%assign i i+1
%endrep

%macro isr_to_table 1
 mov qword [rdi + (%1 * 8)], isr_entry_point%1
%endmacro




; Fill the interrupt table passed in RDI
isr_handlers_fill:

%assign i 0 
    %rep 256
       isr_to_table i
%assign i i+1
%endrep

    ret

; Disable the interrupts
disable_interrupts:
    cli
    ret

; Enable the interrupts
enable_interrupts:
    sti
    ret

; Load the Interrupt Descriptor Table Register
; RDI is the address of the structure that 
; holds the linear address and the limit
load_idt:
    lidt[rdi]
    ret

; Load the Global Descriptor Table Register
; RDI is the address of the structure that 
; holds the linear address and the limit
load_gdt:
    lgdt[rdi]
    ret

; Load the Task State Segment register
; RDI is the offset of the segment that
; describes the TSS in GDT
load_tss:
    ltr di
    ret


interrupt_call:
    int 32
    ret