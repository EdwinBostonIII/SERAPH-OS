/**
 * @file kmain.c
 * @brief MC25: Kernel Entry Point
 *
 * This is where the kernel takes over from the bootloader. The entry point
 * receives a Seraph_BootInfo structure containing all the information needed
 * to initialize the kernel.
 *
 * Initialization Sequence:
 *   1. Validate boot info
 *   2. Initialize early console (framebuffer)
 *   3. Initialize Physical Memory Manager (PMM)
 *   4. Initialize Virtual Memory Manager (VMM)
 *   5. Initialize Kernel Allocator (kmalloc)
 *   6. Set up interrupt handling
 *   7. Initialize scheduler
 *   8. Start first user process
 */

#include "seraph/boot.h"
#include "seraph/void.h"
#include "seraph/early_mem.h"
#include "seraph/pmm.h"
#include "seraph/vmm.h"
#include "seraph/kmalloc.h"
#include "seraph/interrupts.h"
#include "seraph/scheduler.h"
#include "seraph/sovereign.h"
#include "seraph/strand.h"
#include "seraph/capability.h"
#include "seraph/chronon.h"
#include "seraph/galactic.h"

/*============================================================================
 * Early Console (Framebuffer)
 *============================================================================*/

/** Global pointer to boot info (set after validation) */
static const Seraph_BootInfo* g_boot_info = NULL;

/** Simple 8x16 font for early console (extended ASCII subset) */
static const uint8_t font_8x16[128][16] = {
    /* Basic Latin characters for boot display */
    [' '] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['!'] = {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['('] = {0x0C, 0x18, 0x30, 0x30, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    [')'] = {0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    [':'] = {0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['='] = {0x00, 0x00, 0x7E, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* Digits */
    ['0'] = {0x3C, 0x66, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['1'] = {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['2'] = {0x3C, 0x66, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x66, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['3'] = {0x3C, 0x66, 0x06, 0x1C, 0x06, 0x06, 0x06, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['4'] = {0x0C, 0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C, 0x0C, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['5'] = {0x7E, 0x60, 0x60, 0x7C, 0x06, 0x06, 0x06, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['6'] = {0x1C, 0x30, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['7'] = {0x7E, 0x06, 0x0C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['8'] = {0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['9'] = {0x3C, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0C, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* Uppercase letters */
    ['A'] = {0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['B'] = {0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['C'] = {0x3C, 0x66, 0x60, 0x60, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['D'] = {0x78, 0x6C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x6C, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['E'] = {0x7E, 0x60, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['F'] = {0x7E, 0x60, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['G'] = {0x3C, 0x66, 0x60, 0x60, 0x6E, 0x66, 0x66, 0x66, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['H'] = {0x66, 0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['I'] = {0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['J'] = {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x6C, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['K'] = {0x66, 0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['L'] = {0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['M'] = {0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['N'] = {0x66, 0x76, 0x7E, 0x7E, 0x6E, 0x66, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['O'] = {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['P'] = {0x7C, 0x66, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['Q'] = {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x6E, 0x3C, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['R'] = {0x7C, 0x66, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['S'] = {0x3C, 0x66, 0x60, 0x60, 0x3C, 0x06, 0x06, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['T'] = {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['U'] = {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['V'] = {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x3C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['W'] = {0x63, 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['X'] = {0x66, 0x66, 0x3C, 0x3C, 0x18, 0x3C, 0x3C, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['Y'] = {0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['Z'] = {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x60, 0x66, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* Lowercase letters */
    ['a'] = {0x00, 0x00, 0x3C, 0x06, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['b'] = {0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['c'] = {0x00, 0x00, 0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['d'] = {0x06, 0x06, 0x3E, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['e'] = {0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x60, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['f'] = {0x1C, 0x36, 0x30, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['g'] = {0x00, 0x00, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['h'] = {0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['i'] = {0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['j'] = {0x0C, 0x00, 0x1C, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['k'] = {0x60, 0x60, 0x66, 0x6C, 0x78, 0x78, 0x6C, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['l'] = {0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['m'] = {0x00, 0x00, 0x76, 0x7F, 0x6B, 0x6B, 0x63, 0x63, 0x63, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['n'] = {0x00, 0x00, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['o'] = {0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['p'] = {0x00, 0x00, 0x7C, 0x66, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['q'] = {0x00, 0x00, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['r'] = {0x00, 0x00, 0x7C, 0x66, 0x60, 0x60, 0x60, 0x60, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['s'] = {0x00, 0x00, 0x3E, 0x60, 0x60, 0x3C, 0x06, 0x06, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['t'] = {0x30, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x30, 0x36, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['u'] = {0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['v'] = {0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x3C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['w'] = {0x00, 0x00, 0x63, 0x63, 0x6B, 0x6B, 0x7F, 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['x'] = {0x00, 0x00, 0x66, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['y'] = {0x00, 0x00, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x0C, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['z'] = {0x00, 0x00, 0x7E, 0x0C, 0x18, 0x30, 0x60, 0x66, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* Special characters */
    ['-'] = {0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['['] = {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    [']'] = {0x78, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['/'] = {0x06, 0x06, 0x0C, 0x0C, 0x18, 0x30, 0x30, 0x60, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['.'] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    [','] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['*'] = {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['@'] = {0x3C, 0x66, 0x6E, 0x6E, 0x6E, 0x60, 0x60, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['+'] = {0x00, 0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['|'] = {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

/** Console state */
static struct {
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint32_t fg_color;
    uint32_t bg_color;
} console = {
    .cursor_x = 0,
    .cursor_y = 0,
    .fg_color = 0xFFFFFFFF,  /* White */
    .bg_color = 0x00102030,  /* Dark blue */
};

/**
 * @brief Put a pixel on the framebuffer
 */
static void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!g_boot_info || !seraph_boot_has_framebuffer(g_boot_info)) return;
    if (x >= g_boot_info->fb_width || y >= g_boot_info->fb_height) return;

    uint32_t* fb = (uint32_t*)g_boot_info->framebuffer_base;
    uint32_t stride = g_boot_info->fb_stride / 4;
    fb[y * stride + x] = color;
}

/**
 * @brief Draw a character at the given position
 */
static void fb_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    if ((uint8_t)c >= 128) return;

    const uint8_t* glyph = font_8x16[(uint8_t)c];

    for (int row = 0; row < 16; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            uint32_t color = (bits & (0x80 >> col)) ? fg : bg;
            fb_put_pixel(x + (uint32_t)col, y + (uint32_t)row, color);
        }
    }
}

/**
 * @brief Scroll the console up by one line
 */
static void console_scroll(void) {
    if (!g_boot_info || !seraph_boot_has_framebuffer(g_boot_info)) return;

    uint32_t* fb = (uint32_t*)g_boot_info->framebuffer_base;
    uint32_t stride = g_boot_info->fb_stride / 4;
    uint32_t char_height = 16;

    /* Move everything up by one character line */
    for (uint32_t y = 0; y < g_boot_info->fb_height - char_height; y++) {
        for (uint32_t x = 0; x < g_boot_info->fb_width; x++) {
            fb[y * stride + x] = fb[(y + char_height) * stride + x];
        }
    }

    /* Clear bottom line */
    for (uint32_t y = g_boot_info->fb_height - char_height; y < g_boot_info->fb_height; y++) {
        for (uint32_t x = 0; x < g_boot_info->fb_width; x++) {
            fb[y * stride + x] = console.bg_color;
        }
    }
}

/**
 * @brief Print a character to the console
 */
static void console_putc(char c) {
    if (!g_boot_info || !seraph_boot_has_framebuffer(g_boot_info)) return;

    uint32_t char_width = 8;
    uint32_t char_height = 16;
    uint32_t cols = g_boot_info->fb_width / char_width;
    uint32_t rows = g_boot_info->fb_height / char_height;

    if (c == '\n') {
        console.cursor_x = 0;
        console.cursor_y++;
    } else if (c == '\r') {
        console.cursor_x = 0;
    } else if (c == '\t') {
        console.cursor_x = (console.cursor_x + 8) & ~7;
    } else {
        fb_draw_char(console.cursor_x * char_width, console.cursor_y * char_height,
                     c, console.fg_color, console.bg_color);
        console.cursor_x++;
    }

    /* Handle line wrap */
    if (console.cursor_x >= cols) {
        console.cursor_x = 0;
        console.cursor_y++;
    }

    /* Handle scroll */
    while (console.cursor_y >= rows) {
        console_scroll();
        console.cursor_y--;
    }
}

/**
 * @brief Print a string to the console
 */
void console_puts(const char* str) {
    if (!str) return;
    while (*str) {
        console_putc(*str++);
    }
}

/**
 * @brief Print a hexadecimal number
 */
void console_put_hex(uint64_t value) {
    static const char hex_chars[] = "0123456789ABCDEF";
    char buffer[19]; /* "0x" + 16 digits + null */
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[18] = '\0';

    for (int i = 15; i >= 0; i--) {
        buffer[2 + (15 - i)] = hex_chars[(value >> (i * 4)) & 0xF];
    }

    console_puts(buffer);
}

/**
 * @brief Print a decimal number
 */
static void console_put_dec(uint64_t value) {
    char buffer[21];
    buffer[20] = '\0';
    int i = 19;

    if (value == 0) {
        console_putc('0');
        return;
    }

    while (value > 0 && i >= 0) {
        buffer[i--] = '0' + (value % 10);
        value /= 10;
    }

    console_puts(&buffer[i + 1]);
}

/**
 * @brief Clear the console
 */
static void console_clear(void) {
    if (!g_boot_info || !seraph_boot_has_framebuffer(g_boot_info)) return;

    uint32_t* fb = (uint32_t*)g_boot_info->framebuffer_base;
    uint32_t pixels = (g_boot_info->fb_stride / 4) * g_boot_info->fb_height;

    for (uint32_t i = 0; i < pixels; i++) {
        fb[i] = console.bg_color;
    }

    console.cursor_x = 0;
    console.cursor_y = 0;
}

/*============================================================================
 * Graphics Primitives for Visual Demo
 *============================================================================*/

/**
 * @brief Fill the screen with a solid color
 */
static void fb_fill(uint32_t color) {
    if (!g_boot_info || !seraph_boot_has_framebuffer(g_boot_info)) return;

    uint32_t* fb = (uint32_t*)g_boot_info->framebuffer_base;
    uint32_t pixels = (g_boot_info->fb_stride / 4) * g_boot_info->fb_height;

    for (uint32_t i = 0; i < pixels; i++) {
        fb[i] = color;
    }
}

/**
 * @brief Draw a filled rectangle
 */
static void fb_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!g_boot_info || !seraph_boot_has_framebuffer(g_boot_info)) return;

    uint32_t* fb = (uint32_t*)g_boot_info->framebuffer_base;
    uint32_t stride = g_boot_info->fb_stride / 4;
    uint32_t max_x = g_boot_info->fb_width;
    uint32_t max_y = g_boot_info->fb_height;

    for (uint32_t py = y; py < y + h && py < max_y; py++) {
        for (uint32_t px = x; px < x + w && px < max_x; px++) {
            fb[py * stride + px] = color;
        }
    }
}

/**
 * @brief Print string at specific pixel coordinates
 */
static void fb_print_at(uint32_t x, uint32_t y, const char* str, uint32_t fg, uint32_t bg) {
    if (!str) return;
    uint32_t px = x;
    while (*str) {
        fb_draw_char(px, y, *str, fg, bg);
        px += 8;
        str++;
    }
}

/*============================================================================
 * Kernel Memory Management Globals
 *============================================================================*/

static Seraph_PMM g_pmm;
static Seraph_VMM g_vmm;

/*============================================================================
 * Init Strand - The First Thread in SERAPH
 *
 * This demonstrates SERAPH's unique features:
 *   - VOID semantics (divide by zero produces VOID, not crash)
 *   - Capability creation and checking
 *   - Chronon-based temporal tracking
 *   - Galactic predictive scheduling
 *============================================================================*/

/**
 * @brief Entry point for the init strand
 *
 * This function runs as the first user-level strand after boot.
 * It demonstrates SERAPH's unique features: VOID propagation,
 * capability-based memory safety, and Chronon temporal tracking.
 *
 * @param arg Unused argument
 */
static void seraph_init_main(void* arg) {
    (void)arg;  /* Unused */

    /*========================================================================
     * SERAPHIM ENGINE: VISUAL BOOT DEMONSTRATION
     *
     * This demonstrates SERAPH's unique features with visual feedback:
     *   1. Dark ethereal theme (Dark Grey Void)
     *   2. VOID Semantics - division by zero produces VOID, not crash
     *   3. Capability-based memory safety
     *========================================================================*/

    /* Step 1: Clear to dark grey void */
    fb_fill(0xFF101010);

    /* Step 2: Draw header text */
    fb_print_at(100, 100, "SERAPHIM ENGINE: ONLINE", 0xFF00FF00, 0xFF101010);

    /* Step 3: Draw a decorative green rectangle */
    fb_draw_rect(100, 130, 280, 4, 0xFF00FF00);

    /*------------------------------------------------------------------------
     * DEMONSTRATION: VOID Semantics
     *
     * In traditional systems, division by zero causes SIGFPE and program crash.
     * In SERAPH, division by zero injects VOID into the result register,
     * and execution continues gracefully.
     *------------------------------------------------------------------------*/

    volatile uint64_t x = 10;
    volatile uint64_t y = 0;
    volatile uint64_t z;

    /* This would crash on traditional systems. In SERAPH, it produces VOID. */
    z = x / y;

    /* Display result visually */
    fb_print_at(100, 160, "Void Calculation Result: ", 0xFFFFFFFF, 0xFF101010);

    if (SERAPH_IS_VOID_U64(z)) {
        fb_print_at(300, 160, "VOID (Correct)", 0xFF00FFFF, 0xFF101010);
        fb_draw_rect(100, 190, 200, 20, 0xFF00FFFF);  /* Cyan success bar */
    } else {
        fb_print_at(300, 160, "FAILURE", 0xFFFF0000, 0xFF101010);
        fb_draw_rect(100, 190, 200, 20, 0xFFFF0000);  /* Red failure bar */
    }

    /* Draw additional system info */
    fb_print_at(100, 230, "Division by zero: No crash!", 0xFF88FF88, 0xFF101010);
    fb_print_at(100, 260, "VOID semantics operational", 0xFF88FF88, 0xFF101010);

    /*------------------------------------------------------------------------
     * Continue with text-mode debug output
     *------------------------------------------------------------------------*/
    console_puts("\n");
    console_puts("[INIT] SERAPH Init Strand Started\n");
    console_puts("[INIT] Demonstrating SERAPH unique features...\n\n");

    console_puts("[INIT] === VOID Semantics Demo ===\n");
    console_puts("[INIT] Division by zero completed (no crash!)\n");
    console_puts("[INIT] Result: ");
    console_put_hex(z);
    console_puts("\n");

    if (SERAPH_IS_VOID_U64(z)) {
        console_puts("[INIT] Result is VOID (as expected) - VOID propagation works!\n");
    } else {
        console_puts("[INIT] Result is NOT VOID (unexpected)\n");
    }
    console_puts("\n");

    /*------------------------------------------------------------------------
     * DEMONSTRATION 2: Chronon Temporal Tracking
     *
     * Each Strand has its own logical time (Chronon). Time progresses
     * independently for each strand, enabling lock-free temporal reasoning.
     *------------------------------------------------------------------------*/
    console_puts("[INIT] === Chronon Temporal Demo ===\n");

    Seraph_Strand* current = seraph_strand_current();
    if (current) {
        console_puts("[INIT] Current strand Chronon: ");
        console_put_dec(current->chronon);
        console_puts("\n");

        /* Tick the chronon forward */
        for (int i = 0; i < 5; i++) {
            Seraph_Chronon new_time = seraph_strand_tick();
            console_puts("[INIT] Tick ");
            console_put_dec(i + 1);
            console_puts(": Chronon = ");
            console_put_dec(new_time);
            console_puts("\n");
        }
    } else {
        console_puts("[INIT] No current strand (scheduler not active)\n");
    }
    console_puts("\n");

    /*------------------------------------------------------------------------
     * DEMONSTRATION 3: Capability-Based Memory Safety
     *
     * Memory access requires explicit capabilities. No capability = no access.
     * This prevents entire classes of memory safety bugs by construction.
     *------------------------------------------------------------------------*/
    console_puts("[INIT] === Capability Demo ===\n");

    /* Allocate some memory through the kernel allocator */
    void* test_mem = seraph_kmalloc(256);
    if (test_mem) {
        console_puts("[INIT] Allocated 256 bytes at ");
        console_put_hex((uint64_t)test_mem);
        console_puts("\n");

        /* Create a capability for this memory */
        Seraph_Capability cap = seraph_cap_create(
            test_mem, 256,
            1,  /* generation */
            SERAPH_CAP_READ | SERAPH_CAP_WRITE
        );

        if (!seraph_cap_is_void(cap)) {
            console_puts("[INIT] Created capability:\n");
            console_puts("[INIT]   Base: ");
            console_put_hex((uint64_t)cap.base);
            console_puts("\n");
            console_puts("[INIT]   Length: ");
            console_put_dec(cap.length);
            console_puts("\n");
            console_puts("[INIT]   Permissions: ");
            console_put_hex(cap.permissions);
            console_puts("\n");
            console_puts("[INIT]   Generation: ");
            console_put_dec(cap.generation);
            console_puts("\n");

            /* Verify capability bounds checking */
            bool in_bounds = seraph_cap_range_valid(cap, 0, 256);
            console_puts("[INIT]   In-bounds access check: ");
            console_puts(in_bounds ? "PASS" : "FAIL");
            console_puts("\n");

            /* Attempt out-of-bounds access (should return FALSE) */
            bool oob_check = seraph_cap_range_valid(cap, 256, 256);  /* Offset 256+256 > length */
            console_puts("[INIT]   Out-of-bounds check: ");
            console_puts(!oob_check ? "BLOCKED (good!)" : "ALLOWED (bad!)");
            console_puts("\n");
        }

        seraph_kfree(test_mem);
        console_puts("[INIT] Memory freed\n");
    }
    console_puts("\n");

    /*------------------------------------------------------------------------
     * DEMONSTRATION 4: Galactic Predictive Scheduling
     *
     * The scheduler uses Galactic numbers (value + derivative) to predict
     * future CPU needs and proactively adjust priorities.
     *------------------------------------------------------------------------*/
    console_puts("[INIT] === Galactic Scheduling Demo ===\n");

    if (seraph_scheduler_is_galactic_enabled()) {
        console_puts("[INIT] Galactic predictive scheduling: ENABLED\n");

        uint64_t adjustments, boosts, demotions;
        seraph_scheduler_galactic_stats(&adjustments, &boosts, &demotions);

        console_puts("[INIT] Priority adjustments: ");
        console_put_dec(adjustments);
        console_puts("\n");
        console_puts("[INIT] Priority boosts: ");
        console_put_dec(boosts);
        console_puts("\n");
        console_puts("[INIT] Priority demotions: ");
        console_put_dec(demotions);
        console_puts("\n");
    } else {
        console_puts("[INIT] Galactic scheduling: DISABLED\n");
    }
    console_puts("\n");

    /*------------------------------------------------------------------------
     * DEMONSTRATION 5: Scheduler Statistics
     *------------------------------------------------------------------------*/
    console_puts("[INIT] === Scheduler Stats ===\n");

    const Seraph_Scheduler_Stats* stats = seraph_scheduler_stats();
    if (stats) {
        console_puts("[INIT] Context switches: ");
        console_put_dec(stats->total_switches);
        console_puts("\n");
        console_puts("[INIT] Preemptions: ");
        console_put_dec(stats->preemptions);
        console_puts("\n");
        console_puts("[INIT] Yields: ");
        console_put_dec(stats->yields);
        console_puts("\n");
        console_puts("[INIT] Ready strands: ");
        console_put_dec(stats->ready_count);
        console_puts("\n");
    }
    console_puts("\n");

    /*------------------------------------------------------------------------
     * Main Loop: Keep the system alive
     *------------------------------------------------------------------------*/
    console_puts("[INIT] === SERAPH Boot Complete ===\n");
    console_puts("[INIT] Init strand entering idle loop...\n");
    console_puts("[INIT] (Yielding periodically to demonstrate preemption)\n\n");

    uint64_t loop_count = 0;
    while (1) {
        loop_count++;

        /* Print status every million iterations */
        if ((loop_count & 0xFFFFF) == 0) {
            console_puts("[INIT] Tick: ");
            console_put_dec(seraph_scheduler_get_global_tick());
            console_puts(" | Loops: ");
            console_put_dec(loop_count >> 20);
            console_puts("M\n");
        }

        /* Yield to let other strands run */
        seraph_strand_yield();
    }
}

/*============================================================================
 * Kernel Panic
 *============================================================================*/

/**
 * @brief Halt the system with a panic message
 */
static void __attribute__((noreturn)) kernel_panic(const char* message) {
    console.fg_color = 0xFFFF0000; /* Red */
    console_puts("\n*** KERNEL PANIC ***\n");
    console_puts(message);
    console_puts("\n\nSystem halted.\n");

    /* Disable interrupts and halt */
    while (1) {
        __asm__ volatile("cli; hlt");
    }
}

/*============================================================================
 * Kernel Main Entry Point
 *============================================================================*/

/**
 * @brief Kernel entry point
 *
 * This is called by the bootloader after ExitBootServices.
 * At this point:
 *   - We have no UEFI services
 *   - Framebuffer is available
 *   - Memory map is in boot_info
 *   - Kernel stack is set up
 *
 * @param boot_info Boot information from bootloader
 */
/*
 * System V ABI: Bootloader explicitly puts boot_info in RDI via inline asm
 * MinGW defaults to MS ABI (RCX), so we force System V (RDI)
 */
/* Debug helper - draw a colored bar at row Y */
static void debug_bar(uint64_t fb_addr, int row, uint32_t color) {
    if (fb_addr) {
        volatile uint32_t* fb = (volatile uint32_t*)fb_addr;
        for (int x = 0; x < 400; x++) {
            fb[row * 1920 + x] = color;
        }
    }
}

__attribute__((sysv_abi, noreturn))
void kernel_main(Seraph_BootInfo* boot_info) {
    /* Get framebuffer - try both boot_info and hardcoded address */
    uint64_t fb_addr = 0xC0000000ULL;  /* Default from QEMU */

    /* Row 0: YELLOW = kernel_main entered */
    debug_bar(fb_addr, 0, 0xFFFFFF00);

    /* Row 1: Check boot_info pointer */
    if (boot_info) {
        debug_bar(fb_addr, 1, 0xFF00FF00);  /* GREEN = boot_info not NULL */
        if (boot_info->framebuffer_base) {
            fb_addr = boot_info->framebuffer_base;
            debug_bar(fb_addr, 2, 0xFF00FFFF);  /* CYAN = has FB address */
        } else {
            debug_bar(fb_addr, 2, 0xFFFF0000);  /* RED = no FB address */
        }
    } else {
        debug_bar(fb_addr, 1, 0xFFFF0000);  /* RED = boot_info is NULL */
        /* Halt - can't continue without boot_info */
        while(1) __asm__ volatile("hlt");
    }

    /* Row 3: Check boot_info_valid */
    if (!seraph_boot_info_valid(boot_info)) {
        debug_bar(fb_addr, 3, 0xFFFF0000);  /* RED = invalid */
        while (1) __asm__ volatile("cli; hlt");
    }
    debug_bar(fb_addr, 3, 0xFF00FF00);  /* GREEN = valid */

    g_boot_info = boot_info;

    /* Row 4: About to clear console */
    debug_bar(fb_addr, 4, 0xFF00FF00);

    /*------------------------------------------------------------------------
     * Step 2: Initialize early console
     *------------------------------------------------------------------------*/
    /* Row 5: Before console_clear */
    debug_bar(fb_addr, 5, 0xFFFF00FF);  /* MAGENTA = about to clear */

    console_clear();

    console_puts("SERAPH Operating System\n");
    console_puts("=======================\n\n");

    console_puts("[KERNEL] Boot info validated\n");
    console_puts("[KERNEL] Framebuffer: ");
    console_put_dec(boot_info->fb_width);
    console_putc('x');
    console_put_dec(boot_info->fb_height);
    console_puts(" @ ");
    console_put_hex(boot_info->framebuffer_base);
    console_putc('\n');

    /*------------------------------------------------------------------------
     * Step 3: Print memory information
     *------------------------------------------------------------------------*/
    console_puts("[KERNEL] Memory map: ");
    console_put_dec(boot_info->memory_map_count);
    console_puts(" entries\n");

    uint64_t total_mem = seraph_boot_total_conventional_memory(boot_info);
    console_puts("[KERNEL] Total RAM: ");
    console_put_dec(total_mem / (1024 * 1024));
    console_puts(" MB\n");

    /*------------------------------------------------------------------------
     * DEBUG: Print memory map details before early_mem_init
     *------------------------------------------------------------------------*/
    console_puts("[DEBUG] Memory map diagnostic:\n");
    console_puts("  memory_map_base: ");
    console_put_hex(boot_info->memory_map_base);
    console_puts("\n  memory_map_size: ");
    console_put_dec(boot_info->memory_map_size);
    console_puts("\n  memory_desc_size: ");
    console_put_dec(boot_info->memory_desc_size);
    console_puts("\n  memory_map_count: ");
    console_put_dec(boot_info->memory_map_count);
    console_puts("\n");

    /* Try to read first memory descriptor */
    if (boot_info->memory_map_count > 0 && boot_info->memory_map_base != 0) {
        const Seraph_Memory_Descriptor* desc = seraph_boot_get_memory_desc(boot_info, 0);
        if (desc) {
            console_puts("  First descriptor:\n");
            console_puts("    phys_start: ");
            console_put_hex(desc->phys_start);
            console_puts("\n    page_count: ");
            console_put_dec(desc->page_count);
            console_puts("\n    type: ");
            console_put_dec(desc->type);
            console_puts("\n");
        } else {
            console_puts("  ERROR: Could not read first descriptor!\n");
        }
    } else {
        console_puts("  ERROR: No memory map or base is NULL!\n");
    }

    /*------------------------------------------------------------------------
     * Step 4: Initialize Early Memory (Bootstrap Paging)
     *------------------------------------------------------------------------*/
    /* Row 10: About to call early_mem_init */
    debug_bar(boot_info->framebuffer_base, 10, 0xFFFFFF00);  /* YELLOW */

    console_puts("[KERNEL] Initializing early memory (bootstrap paging)...\n");
    Seraph_EarlyMem_Result early_result = seraph_early_mem_init(boot_info);

    /* Row 11: After early_mem_init returned */
    debug_bar(boot_info->framebuffer_base, 11, 0xFF00FFFF);  /* CYAN */
    if (early_result != SERAPH_EARLY_MEM_OK) {
        console_puts("[KERNEL] Early memory init failed: ");
        console_put_dec(early_result);
        console_puts("\n");
        kernel_panic("Failed to initialize early memory");
    }

    const Seraph_EarlyMem_State* early_state = seraph_early_mem_get_state();
    console_puts("[KERNEL] Early memory: ");
    console_put_dec(early_state->total_allocated);
    console_puts(" pages allocated for page tables\n");
    console_puts("[KERNEL] PML4 @ ");
    console_put_hex(boot_info->pml4_phys);
    console_puts("\n");

    /*------------------------------------------------------------------------
     * Step 5: Initialize Physical Memory Manager
     *------------------------------------------------------------------------*/
    console_puts("[KERNEL] Initializing PMM...\n");
    seraph_pmm_init(&g_pmm, boot_info);

    console_puts("[KERNEL] PMM: ");
    console_put_dec(seraph_pmm_get_free_pages(&g_pmm));
    console_puts(" free pages (");
    console_put_dec(seraph_pmm_get_free_memory(&g_pmm) / (1024 * 1024));
    console_puts(" MB)\n");

    /*------------------------------------------------------------------------
     * Step 6: Initialize Virtual Memory Manager
     *------------------------------------------------------------------------*/
    console_puts("[KERNEL] Initializing VMM...\n");
    seraph_vmm_init(&g_vmm, &g_pmm, boot_info->pml4_phys);

    console_puts("[KERNEL] VMM initialized\n");

    /*------------------------------------------------------------------------
     * Step 7: Initialize Kernel Allocator
     *------------------------------------------------------------------------*/
    console_puts("[KERNEL] Initializing kmalloc...\n");
    seraph_kmalloc_init(&g_vmm, &g_pmm);

    if (seraph_kmalloc_is_initialized()) {
        console_puts("[KERNEL] kmalloc ready\n");
    } else {
        kernel_panic("Failed to initialize kmalloc");
    }

    /*------------------------------------------------------------------------
     * Step 8: Test allocations
     *------------------------------------------------------------------------*/
    console_puts("[KERNEL] Testing allocations...\n");

    void* test1 = seraph_kmalloc(64);
    void* test2 = seraph_kmalloc(128);
    void* test3 = seraph_kmalloc(4096);

    console_puts("[KERNEL] Allocated: ");
    console_put_hex((uint64_t)test1);
    console_puts(", ");
    console_put_hex((uint64_t)test2);
    console_puts(", ");
    console_put_hex((uint64_t)test3);
    console_putc('\n');

    seraph_kfree(test1);
    seraph_kfree(test2);
    seraph_kfree(test3);
    console_puts("[KERNEL] Allocations freed\n");

    /*------------------------------------------------------------------------
     * Step 9: Initialize Interrupt Descriptor Table
     *
     * The IDT provides exception handlers with SERAPH's unique VOID semantics:
     * - #DE (Divide Error) injects VOID into RAX and continues execution
     * - #PF (Page Fault) routes to VMM for demand paging
     * - #GP (General Protection) terminates the offending Sovereign
     *------------------------------------------------------------------------*/
    console_puts("[KERNEL] Initializing IDT...\n");
    seraph_idt_init();
    console_puts("[KERNEL] IDT initialized (VOID injection ready)\n");

    /*------------------------------------------------------------------------
     * Step 10: Initialize Sovereign Subsystem (THE PRIMORDIAL)
     *
     * THE PRIMORDIAL is the root Sovereign - the ancestor of all processes.
     * It is created statically at boot and has full authority over the system.
     *------------------------------------------------------------------------*/
    console_puts("[KERNEL] Initializing Sovereign subsystem...\n");
    seraph_sovereign_subsystem_init();

    if (seraph_the_primordial != NULL) {
        console_puts("[KERNEL] THE PRIMORDIAL created (ID: ");
        console_put_hex(seraph_the_primordial->id.quads[0]);
        console_puts(")\n");
    } else {
        kernel_panic("Failed to create THE PRIMORDIAL");
    }

    /*------------------------------------------------------------------------
     * Step 11: Initialize Scheduler
     *
     * The preemptive scheduler manages Strand execution with:
     * - Priority-based scheduling (7 priority levels)
     * - Galactic predictive scheduling (automatic differentiation)
     * - APIC timer for preemption
     *------------------------------------------------------------------------*/
    console_puts("[KERNEL] Initializing scheduler...\n");
    seraph_scheduler_init();
    console_puts("[KERNEL] Scheduler initialized\n");

    /*------------------------------------------------------------------------
     * Step 12: Enable Galactic Predictive Scheduling
     *
     * Galactic numbers (hyper-dual numbers) enable automatic differentiation
     * of execution time trends. The scheduler uses this to predict future
     * CPU needs and proactively adjust priorities via gradient descent.
     *------------------------------------------------------------------------*/
    console_puts("[KERNEL] Enabling Galactic predictive scheduling...\n");
    seraph_scheduler_set_galactic_enabled(true);
    console_puts("[KERNEL] Galactic scheduling enabled\n");

    /*------------------------------------------------------------------------
     * Step 13: Create Init Strand
     *
     * The init strand is the first thread in the system. It runs as part
     * of THE PRIMORDIAL and demonstrates SERAPH's unique features:
     * - VOID propagation (divide by zero produces VOID, not crash)
     * - Capability-based memory safety
     * - Chronon temporal tracking
     *------------------------------------------------------------------------*/
    console_puts("[KERNEL] Creating init strand...\n");

    /* Allocate strand structure */
    console_puts("[KERNEL]   kmalloc strand struct...\n");
    Seraph_Strand* init_strand = (Seraph_Strand*)seraph_kmalloc(sizeof(Seraph_Strand));
    if (init_strand == NULL) {
        kernel_panic("Failed to allocate init strand");
    }
    console_puts("[KERNEL]   struct allocated at ");
    console_put_hex((uint64_t)init_strand);
    console_puts("\n");

    /* Debug: draw bar before call */
    {
        volatile uint32_t* fb = (volatile uint32_t*)g_boot_info->framebuffer_base;
        for (int x = 0; x < 400; x++) fb[350 * 1920 + x] = 0xFFFF0000; /* Red bar */
    }

    console_puts("[KERNEL]   calling strand_create...\n");

    /* Debug: draw bar after print */
    {
        volatile uint32_t* fb = (volatile uint32_t*)g_boot_info->framebuffer_base;
        for (int x = 0; x < 400; x++) fb[352 * 1920 + x] = 0xFF00FF00; /* Green bar */
    }

    /* Create the strand with our init_main entry point */
    Seraph_Strand_Error err = seraph_strand_create(
        init_strand,
        seraph_init_main,
        NULL,
        4096  /* Small stack for faster boot */
    );

    /* Debug: draw bar after call */
    {
        volatile uint32_t* fb = (volatile uint32_t*)g_boot_info->framebuffer_base;
        for (int x = 0; x < 400; x++) fb[354 * 1920 + x] = 0xFF0000FF; /* Blue bar */
    }

    console_puts("[KERNEL]   strand_create done\n");

    if (err != SERAPH_STRAND_OK) {
        kernel_panic("Failed to create init strand");
    }

    /* Mark as kernel strand */
    init_strand->flags |= SERAPH_STRAND_FLAG_KERNEL;

    /* Attach to THE PRIMORDIAL */
    seraph_the_primordial->strands[0] = init_strand;
    seraph_the_primordial->strand_count = 1;
    seraph_the_primordial->running_strands = 1;
    seraph_the_primordial->main_strand_idx = 0;

    console_puts("[KERNEL] Init strand created (ID: ");
    console_put_hex(init_strand->strand_id);
    console_puts(")\n");

    /* Add to scheduler ready queue */
    seraph_scheduler_ready(init_strand);
    console_puts("[KERNEL] Init strand added to scheduler\n");

    /*------------------------------------------------------------------------
     * Step 14: Enable Interrupts
     *------------------------------------------------------------------------*/
    console_puts("[KERNEL] Enabling interrupts...\n");
    seraph_int_enable();
    console_puts("[KERNEL] Interrupts enabled\n");

    /*------------------------------------------------------------------------
     * Step 15: Start Scheduler
     *
     * This enables the APIC timer and begins preemptive scheduling.
     * From this point forward, the scheduler controls execution.
     * The idle loop below catches any returns from the scheduler.
     *------------------------------------------------------------------------*/
    console_puts("\n");
    console_puts("[KERNEL] =========================================\n");
    console_puts("[KERNEL]   SERAPH Operating System - ONLINE       \n");
    console_puts("[KERNEL]   Starting preemptive scheduler...       \n");
    console_puts("[KERNEL] =========================================\n");
    console_puts("\n");

    seraph_scheduler_start();

    /*------------------------------------------------------------------------
     * Fallback: If scheduler returns, enter idle loop
     *------------------------------------------------------------------------*/
    console_puts("[KERNEL] Scheduler returned - entering kernel idle loop\n");

    while (1) {
        __asm__ volatile("hlt");
    }
}

/*============================================================================
 * UEFI Entry Point Wrapper
 *
 * This is the actual entry point called by the SERAPH bootloader.
 * We need to set up the stack properly before calling kernel_main.
 *
 * CALLING CONVENTION NOTES:
 *   - Standard UEFI uses Microsoft x64 ABI (arguments in RCX, RDX, R8, R9)
 *   - Our bootloader (efi_main.c) uses EXPLICIT inline assembly to pass
 *     boot_info in RDI (System V convention) before calling this entry point
 *   - This is intentional: the kernel uses System V AMD64 ABI throughout
 *   - The bootloader's inline asm at lines 477-484 explicitly sets RDI
 *
 * Therefore: boot_info is ALREADY in RDI when we arrive here.
 * DO NOT move RCX→RDI as that would clobber the correct value!
 *
 * If using a different bootloader that passes boot_info in RCX (true MS ABI),
 * uncomment the ABI translation below:
 *   // __asm__ volatile("mov %%rcx, %%rdi\n" ::: "rdi");
 *============================================================================*/

void __attribute__((noreturn, naked)) _start(void) {
    /*
     * Register state when called from SERAPH bootloader (efi_main.c):
     *   RDI = boot_info pointer (set by bootloader's inline asm)
     *   RSP = kernel stack top (set by bootloader)
     *
     * If called from a standard UEFI bootloader using MS ABI:
     *   RCX = boot_info pointer
     *   (would need: mov %%rcx, %%rdi)
     */
    __asm__ volatile(
        /* Set up stack frame */
        "mov %%rsp, %%rbp\n"          /* Set up frame pointer */
        "and $-16, %%rsp\n"           /* Align stack to 16 bytes (ABI requirement) */

        /* Call kernel_main with boot_info already in RDI */
        "call kernel_main\n"

        /* Kernel should never return, but halt if it does */
        "cli\n"
        "1: hlt\n"
        "jmp 1b\n"
        ::: "memory"
    );
    __builtin_unreachable();
}
