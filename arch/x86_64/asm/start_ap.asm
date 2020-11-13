;Appilication Processor startup code
global __start_ap_begin
global __start_ap_end
global __start_ap_pt_base
global __start_ap_pml5_on
global __start_ap_nx_on
global __start_ap_stack
global __start_ap_entry_pt

extern  cpu_entry_point

section .ap_init

[BITS 16]

__start_ap_begin:

    cli
    cld
    mov edi, cs

start_ap:

    ; set up segment registers 
    mov ax, di
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; tweak the address of the GDT a bit
    mov eax, edi ; get the segment
    shl eax, 4   ; transform the segment into a full address
    add eax, GDT64 - __start_ap_begin   ; add the offset to the base address
    mov [GDT_PTR64_ADDR - __start_ap_begin], eax ; save the newly calculated address

    ;Load CR3 with the PML4 (it was set by the BSP)
    mov edx, dword [__start_ap_pt_base - __start_ap_begin]
    mov cr3, edx

    mov eax, cr4
    or  eax, 00100000b                   ;Set the PAE.
    mov cr4, eax

    mov ecx, 0xC0000080               ; Read from the EFER MSR. 
    rdmsr    

    or eax, 0x00000100                ; Set the LME bit.
    wrmsr
    
; check NX
    xor eax, eax
    mov eax, 0x80000001
    cpuid

    test edx, (1 << 20)
    jz .skip_nx

    mov eax, [__start_ap_nx_on - __start_ap_begin]
    test eax, eax
    jz .skip_nx

    mov ecx, 0xC0000080               ; Read from the EFER MSR. 
    rdmsr    
    or eax, (1 << 11)                ; Set the NXE bit.
    wrmsr   

.skip_nx:
    ; check if BSP has PML5
    mov eax, [__start_ap_pml5_on - __start_ap_begin]
    test eax, eax
    jz enable_paging

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
    or  ebx, 0x80000001                 ; - by enabling paging and protection simultaneously.
  
    mov cr0, ebx                    
    
    lgdt [GDT_PTR64 - __start_ap_begin]

    jmp 0x8:(0x8000  + start_64 - __start_ap_begin)
    
   
   ; jmp 0x8:rax


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
GDT_PTR64_ADDR:
    dd 0x0

; define data
__start_ap_pt_base  dq 0x0
__start_ap_cpu_on   db 0x0
__start_ap_pml5_on  db 0x0
__start_ap_nx_on    db 0x0
__start_ap_stack    dq 0x0
__start_ap_entry_pt dq 0x0



__start_ap_end:

; We're running now with the big boys
[BITS 64]

section .text

ap_start_higher:
    mov rax, 0x10
    mov es, rax
    mov ss, rax
    mov ds, rax
    mov fs, rax
    mov gs, rax


    mov rsp, qword [0x8000 + __start_ap_stack - __start_ap_begin]
    mov rbp, rsp
    
    mov rcx, qword [0x8000 + __start_ap_entry_pt - __start_ap_begin]
    cld
    call rcx
    
    .halt:
        hlt
        jmp .halt
