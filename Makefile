# AmenOS - budowanie: make  |  uruchomienie w emulatorze: make uruchom
CC      = gcc
CFLAGS  = -m32 -std=gnu11 -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
          -fno-builtin -nostdlib -mno-sse -mno-mmx -mno-80387 -O2 -Wall -Wextra
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

OBJS = build/boot.o build/kmain.o build/vga.o build/klawiatura.o build/rtc.o \
       build/liturgia.o build/powloka.o build/lib.o build/dzwiek.o

all: amenos.iso

build/%.o: src/%.c src/kernel.h src/font_pl.h src/muzyka_dane.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/boot.o: src/boot.asm | build
	nasm -f elf32 $< -o $@

build:
	mkdir -p build

iso/boot/amenos.bin: $(OBJS) linker.ld
	ld $(LDFLAGS) -o $@ $(OBJS)
	grub-file --is-x86-multiboot $@

amenos.iso: iso/boot/amenos.bin iso/boot/grub/grub.cfg
	grub-mkrescue -o $@ iso 2>/dev/null

uruchom: amenos.iso
	qemu-system-i386 -cdrom amenos.iso -rtc base=localtime -m 64

clean:
	rm -rf build iso/boot/amenos.bin amenos.iso

.PHONY: all uruchom clean
