/* SERAPH Kernel Linker Script
 *
 * This script links the kernel into an ELF64 executable.
 * The kernel is loaded at the higher-half virtual address.
 */

OUTPUT_FORMAT("elf64-x86-64", "elf64-x86-64", "elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

/* Virtual address where kernel is mapped */
KERNEL_VMA = 0xFFFF800000000000;

/* Physical address where kernel is loaded */
KERNEL_LMA = 0x100000;

SECTIONS
{
    . = KERNEL_VMA;

    .text ALIGN(4096) : AT(KERNEL_LMA) {
        *(.text)
        *(.text.*)
    }

    .rodata ALIGN(4096) : {
        *(.rodata)
        *(.rodata.*)
    }

    .data ALIGN(4096) : {
        *(.data)
        *(.data.*)
    }

    .bss ALIGN(4096) : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }

    /* Kernel end marker */
    . = ALIGN(4096);
    __kernel_end = .;

    /DISCARD/ : {
        *(.note.*)
        *(.comment)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}
