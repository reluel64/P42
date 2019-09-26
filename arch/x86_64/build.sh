INCLUDE_DIR='./h'
rm -fR *.o *.bin
nasm -felf64 ./asm/bootstrap.asm   -o bootstrap.o
nasm -felf64 ./asm/interrupts.asm -o interrupts.o
x86_64-elf-gcc -I${INCLUDE_DIR} -c ./src/main.c -o main.o -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel 
x86_64-elf-gcc -I${INCLUDE_DIR} -c ./src/vga.c -o vga.o -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel  
x86_64-elf-gcc -I${INCLUDE_DIR} -c ./src/interrupts_main.c -o interrupts_main.o -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel  
x86_64-elf-gcc -I${INCLUDE_DIR} -c ./src/descriptors.c -o descriptors.o -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -mcmodel=kernel  
x86_64-elf-gcc -T linker.ld -o test.bin -ffreestanding -nostdlib interrupts.o bootstrap.o main.o vga.o descriptors.o interrupts_main.o -lgcc -z max-page-size=0x1000

cp test.bin /media/alex/4D7E-2E98
sync