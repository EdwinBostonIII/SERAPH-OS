/**
 * @file pic.c
 * @brief MC23: 8259 Programmable Interrupt Controller Management
 *
 * SERAPH: Semantic Extensible Resilient Automatic Persistent Hypervisor
 *
 * The 8259 PIC is the classic PC interrupt controller. Although modern
 * systems use the APIC (Advanced Programmable Interrupt Controller),
 * the legacy 8259 is still present and must be properly configured.
 *
 * PIC ARCHITECTURE:
 *
 *   Master PIC (PIC1): Handles IRQ 0-7
 *   Slave PIC (PIC2):  Handles IRQ 8-15
 *
 * The slave is cascaded through the master's IRQ2 line.
 *
 * By default, the BIOS maps IRQs to vectors 0x08-0x0F (master) and
 * 0x70-0x77 (slave). This conflicts with CPU exceptions (0x00-0x1F),
 * so we remap:
 *
 *   IRQ 0-7  -> Vectors 0x20-0x27 (32-39)
 *   IRQ 8-15 -> Vectors 0x28-0x2F (40-47)
 *
 * I/O PORTS:
 *
 *   Master PIC: 0x20 (command), 0x21 (data)
 *   Slave PIC:  0xA0 (command), 0xA1 (data)
 *
 * INITIALIZATION CONTROL WORDS (ICW):
 *
 *   ICW1: Initialization command (edge triggering, cascading, ICW4 needed)
 *   ICW2: Vector offset (where IRQs start)
 *   ICW3: Cascade configuration
 *   ICW4: Environment mode (8086 mode, auto EOI, etc.)
 *
 * OPERATION CONTROL WORDS (OCW):
 *
 *   OCW1: Interrupt mask (written to data port)
 *   OCW2: EOI command (written to command port)
 *   OCW3: Read IRR/ISR commands
 */

#include "seraph/interrupts.h"
#include <stdint.h>

/*============================================================================
 * PIC Constants
 *============================================================================*/

/** Master PIC ports */
#define PIC1_COMMAND    0x20
#define PIC1_DATA       0x21

/** Slave PIC ports */
#define PIC2_COMMAND    0xA0
#define PIC2_DATA       0xA1

/** Initialization Control Word 1 bits */
#define ICW1_INIT       0x10    /**< Initialization bit (required) */
#define ICW1_ICW4       0x01    /**< ICW4 needed */
#define ICW1_SINGLE     0x02    /**< Single mode (vs cascade) */
#define ICW1_INTERVAL4  0x04    /**< Call address interval 4 (vs 8) */
#define ICW1_LEVEL      0x08    /**< Level triggered (vs edge) */

/** Initialization Control Word 4 bits */
#define ICW4_8086       0x01    /**< 8086/88 mode */
#define ICW4_AUTO_EOI   0x02    /**< Auto EOI mode */
#define ICW4_BUF_SLAVE  0x08    /**< Buffered mode (slave) */
#define ICW4_BUF_MASTER 0x0C    /**< Buffered mode (master) */
#define ICW4_SFNM       0x10    /**< Special fully nested mode */

/** End of Interrupt command */
#define PIC_EOI         0x20

/** Read ISR/IRR commands (OCW3) */
#define PIC_READ_IRR    0x0A    /**< Read Interrupt Request Register */
#define PIC_READ_ISR    0x0B    /**< Read In-Service Register */

/*============================================================================
 * Port I/O
 *============================================================================*/

/**
 * @brief Write byte to I/O port
 */
static inline void outb(uint16_t port, uint8_t value) {
#if defined(__GNUC__) || defined(__clang__)
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
#elif defined(_MSC_VER)
    /* MSVC requires intrinsic or separate .asm file */
    extern void _seraph_outb(uint16_t port, uint8_t value);
    _seraph_outb(port, value);
#endif
}

/**
 * @brief Read byte from I/O port
 */
static inline uint8_t inb(uint16_t port) {
#if defined(__GNUC__) || defined(__clang__)
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
#elif defined(_MSC_VER)
    extern uint8_t _seraph_inb(uint16_t port);
    return _seraph_inb(port);
#else
    return 0;
#endif
}

/**
 * @brief Short I/O delay for PIC synchronization
 *
 * Some PICs need time between I/O operations. Writing to port 0x80
 * (POST diagnostic port) provides about 1us of delay.
 */
static inline void io_wait(void) {
#if defined(__GNUC__) || defined(__clang__)
    __asm__ volatile("outb %%al, $0x80" : : "a"(0));
#elif defined(_MSC_VER)
    extern void _seraph_io_wait(void);
    _seraph_io_wait();
#endif
}

/*============================================================================
 * PIC State
 *============================================================================*/

/** Current interrupt mask (cached to avoid port reads) */
static uint16_t g_pic_mask = 0xFFFF;  /* All masked by default */

/*============================================================================
 * PIC API Implementation
 *============================================================================*/

/**
 * @brief Initialize and remap the 8259 PICs
 */
void seraph_pic_init(void) {
    /* Save current masks (BIOS may have set them) */
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    /*
     * ICW1: Start initialization in cascade mode, ICW4 needed
     */
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();

    /*
     * ICW2: Set vector offsets
     * Master: IRQ 0-7 -> Vectors 32-39
     * Slave:  IRQ 8-15 -> Vectors 40-47
     */
    outb(PIC1_DATA, SERAPH_IRQ_BASE);       /* Master offset */
    io_wait();
    outb(PIC2_DATA, SERAPH_IRQ_BASE + 8);   /* Slave offset */
    io_wait();

    /*
     * ICW3: Cascade configuration
     * Master: Slave attached to IRQ2 (bit 2 = 1)
     * Slave:  Cascade identity is 2 (connected to master's IRQ2)
     */
    outb(PIC1_DATA, 0x04);  /* 0000 0100: slave on IRQ2 */
    io_wait();
    outb(PIC2_DATA, 0x02);  /* Slave identity 2 */
    io_wait();

    /*
     * ICW4: Set 8086 mode
     */
    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    /*
     * Restore saved masks (or mask everything for safety)
     * Initially mask everything except cascade (IRQ2)
     */
    g_pic_mask = 0xFFFB;  /* All masked except IRQ2 for cascade */
    outb(PIC1_DATA, (uint8_t)(g_pic_mask & 0xFF));
    io_wait();
    outb(PIC2_DATA, (uint8_t)(g_pic_mask >> 8));
    io_wait();

    (void)mask1;
    (void)mask2;
}

/**
 * @brief Send End-of-Interrupt signal
 */
void seraph_pic_eoi(uint8_t irq) {
    /* If the IRQ came from the slave PIC (IRQ 8-15),
     * we must send EOI to both PICs */
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    /* Always send EOI to master */
    outb(PIC1_COMMAND, PIC_EOI);
}

/**
 * @brief Mask (disable) a specific IRQ
 */
void seraph_pic_mask(uint8_t irq) {
    if (irq >= 16) return;

    g_pic_mask |= (1u << irq);

    if (irq < 8) {
        outb(PIC1_DATA, (uint8_t)(g_pic_mask & 0xFF));
    } else {
        outb(PIC2_DATA, (uint8_t)(g_pic_mask >> 8));
    }
}

/**
 * @brief Unmask (enable) a specific IRQ
 */
void seraph_pic_unmask(uint8_t irq) {
    if (irq >= 16) return;

    g_pic_mask &= ~(1u << irq);

    if (irq < 8) {
        outb(PIC1_DATA, (uint8_t)(g_pic_mask & 0xFF));
    } else {
        outb(PIC2_DATA, (uint8_t)(g_pic_mask >> 8));
    }
}

/**
 * @brief Disable all IRQs
 */
void seraph_pic_disable_all(void) {
    g_pic_mask = 0xFFFF;
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

/**
 * @brief Get current mask
 */
uint16_t seraph_pic_get_mask(void) {
    return g_pic_mask;
}

/**
 * @brief Set IRQ mask
 */
void seraph_pic_set_mask(uint16_t mask) {
    g_pic_mask = mask;
    outb(PIC1_DATA, (uint8_t)(mask & 0xFF));
    outb(PIC2_DATA, (uint8_t)(mask >> 8));
}

/*============================================================================
 * Helper Functions
 *============================================================================*/

/**
 * @brief Read the Interrupt Request Register
 *
 * Shows which interrupts are pending (requested but not yet being serviced).
 */
uint16_t seraph_pic_get_irr(void) {
    outb(PIC1_COMMAND, PIC_READ_IRR);
    outb(PIC2_COMMAND, PIC_READ_IRR);
    return (uint16_t)(inb(PIC1_COMMAND) | (inb(PIC2_COMMAND) << 8));
}

/**
 * @brief Read the In-Service Register
 *
 * Shows which interrupts are currently being serviced.
 */
uint16_t seraph_pic_get_isr(void) {
    outb(PIC1_COMMAND, PIC_READ_ISR);
    outb(PIC2_COMMAND, PIC_READ_ISR);
    return (uint16_t)(inb(PIC1_COMMAND) | (inb(PIC2_COMMAND) << 8));
}

/**
 * @brief Check if an IRQ is a spurious interrupt
 *
 * A spurious interrupt occurs when the IRQ line is deasserted
 * before the CPU acknowledges it. We should NOT send EOI for
 * spurious interrupts.
 *
 * @param irq The IRQ to check (7 or 15 typically)
 * @return true if the interrupt was spurious
 */
bool seraph_pic_is_spurious(uint8_t irq) {
    /* Read the ISR to see if the IRQ is really being serviced */
    uint16_t isr = seraph_pic_get_isr();

    /* If the IRQ bit is not set in ISR, it's spurious */
    return (isr & (1u << irq)) == 0;
}
