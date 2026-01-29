/**
 * @file graphics.c
 * @brief MC22: GOP Framebuffer Initialization
 *
 * Initializes the UEFI Graphics Output Protocol (GOP) framebuffer.
 * This gives the kernel a linear framebuffer for early console output.
 *
 * GOP Overview:
 *   - GOP replaces the old VGA BIOS and VBE
 *   - Provides a simple linear framebuffer
 *   - Supports mode enumeration and switching
 *   - Framebuffer persists after ExitBootServices()
 *
 * Mode Selection Strategy:
 *   1. Try to find a mode matching preferred resolution
 *   2. Fall back to highest available resolution
 *   3. Prefer BGRA8 format (most common)
 */

#include "seraph/uefi_types.h"
#include "seraph/boot.h"
#include "uefi_crt.h"

/*============================================================================
 * GOP Protocol GUID
 *============================================================================*/

static EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

/*============================================================================
 * Mode Information
 *============================================================================*/

/**
 * @brief Score a graphics mode for selection
 *
 * Higher score = better mode. Prefers:
 *   - Higher resolution
 *   - BGRA/RGBA formats
 *   - Closer to preferred resolution
 *
 * @param info Mode information
 * @param pref_width Preferred width (0 = any)
 * @param pref_height Preferred height (0 = any)
 * @return Score (higher is better)
 */
static UINT64 score_mode(
    const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info,
    UINT32 pref_width,
    UINT32 pref_height)
{
    UINT64 score = 0;

    /* Base score from total pixels */
    score = (UINT64)info->HorizontalResolution * info->VerticalResolution;

    /* Prefer supported pixel formats */
    switch (info->PixelFormat) {
        case PixelBlueGreenRedReserved8BitPerColor:
            score += 10000000; /* BGRA8 is most common and efficient */
            break;
        case PixelRedGreenBlueReserved8BitPerColor:
            score += 9000000;  /* RGBA8 is also good */
            break;
        case PixelBitMask:
            score += 5000000;  /* Bitmask requires more work */
            break;
        case PixelBltOnly:
            score = 0;         /* No direct framebuffer access - unusable */
            return score;
        default:
            break;
    }

    /* Bonus for matching preferred resolution */
    if (pref_width > 0 && pref_height > 0) {
        if (info->HorizontalResolution == pref_width &&
            info->VerticalResolution == pref_height) {
            score += 100000000; /* Exact match */
        }
    }

    return score;
}

/*============================================================================
 * Public Functions
 *============================================================================*/

/**
 * @brief Initialize graphics and fill boot info
 *
 * Locates GOP protocol, selects best mode, and fills in framebuffer
 * information in the boot info structure.
 *
 * @param system_table EFI system table
 * @param boot_info Boot info to fill
 * @param pref_width Preferred width (0 = highest available)
 * @param pref_height Preferred height (0 = highest available)
 * @return EFI_SUCCESS or error code
 */
EFI_STATUS graphics_init(
    EFI_SYSTEM_TABLE* system_table,
    Seraph_BootInfo* boot_info,
    UINT32 pref_width,
    UINT32 pref_height)
{
    EFI_STATUS status;
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = NULL;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* con = system_table->ConOut;

    if (!boot_info) {
        return EFI_INVALID_PARAMETER;
    }

    /* Locate GOP protocol */
    status = system_table->BootServices->LocateProtocol(
        &gop_guid,
        NULL,
        (VOID**)&gop
    );

    if (EFI_ERROR(status) || !gop) {
        efi_print(con, L"[GRAPHICS] GOP not found\r\n");
        return status;
    }

    efi_print(con, L"[GRAPHICS] GOP found, ");
    efi_print_uint64(con, gop->Mode->MaxMode, 10);
    efi_print(con, L" modes available\r\n");

    /* Find best mode */
    UINT32 best_mode = gop->Mode->Mode; /* Current mode as fallback */
    UINT64 best_score = 0;

    for (UINT32 mode_num = 0; mode_num < gop->Mode->MaxMode; mode_num++) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info;
        UINTN info_size;

        status = gop->QueryMode(gop, mode_num, &info_size, &info);
        if (EFI_ERROR(status)) {
            continue;
        }

        UINT64 mode_score = score_mode(info, pref_width, pref_height);

        if (mode_score > best_score) {
            best_score = mode_score;
            best_mode = mode_num;
        }
    }

    /* Switch to best mode if different from current */
    if (best_mode != gop->Mode->Mode) {
        efi_print(con, L"[GRAPHICS] Switching to mode ");
        efi_print_uint64(con, best_mode, 10);
        efi_print_newline(con);

        status = gop->SetMode(gop, best_mode);
        if (EFI_ERROR(status)) {
            efi_print(con, L"[GRAPHICS] SetMode failed, using current mode\r\n");
            /* Continue with current mode */
        }
    }

    /* Fill boot info with framebuffer details */
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* current_info = gop->Mode->Info;

    boot_info->framebuffer_base = gop->Mode->FrameBufferBase;
    boot_info->framebuffer_size = gop->Mode->FrameBufferSize;
    boot_info->fb_width = current_info->HorizontalResolution;
    boot_info->fb_height = current_info->VerticalResolution;
    boot_info->fb_stride = current_info->PixelsPerScanLine * 4; /* Assume 4 bytes/pixel */

    /* Determine pixel format */
    switch (current_info->PixelFormat) {
        case PixelBlueGreenRedReserved8BitPerColor:
            boot_info->fb_format = SERAPH_FB_FORMAT_BGRA8;
            break;
        case PixelRedGreenBlueReserved8BitPerColor:
            boot_info->fb_format = SERAPH_FB_FORMAT_RGBA8;
            break;
        case PixelBitMask:
            /* Analyze bitmask to determine format */
            if (current_info->PixelInformation.BlueMask == 0x000000FF) {
                boot_info->fb_format = SERAPH_FB_FORMAT_BGRA8;
            } else {
                boot_info->fb_format = SERAPH_FB_FORMAT_RGBA8;
            }
            break;
        default:
            boot_info->fb_format = SERAPH_FB_FORMAT_BGRA8;
            break;
    }

    boot_info->flags |= SERAPH_BOOT_FLAG_FRAMEBUFFER;

    efi_print(con, L"[GRAPHICS] ");
    efi_print_uint64(con, boot_info->fb_width, 10);
    efi_print(con, L"x");
    efi_print_uint64(con, boot_info->fb_height, 10);
    efi_print(con, L" @ ");
    efi_print_hex(con, boot_info->framebuffer_base);
    efi_print_newline(con);

    return EFI_SUCCESS;
}

/**
 * @brief Clear screen to a color
 *
 * @param boot_info Boot info with framebuffer
 * @param color 32-bit color (format depends on fb_format)
 */
VOID graphics_clear(const Seraph_BootInfo* boot_info, UINT32 color) {
    if (!boot_info || boot_info->framebuffer_base == 0) {
        return;
    }

    UINT32* fb = (UINT32*)boot_info->framebuffer_base;
    UINTN pixels = (boot_info->fb_stride / 4) * boot_info->fb_height;

    for (UINTN i = 0; i < pixels; i++) {
        fb[i] = color;
    }
}

/**
 * @brief Draw a rectangle
 *
 * @param boot_info Boot info with framebuffer
 * @param x X coordinate
 * @param y Y coordinate
 * @param width Rectangle width
 * @param height Rectangle height
 * @param color 32-bit color
 */
VOID graphics_fill_rect(
    const Seraph_BootInfo* boot_info,
    UINT32 x, UINT32 y,
    UINT32 width, UINT32 height,
    UINT32 color)
{
    if (!boot_info || boot_info->framebuffer_base == 0) {
        return;
    }

    UINT32* fb = (UINT32*)boot_info->framebuffer_base;
    UINT32 stride = boot_info->fb_stride / 4;

    /* Clip to screen bounds */
    if (x >= boot_info->fb_width || y >= boot_info->fb_height) {
        return;
    }
    if (x + width > boot_info->fb_width) {
        width = boot_info->fb_width - x;
    }
    if (y + height > boot_info->fb_height) {
        height = boot_info->fb_height - y;
    }

    for (UINT32 row = 0; row < height; row++) {
        UINT32* row_ptr = fb + (y + row) * stride + x;
        for (UINT32 col = 0; col < width; col++) {
            row_ptr[col] = color;
        }
    }
}

/**
 * @brief Put a pixel
 *
 * @param boot_info Boot info with framebuffer
 * @param x X coordinate
 * @param y Y coordinate
 * @param color 32-bit color
 */
VOID graphics_put_pixel(
    const Seraph_BootInfo* boot_info,
    UINT32 x, UINT32 y,
    UINT32 color)
{
    if (!boot_info || boot_info->framebuffer_base == 0) {
        return;
    }

    if (x >= boot_info->fb_width || y >= boot_info->fb_height) {
        return;
    }

    UINT32* fb = (UINT32*)boot_info->framebuffer_base;
    UINT32 stride = boot_info->fb_stride / 4;

    fb[y * stride + x] = color;
}
