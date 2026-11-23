# AmenOS - budowanie: make  |  uruchomienie w emulatorze: make uruchom
CC      = gcc
CFLAGS  = -m32 -std=gnu11 -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
          -fno-builtin -nostdlib -mno-sse -mno-mmx -mno-80387 -O2 -Wall -Wextra
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib
LIBGCC  := $(shell $(CC) -m32 -print-libgcc-file-name)

OBJS = build/boot.o build/kmain.o build/vga.o build/klawiatura.o build/rtc.o \
       build/liturgia.o build/powloka.o build/lib.o build/dzwiek.o \
       build/e1000.o build/siec.o build/przegladarka.o \
       build/fb.o build/gui.o \
       build/pci.o build/usb.o build/xhci.o build/uhci.o \
       build/obrazy.o build/www_gui.o build/kalkulator.o build/aplikacje.o

all: amenos.iso

build/%.o: src/%.c src/kernel.h src/usb.h src/www.h src/obrazy.h src/font_pl.h src/muzyka_dane.h src/znaki_dane.h src/grafika_dane.h src/wyglad_dane.h src/apki.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/boot.o: src/boot.asm | build
	nasm -f elf32 $< -o $@

build/boot_tekst.o: src/boot.asm | build
	nasm -f elf32 -DTEKST $< -o $@

build:
	mkdir -p build

iso/boot/amenos.bin: $(OBJS) linker.ld
	ld $(LDFLAGS) -o $@ $(OBJS) $(LIBGCC)
	grub-file --is-x86-multiboot $@

iso/boot/amenos-tekst.bin: $(filter-out build/boot.o,$(OBJS)) build/boot_tekst.o linker.ld
	ld $(LDFLAGS) -o $@ build/boot_tekst.o $(filter-out build/boot.o,$(OBJS)) $(LIBGCC)
	grub-file --is-x86-multiboot $@

amenos.iso: iso/boot/amenos.bin iso/boot/amenos-tekst.bin iso/boot/grub/grub.cfg
	grub-mkrescue -o $@ iso 2>/dev/null

uruchom: amenos.iso
	qemu-system-i386 -cdrom amenos.iso -rtc base=localtime -m 128 -nic user,model=e1000 \
		-device qemu-xhci -device usb-kbd -device usb-mouse

clean:
	rm -rf build iso/boot/amenos.bin iso/boot/amenos-tekst.bin amenos.iso

.PHONY: all uruchom clean
