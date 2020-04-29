; Paging tools
; Part of P42
global __write_cr3
global __read_cr3
global __invlpg
global __read_cr2
global __write_cr2
global __pml5_is_enabled

__write_cr3:
    mov cr3, rdi
    ret

__read_cr3:
    mov rax, cr3
    ret

__write_cr2:
    mov rax, rdi
    mov cr2, rax
    ret

__read_cr2:
    mov rax, cr2
    ret

__invlpg:
    invlpg [rdi]
    ret

__pml5_is_enabled:
    xor eax, eax
    xor ecx, ecx
    mov eax, 0x7
    cpuid
    test ecx, (1 << 16)
    jz .no_pml5

    mov rcx, cr4

    test rcx, (1 << 12)
    jz .no_pml5
    
    mov rax, 1
    ret
    
    .no_pml5:
        mov rax, 0
        ret
    