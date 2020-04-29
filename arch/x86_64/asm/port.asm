; Port based I/O routines 

global __inb
global __outb
global __inw
global __outw
global __ind
global __outd

__inb:
    mov rdx, rdi ; port
    xor rax, rax
   
    in al, dx

    ret

__outb:
    mov rdx, rdi
    mov rax, rsi

    out dx, al
    
    ret

__inw:
    mov rdx, rdi ; port
    xor rax, rax

    in ax, dx
    
    ret

__outw:
    mov rdx, rdi
    mov rax, rsi

    out dx, ax
    
    ret

__ind:
    mov rdx, rdi ; port
    xor rax, rax
   
    in eax, dx

    ret

__outd:
    mov rdx, rdi
    mov rax, rsi

    out dx, eax
    
    ret
