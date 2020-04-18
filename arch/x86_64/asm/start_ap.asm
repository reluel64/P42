;Appilication Processor startup code


global start_ap_begin
global start_ap_end
global page_base
global gdt_base
global cpu_on
section .ap_init
%define BOOT_PAGING        0x20000000
%define BOOT_PAGING_LENGTH  0x203000
%define PAGE_PRESENT             (1 << 0)
%define PAGE_WRITE               (1 << 1)
%define PML4_ADDR                (BOOT_PAGING)
%define PDPT_ADDR                (BOOT_PAGING + 0x1000)
%define PDT_ADDR                 (BOOT_PAGING + 0x2000)
%define PT_ADDR                  (BOOT_PAGING + 0x3000)
[BITS 16]

start_ap_begin:

    cli
    cld
    jmp 0x0: 0x8000 + start_ap - start_ap_begin

start_ap:

    mov ax, 0x0
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ;Load CR3 with the PML4
    mov edx, [0x8000 + page_base - start_ap_begin]   ;Point CR3 at the PML4.
    mov cr3, edx
 
    mov eax, cr4
    or  eax, 00100000b                   ;Set the PAE.
    mov cr4, eax

    mov ecx, 0xC0000080               ; Read from the EFER MSR. 
    rdmsr    

    or eax, 0x00000100                ; Set the LME bit.
    wrmsr

    mov ebx, cr0                      ; Activate long mode -
    or ebx,0x80000001                 ; - by enabling paging and protection simultaneously.
  
    mov cr0, ebx                    

lgdt[0x8000 + GDT_PTR64 - start_ap_begin]
 
jmp 0x8:(0x8000 + start_64 - start_ap_begin)


[BITS 64]

start_64:
    mov rcx, ap_start_higher
    jmp rcx


[BITS 32]
align 16

GDT64:
    NULL64:     dq      0x00
    KCODE64:    dd      0x00
                db      0x00
                db      0x9A
                db      0xA0
                db      0x0

    KDATA64:    dd      0x00
                db      0x00
                db      0x92
                db      0xA0
                db      0x0

align 16
GDT_PTR64:
    dw GDT_PTR64 - GDT64 - 1
    dd 0x8000 + GDT64 - start_ap_begin

page_base dq 0x0
gdt_base  dq 0x0
cpu_on    db 0x0
start_ap_end:



[BITS 64]

section .text

ap_start_higher:
mov rax, 0x8000 + cpu_on - start_ap_begin
mov [rax], byte 0x1
hlt








