global __has_nx
global __max_linear_address
global __max_physical_address
global __enable_nx
global __enable_pml5
global __enable_wp
global __read_apic_base
global __write_apic_base
global __wbinvd
global __pause
global __check_x2apic
global __has_smt
global __cpu_switch_stack
global __stack_pointer
global __freq_info
global __cpuid
global __wrmsr
global __rdmsr
global __tsc_info

extern kstack_top
extern kstack_base
;----------------------------------------
__has_nx:
    xor rax, rax
    mov eax, 0x80000001
    cpuid
    and edx, (1 << 20)
    shr edx, 20
    xor rax, rax
    mov eax, edx
    ret
;----------------------------------------
__enable_nx:
    mov ecx, 0xC0000080               ; Read from the EFER MSR. 
    rdmsr    
    or eax, (1 << 11)                ; Set the NXE bit.
    wrmsr
    ret
;----------------------------------------
__max_linear_address:
    xor rax, rax
    mov eax, 0x80000008
    cpuid
    and eax, 0xFF00
    shr eax, 8
    ret
;----------------------------------------
__max_physical_address:
    xor rax, rax
    mov eax, 0x80000008
    cpuid
    and eax, 0xFF
    ret
;----------------------------------------
__read_apic_base:
    mov ecx, 0x1B
    rdmsr
    shl rdx, 32
    or rax, rdx
    ret
;----------------------------------------
__write_apic_base:
    mov ecx, 0x1B
    mov eax, edi
    wrmsr
    ret
;----------------------------------------
__check_x2apic:
    xor rax, rax
    xor rcx, rcx
    mov rax, 0x1
    cpuid
    and rcx, (1 << 21)
    shr rcx, 21
    mov rax, rcx
    ret
;----------------------------------------
__wbinvd:
    wbinvd
    ret
;----------------------------------------
__pause:
    pause
    ret
;----------------------------------------
__enable_wp:
    mov rax, cr0
    or eax, (1 << 16)
    mov cr0, rax
    ret
;----------------------------------------
__has_smt:
    mov rax, 1
    xor rdx,rdx
    xor rcx, rcx
    cpuid
    and rdx, (1 << 28)
    shr rdx, 28
    mov rax, rdx
    ret
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
__tsc_info:
    mov eax, 0x15
    mov r8, rdx
    xor rcx, rcx
    xor rdx, rdx

    cpuid

    mov dword [rdi],  eax
    mov dword [rsi],  ebx
    mov dword [r8],   ecx
    ret
;----------------------------------------
__freq_info:
    mov eax, 0x16
    xor rcx, rcx
    xor rdx, rdx
    cpuid

    mov dword [rdi],  ecx
    mov dword [rsi],  ebx
    ret
;----------------------------------------
__has_apic:
    mov eax, 0x1
    cpuid
    mov eax, edx
    and eax, (1 << 9)
    shr eax, 9
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
    
    mov r8, rdx
    mov r9, rcx

    mov ecx, dword [r9]
    mov eax, dword [rdi]

    cpuid

    mov dword [rdi], eax
    mov dword [rsi], ebx
    mov dword [r8],  edx
    mov dword [r9],  ecx

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