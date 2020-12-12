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
global __cpu_context_restore
global __test_call
extern entry_pt


__cpu_context_restore:
    cli

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
    pop rbp

    iretq
