BITS 64 
global __wbinvd
global __pause
global __cpu_switch_stack
global __stack_pointer
global __cpuid
global __wrmsr
global __rdmsr
global __invd
global __write_cr3
global __read_cr3
global __invlpg
global __read_cr2
global __write_cr2
global __read_cr4
global __write_cr4
global __read_cr0
global __write_cr0
global __read_cr8
global __write_cr8
global __hlt
global __cpu_context_restore
global __resched_interrupt
;----------------------------------------
__wbinvd:
    wbinvd
    ret
;----------------------------------------
__pause:
    pause
    ret
;----------------------------------------

;----------------------------------------
;RDI -> new stack base
;RSI -> new stack top
;RDX -> old stack base

__cpu_switch_stack:
    ; Fixup RSP 
    mov rax, qword [rsp]    ; save return address
    mov rsp, rsi           ;set new address
    push rax

    ;Fixup RBP
    mov rax, rdx    ; save RDX to RAX
    sub rax, rbp    ; calculate offset from base
    mov rbp, rdi    ; set rbp to the base of the new stack
    sub rbp, rax    ; subtract offset from rbp
    ret
;----------------------------------------
__stack_pointer:
    mov rax, rsp
    add rax, 8 ; compensate the push made for this call
    ret

;----------------------------------------
; RDI -> RAX
; RSI -> RBX
; RDX -> RCX
; RCX -> RDX

__cpuid:
    ;save registers
    push rbp
    mov rbp, rsp
    
    push rax
    push rbx
    push rcx
    push rdx

    mov r8, rdx
    mov r9, rcx

    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx

    mov edx, dword [r9]
    mov ecx, dword [r8]
    mov eax, dword [rdi]
    mov ebx, dword [rsi]
    
    cpuid

    mov dword [rdi], eax
    mov dword [rsi], ebx
    mov dword [r8],  ecx
    mov dword [r9],  edx

    pop rdx
    pop rcx
    pop rbx
    pop rax

    leave
    ret
;----------------------------------------
__rdmsr:
    mov rcx, rdi
    rdmsr
    shl rdx, 32
    or rax, rdx
    ret
;----------------------------------------
__wrmsr:
    mov rcx, rdi
    mov rax, rsi
    shr rsi, 32
    mov edx, esi
    wrmsr
    ret
;----------------------------------------
__invd:
    invd
    ret
;----------------------------------------
__read_cr4:
    mov rax, cr4
    ret
;----------------------------------------
__write_cr4:
    mov cr4, rdi
    ret
;----------------------------------------
__write_cr3:
    mov cr3, rdi
    ret
;----------------------------------------
__read_cr3:
    mov rax, cr3
    ret
;----------------------------------------
__write_cr2:
    mov rax, rdi
    mov cr2, rax
    ret
;----------------------------------------
__read_cr2:
    mov rax, cr2
    ret
;----------------------------------------
__write_cr0:
    mov rax, rdi
    mov cr0, rax
    ret
;----------------------------------------
__read_cr0:
    mov rax, cr0
    ret
;----------------------------------------
__read_cr8:
    mov rax, cr8
    ret

__write_cr8:
    mov cr8, rdi
    ret

;----------------------------------------
__invlpg:
    invlpg [rdi]
    ret

__hlt:
    hlt
    ret

__cpu_context_restore:
    cli
    ; restore the segments
    pop rax
    
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ;check if address space needs switching
    pop rax
    mov rbx, cr3
    cmp rbx, rax

    je .no_space_switch
    
    mov cr3, rax

.no_space_switch:
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
    pop rbp

    iretq



__resched_interrupt:
    int 240
    ret

; RDI - next thread
; RSI - current thread
; RIP is already saved in the stack 
__cpu_switch_to:

; save the context of the current task
    mov qword [rsi]       ,  rax
    mov qword [rsi + 0x8] ,  rbx
    mov qword [rsi + 0x10],  rcx
    mov qword [rsi + 0x18],  rdx
    mov qword [rsi + 0x20],  rsi
    mov qword [rsi + 0x28],  rdi
    mov qword [rsi + 0x30],  r8
    mov qword [rsi + 0x38],  r9
    mov qword [rsi + 0x40],  r10
    mov qword [rsi + 0x48],  r11
    mov qword [rsi + 0x50],  r12
    mov qword [rsi + 0x58],  r13
    mov qword [rsi + 0x60],  r14
    mov qword [rsi + 0x68],  r15
    mov qword [rsi + 0x70],  rbp
    mov qword [rsi + 0x78],  rsp
    
; Save RFLAGS
    pushf
    pop rax

    mov qword [rsi + 0x80], rax

; load the next task context

    mov rax, cr3
    mov rbx, mov qword [rsi + 0x88]

    cmp rax, rbx

    je .no_vm_change

    mov cr3, rbx

.no_vm_change:
    
    mov rax, qword [rdi]
    mov rbx, qword [rdi + 0x8]
    mov rcx, qword [rdi + 0x10]
    mov rdx, qword [rdi + 0x18]
    mov rsi, qword [rdi + 0x20]
    mov r8,  qword [rdi + 0x30]
    mov r9,  qword [rdi + 0x38]
    mov r10, qword [rdi + 0x40]
    mov r11, qword [rdi + 0x48]
    mov r12, qword [rdi + 0x50]
    mov r13, qword [rdi + 0x58]
    mov r14, qword [rdi + 0x60]
    mov r15, qword [rdi + 0x68]
    mov rbp, qword [rdi + 0x70]
    mov rsp, qword [rdi + 0x78]

    ;update RDI last - same reason as RSI
    mov rdi, qword [rdi + 0x28]

    

    iretq