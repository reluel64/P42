; Interrupt handling
; We are using the assembler capability to define macros 
; so that we can save a lot of duplicate code being written

global disable_interrupts
global enable_interrupts
global isr_entry
global load_idt
global load_gdt
global load_tss
global test_interrupt
global interrupt_call
global isr_handlers_fill
global isr_no_ec_begin
global isr_no_ec_end
global isr_ec_begin
global isr_ec_end
global isr_no_ec_sz_start
global isr_no_ec_sz_end
global isr_ec_sz_start
global isr_ec_sz_end
extern isr_handler
; ASM stub for ISRs that do not have 
; error codes
%macro isr_no_ec_def 1
isr_%1:

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
    mov rsi, 0
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

; ASM stub for ISRs that do have 
; error codes
%macro isr_with_ec_def 1
isr_%1:

    pop rax
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
    mov rsi, rax
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
    
    iretq
%endmacro

; We are lazy - ask the assembler to create all the 256 interrupt handlers
isr_no_ec_begin:
%assign i 0 
    %rep 8
        isr_no_ec_def i
%assign i i+1
%endrep

%assign i 9 
    %rep 1
        isr_no_ec_def i
%assign i i+1
%endrep

%assign i 16 
    %rep 1
        isr_no_ec_sz_start:
        isr_no_ec_def i
        isr_no_ec_sz_end:
%assign i i+1
%endrep

%assign i 18
    %rep 3
        isr_no_ec_def i
%assign i i+1
%endrep

%assign i 32
    %rep 256-32
        isr_no_ec_def i
%assign i i+1
%endrep


isr_no_ec_end:

isr_ec_begin:
%assign i 8 
    %rep 1
        isr_with_ec_def i
%assign i i+1
%endrep

%assign i 10
    %rep 5
        isr_with_ec_def i
%assign i i+1
%endrep

%assign i 17
    %rep 1
        isr_ec_sz_start:
        isr_with_ec_def i
        isr_ec_sz_end:
%assign i i+1
%endrep
isr_ec_end:

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