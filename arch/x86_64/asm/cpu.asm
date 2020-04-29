global __has_nx
global __max_linear_address
global __max_physical_address
global __enable_nx
global __enable_pml5
global __enable_wp
global __read_lapic_base
global __write_lapic_base
global __wbinvd
global __pause

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

