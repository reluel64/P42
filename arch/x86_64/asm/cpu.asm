global has_pml5
global has_nx
global max_linear_address
global max_physical_address
global enable_nx
global enable_pml5
global enable_wp
global read_lapic_base
global write_lapic_base
global __wbinvd
global __pause
; Check page 15 from 
; https://software.intel.com/sites/default/files/managed/2b/80/5-level_paging_white_paper.pdf

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


read_lapic_base:
    mov ecx, 0x1B
    rdmsr    
    ret

write_lapic_base:
    mov ecx, 0x1B
    mov eax, edi
    wrmsr
    ret

disable_pic:
mov al, 0xff
out 0xa1, al
out 0x21, al
ret

__wbinvd:
    wbinvd
    ret

__pause:
    pause
    ret

enable_wp:
    mov rax, cr0
    or eax, (1 << 16)
    mov cr0, rax
    ret

global disable_pic
