; AmenOS - punkt startowy (Multiboot 1, ładowany przez GRUB)
MBALIGN  equ 1 << 0
MEMINFO  equ 1 << 1
FLAGS    equ MBALIGN | MEMINFO
MAGIC    equ 0x1BADB002
CHECKSUM equ -(MAGIC + FLAGS)

section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM

section .bss
align 16
stos_dno:
    resb 32768
stos_szczyt:

section .text
global _start
extern kmain
_start:
    mov esp, stos_szczyt
    push ebx            ; wskaźnik na strukturę Multiboot
    push eax            ; magiczna liczba 0x2BADB002
    call kmain
    cli
.stop:
    hlt
    jmp .stop

section .note.GNU-stack noalloc noexec nowrite progbits
