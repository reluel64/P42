;Appilication Processor startup code
global __start_ap_begin
global __start_ap_end
global __start_ap_pt_base
global __start_ap_pml5_on
global __start_ap_nx_on
global __start_ap_stack
global cpu_entry_point
section .ap_init

[BITS 16]

__start_ap_begin:

    cli
    cld
    jmp 0x0: 0x8000 + start_ap - __start_ap_begin

start_ap:
    ; set up segment registers 
    mov ax, 0x0
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ;Load CR3 with the PML4 (it was set by the BSP)
    mov edx, [0x8000 + __start_ap_pt_base - __start_ap_begin]   ;Point CR3 at the PML4.
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

lgdt[0x8000 + GDT_PTR64 - __start_ap_begin]
 
jmp 0x8:(0x8000 + start_64 - __start_ap_begin)


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
    dd 0x8000 + GDT64 - __start_ap_begin


; define data
__start_ap_pt_base dq 0x0
__start_ap_cpu_on  db 0x0
__start_ap_pml5_on db 0x0
__start_ap_nx_on   db 0x0
__start_ap_stack   dq 0x0

__start_ap_end:

; We're running now with the big boys
[BITS 64]

section .text

ap_start_higher:
    
    call cpu_entry_point

;mov rax, 0x8000 + cpu_on - __start_ap_begin
;mov [rax], byte 0x1
call halt








