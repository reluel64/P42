; Paging tools
; Part of P42
global flush_pages
global write_cr3
global read_cr3
global __invlpg
global randomize
global random_seed

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

random_seed:
    rdtsc       ; issue a timestamp counter read
    shl rdx, 32
    or rax, rdx  ; make it a 64 bit number
    ret

;Randomization using xorshift64 algorithm
randomize:
    mov rdx, rdi
    shl rdi, 13
    xor rdx, rdi
    mov rdi, rdx
    shr rdi, 7
    xor rdx, rdi
    mov rdi, rdx
    shl rdi, 17
    xor rdx, rdi
    mov rax, rdx
    ret