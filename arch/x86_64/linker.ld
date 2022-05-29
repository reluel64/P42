KERNEL_VMA         = 0xFFFFFFFF80000000;
KERNEL_LMA         = 0x1000000;
BOOT_PAGING        = 0x100000;
BOOT_PAGING_LENGTH = 0x204000;
BOOT_PAGING_END    = BOOT_PAGING + BOOT_PAGING_LENGTH;
AP_START           = 0x7c00;

ENTRY(kernel_init)

SECTIONS
{

    . = KERNEL_LMA;

    BOOTSTRAP_START = .;

    .bootstrap_text : AT(ADDR(.bootstrap_text))
    {
        *(.multiboot)
        *(.bootstrap_text)
        . = ALIGN(4096);
    }

    .bootstrap_stack : AT(ADDR(.bootstrap_stack))
    {
        *(.bootstrap_stack)
        . = ALIGN(4096);
    }
    
    .bootstrap_rodata : AT(ADDR(.bootstrap_rodata))
    {
        *(.bootstrap_rodata)
        . = ALIGN(4096);
    }

    .bootstrap_data : AT(ADDR(.bootstrap_data))
    {
        *(.bootstrap_data)
        . = ALIGN(4096);
    }

    BOOTSTRAP_END = .;

    . += KERNEL_VMA;
    
    .text : AT(ADDR(.text) - KERNEL_VMA)
    {
        _code = .;
        *(.text)
        . = ALIGN(4096);
        _code_end = .;
    }

    .rodata : AT(ADDR(.rodata) - KERNEL_VMA)
    {
        _rodata = .;
        *(.rodata*)
         *(.ap_init)
        . = ALIGN(4096);
        _rodata_end = .;
    }

   .data : AT(ADDR(.data) - KERNEL_VMA)
   {
        _data = .;
        *(.data)
        . = ALIGN(4096);
        _data_end = .;
   }

   .eh_frame : AT(ADDR(.eh_frame) - KERNEL_VMA)
   {
       _ehframe = .;
       *(.eh_frame)
        . = ALIGN(4096);
   }

   .bss : AT(ADDR(.bss) - KERNEL_VMA)
   {
       _bss = .;
       *(.bss)
        *(COMMON)
       . = ALIGN(4096);
       _bss_end = .;
   }
   
   /DISCARD/ :
   {
        *(.comment)
   }
    . = ALIGN(4096);

    KERNEL_VMA_END = .;
    KERNEL_LMA_END = . - KERNEL_VMA;
    KERNEL_IMAGE_LEN =  KERNEL_LMA_END - KERNEL_LMA;
}