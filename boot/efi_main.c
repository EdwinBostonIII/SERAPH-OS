/**
 * @file efi_main.c
 * @brief MC24: UEFI Entry Point
 *
 * Main entry point for the SERAPH UEFI bootloader. This file orchestrates
 * the entire boot process:
 *
 *   1. Initialize console
 *   2. Set up graphics (GOP)
 *   3. Load kernel ELF64 from disk
 *   4. Find ACPI/SMBIOS tables
 *   5. Allocate kernel stack and arena
 *   6. Set up initial page tables
 *   7. Capture memory map
 *   8. Exit boot services
 *   9. Jump to kernel
 *
 * The bootloader is built as a PE32+ executable for UEFI.
 */

#include "seraph/uefi_types.h"
#include "seraph/boot.h"
#include "uefi_crt.h"
#include "elf64_loader.h"

/*============================================================================
 * External Functions (from other boot modules)
 *============================================================================*/

/* From graphics.c */
extern EFI_STATUS graphics_init(
    EFI_SYSTEM_TABLE* system_table,
    Seraph_BootInfo* boot_info,
    UINT32 pref_width,
    UINT32 pref_height);

extern VOID graphics_clear(const Seraph_BootInfo* boot_info, UINT32 color);

/* From memory_map.c */
extern EFI_STATUS memory_map_get(
    EFI_BOOT_SERVICES* boot_services,
    Seraph_BootInfo* boot_info,
    UINTN* map_key);

extern VOID memory_map_print(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* con,
    const Seraph_BootInfo* boot_info);

extern BOOLEAN memory_map_find_for_kernel(
    const Seraph_BootInfo* boot_info,
    UINT64 size,
    UINT64 align,
    UINT64* base_out);

/*============================================================================
 * GUIDs
 *============================================================================*/

static EFI_GUID loaded_image_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
static EFI_GUID simple_fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID file_info_guid = EFI_FILE_INFO_ID;
static EFI_GUID acpi_20_guid = EFI_ACPI_20_TABLE_GUID;
static EFI_GUID acpi_10_guid = EFI_ACPI_TABLE_GUID;
static EFI_GUID smbios_guid = SMBIOS_TABLE_GUID;
static EFI_GUID smbios3_guid = SMBIOS3_TABLE_GUID;

/*============================================================================
 * Boot Configuration
 *============================================================================*/

/** Kernel filename to load */
static CHAR16 kernel_filename[] = L"\\EFI\\SERAPH\\kernel.elf";

/** Kernel stack size (64KB) */
#define KERNEL_STACK_SIZE (64 * 1024)

/** Primordial arena size (4MB) */
#define PRIMORDIAL_ARENA_SIZE (4 * 1024 * 1024)

/** Preferred screen resolution */
#define PREFERRED_WIDTH  1920
#define PREFERRED_HEIGHT 1080

/*============================================================================
 * Static Storage
 *============================================================================*/

/** Boot info structure (must survive ExitBootServices) */
static Seraph_BootInfo boot_info __attribute__((aligned(4096)));

/*============================================================================
 * Helper Functions
 *============================================================================*/

/**
 * @brief Wait for a keypress
 */
static VOID wait_for_key(EFI_SYSTEM_TABLE* st) {
    EFI_STATUS status;
    UINTN index;
    status = st->BootServices->WaitForEvent(1, &st->ConIn->WaitForKey, &index);
    (void)status;
}

/**
 * @brief Print error and halt
 */
static VOID fatal_error(EFI_SYSTEM_TABLE* st, const CHAR16* msg) {
    efi_print(st->ConOut, L"\r\n*** FATAL ERROR: ");
    efi_print(st->ConOut, msg);
    efi_print(st->ConOut, L"\r\n\r\nPress any key to reboot...\r\n");
    wait_for_key(st);
}

/**
 * @brief Find ACPI RSDP in configuration tables
 */
static UINT64 find_acpi_rsdp(EFI_SYSTEM_TABLE* st, UINT32* flags) {
    /* Try ACPI 2.0+ first */
    for (UINTN i = 0; i < st->NumberOfTableEntries; i++) {
        if (efi_guid_equal(&st->ConfigurationTable[i].VendorGuid, &acpi_20_guid)) {
            *flags |= SERAPH_BOOT_FLAG_ACPI_V2;
            return (UINT64)st->ConfigurationTable[i].VendorTable;
        }
    }

    /* Fall back to ACPI 1.0 */
    for (UINTN i = 0; i < st->NumberOfTableEntries; i++) {
        if (efi_guid_equal(&st->ConfigurationTable[i].VendorGuid, &acpi_10_guid)) {
            *flags |= SERAPH_BOOT_FLAG_ACPI_V1;
            return (UINT64)st->ConfigurationTable[i].VendorTable;
        }
    }

    return 0;
}

/**
 * @brief Find SMBIOS entry point
 */
static UINT64 find_smbios(EFI_SYSTEM_TABLE* st, UINT32* flags) {
    /* Try SMBIOS 3.0 first */
    for (UINTN i = 0; i < st->NumberOfTableEntries; i++) {
        if (efi_guid_equal(&st->ConfigurationTable[i].VendorGuid, &smbios3_guid)) {
            *flags |= SERAPH_BOOT_FLAG_SMBIOS;
            return (UINT64)st->ConfigurationTable[i].VendorTable;
        }
    }

    /* Fall back to SMBIOS 2.x */
    for (UINTN i = 0; i < st->NumberOfTableEntries; i++) {
        if (efi_guid_equal(&st->ConfigurationTable[i].VendorGuid, &smbios_guid)) {
            *flags |= SERAPH_BOOT_FLAG_SMBIOS;
            return (UINT64)st->ConfigurationTable[i].VendorTable;
        }
    }

    return 0;
}

/**
 * @brief Load kernel file from disk
 */
static EFI_STATUS load_kernel_file(
    EFI_HANDLE image_handle,
    EFI_SYSTEM_TABLE* st,
    VOID** buffer_out,
    UINTN* size_out)
{
    EFI_STATUS status;
    EFI_LOADED_IMAGE_PROTOCOL* loaded_image;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs;
    EFI_FILE_PROTOCOL* root;
    EFI_FILE_PROTOCOL* kernel_file;

    efi_print(st->ConOut, L"[BOOT] Loading kernel: ");
    efi_print(st->ConOut, kernel_filename);
    efi_print_newline(st->ConOut);

    /* Get loaded image protocol to find our device */
    status = st->BootServices->HandleProtocol(
        image_handle,
        &loaded_image_guid,
        (VOID**)&loaded_image
    );
    if (EFI_ERROR(status)) {
        return status;
    }

    /* Get file system protocol */
    status = st->BootServices->HandleProtocol(
        loaded_image->DeviceHandle,
        &simple_fs_guid,
        (VOID**)&fs
    );
    if (EFI_ERROR(status)) {
        return status;
    }

    /* Open root directory */
    status = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(status)) {
        return status;
    }

    /* Open kernel file */
    status = root->Open(
        root,
        &kernel_file,
        kernel_filename,
        EFI_FILE_MODE_READ,
        0
    );
    if (EFI_ERROR(status)) {
        root->Close(root);
        return status;
    }

    /* Get file size */
    UINT8 info_buffer[256];
    UINTN info_size = sizeof(info_buffer);
    status = kernel_file->GetInfo(
        kernel_file,
        &file_info_guid,
        &info_size,
        info_buffer
    );
    if (EFI_ERROR(status)) {
        kernel_file->Close(kernel_file);
        root->Close(root);
        return status;
    }

    EFI_FILE_INFO* file_info = (EFI_FILE_INFO*)info_buffer;
    UINTN file_size = (UINTN)file_info->FileSize;

    efi_print(st->ConOut, L"[BOOT] Kernel size: ");
    efi_print_uint64(st->ConOut, file_size, 10);
    efi_print(st->ConOut, L" bytes\r\n");

    /* Allocate buffer for kernel file */
    VOID* buffer;
    status = st->BootServices->AllocatePool(
        EfiLoaderData,
        file_size,
        &buffer
    );
    if (EFI_ERROR(status)) {
        kernel_file->Close(kernel_file);
        root->Close(root);
        return status;
    }

    /* Read kernel file */
    UINTN read_size = file_size;
    status = kernel_file->Read(kernel_file, &read_size, buffer);
    if (EFI_ERROR(status) || read_size != file_size) {
        st->BootServices->FreePool(buffer);
        kernel_file->Close(kernel_file);
        root->Close(root);
        return EFI_LOAD_ERROR;
    }

    kernel_file->Close(kernel_file);
    root->Close(root);

    *buffer_out = buffer;
    *size_out = file_size;

    return EFI_SUCCESS;
}

/*============================================================================
 * Kernel Entry Type
 *============================================================================*/

typedef VOID (EFIAPI *KernelEntry)(Seraph_BootInfo* boot_info);

/*============================================================================
 * EFI Main Entry Point
 *============================================================================*/

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table) {
    EFI_STATUS status;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* con = system_table->ConOut;
    EFI_BOOT_SERVICES* bs = system_table->BootServices;

    /* Disable watchdog timer */
    bs->SetWatchdogTimer(0, 0, 0, NULL);

    /* Clear screen and show banner */
    con->ClearScreen(con);
    efi_print(con, L"=================================================\r\n");
    efi_print(con, L"       SERAPH Operating System Bootloader\r\n");
    efi_print(con, L"=================================================\r\n\r\n");

    /* Initialize boot info structure */
    efi_memset(&boot_info, 0, sizeof(boot_info));
    boot_info.magic = SERAPH_BOOT_MAGIC;
    boot_info.version = SERAPH_BOOT_VERSION;
    boot_info.flags = 0;

    /*------------------------------------------------------------------------
     * Step 1: Initialize Graphics
     *------------------------------------------------------------------------*/
    efi_print(con, L"[BOOT] Initializing graphics...\r\n");
    status = graphics_init(system_table, &boot_info, PREFERRED_WIDTH, PREFERRED_HEIGHT);
    if (EFI_ERROR(status)) {
        efi_print(con, L"[BOOT] Graphics init failed (non-fatal)\r\n");
        /* Continue without graphics */
    }

    /*------------------------------------------------------------------------
     * Step 2: Find ACPI and SMBIOS
     *------------------------------------------------------------------------*/
    efi_print(con, L"[BOOT] Searching for ACPI/SMBIOS...\r\n");
    /* Use local variable to avoid unaligned access on packed struct member */
    UINT32 boot_flags = boot_info.flags;
    boot_info.rsdp_address = find_acpi_rsdp(system_table, &boot_flags);
    boot_info.smbios_address = find_smbios(system_table, &boot_flags);
    boot_info.flags = boot_flags;

    if (boot_info.rsdp_address) {
        efi_print(con, L"[BOOT] ACPI RSDP @ ");
        efi_print_hex(con, boot_info.rsdp_address);
        efi_print_newline(con);
    }
    if (boot_info.smbios_address) {
        efi_print(con, L"[BOOT] SMBIOS @ ");
        efi_print_hex(con, boot_info.smbios_address);
        efi_print_newline(con);
    }

    /*------------------------------------------------------------------------
     * Step 3: Load Kernel ELF
     *------------------------------------------------------------------------*/
    VOID* kernel_buffer;
    UINTN kernel_file_size;
    status = load_kernel_file(image_handle, system_table, &kernel_buffer, &kernel_file_size);
    if (EFI_ERROR(status)) {
        fatal_error(system_table, L"Failed to load kernel file");
        return status;
    }

    /* Validate and load ELF */
    if (!elf64_is_valid(kernel_buffer, kernel_file_size)) {
        fatal_error(system_table, L"Kernel is not a valid ELF64 executable");
        return EFI_LOAD_ERROR;
    }

    elf64_print_header(con, kernel_buffer);

    Elf64_LoadResult load_result;
    status = elf64_load(kernel_buffer, kernel_file_size, bs, &load_result);
    if (EFI_ERROR(status)) {
        fatal_error(system_table, L"Failed to load kernel ELF");
        return status;
    }

    efi_print(con, L"[BOOT] Kernel loaded @ ");
    efi_print_hex(con, load_result.phys_base);
    efi_print(con, L" entry @ ");
    efi_print_hex(con, load_result.entry_point);
    efi_print_newline(con);

    boot_info.kernel_phys_base = load_result.phys_base;
    boot_info.kernel_virt_base = load_result.virt_base;
    boot_info.kernel_size = load_result.total_size;

    /* Free the file buffer (no longer needed) */
    bs->FreePool(kernel_buffer);

    /*------------------------------------------------------------------------
     * Step 4: Allocate Kernel Stack
     *------------------------------------------------------------------------*/
    efi_print(con, L"[BOOT] Allocating kernel stack...\r\n");
    EFI_PHYSICAL_ADDRESS stack_phys;
    UINTN stack_pages = KERNEL_STACK_SIZE / 4096;
    status = bs->AllocatePages(AllocateAnyPages, EfiLoaderData, stack_pages, &stack_phys);
    if (EFI_ERROR(status)) {
        fatal_error(system_table, L"Failed to allocate kernel stack");
        return status;
    }
    boot_info.stack_phys = stack_phys;
    boot_info.stack_size = KERNEL_STACK_SIZE;
    efi_memset((VOID*)stack_phys, 0, KERNEL_STACK_SIZE);

    efi_print(con, L"[BOOT] Stack @ ");
    efi_print_hex(con, stack_phys);
    efi_print_newline(con);

    /*------------------------------------------------------------------------
     * Step 5: Allocate Primordial Arena
     *------------------------------------------------------------------------*/
    efi_print(con, L"[BOOT] Allocating primordial arena...\r\n");
    EFI_PHYSICAL_ADDRESS arena_phys;
    UINTN arena_pages = PRIMORDIAL_ARENA_SIZE / 4096;
    status = bs->AllocatePages(AllocateAnyPages, EfiLoaderData, arena_pages, &arena_phys);
    if (EFI_ERROR(status)) {
        fatal_error(system_table, L"Failed to allocate primordial arena");
        return status;
    }
    boot_info.primordial_arena_phys = arena_phys;
    boot_info.primordial_arena_size = PRIMORDIAL_ARENA_SIZE;
    efi_memset((VOID*)arena_phys, 0, PRIMORDIAL_ARENA_SIZE);

    efi_print(con, L"[BOOT] Arena @ ");
    efi_print_hex(con, arena_phys);
    efi_print_newline(con);

    /*------------------------------------------------------------------------
     * Step 6: Get Memory Map (must be last before ExitBootServices)
     *------------------------------------------------------------------------*/
    efi_print(con, L"[BOOT] Getting memory map...\r\n");
    UINTN map_key;
    status = memory_map_get(bs, &boot_info, &map_key);
    if (EFI_ERROR(status)) {
        fatal_error(system_table, L"Failed to get memory map");
        return status;
    }

    memory_map_print(con, &boot_info);

    /*------------------------------------------------------------------------
     * Step 7: Exit Boot Services
     *------------------------------------------------------------------------*/
    efi_print(con, L"\r\n[BOOT] Exiting boot services...\r\n");

    /* Memory map may have changed, get it again */
    status = memory_map_get(bs, &boot_info, &map_key);
    if (EFI_ERROR(status)) {
        fatal_error(system_table, L"Failed to get final memory map");
        return status;
    }

    status = bs->ExitBootServices(image_handle, map_key);
    if (EFI_ERROR(status)) {
        /* Try once more with fresh map key */
        status = memory_map_get(bs, &boot_info, &map_key);
        if (!EFI_ERROR(status)) {
            status = bs->ExitBootServices(image_handle, map_key);
        }
        if (EFI_ERROR(status)) {
            /* This is fatal - cannot continue */
            return status;
        }
    }

    /*------------------------------------------------------------------------
     * Step 8: Jump to Kernel
     *
     * At this point:
     *   - Boot services are gone
     *   - We have the final memory map
     *   - Framebuffer is still accessible
     *   - Kernel is loaded in memory
     *
     * We set up the stack and jump to the kernel entry point.
     *------------------------------------------------------------------------*/

    /* Clear screen to indicate successful boot services exit */
    if (boot_info.flags & SERAPH_BOOT_FLAG_FRAMEBUFFER) {
        graphics_clear(&boot_info, 0x00102030); /* Dark blue */
    }

    /* Calculate stack top (stack grows down, 16-byte aligned for ABI) */
    UINT64 stack_top = (boot_info.stack_phys + boot_info.stack_size) & ~0xFULL;

    /* Get kernel entry point */
    KernelEntry kernel_entry = (KernelEntry)(load_result.phys_base +
                                              (load_result.entry_point - load_result.virt_base));

    /* Switch to kernel stack and jump to kernel entry
     * RDI = pointer to boot_info (first argument per System V ABI)
     * RSP = new stack pointer
     * Then call kernel entry point */
    __asm__ volatile(
        "mov %0, %%rsp\n\t"     /* Set new stack pointer */
        "mov %1, %%rdi\n\t"     /* First argument: boot_info pointer */
        "call *%2\n\t"          /* Call kernel entry */
        :
        : "r"(stack_top), "r"(&boot_info), "r"(kernel_entry)
        : "rdi", "memory"
    );

    /* Should never reach here */
    while (1) {
        __asm__ volatile("hlt");
    }

    return EFI_SUCCESS;
}
