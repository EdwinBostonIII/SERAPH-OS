/**
 * @file early_mem.c
 * @brief MC26: Early Memory Initialization - Bootstrap Paging Implementation
 *
 * This module bootstraps the kernel's virtual memory layout before the VMM
 * is initialized. It runs with UEFI's identity mapping still active.
 *
 * Memory Layout After Initialization:
 *
 *   0x0000_0000_0000_0000 - 0x0000_0001_0000_0000 : Identity map (4GB)
 *   0xFFFF_8000_0000_0000 - 0xFFFF_8XXX_XXXX_XXXX : Physical memory map
 *   0xFFFF_FFFF_8000_0000 - 0xFFFF_FFFF_FFFF_FFFF : Kernel higher-half
 *
 * Page Table Structure (4-level paging):
 *   PML4[0]   -> Identity mapping (lower 4GB)
 *   PML4[256] -> Physical memory map start
 *   PML4[510] -> Recursive mapping (self-reference)
 *   PML4[511] -> Kernel code (higher half)
 */

#include "seraph/early_mem.h"
#include "seraph/boot.h"
#include "seraph/vmm.h"

#include <string.h>

/*============================================================================
 * Page Table Constants
 *============================================================================*/

/** Page sizes */
#define PAGE_SIZE_4K   0x1000ULL
#define PAGE_SIZE_2M   0x200000ULL
#define PAGE_SIZE_1G   0x40000000ULL

/** Page table entry flags */
#define PTE_PRESENT    (1ULL << 0)
#define PTE_WRITABLE   (1ULL << 1)
#define PTE_USER       (1ULL << 2)
#define PTE_HUGE       (1ULL << 7)
#define PTE_GLOBAL     (1ULL << 8)
#define PTE_NX         (1ULL << 63)

/** Physical address mask */
#define PTE_ADDR_MASK  0x000FFFFFFFFFF000ULL

/** PML4 indices for key regions */
#define PML4_IDX_IDENTITY      0     /* Identity map starts at index 0 */
#define PML4_IDX_PHYS_MAP      256   /* Physical map at 0xFFFF800000000000 */
#define PML4_IDX_RECURSIVE     510   /* Recursive mapping */
#define PML4_IDX_KERNEL        511   /* Kernel higher-half */

/** Number of PML4 entries to identity map (4GB = 4 entries, each covers 512GB... wait, that's wrong)
 *  Actually: PML4 entry covers 512GB, PDPT entry covers 1GB, PD entry covers 2MB
 *  For 4GB identity map: need 4 PDPT entries (4 x 1GB pages)
 */
#define IDENTITY_MAP_SIZE_GB   4

/*============================================================================
 * Static State
 *============================================================================*/

/** Early memory allocator state */
static Seraph_EarlyMem_State g_early_state = {
    .total_allocated = 0,
    .pml4_phys = 0,
    .allocated_pages = {0},
    .initialized = false,
};

/** Cursor for bump allocator - tracks where we are in memory map scanning */
static struct {
    uint32_t current_region;     /* Current memory map entry index */
    uint64_t current_offset;     /* Offset within current region */
} g_alloc_cursor = {0, 0};

/** Arena-based allocator state */
static struct {
    uint64_t arena_base;
    uint64_t arena_size;
    uint64_t arena_offset;
} g_arena = {0, 0, 0};

/**
 * @brief Allocate a page from the primordial arena
 */
static uint64_t arena_alloc_page(void) {
    if (g_arena.arena_base == 0) return 0;
    if (g_arena.arena_offset + PAGE_SIZE_4K > g_arena.arena_size) return 0;

    uint64_t page = g_arena.arena_base + g_arena.arena_offset;
    g_arena.arena_offset += PAGE_SIZE_4K;

    /* Zero the page */
    memset((void*)page, 0, PAGE_SIZE_4K);

    /* Track it */
    if (g_early_state.total_allocated < SERAPH_EARLY_MEM_MAX_PAGES) {
        g_early_state.allocated_pages[g_early_state.total_allocated] = page;
    }
    g_early_state.total_allocated++;

    return page;
}

/*============================================================================
 * CR3 Management
 *============================================================================*/

/**
 * @brief Read CR3 (current PML4 physical address)
 */
static inline uint64_t read_cr3(void) {
    uint64_t value;
    __asm__ volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

/**
 * @brief Write CR3 (switch page tables)
 */
static inline void write_cr3(uint64_t value) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(value) : "memory");
}

/*============================================================================
 * Early Page Allocator
 *============================================================================*/

/**
 * @brief Check if a memory region is usable after ExitBootServices
 *
 * The bootloader converts UEFI types to SERAPH types:
 *   - SERAPH_MEM_LOADER_CODE (1): Our bootloader code, becomes free
 *   - SERAPH_MEM_LOADER_DATA (2): Bootloader data, becomes free
 *   - SERAPH_MEM_BOOT_SERVICES (3): UEFI boot services (code+data), becomes free
 *   - SERAPH_MEM_CONVENTIONAL (7): Always free
 *
 * Memory types that must NOT be used:
 *   - SERAPH_MEM_RUNTIME_SERVICES (4): UEFI runtime services (must preserve!)
 *   - SERAPH_MEM_RESERVED (0): Reserved memory
 *   - Others: ACPI, MMIO, etc.
 */
static bool is_usable_memory(uint32_t type) {
    switch (type) {
        case SERAPH_MEM_LOADER_CODE:    /* 1 - bootloader code, becomes free */
        case SERAPH_MEM_LOADER_DATA:    /* 2 - bootloader data, becomes free */
        case SERAPH_MEM_BOOT_SERVICES:  /* 3 - UEFI boot services, becomes free */
        case SERAPH_MEM_CONVENTIONAL:   /* 7 - always free */
            return true;
        default:
            return false;
    }
}

/* Debug: print to framebuffer */
static void early_print_type(uint64_t fb, int y, uint32_t type, uint64_t pages) {
    if (!fb) return;
    volatile uint32_t* framebuffer = (volatile uint32_t*)fb;
    /* Draw a colored pixel based on type - each type gets a different color */
    uint32_t colors[] = {
        0xFFFF0000,  /* 0: Reserved - Red */
        0xFF00FF00,  /* 1: LoaderCode - Green */
        0xFF0000FF,  /* 2: LoaderData - Blue */
        0xFFFFFF00,  /* 3: BootServices - Yellow */
        0xFFFF00FF,  /* 4: RuntimeServices - Magenta */
        0xFF808080,  /* 5: unused */
        0xFF808080,  /* 6: unused */
        0xFF00FFFF,  /* 7: Conventional - Cyan */
    };
    uint32_t color = (type < 8) ? colors[type] : 0xFFFFFFFF;
    /* Draw bar at row y, width based on pages (capped) */
    int width = (pages > 1000) ? 200 : (int)(pages / 5);
    if (width < 10) width = 10;
    for (int x = 0; x < width; x++) {
        framebuffer[y * 1920 + x] = color;
    }
}

/* Debug: draw a number digit at position */
static void draw_hex_digit(uint64_t fb, int x, int y, int digit) {
    if (!fb) return;
    volatile uint32_t* framebuffer = (volatile uint32_t*)fb;
    uint32_t color = 0xFFFFFFFF;
    /* Simple 4x6 hex digit patterns */
    static const uint8_t digits[16][6] = {
        {0xF,0x9,0x9,0x9,0xF,0}, /* 0 */
        {0x2,0x2,0x2,0x2,0x2,0}, /* 1 */
        {0xF,0x1,0xF,0x8,0xF,0}, /* 2 */
        {0xF,0x1,0xF,0x1,0xF,0}, /* 3 */
        {0x9,0x9,0xF,0x1,0x1,0}, /* 4 */
        {0xF,0x8,0xF,0x1,0xF,0}, /* 5 */
        {0xF,0x8,0xF,0x9,0xF,0}, /* 6 */
        {0xF,0x1,0x1,0x1,0x1,0}, /* 7 */
        {0xF,0x9,0xF,0x9,0xF,0}, /* 8 */
        {0xF,0x9,0xF,0x1,0xF,0}, /* 9 */
        {0xF,0x9,0xF,0x9,0x9,0}, /* A */
        {0xE,0x9,0xE,0x9,0xE,0}, /* B */
        {0xF,0x8,0x8,0x8,0xF,0}, /* C */
        {0xE,0x9,0x9,0x9,0xE,0}, /* D */
        {0xF,0x8,0xF,0x8,0xF,0}, /* E */
        {0xF,0x8,0xF,0x8,0x8,0}, /* F */
    };
    digit &= 0xF;
    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 4; col++) {
            if (digits[digit][row] & (8 >> col)) {
                framebuffer[(y + row) * 1920 + x + col] = color;
            }
        }
    }
}

static void draw_hex32(uint64_t fb, int x, int y, uint32_t val) {
    for (int i = 0; i < 8; i++) {
        draw_hex_digit(fb, x + i * 5, y, (val >> (28 - i * 4)) & 0xF);
    }
}

uint64_t seraph_early_alloc_page(const Seraph_BootInfo* boot_info) {
    if (!boot_info || boot_info->memory_map_count == 0) {
        return 0;
    }

    uint64_t fb = boot_info->framebuffer_base;

    /* Debug: On first call, show first 10 memory regions with details */
    static bool debug_printed = false;
    if (!debug_printed && fb) {
        debug_printed = true;
        /* Show memory_map_count */
        draw_hex32(fb, 400, 50, boot_info->memory_map_count);
        /* Show memory_desc_size */
        draw_hex32(fb, 400, 60, (uint32_t)boot_info->memory_desc_size);

        for (uint32_t i = 0; i < boot_info->memory_map_count && i < 15; i++) {
            const Seraph_Memory_Descriptor* d = seraph_boot_get_memory_desc(boot_info, i);
            if (d) {
                int row_y = 80 + i * 10;
                /* Show type */
                draw_hex32(fb, 10, row_y, d->type);
                /* Show phys_start (low 32 bits) */
                draw_hex32(fb, 60, row_y, (uint32_t)d->phys_start);
                /* Show page_count */
                draw_hex32(fb, 120, row_y, (uint32_t)d->page_count);
                /* Color bar for type */
                early_print_type(fb, row_y / 4 + 25, d->type, d->page_count);
            }
        }
    }

    /* Search from current cursor position */
    while (g_alloc_cursor.current_region < boot_info->memory_map_count) {
        const Seraph_Memory_Descriptor* desc = seraph_boot_get_memory_desc(
            boot_info, g_alloc_cursor.current_region);

        if (!desc) {
            g_alloc_cursor.current_region++;
            g_alloc_cursor.current_offset = 0;
            continue;
        }

        /* Only use memory types that are free after ExitBootServices */
        if (!is_usable_memory(desc->type)) {
            g_alloc_cursor.current_region++;
            g_alloc_cursor.current_offset = 0;
            continue;
        }

        /* Only use memory below 4GB - we need identity mapping to access it */
        if (desc->phys_start >= 0x100000000ULL) {
            g_alloc_cursor.current_region++;
            g_alloc_cursor.current_offset = 0;
            continue;
        }

        /* Check if there's space in this region */
        uint64_t region_size = desc->page_count * PAGE_SIZE_4K;
        if (g_alloc_cursor.current_offset + PAGE_SIZE_4K <= region_size) {
            uint64_t phys = desc->phys_start + g_alloc_cursor.current_offset;
            g_alloc_cursor.current_offset += PAGE_SIZE_4K;

            /* Track allocated page for PMM to later mark as used */
            if (g_early_state.total_allocated < SERAPH_EARLY_MEM_MAX_PAGES) {
                g_early_state.allocated_pages[g_early_state.total_allocated] = phys;
            }
            g_early_state.total_allocated++;

            /* Zero the page (using identity mapping - still active during early boot) */
            memset((void*)phys, 0, PAGE_SIZE_4K);

            return phys;
        }

        /* Move to next region */
        g_alloc_cursor.current_region++;
        g_alloc_cursor.current_offset = 0;
    }

    return 0; /* Out of memory */
}

uint64_t seraph_early_get_total_memory(const Seraph_BootInfo* boot_info) {
    if (!boot_info) return 0;

    uint64_t max_addr = 0;

    for (uint32_t i = 0; i < boot_info->memory_map_count; i++) {
        const Seraph_Memory_Descriptor* desc = seraph_boot_get_memory_desc(boot_info, i);
        if (!desc) continue;

        uint64_t end_addr = desc->phys_start + desc->page_count * PAGE_SIZE_4K;
        if (end_addr > max_addr) {
            max_addr = end_addr;
        }
    }

    return max_addr;
}

/*============================================================================
 * Page Table Creation Helpers
 *============================================================================*/

/**
 * @brief Create a page table entry
 */
static inline uint64_t make_pte(uint64_t phys_addr, uint64_t flags) {
    return (phys_addr & PTE_ADDR_MASK) | flags;
}

/**
 * @brief Get or create a page table at the next level
 *
 * If the entry is present, return the physical address of the next level table.
 * If not present, allocate a new page table and set the entry.
 *
 * @param table Current page table (accessed via identity mapping)
 * @param index Entry index
 * @param boot_info For allocating new pages
 * @return Physical address of next level table, or 0 on failure
 */
static uint64_t get_or_create_table(uint64_t* table, int index,
                                     const Seraph_BootInfo* boot_info) {
    (void)boot_info;  /* Now using arena instead */

    if (table[index] & PTE_PRESENT) {
        return table[index] & PTE_ADDR_MASK;
    }

    /* Use arena allocator instead of memory map scanning */
    uint64_t new_table = arena_alloc_page();
    if (new_table == 0) {
        return 0;
    }

    table[index] = make_pte(new_table, PTE_PRESENT | PTE_WRITABLE);
    return new_table;
}

/*============================================================================
 * Identity Mapping Setup
 *============================================================================*/

/**
 * @brief Set up identity mapping for the lower address space
 *
 * Maps the first IDENTITY_MAP_SIZE_GB using 1GB huge pages for efficiency.
 * This ensures boot code continues to work during the CR3 switch.
 *
 * @param pml4 PML4 table (accessed via identity mapping)
 * @param boot_info For allocating page tables
 * @return true on success
 */
static bool setup_identity_mapping(uint64_t* pml4, const Seraph_BootInfo* boot_info) {
    /* Get or create PDPT for identity mapping */
    uint64_t pdpt_phys = get_or_create_table(pml4, PML4_IDX_IDENTITY, boot_info);
    if (pdpt_phys == 0) return false;

    uint64_t* pdpt = (uint64_t*)pdpt_phys;

    /* Map first N gigabytes using 1GB huge pages */
    for (int i = 0; i < IDENTITY_MAP_SIZE_GB; i++) {
        uint64_t phys_addr = (uint64_t)i * PAGE_SIZE_1G;
        pdpt[i] = make_pte(phys_addr, PTE_PRESENT | PTE_WRITABLE | PTE_HUGE);
    }

    return true;
}

/*============================================================================
 * Physical Memory Map Setup
 *============================================================================*/

/**
 * @brief Set up the physical memory map at SERAPH_PHYS_MAP_BASE
 *
 * Maps all physical RAM so that seraph_phys_to_virt() works.
 * Uses 2MB huge pages for efficiency.
 *
 * @param pml4 PML4 table
 * @param boot_info For memory size and allocation
 * @return true on success
 */
static bool setup_physical_map(uint64_t* pml4, const Seraph_BootInfo* boot_info) {
    uint64_t total_mem = seraph_early_get_total_memory(boot_info);
    if (total_mem == 0) return false;

    /* Round up to next 2MB boundary */
    uint64_t map_size = (total_mem + PAGE_SIZE_2M - 1) & ~(PAGE_SIZE_2M - 1);
    uint64_t num_2mb_pages = map_size / PAGE_SIZE_2M;

    /*
     * Physical map starts at SERAPH_PHYS_MAP_BASE (0xFFFF800000000000)
     * This is PML4[256] in the higher half.
     *
     * Use 2MB huge pages for compatibility (not all hypervisors support 1GB pages).
     * Structure: PML4[256] -> PDPT -> PD -> 2MB pages
     */

    uint64_t current_phys = 0;
    uint64_t remaining_2mb = num_2mb_pages;
    int pml4_idx = PML4_IDX_PHYS_MAP;

    while (remaining_2mb > 0 && pml4_idx < PML4_IDX_RECURSIVE) {
        /* Get or create PDPT */
        uint64_t pdpt_phys = get_or_create_table(pml4, pml4_idx, boot_info);
        if (pdpt_phys == 0) return false;
        uint64_t* pdpt = (uint64_t*)pdpt_phys;

        /* Each PDPT entry covers 1GB = 512 x 2MB pages */
        for (int pdpt_idx = 0; pdpt_idx < 512 && remaining_2mb > 0; pdpt_idx++) {
            /* Get or create PD for this 1GB region */
            uint64_t pd_phys = get_or_create_table(pdpt, pdpt_idx, boot_info);
            if (pd_phys == 0) return false;
            uint64_t* pd = (uint64_t*)pd_phys;

            /* Fill PD with 2MB huge page entries */
            int entries_to_map = (remaining_2mb > 512) ? 512 : (int)remaining_2mb;
            for (int pd_idx = 0; pd_idx < entries_to_map; pd_idx++) {
                /* Use 2MB huge pages (no NX to avoid WHP validation issues) */
                pd[pd_idx] = make_pte(current_phys, PTE_PRESENT | PTE_WRITABLE | PTE_HUGE);
                current_phys += PAGE_SIZE_2M;
                remaining_2mb--;
            }
        }
        pml4_idx++;
    }

    return true;
}

/*============================================================================
 * Framebuffer Mapping
 *============================================================================*/

/**
 * @brief Ensure framebuffer is mapped
 *
 * The framebuffer may be outside the identity-mapped region.
 * Map it so early console output continues to work.
 *
 * @param pml4 PML4 table
 * @param boot_info Contains framebuffer address
 * @return true on success
 */
static bool setup_framebuffer_mapping(uint64_t* pml4, const Seraph_BootInfo* boot_info) {
    if (!seraph_boot_has_framebuffer(boot_info)) {
        return true; /* No framebuffer, nothing to do */
    }

    uint64_t fb_base = boot_info->framebuffer_base;
    uint64_t fb_size = boot_info->framebuffer_size;
    uint64_t fb_end = fb_base + fb_size;

    /* Check if framebuffer is within identity-mapped region */
    if (fb_end <= IDENTITY_MAP_SIZE_GB * PAGE_SIZE_1G) {
        return true; /* Already mapped by identity mapping */
    }

    /*
     * Framebuffer is outside identity map. We need to add identity mapping
     * for it. Calculate which 1GB regions we need to map.
     */
    uint64_t start_1gb = fb_base & ~(PAGE_SIZE_1G - 1);
    uint64_t end_1gb = (fb_end + PAGE_SIZE_1G - 1) & ~(PAGE_SIZE_1G - 1);

    /* Get PDPT for the PML4 entry containing the framebuffer */
    int pml4_idx = (int)(start_1gb >> 39) & 0x1FF;

    /* Only handle single PML4 entry for simplicity */
    uint64_t pdpt_phys = get_or_create_table(pml4, pml4_idx, boot_info);
    if (pdpt_phys == 0) return false;

    uint64_t* pdpt = (uint64_t*)pdpt_phys;

    /* Map 1GB huge pages covering the framebuffer */
    for (uint64_t addr = start_1gb; addr < end_1gb; addr += PAGE_SIZE_1G) {
        int pdpt_idx = (int)(addr >> 30) & 0x1FF;
        if (!(pdpt[pdpt_idx] & PTE_PRESENT)) {
            /* Map as uncacheable for framebuffer (write-combining would be better) */
            pdpt[pdpt_idx] = make_pte(addr, PTE_PRESENT | PTE_WRITABLE | PTE_HUGE);
        }
    }

    return true;
}

/*============================================================================
 * Main Initialization
 *============================================================================*/

/* Debug: draw colored bar at row y to show progress */
static void early_debug_bar(uint64_t fb_base, int row, uint32_t color) {
    if (fb_base) {
        volatile uint32_t* fb = (volatile uint32_t*)fb_base;
        for (int x = 0; x < 300; x++) {
            fb[row * 1920 + x] = color;
        }
    }
}

/* Legacy name for compatibility */
static void early_debug_pixel(uint64_t fb_base, int x, uint32_t color) {
    (void)x;
    /* Draw at row based on x position for backward compat */
    int row = x / 50;
    early_debug_bar(fb_base, row, color);
}

Seraph_EarlyMem_Result seraph_early_mem_init(Seraph_BootInfo* boot_info) {
    uint64_t fb = boot_info ? boot_info->framebuffer_base : 0;

    early_debug_pixel(fb, 0, 0xFFFFFF00);  /* Yellow = entered function */

    if (!boot_info) {
        early_debug_pixel(fb, 100, 0xFFFF0000);  /* Red = no boot_info */
        return SERAPH_EARLY_MEM_INVALID_BOOTINFO;
    }

    early_debug_pixel(fb, 50, 0xFF00FF00);  /* Green = boot_info exists */

    if (!seraph_boot_info_valid(boot_info)) {
        early_debug_pixel(fb, 100, 0xFFFF00FF);  /* Magenta = invalid boot_info */
        return SERAPH_EARLY_MEM_INVALID_BOOTINFO;
    }

    early_debug_pixel(fb, 100, 0xFF00FF00);  /* Green = boot_info valid */

    if (boot_info->memory_map_count == 0) {
        early_debug_pixel(fb, 150, 0xFFFF8000);  /* Orange = no memory map */
        return SERAPH_EARLY_MEM_NO_MEMORY_MAP;
    }

    early_debug_pixel(fb, 150, 0xFF00FF00);  /* Green = has memory map */

    /* Already initialized? */
    if (g_early_state.initialized) {
        return SERAPH_EARLY_MEM_OK;
    }

    early_debug_pixel(fb, 200, 0xFF00FFFF);  /* Cyan = starting allocation */

    /*------------------------------------------------------------------------
     * Step 1: Initialize arena allocator from primordial arena
     *------------------------------------------------------------------------*/
    if (boot_info->primordial_arena_phys == 0 || boot_info->primordial_arena_size < PAGE_SIZE_4K * 16) {
        early_debug_bar(fb, 20, 0xFFFF0000);  /* Red = no arena */
        return SERAPH_EARLY_MEM_NO_MEMORY;
    }

    /* Initialize arena */
    g_arena.arena_base = boot_info->primordial_arena_phys;
    g_arena.arena_size = boot_info->primordial_arena_size;
    g_arena.arena_offset = 0;

    early_debug_bar(fb, 21, 0xFF00FF00);  /* Green = arena initialized */

    /*------------------------------------------------------------------------
     * Step 2: Check if hypervisor/bootloader already provided valid page tables
     *
     * If pml4_phys is already set, the hypervisor has created page tables
     * with identity mapping and recursive mapping. We use those instead of
     * allocating from the arena (which keeps the arena free for PMM bitmap).
     *------------------------------------------------------------------------*/
    uint64_t pml4_phys;
    bool using_existing_pml4 = false;

    if (boot_info->pml4_phys != 0) {
        /* Hypervisor provided page tables - use them */
        pml4_phys = boot_info->pml4_phys;
        using_existing_pml4 = true;
        early_debug_bar(fb, 22, 0xFF00FFFF);  /* Cyan = using existing PML4 */
    } else {
        /* Allocate PML4 from arena */
        pml4_phys = arena_alloc_page();
        if (pml4_phys == 0) {
            early_debug_pixel(fb, 250, 0xFFFF0000);  /* Red = alloc failed */
            return SERAPH_EARLY_MEM_NO_MEMORY;
        }
        early_debug_pixel(fb, 250, 0xFF00FF00);  /* Green = PML4 allocated */
    }

    uint64_t* pml4 = (uint64_t*)pml4_phys; /* Identity mapping still active */

    if (!using_existing_pml4) {
        /*--------------------------------------------------------------------
         * Step 3a: Set up recursive mapping (PML4[510] -> PML4)
         *--------------------------------------------------------------------*/
        pml4[PML4_IDX_RECURSIVE] = make_pte(pml4_phys, PTE_PRESENT | PTE_WRITABLE);
        early_debug_pixel(fb, 300, 0xFF00FF00);  /* Green = recursive set */

        /*--------------------------------------------------------------------
         * Step 3b: Set up identity mapping for lower 4GB
         *--------------------------------------------------------------------*/
        if (!setup_identity_mapping(pml4, boot_info)) {
            early_debug_pixel(fb, 350, 0xFFFF0000);  /* Red = identity map failed */
            return SERAPH_EARLY_MEM_NO_MEMORY;
        }
        early_debug_pixel(fb, 350, 0xFF00FF00);  /* Green = identity done */
    } else {
        /* Using existing PML4 - hypervisor already set up identity + recursive */
        early_debug_pixel(fb, 300, 0xFF00FFFF);  /* Cyan = skipped recursive */
        early_debug_pixel(fb, 350, 0xFF00FFFF);  /* Cyan = skipped identity */
    }

    /*------------------------------------------------------------------------
     * Step 4: Set up physical memory map at SERAPH_PHYS_MAP_BASE
     * (Always needed, even if using existing PML4)
     *------------------------------------------------------------------------*/
    if (!setup_physical_map(pml4, boot_info)) {
        early_debug_pixel(fb, 400, 0xFFFF0000);  /* Red = phys map failed */
        return SERAPH_EARLY_MEM_NO_MEMORY;
    }
    early_debug_pixel(fb, 400, 0xFF00FF00);  /* Green = phys map done */

    /*------------------------------------------------------------------------
     * Step 5: Map framebuffer if outside identity region
     *------------------------------------------------------------------------*/
    if (!setup_framebuffer_mapping(pml4, boot_info)) {
        early_debug_pixel(fb, 450, 0xFFFF0000);  /* Red = FB map failed */
        return SERAPH_EARLY_MEM_NO_MEMORY;
    }
    early_debug_pixel(fb, 450, 0xFF00FF00);  /* Green = FB map done */

    /*------------------------------------------------------------------------
     * Step 6: Update boot_info with PML4 address
     *------------------------------------------------------------------------*/
    boot_info->pml4_phys = pml4_phys;

    /*------------------------------------------------------------------------
     * Step 7: Flush TLB / activate page tables
     *
     * Always write CR3 to flush the TLB. This is needed whether we
     * created new page tables or modified existing ones (adding physical
     * map, framebuffer mappings, etc.).
     *------------------------------------------------------------------------*/
    write_cr3(pml4_phys);

    /*------------------------------------------------------------------------
     * Step 8: Update arena info for PMM
     *
     * The PMM will place its bitmap at primordial_arena_phys. We've allocated
     * some pages from the arena for page tables (physical map PDPTs, etc.).
     * Update the arena info so PMM starts AFTER our allocations, avoiding
     * overwriting our page tables.
     *------------------------------------------------------------------------*/
    if (g_arena.arena_offset > 0) {
        boot_info->primordial_arena_phys = g_arena.arena_base + g_arena.arena_offset;
        boot_info->primordial_arena_size = g_arena.arena_size - g_arena.arena_offset;
    }

    /*------------------------------------------------------------------------
     * Step 9: Update state
     *------------------------------------------------------------------------*/
    g_early_state.pml4_phys = pml4_phys;
    g_early_state.initialized = true;

    return SERAPH_EARLY_MEM_OK;
}

const Seraph_EarlyMem_State* seraph_early_mem_get_state(void) {
    return &g_early_state;
}
