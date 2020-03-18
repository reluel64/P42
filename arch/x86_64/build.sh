INCLUDE_DIR='./h'
rm -fR *.o *.bin
nasm -felf64 ./asm/bootstrap.asm   -o bootstrap.o
nasm -felf64 ./asm/interrupts.asm -o interrupts.o
nasm -felf64 ./asm/io.asm -o io.o
nasm -felf64 ./asm/paging.asm -o paging.o
nasm -felf64 ./asm/cpu.asm -o cpu.o
x86_64-elf-gcc -I${INCLUDE_DIR} -c ./src/main.c -o main.o -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel
x86_64-elf-gcc -I${INCLUDE_DIR} -c ./src/vga.c -o vga.o -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel
x86_64-elf-gcc -I${INCLUDE_DIR} -c ./src/physmm.c -o physmm.o -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel
x86_64-elf-gcc -I${INCLUDE_DIR} -c ./src/memory_map.c -o memory_map.o -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel
x86_64-elf-gcc -I${INCLUDE_DIR} -c ./src/utils.c -o utils.o -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel
x86_64-elf-gcc -I${INCLUDE_DIR} -c ./src/serial.c -o serial.o -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel
x86_64-elf-gcc -I${INCLUDE_DIR} -c ./src/pagemgr.c -o pagemgr.o -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel
x86_64-elf-gcc -I${INCLUDE_DIR} -c ./src/linked_list.c -o linked_list.o -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel
x86_64-elf-gcc -I${INCLUDE_DIR} -c ./src/vmmgr.c -o vmmgr.o -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel
x86_64-elf-gcc -I${INCLUDE_DIR} -c ./src/gdt.c -o gdt.o -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel
x86_64-elf-gcc -I${INCLUDE_DIR} -c ./src/isr.c -o isr.o -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel
x86_64-elf-gcc  -T linker.ld -o test.bin  -ffreestanding -nostdlib isr.o gdt.o interrupts.o bootstrap.o main.o vga.o vmmgr.o serial.o physmm.o io.o paging.o memory_map.o utils.o pagemgr.o cpu.o linked_list.o -lgcc -z max-page-size=0x1000

rm *.o

cp test.bin /media/alex/4D7E-2E98
sync