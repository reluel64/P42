; Paging tools
; Part of P42
global flush_pages
global write_cr3
global read_cr3
global __invlpg
global randomize
global random_seed
global has_pml5
global has_nx
global read_cr2
global write_cr2
global __wbinvd
global __use_pml5



reload_pages:
    mov rax, cr3
    mov cr3, rax
    ret

write_cr3:
    mov cr3, rdi
    ret

read_cr3:
    mov rax, cr3
    ret

write_cr2:
    mov rax, rdi
    mov cr2, rax
    ret

read_cr2:
    mov rax, cr2
    ret


__invlpg:
    invlpg [rdi]
    ret

__use_pml5:
    xor eax, eax
    xor ecx, ecx
    mov eax, 0x7
    cpuid
    test ecx, (1 << 16)
    jz no_pml5

    mov rcx, cr4

    test rcx, (1 << 12)
    jz no_pml5
    
    mov rax, 1
    ret
    
    no_pml5:
        mov rax, 0
        ret
    