;P42 x86_64 Kernel bootstrap
;---------------------------------------------------------
global kernel_init
global from_multiboot
global mb_present
global mb_addr
global halt
extern kmain

%define MAGIC_VAL                0x1BADB002
%define KERNEL_VIRTUAL_BASE      0xFFFFFFFF80000000
%define KERNEL_PHYSICAL_ADDRESS  0x1000000
%define PAGE_PRESENT             (1 << 0)
%define PAGE_WRITE               (1 << 1)
%define MULTIBOOT_SIG            0x2BADB002

[BITS 32]

section .bootstrap_text
multiboot:

    magic:          dd 0x1BADB002   ;magic
    flags:          dd 0x1
    checksum        dd -(MAGIC_VAL + 1)
    header_addr     dd 0x0
    ld_addr         dd 0x0
    ld_end_addr     dd 0x0
    bss_end_addr    dd 0x0
    entry_addr      dd 0x0
    mode_type       dd 0x0
    width           dd 0x0
    height          dd 0x0
    depth           dd 0x0
; Small printing routine

print:
    mov edi, 0xb8000

    next_char:
        lodsb
        or al,al
        jz print_done
        stosb
        mov al, 0x07
        stosb
        jmp next_char
print_done:
    cli
    hlt

cpuid_not_supported:
    mov esi, no_cpuid_msg - KERNEL_VIRTUAL_BASE
    jmp print

no_64_bit:
    mov esi, no_64_msg  - KERNEL_VIRTUAL_BASE
    jmp print

halt_cpu:
    hlt
    jmp halt_cpu


kernel_init:
    cli
    mov edx, kstack_top - KERNEL_VIRTUAL_BASE
    mov ebp, kstack_base - KERNEL_VIRTUAL_BASE
    mov esp, edx
    mov [mb_present - KERNEL_VIRTUAL_BASE], eax
    mov [mb_addr    - KERNEL_VIRTUAL_BASE], ebx
;Save multiboot
    push eax
    push ebx


;Check CPUID

;The ID flag (bit 21) in the EFLAGS register indicates 
;support for the CPUID instruction. 
;If a software procedure can set and clear this flag, 
;the processor executing the procedure supports the CPUID instruction. 
;The CPUID instruction will cause the invalid opcode exception (#UD) if executed on a 
;processor that does not support it.

    pushfd              ; push EFLAGS in stack
    pop edx             ; pop EFLAGS in edx     

    mov ecx, edx        ; save EFLAGS in ECS for later
    
    xor edx, (1 << 21)  ; flip the bit

    push edx            ; push edx to stack
    popfd               ; pop content of edx to eflags

    pushfd              ; push eflags to stack
    pop edx             ; take eflags to edx

    push ecx
    popfd

    xor eax, ecx         ; compare
    jz cpuid_not_supported



;Check if we support Long mode checking
    mov eax, 0x80000000 ;get highest processor info
    cpuid
    cmp eax, 0x80000001 ; if it's greater then we're good to go
    
    jb no_64_bit
    
; check if we support long mode
    mov eax, 0x80000001
    cpuid
    test edx, ( 1 << 29 ) ; if bit 29 is set to 1 then we support 64 bit
   
    jz no_64_bit    
   
; start filling the tables
    mov ebx, (PAGE_PRESENT | PAGE_WRITE)  ; page attributes
    mov ecx, 0x1000                       ; page count
    mov edi, (PT0 - KERNEL_VIRTUAL_BASE)  ; start from page table 0
  

fill_tables:
    mov dword [edi], ebx                  ;set the page
    add ebx, 0x1000                       ;set the next physical address to map
    add edi, 8                            ;go to the next page
    loop fill_tables                      

;Prepare enabling long mode
    mov eax, cr4
    and eax, 11011111b                    ;Disable PAE.
    mov cr4, eax
    
    mov eax, cr0
    and eax, 0x7FFFFFFF                   ;clear bit 31 (PAGING)
    mov cr0, eax

;Load CR3 with the PML4
    mov edx, PML4 - KERNEL_VIRTUAL_BASE   ;Point CR3 at the PML4.
    mov cr3, edx
 
    mov eax, cr4
    or eax, 00100000b                   ;Set the PAE.
    mov cr4, eax

    mov ecx, 0xC0000080               ; Read from the EFER MSR. 
    rdmsr    

    or eax, 0x00000100                ; Set the LME bit.
    wrmsr
 

    mov ebx, cr0                      ; Activate long mode -
    or ebx,0x80000001                 ; - by enabling paging and protection simultaneously.
     
    mov cr0, ebx                    
   
    lgdt [GDT_PTR - KERNEL_VIRTUAL_BASE]

    jmp 0x08: (enter_64_bit)
    
[BITS 64]

enter_64_bit:
mov rcx, kernel_higher_half
jmp rcx

section .text

kernel_higher_half:

    mov rax, PML4
    mov qword [rax],0
    lgdt [GDT_PTR]
    mov rax, 0x10

    mov ds, rax
    mov ss, rax
    mov es, rax
    mov fs, rax
    mov gs, rax
    mov rbp, kstack_base
    mov rsp, kstack_top 
    invlpg [0]
    call kmain

halt:
    hlt
    ret

section .data


PML4:
    PML4_IDENT: dq       (PDPT - KERNEL_VIRTUAL_BASE) + PAGE_PRESENT + PAGE_WRITE
    times 510 dq 0
    PML4_VIRT:  dq       (PDPT - KERNEL_VIRTUAL_BASE) +  PAGE_PRESENT + PAGE_WRITE

PDPT:
    PDPT_IDENT: dq       (PDT - KERNEL_VIRTUAL_BASE)  + PAGE_PRESENT + PAGE_WRITE
    times 509 dq 0
    PDPT_VIRT:  dq       (PDT - KERNEL_VIRTUAL_BASE)  + PAGE_PRESENT + PAGE_WRITE
    times 1 dq 0
PDT:
    dq       (PT0 - KERNEL_VIRTUAL_BASE) + PAGE_PRESENT + PAGE_WRITE
    dq       (PT1 - KERNEL_VIRTUAL_BASE) + PAGE_PRESENT + PAGE_WRITE
    dq       (PT2 - KERNEL_VIRTUAL_BASE) + PAGE_PRESENT + PAGE_WRITE
    dq       (PT3 - KERNEL_VIRTUAL_BASE) + PAGE_PRESENT + PAGE_WRITE
    dq       (PT4 - KERNEL_VIRTUAL_BASE) + PAGE_PRESENT + PAGE_WRITE
    dq       (PT5 - KERNEL_VIRTUAL_BASE) + PAGE_PRESENT + PAGE_WRITE
    dq       (PT6 - KERNEL_VIRTUAL_BASE) + PAGE_PRESENT + PAGE_WRITE
    dq       (PT7 - KERNEL_VIRTUAL_BASE) + PAGE_PRESENT + PAGE_WRITE
    
    times 504 dq 0
    
PT0:
    times 512 dq 0
PT1:
    times 512 dq 0
PT2:
    times 512 dq 0
PT3:
    times 512 dq 0
PT4:
    times 512 dq 0
PT5:
    times 512 dq 0
PT6:
    times 512 dq 0
PT7:
    times 512 dq 0

section .rodata
align 8

GDT:
    NULL:     dq      0x00
    KCODE:    dd      0x00
              db      0x00
              db      0x9A
              db      0xA0
              db      0x0

    KDATA:    dd      0x00
              db      0x00
              db      0x92
              db      0xA0
              db      0x0

align 16

GDT_PTR:
    dw GDT_PTR - GDT - 1
    dd GDT - KERNEL_VIRTUAL_BASE

IDT_PTR:
   dw 0
   dd 0

mb_present              dd 0
mb_addr                 dd 0

section .bss
kstack_base: 
    resb 16384
kstack_top:

section .rodata
no_cpuid_msg            db "NO CPUID",  0x0
no_64_msg               db "NO 64-bit", 0x0

