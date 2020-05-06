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

extern kstack_top
extern kstack_base

__has_nx:
    xor rax, rax
    mov eax, 0x80000001
    cpuid
    and edx, (1 << 20)
    shr edx, 20
    xor rax, rax
    mov eax, edx
    ret
    
__enable_nx:
    mov ecx, 0xC0000080               ; Read from the EFER MSR. 
    rdmsr    
    or eax, (1 << 11)                ; Set the NXE bit.
    wrmsr
    ret

__max_linear_address:
    xor rax, rax
    mov eax, 0x80000008
    cpuid
    and eax, 0xFF00
    shr eax, 8
    ret

__max_physical_address:
    xor rax, rax
    mov eax, 0x80000008
    cpuid
    and eax, 0xFF
    ret

__read_apic_base:
    mov ecx, 0x1B
    rdmsr    
    ret

__write_apic_base:
    mov ecx, 0x1B
    mov eax, edi
    wrmsr
    ret

__check_x2apic:
    xor rax, rax
    xor rcx, rcx
    mov rax, 0x1
    cpuid
    and rcx, (1 << 21)
    shr rcx, 21
    mov rax, rcx
    ret

__wbinvd:
    wbinvd
    ret

__pause:
    pause
    ret

__enable_wp:
    mov rax, cr0
    or eax, (1 << 16)
    mov cr0, rax
    ret

__has_smt:
    mov rax, 1
    xor rdx,rdx
    xor rcx, rcx
    cpuid
    and rdx, (1 << 28)
    shr rdx, 28
    mov rax, rdx
    ret


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

    ; adjust the offset from [RBP]
   ; mov rax, qword [rbp] ; get the content of rbp
   ; mov rcx, rdx         ; save the old base on RAX
   ; sub rcx, rax         ; calculate offset from base
   ; mov rax, rdi        
   ; sub rax, rcx         ; calculate new address
   ; mov qword [rbp], rax ;save new address

    ;sub rbp, 8
    mov rax, rbp
    
    ret
    
__stack_pointer:
    mov rax, rsp
    add rax, 8 ; compensate the push made for this call
    ret
