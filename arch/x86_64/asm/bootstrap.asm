;P42 x86_64 Kernel bootstrap
;---------------------------------------------------------
[BITS 32]

%define KERNEL_VMA               0xFFFFFFFF80000000
%define MAGIC_VAL                0x1BADB002
%define PAGE_PRESENT             (1 << 0)
%define PAGE_WRITE               (1 << 1)
%define MULTIBOOT_SIG            0x2BADB002
%define PML5_ADDR                (BOOT_PAGING)
%define PML4_ADDR                (BOOT_PAGING + 0x1000)
%define PDPT_ADDR                (BOOT_PAGING + 0x2000)
%define PDT_ADDR                 (BOOT_PAGING + 0x3000)
%define PT_ADDR                  (BOOT_PAGING + 0x4000)

global kernel_init
global mem_map_sig
global mem_map_addr
global kstack_base
global kstack_top

extern kmain
extern BOOT_PAGING
extern BOOT_PAGING_LENGTH

section .multiboot
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

section .bootstrap_text
align 0x1000


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
    mov esi, no_cpuid_msg
    jmp print

no_64_bit:
    mov esi, no_64_msg
    jmp print

halt_cpu:
    hlt
    jmp halt_cpu


kernel_init:
    cli
    mov edx, kstack_base - KERNEL_VMA
    mov ebp, kstack_base - KERNEL_VMA
    mov esp, edx
    mov [mem_map_sig], eax
    mov [mem_map_addr], ebx

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
   
   ;begin building temporary page structures
    ;1) Clear area
    mov ecx, BOOT_PAGING_LENGTH
    mov edi, BOOT_PAGING
    xor eax, eax
    cld
    rep stosb
    
;Check if we support PML5
    xor eax, eax
    xor ecx, ecx
    mov eax, 0x7
    cpuid
    test ecx, (1 << 16)
    jz	fill_pml4
    
;Fill PML5	
    mov edi, PML5_ADDR
    mov dword [edi],                PML4_ADDR + PAGE_PRESENT + PAGE_WRITE
    mov dword [edi + (511 * 0x8)],  PML4_ADDR + PAGE_PRESENT + PAGE_WRITE
    

fill_pml4:
    ;2) Fill  PML4T
    mov edi, PML4_ADDR
    mov dword [edi],                PDPT_ADDR + PAGE_PRESENT + PAGE_WRITE
    mov dword [edi + (511 * 0x8)],  PDPT_ADDR + PAGE_PRESENT + PAGE_WRITE

    ;3) Fill PDPT
    mov edi, PDPT_ADDR
    mov dword [edi],                PDT_ADDR + PAGE_PRESENT + PAGE_WRITE
    mov dword [edi +  (510 * 0x8)], PDT_ADDR + PAGE_PRESENT + PAGE_WRITE

    ;4) Fill PDT
    mov edi, PDT_ADDR
    mov ebx, PT_ADDR + PAGE_WRITE + PAGE_PRESENT
    mov ecx, 512

    fill_pdt:
        mov dword [edi], ebx
        add ebx, 0x1000
        add edi, 8
        loop fill_pdt

    ; start filling the tables
    mov ebx, (PAGE_PRESENT | PAGE_WRITE)  ; page attributes
    mov ecx, 512*512                      ; page count
    mov edi, PT_ADDR                      ; start from page table 0

    fill_tables:
        mov dword [edi], ebx              ;set the page
        add ebx, 0x1000                   ;set the next physical address to map
        add edi, 8                        ;go to the next page
        loop fill_tables   

;Prepare enabling long mode
    mov eax, cr4
    and eax, 11011111b                    ;Disable PAE.
    mov cr4, eax
    
    mov eax, cr0
    and eax, ~(1 << 31)                   ;clear bit 31 (PAGING)
    mov cr0, eax

;Load CR3 with the PML4

    xor eax, eax
    xor ecx, ecx
    mov eax, 0x7
    cpuid
    test ecx, (1 << 16)
    jz	load_pml4


load_pml5:
    mov edx, PML5_ADDR   ;Point CR3 at the PML5
    jmp set_pg_base
load_pml4:
    mov edx, PML4_ADDR


set_pg_base:
    mov cr3, edx
 
    mov eax, cr4
    or eax, 00100000b                   ;Set the PAE.
    mov cr4, eax

    mov ecx, 0xC0000080               ; Read from the EFER MSR. 
    rdmsr    

    or eax, 0x00000100                ; Set the LME bit.
    wrmsr

    xor eax, eax
    xor ecx, ecx
    mov eax, 0x7
    cpuid
    test ecx, (1 << 16)

    jz	enable_paging

; otherwise enable PML5 and then paging
    mov eax, cr4
    or eax, (1 << 12)
    mov cr4, eax

enable_paging:
    mov ebx, cr0                      ; Activate long mode -
    or ebx,0x80000001                 ; - by enabling paging and protection simultaneously.
  
    mov cr0, ebx                    

    lgdt [GDT_PTR]

    jmp 0x08: (enter_64_bit)
    
[BITS 64]

enter_64_bit:
    mov rcx, kernel_higher_half
    jmp rcx

section .text

kernel_higher_half:
    lgdt [GDT_PTR]
    mov rax, 0x10

    mov ds, rax
    mov ss, rax
    mov es, rax
    mov fs, rax
    mov gs, rax
    mov rbp, kstack_base
    mov rsp, kstack_base
    ; clear the interrupts before entering kmain

    cli
    call kmain

halt:
    hlt
    jmp halt

[BITS 32]

section .bootstrap_rodata
align 0x1000

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

align 0x1000

GDT_PTR:
    dw GDT_PTR - GDT - 1
    dd GDT

IDT_PTR:
   dw 0
   dd 0

mem_map_addr            dd 0
mem_map_sig             dd 0

no_cpuid_msg            db "NO CPUID",  0x0
no_64_msg               db "NO 64-bit", 0x0
      
section .bss
kstack_top: 
    resb 16384
kstack_base:
