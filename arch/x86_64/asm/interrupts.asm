; Interrupt handling
; We are using the assembler capability to define macros 
; so that we can save a lot of duplicate code being written

global _cli
global _sti
global _lidt
global _lgdt
global _ltr
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
global _geti
global _flush_gdt
extern isr_dispatcher
; ASM stub for ISRs that do not have 
; error codes
[BITS 64]
%macro isr_no_ec_def 1
isr_%1:
    push rbp
    mov rbp, rsp
    push r11
    push r10
    push r9
    push r8
    push rdi
    push rsi
    push rcx
    push rdx
    push rax

    cld
    mov rdi, %1
    mov rsi, 0
    call isr_dispatcher
    
    pop rax
    pop rdx
    pop rcx
    pop rsi
    pop rdi
    pop r8
    pop r9
    pop r10
    pop r11
    pop rbp
    iretq
%endmacro

; ASM stub for ISRs that do have 
; error codes
%macro isr_with_ec_def 1
extern dummy_isr
isr_%1:

    push rbp
    mov rbp, rsp
    push r11
    push r10
    push r9
    push r8
    push rdi
    push rsi
    push rcx
    push rdx
    push rax

    cld
    mov rdi, %1
    mov rsi, [rbp + 0x8]
    mov rdx, [rbp + 0x10]
    call isr_dispatcher

    pop rax
    pop rdx
    pop rcx
    pop rsi
    pop rdi
    pop r8
    pop r9
    pop r10
    pop r11
    pop rbp
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
_cli:
    cli
    ret

; Enable the interrupts
_sti:
    sti
    ret

; retrieves the status of the
; interrupt flag from EFLAGS
_geti:
    pushfq
    pop rax
    and rax, 0x0200
    ret

; Load the Interrupt Descriptor Table Register
; RDI is the address of the structure that 
; holds the linear address and the limit
_lidt:
    lidt[rdi]
    ret

; Load the Global Descriptor Table Register
; RDI is the address of the structure that 
; holds the linear address and the limit
_lgdt:
    lgdt [rdi]
    ret

; Load the Task State Segment register
; RDI is the offset of the segment that
; describes the TSS in GDT
_ltr:
    ltr di
    ret


_flush_gdt:
    mov ax, 0x10
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov gs, ax
    mov fs, ax
    push 0x8
    push .flush_done
    retfq

.flush_done:
    ret
