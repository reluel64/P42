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
flush_pages:
    mov rax, cr3
    mov cr3, rax
    ret

write_cr3:
    mov cr3, rdi
    ret

read_cr3:
    mov rax, cr3
    ret

__invlpg:
    invlpg [rdi]
    ret