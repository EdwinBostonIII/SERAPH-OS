/* SERAPH UEFI Bootloader Linker Script
 *
 * This script links the boot code into an EFI PE32+ executable.
 * UEFI requires specific section layout and alignment.
 */

OUTPUT_FORMAT("pei-x86-64", "pei-x86-64", "pei-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(efi_main)

SECTIONS
{
    . = 0;

    /* PE header will be added by linker */

    .text ALIGN(4096) : {
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
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }

    .reloc ALIGN(4096) : {
        *(.reloc)
    }

    /DISCARD/ : {
        *(.note.*)
        *(.comment)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}
