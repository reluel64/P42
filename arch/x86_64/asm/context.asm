; These offsets must be kept in sync 
; with those from context.h

%define RAX_OFFSET    0x0000
%define RBX_OFFSET    0x0008
%define RCX_OFFSET    0x0010
%define RDX_OFFSET    0x0018
%define R8_OFFSET     0x0020
%define R9_OFFSET     0x0028
%define R10_OFFSET    0x0030
%define R11_OFFSET    0x0038
%define R12_OFFSET    0x0040
%define R13_OFFSET    0x0048
%define R14_OFFSET    0x0050
%define R15_OFFSET    0x0058
%define RSP_OFFSET    0x0060
%define RBP_OFFSET    0x0068
%define RFLAGS_OFFSET 0x0070
%define DS_OFFSET     0x0078
%define CS_OFFSET     0x0080
%define RIP_OFFSET    0x0088
%define CR3_OFFSET    0x0090

;-------------------------------------------------------------------------------
global __context_save
global __context_load
;-------------------------------------------------------------------------------


; Saves the context of the calling thread
; The execution will be resumed from the pointer that is saved
; in the buffer pointed by RDI
__context_save:
    ; save the context of the current task

    ;Save the stack
    mov qword [rdi + RBP_OFFSET],  rbp
    mov qword [rdi + RSP_OFFSET],  rsp

    ; Save RFLAGS
    pushfq
    pop rax
    mov qword [rdi + RFLAGS_OFFSET], rax

    mov qword [rdi + RAX_OFFSET], rax
    mov qword [rdi + RBX_OFFSET], rbx
    mov qword [rdi + RCX_OFFSET], rcx
    mov qword [rdi + RDX_OFFSET], rdx
    mov qword [rdi + R8_OFFSET],  r8
    mov qword [rdi + R9_OFFSET],  r9
    mov qword [rdi + R10_OFFSET], r10
    mov qword [rdi + R11_OFFSET], r11
    mov qword [rdi + R12_OFFSET], r12
    mov qword [rdi + R13_OFFSET], r13
    mov qword [rdi + R14_OFFSET], r14
    mov qword [rdi + R15_OFFSET], r15

    ; save segments
    mov qword [rdi + DS_OFFSET], ds
    mov qword [rdi + CS_OFFSET], cs

    ;save instruction pointer - it's at the top of the stack
    mov rsi, qword [rsp]
    mov qword [rdi + RIP_OFFSET], rsi

    ; save address space
    mov rsi, cr3
    mov qword [rdi + CR3_OFFSET], rsi

    ret

; Load the context
__context_load:

    mov rbp, qword [rdi + RBP_OFFSET]
    mov rsp, qword [rdi + RSP_OFFSET]

    mov rax, qword [rdi + RAX_OFFSET]
    mov rbx, qword [rdi + RBX_OFFSET]
    mov rcx, qword [rdi + RCX_OFFSET]
    mov rdx, qword [rdi + RDX_OFFSET]
    mov r8,  qword [rdi + R8_OFFSET]
    mov r9,  qword [rdi + R9_OFFSET]
    mov r10, qword [rdi + R10_OFFSET]
    mov r11, qword [rdi + R11_OFFSET]
    mov r12, qword [rdi + R12_OFFSET]
    mov r13, qword [rdi + R13_OFFSET]
    mov r14, qword [rdi + R14_OFFSET]
    mov r15, qword [rdi + R15_OFFSET]

    ; check if we need to switch the address space
    mov rsi, cr3
    cmp rsi, qword [rdi + CR3_OFFSET]

    je .skip_va_change

    mov cr3, rsi


.skip_va_change:
     
    mov ds, qword [rdi + DS_OFFSET]
    mov es, qword [rdi + DS_OFFSET]
    mov fs, qword [rdi + DS_OFFSET]
    mov gs, qword [rdi + DS_OFFSET]

    ; check segment change
    mov rsi, cs
    cmp rsi, qword [rdi +  CS_OFFSET]

    jne .segment_switch

    mov ss, qword [rdi + DS_OFFSET]

    ;restore rflags
    mov rsi, qword [rdi + RFLAGS_OFFSET]

    push rsi
    popfq 

    ret
    


.segment_switch:
    
    add rsp, 0x8
    mov qword [rdi + RSP_OFFSET],  rsp

    ; To switch segments (spaces) we need to emulate
    ; that we are coming from an interrupt
    ; Therefore we asssemble an interrupt frame which 
    ; shows like this
    ;
    ; +--------+
    ; |  SS    |
    ; +--------+
    ; | RSP    |
    ; +--------+
    ; | RFLAGS |
    ; +--------+
    ; | RSP    |
    ; +--------+
    ; | CS     |
    ; +--------+
    ; | RIP    |
    ; +--------+
    
    ; clear the NT flag otherwise we will be pleased with a GPF
    pushfq
    pop rsi
    and rsi, ~(1 << 14)
    push rsi
    popfq
    
    
    ;SS
    mov rsi, qword [rdi + DS_OFFSET]
    push rsi

    ;RSP
    mov rsi, qword[rdi + RSP_OFFSET]
    push rsi

    ;RFLAGS
    mov rsi, qword[rdi + RFLAGS_OFFSET]
    push rsi

    ; CS
    mov rsi, qword[rdi + CS_OFFSET]
    push rsi
    
    ;RIP
    mov rsi, qword[rdi + RIP_OFFSET]
    push rsi
 
    iretq


