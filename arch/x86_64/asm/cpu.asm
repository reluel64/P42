global has_pml5
global has_nx
global max_linear_address
global max_physical_address
global enable_nx
global enable_pml5
global enable_wp

; Check page 15 from 
; https://software.intel.com/sites/default/files/managed/2b/80/5-level_paging_white_paper.pdf

has_pml5:
    xor rax, rax
    xor rcx, rcx
    mov eax, 0x7
    cpuid
    and ecx, (1 << 16)
    shr ecx, 16
    xor rax, rax
    mov eax, ecx
    ret

has_nx:
    xor rax, rax
    mov eax, 0x80000001
    cpuid
    and edx, (1 << 20)
    shr edx, 20
    xor rax, rax
    mov eax, edx
    ret
    
enable_nx:
    mov ecx, 0xC0000080               ; Read from the EFER MSR. 
    rdmsr    
    or eax, (1 << 11)                ; Set the NXE bit.
    wrmsr
    ret

max_linear_address:
    xor rax, rax
    mov eax, 0x80000008
    cpuid
    and eax, 0xFF00
    shr eax, 8
    ret

max_physical_address:
    xor rax, rax
    mov eax, 0x80000008
    cpuid
    and eax, 0xFF
    ret

[BITS 32]

enable_pml5:
    mov eax, cr4
    or eax, (1 << 12)
    mov cr4, eax
    ret

enable_wp:
    mov eax, cr0
    or eax, (1 << 16)
    mov cr0, eax
    ret