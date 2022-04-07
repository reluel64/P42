[BITS 64]

; These offsets must be kept in sync 
; with those from context.h


%define RAX_INDEX    (0x0000)
%define RBX_INDEX    (0x0001)
%define RCX_INDEX    (0x0002)
%define RDX_INDEX    (0x0003)
%define R8_INDEX     (0x0004)
%define R9_INDEX     (0x0005)
%define R10_INDEX    (0x0006)
%define R11_INDEX    (0x0007)
%define R12_INDEX    (0x0008)
%define R13_INDEX    (0x0009)
%define R14_INDEX    (0x000a)
%define R15_INDEX    (0x000b)
%define RSP_INDEX    (0x000c)
%define RBP_INDEX    (0x000d)
%define RFLAGS_INDEX (0x000e)
%define DS_INDEX     (0x000f)
%define CS_INDEX     (0x0010)
%define RIP_INDEX    (0x0011)
%define CR3_INDEX    (0x0012)
%define ARG_INDEX    (0x0013)
%define TH_INDEX     (0x0014)


%define OFFSET(x) ((x) << 3)

;-------------------------------------------------------------------------------
global __context_switch
global __context_unit_start
;-------------------------------------------------------------------------------


; Switch between two threads
; RDI - previous thread
; RSI - next thread

;--------------------------------SAVE CONTEXT-----------------------------------
__context_switch:
    ; pop the return address -  we will save it separately
    add rsp, 8

    ; Save stack
    mov qword [rdi + OFFSET (RSP_INDEX)], rsp
    mov qword [rdi + OFFSET (RBP_INDEX)], rbp
  
    ; SAVE RIP
    mov rax, qword [rsp - 8]
    mov qword [rdi + OFFSET(RIP_INDEX)], rax

    ; Save registers
    mov qword [rdi + OFFSET (RBX_INDEX)], rbx
    mov qword [rdi + OFFSET (R12_INDEX)], r12
    mov qword [rdi + OFFSET (R13_INDEX)], r13
    mov qword [rdi + OFFSET (R14_INDEX)], r14
    mov qword [rdi + OFFSET (R15_INDEX)], r15

    ; Save RFLAGS
    pushfq
    pop rax
    mov qword [rdi + OFFSET (RFLAGS_INDEX)], rax

;--------------------------------LOAD CONTEXT-----------------------------------
__context_load:
    ; Restore stack
    mov rsp, qword [rsi + OFFSET(RSP_INDEX)]
    mov rbp, qword [rsi + OFFSET(RBP_INDEX)]
    
    ; restore registers
    mov rbx, qword [rsi + OFFSET(RBX_INDEX)]
    mov r12, qword [rsi + OFFSET(R12_INDEX)]
    mov r13, qword [rsi + OFFSET(R13_INDEX)]
    mov r14, qword [rsi + OFFSET(R14_INDEX)]
    mov r15, qword [rsi + OFFSET(R15_INDEX)]

    ; restore RFLAGS
    mov rax, qword [rsi + OFFSET(RFLAGS_INDEX)]

    ; clear interrupt flag
    and rax, ~(1 << 9) 
    push rax
    popfq

    ; Prepare jump to address
    mov rax, qword [rsi + OFFSET(RIP_INDEX)]

    ; Send the parameter to the 
    mov rdi, qword [rsi + OFFSET(TH_INDEX)]
    jmp rax

; Launches the first task for a given unit
__context_unit_start:
    mov rsi, rdi
    jmp __context_load