rm main.o
rm bootstrap.o
rm test.bin
nasm -felf64 bootstrap.asm
nasm -felf64 interrupts.asm
x86_64-elf-gcc -c main.c -o main.o -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel
x86_64-elf-gcc -c vga.c -o vga.o -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel
x86_64-elf-gcc -c descriptors.c -o descriptors.o -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel
x86_64-elf-gcc -T linker.ld -o test.bin -ffreestanding -nostdlib interrupts.o bootstrap.o main.o vga.o descriptors.o -lgcc -z max-page-size=0x1000

cp test.bin /media/alex/4D7E-2E98
sync