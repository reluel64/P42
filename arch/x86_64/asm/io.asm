; Port based I/O routines 

global read_port_b
global write_port_b
global read_port_w
global write_port_w
global read_port_dw
global write_port_dw

read_port_b:
    mov rdx, rdi ; port
    xor rax, rax
   
    in al, dx

    ret

write_port_b:
    mov rdx, rdi
    mov rax, rsi

    out dx, al
    
    ret

read_port_w:
    mov rdx, rdi ; port
    xor rax, rax
   
    in ax, dx

    ret

write_port_w:
    mov rdx, rdi
    mov rax, rsi

    out dx, ax
    
    ret

read_port_dw:
    mov rdx, rdi ; port
    xor rax, rax
   
    in eax, dx

    ret

write_port_dw:
    mov rdx, rdi
    mov rax, rsi

    out dx, eax
    
    ret
