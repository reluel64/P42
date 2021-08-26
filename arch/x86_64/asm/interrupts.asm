; Interrupt handling
; We are using the assembler capability to define macros 
; so that we can save a lot of duplicate code being written

global __cli
global __sti
global __lidt
global __lgdt
global __ltr
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
global __geti
global __sgdt
global __flush_gdt
extern isr_dispatcher
; ASM stub for ISRs that do not have 
; error codes
[BITS 64]
%macro isr_no_ec_def 1
isr_%1:
    push rbp
    mov rbp, rsp
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax

    cld
    mov rdi, %1
    mov rsi, rbp
    add rsi, 0x8
    call isr_dispatcher
    
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    leave

    iretq
%endmacro

; ASM stub for ISRs that do have 
; error codes
%macro isr_with_ec_def 1
isr_%1:
    push rbp
    mov rbp, rsp
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax
   
    cld
    mov rdi, %1
    mov rsi, rbp
    add rsi, 0x10

    call isr_dispatcher

    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    leave
    
    add rsp, 8
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
__cli:
    cli
    ret

; Enable the interrupts
__sti:
    sti
    ret

; retrieves the status of the
; interrupt flag from EFLAGS
__geti:
    pushfq
    pop rax
    shr rax, 9
    and rax, 1
    ret


; Load the Interrupt Descriptor Table Register
; RDI is the address of the structure that 
; holds the linear address and the limit
__lidt:
    lidt [rdi]
    ret

; Load the Global Descriptor Table Register
; RDI is the address of the structure that 
; holds the linear address and the limit
__lgdt:
    lgdt [rdi]
    ret

__sgdt:
    sgdt [rdi]
    ret

; Load the Task State Segment register
; RDI is the offset of the segment that
; describes the TSS in GDT
__ltr:
    ltr di
    ret

__flush_gdt:
    push rbp ;save stack frame
    mov rbp, rsp
    mov ax, 0x10
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov gs, ax
    mov fs, ax
    
    push ax ; push ss in stack
    push rbp

    ; clear NT from RFLAGS
    pushfq                  ; save the RFLAGS so that it can be restored later by IRET
    pushfq                  ; push RFLAGS so that we can pop them into RAX
    pop rax                 ; pop RFLAGS into RAX
    and rax, ~(1 << 14)     ; Clear NT flag from RFLAGS (otherwise we will get an exception when performing iret)
    push rax                ; push modified RFLAGS back
    popfq                   ; set RFLAGS to CPU

    push 0x8                ; push kernel code segment
    push flush_done         ; push return address
    iretq                   ; do iret to reload segments

    flush_done:
        leave
        ret
