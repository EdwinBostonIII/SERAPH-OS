/**
 * @file apic.c
 * @brief Local APIC Implementation
 *
 * MC13/27: The Pulse - Preemptive Scheduler
 *
 * Implements Local APIC initialization, timer control, and IPI support.
 */

#include "seraph/apic.h"
#include "seraph/vmm.h"
#include <string.h>

/*============================================================================
 * Static Configuration
 *============================================================================*/

static Seraph_APIC_Config apic_config = {
    .base_address = SERAPH_APIC_BASE,
    .timer_frequency_hz = 0,
    .timer_initial_count = 0,
    .preemption_hz = 1000,          /* Default: 1000 Hz (1ms preemption) */
    .timer_vector = SERAPH_INT_TIMER,
    .enabled = false,
    .timer_running = false
};

/* Virtual address of APIC (set during initialization) */
static volatile uint32_t* apic_base = NULL;

/*============================================================================
 * CPU Intrinsics
 *============================================================================*/

/* Read MSR */
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

/* Write MSR */
static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = (uint32_t)value;
    uint32_t high = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr" : : "a"(low), "d"(high), "c"(msr));
}

/* Read CPUID */
static inline void cpuid(uint32_t leaf, uint32_t* eax, uint32_t* ebx,
                         uint32_t* ecx, uint32_t* edx) {
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(0));
}

/* Read TSC */
static inline uint64_t rdtsc(void) {
    uint32_t low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

/* Memory fence */
static inline void mfence(void) {
    __asm__ volatile("mfence" ::: "memory");
}

/* I/O port read */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* I/O port write */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

/*============================================================================
 * APIC MSRs
 *============================================================================*/

#define MSR_APIC_BASE   0x1B

/* APIC Base MSR bits */
#define APIC_BASE_BSP           (1ULL << 8)     /* Bootstrap Processor */
#define APIC_BASE_X2APIC_ENABLE (1ULL << 10)    /* x2APIC mode */
#define APIC_BASE_ENABLE        (1ULL << 11)    /* Global APIC enable */
#define APIC_BASE_ADDRESS_MASK  0xFFFFF000ULL   /* Base address mask */

/*============================================================================
 * APIC Register Access
 *============================================================================*/

uint32_t seraph_apic_read(uint32_t offset) {
    if (apic_base == NULL) return 0;
    return apic_base[offset / 4];
}

void seraph_apic_write(uint32_t offset, uint32_t value) {
    if (apic_base == NULL) return;
    apic_base[offset / 4] = value;
    mfence();
}

/*============================================================================
 * APIC Detection
 *============================================================================*/

bool seraph_apic_available(void) {
    uint32_t eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);

    /* Check APIC bit in CPUID.01H:EDX[9] */
    return (edx & (1 << 9)) != 0;
}

/*============================================================================
 * APIC Initialization
 *============================================================================*/

bool seraph_apic_init(void) {
    /* Check if APIC is available */
    if (!seraph_apic_available()) {
        return false;
    }

    /* Get APIC base address from MSR */
    uint64_t apic_msr = rdmsr(MSR_APIC_BASE);

    /* Check if APIC is already enabled */
    if (!(apic_msr & APIC_BASE_ENABLE)) {
        /* Enable APIC */
        apic_msr |= APIC_BASE_ENABLE;
        wrmsr(MSR_APIC_BASE, apic_msr);
    }

    /* Get physical base address */
    uint64_t phys_base = apic_msr & APIC_BASE_ADDRESS_MASK;
    apic_config.base_address = phys_base;

    /* Map APIC registers through VMM
     * The APIC is memory-mapped I/O that must be mapped with
     * uncached attributes for correct behavior.
     */
    apic_base = (volatile uint32_t*)seraph_phys_to_virt(phys_base);

    /* If we're in early boot without the physical map set up,
     * fall back to identity mapping assumption */
    if (apic_base == NULL) {
        apic_base = (volatile uint32_t*)phys_base;
    }

    /* Enable APIC via spurious interrupt vector register */
    uint32_t spurious = seraph_apic_read(SERAPH_APIC_SPURIOUS);
    spurious |= 0x100;                      /* APIC Software Enable */
    spurious = (spurious & 0xFFFFFF00) | SERAPH_INT_SPURIOUS;
    seraph_apic_write(SERAPH_APIC_SPURIOUS, spurious);

    /* Set Task Priority to 0 (accept all interrupts) */
    seraph_apic_write(SERAPH_APIC_TPR, 0);

    /* Clear any pending errors */
    seraph_apic_write(SERAPH_APIC_ESR, 0);
    seraph_apic_write(SERAPH_APIC_ESR, 0);

    /* Mask all LVT entries initially */
    seraph_apic_write(SERAPH_APIC_LVT_TIMER, SERAPH_APIC_TIMER_MASKED);
    seraph_apic_write(SERAPH_APIC_LVT_LINT0, 0x10000);
    seraph_apic_write(SERAPH_APIC_LVT_LINT1, 0x10000);
    seraph_apic_write(SERAPH_APIC_LVT_ERROR, 0x10000);

    /* Calibrate timer */
    apic_config.timer_frequency_hz = seraph_apic_timer_calibrate();
    if (apic_config.timer_frequency_hz == 0) {
        /* Use default estimate if calibration fails */
        apic_config.timer_frequency_hz = 1000000000;  /* 1 GHz estimate */
    }

    apic_config.enabled = true;

    return true;
}

void seraph_apic_enable(void) {
    if (apic_base == NULL) return;

    uint32_t spurious = seraph_apic_read(SERAPH_APIC_SPURIOUS);
    spurious |= 0x100;  /* Set software enable bit */
    seraph_apic_write(SERAPH_APIC_SPURIOUS, spurious);

    apic_config.enabled = true;
}

void seraph_apic_disable(void) {
    if (apic_base == NULL) return;

    uint32_t spurious = seraph_apic_read(SERAPH_APIC_SPURIOUS);
    spurious &= ~0x100;  /* Clear software enable bit */
    seraph_apic_write(SERAPH_APIC_SPURIOUS, spurious);

    apic_config.enabled = false;
}

const Seraph_APIC_Config* seraph_apic_get_config(void) {
    return &apic_config;
}

uint32_t seraph_apic_id(void) {
    if (apic_base == NULL) return 0;
    return seraph_apic_read(SERAPH_APIC_ID) >> 24;
}

uint32_t seraph_apic_version(void) {
    if (apic_base == NULL) return 0;
    return seraph_apic_read(SERAPH_APIC_VERSION) & 0xFF;
}

/*============================================================================
 * Timer Calibration
 *============================================================================*/

/* PIT (Programmable Interval Timer) for calibration */
#define PIT_CHANNEL_0   0x40
#define PIT_COMMAND     0x43
#define PIT_FREQUENCY   1193182     /* PIT oscillator frequency */

uint32_t seraph_apic_timer_calibrate(void) {
    if (apic_base == NULL) return 0;

    /* Use PIT channel 0 for timing reference */
    const uint32_t pit_ticks = 11932;   /* ~10ms at 1193182 Hz */

    /* Configure timer for maximum count, divide by 16 */
    seraph_apic_write(SERAPH_APIC_TIMER_DIVIDE, SERAPH_APIC_DIVIDE_16);
    seraph_apic_write(SERAPH_APIC_TIMER_INIT, 0xFFFFFFFF);

    /* Set up LVT Timer (masked, one-shot) */
    seraph_apic_write(SERAPH_APIC_LVT_TIMER,
                      SERAPH_INT_TIMER | SERAPH_APIC_TIMER_MASKED);

    /* Configure PIT channel 0 for one-shot mode */
    outb(PIT_COMMAND, 0x30);    /* Channel 0, lobyte/hibyte, mode 0 */
    outb(PIT_CHANNEL_0, pit_ticks & 0xFF);
    outb(PIT_CHANNEL_0, (pit_ticks >> 8) & 0xFF);

    /* Start APIC timer */
    seraph_apic_write(SERAPH_APIC_TIMER_INIT, 0xFFFFFFFF);

    /* Wait for PIT to count down */
    uint8_t status;
    do {
        outb(PIT_COMMAND, 0xE2);    /* Read-back command, channel 0 */
        status = inb(PIT_CHANNEL_0);
    } while (!(status & 0x80));     /* Wait for output to go high */

    /* Stop APIC timer */
    seraph_apic_write(SERAPH_APIC_LVT_TIMER, SERAPH_APIC_TIMER_MASKED);

    /* Calculate elapsed APIC ticks */
    uint32_t current = seraph_apic_read(SERAPH_APIC_TIMER_CURRENT);
    uint32_t elapsed = 0xFFFFFFFF - current;

    /* Calculate frequency
     * elapsed ticks in (pit_ticks / PIT_FREQUENCY) seconds
     * frequency = elapsed / (pit_ticks / PIT_FREQUENCY)
     *           = elapsed * PIT_FREQUENCY / pit_ticks
     *
     * Account for divide by 16
     */
    uint64_t freq = ((uint64_t)elapsed * PIT_FREQUENCY * 16) / pit_ticks;

    return (uint32_t)freq;
}

/*============================================================================
 * Timer Control
 *============================================================================*/

void seraph_apic_timer_start(uint32_t ticks) {
    if (apic_base == NULL) return;

    /* Configure divide by 16 */
    seraph_apic_write(SERAPH_APIC_TIMER_DIVIDE, SERAPH_APIC_DIVIDE_16);

    /* Set initial count */
    apic_config.timer_initial_count = ticks;

    /* Configure LVT Timer for periodic mode */
    seraph_apic_write(SERAPH_APIC_LVT_TIMER,
                      apic_config.timer_vector | SERAPH_APIC_TIMER_PERIODIC);

    /* Start the timer */
    seraph_apic_write(SERAPH_APIC_TIMER_INIT, ticks);

    apic_config.timer_running = true;
}

void seraph_apic_timer_start_hz(uint32_t hz) {
    if (hz == 0 || apic_config.timer_frequency_hz == 0) return;

    /* Calculate ticks for desired frequency
     * ticks = timer_frequency / hz / divide_value
     *       = timer_frequency / hz / 16
     */
    uint32_t ticks = apic_config.timer_frequency_hz / hz / 16;
    if (ticks == 0) ticks = 1;

    apic_config.preemption_hz = hz;
    seraph_apic_timer_start(ticks);
}

void seraph_apic_timer_stop(void) {
    if (apic_base == NULL) return;

    /* Mask the timer */
    seraph_apic_write(SERAPH_APIC_LVT_TIMER, SERAPH_APIC_TIMER_MASKED);

    /* Set initial count to 0 */
    seraph_apic_write(SERAPH_APIC_TIMER_INIT, 0);

    apic_config.timer_running = false;
}

void seraph_apic_timer_oneshot(uint32_t ticks) {
    if (apic_base == NULL) return;

    /* Configure divide by 16 */
    seraph_apic_write(SERAPH_APIC_TIMER_DIVIDE, SERAPH_APIC_DIVIDE_16);

    /* Configure LVT Timer for one-shot mode */
    seraph_apic_write(SERAPH_APIC_LVT_TIMER,
                      apic_config.timer_vector | SERAPH_APIC_TIMER_ONESHOT);

    /* Start the timer */
    seraph_apic_write(SERAPH_APIC_TIMER_INIT, ticks);
}

uint32_t seraph_apic_timer_current(void) {
    if (apic_base == NULL) return 0;
    return seraph_apic_read(SERAPH_APIC_TIMER_CURRENT);
}

bool seraph_apic_timer_running(void) {
    return apic_config.timer_running;
}

/*============================================================================
 * End of Interrupt
 *============================================================================*/

void seraph_apic_eoi(void) {
    if (apic_base == NULL) return;
    seraph_apic_write(SERAPH_APIC_EOI, 0);
}

/*============================================================================
 * Inter-Processor Interrupts
 *============================================================================*/

void seraph_apic_send_ipi(uint32_t dest_apic_id,
                           uint8_t vector,
                           Seraph_IPI_Dest dest) {
    if (apic_base == NULL) return;

    /* Build ICR value */
    uint32_t icr_high = dest_apic_id << 24;
    uint32_t icr_low = vector;

    /* Set destination shorthand */
    switch (dest) {
        case SERAPH_IPI_DEST_SELF:
            icr_low |= (1 << 18);   /* Self shorthand */
            break;
        case SERAPH_IPI_DEST_ALL:
            icr_low |= (2 << 18);   /* All including self */
            break;
        case SERAPH_IPI_DEST_ALL_BUT_SELF:
            icr_low |= (3 << 18);   /* All excluding self */
            break;
        case SERAPH_IPI_DEST_SINGLE:
        default:
            /* No shorthand - use destination field */
            break;
    }

    /* Write high dword first (contains destination) */
    seraph_apic_write(SERAPH_APIC_ICR_HIGH, icr_high);

    /* Write low dword to trigger IPI */
    seraph_apic_write(SERAPH_APIC_ICR_LOW, icr_low);
}

void seraph_apic_send_reschedule(uint32_t dest_apic_id) {
    seraph_apic_send_ipi(dest_apic_id, SERAPH_INT_IPI_RESCHEDULE,
                          SERAPH_IPI_DEST_SINGLE);
}

void seraph_apic_broadcast_reschedule(void) {
    seraph_apic_send_ipi(0, SERAPH_INT_IPI_RESCHEDULE,
                          SERAPH_IPI_DEST_ALL_BUT_SELF);
}

void seraph_apic_ipi_wait(void) {
    if (apic_base == NULL) return;

    /* Wait for delivery status bit to clear */
    while (seraph_apic_read(SERAPH_APIC_ICR_LOW) & (1 << 12)) {
        __asm__ volatile("pause");
    }
}

/*============================================================================
 * Error Handling
 *============================================================================*/

uint32_t seraph_apic_error_status(void) {
    if (apic_base == NULL) return 0;

    /* Must write before reading (documentation requirement) */
    seraph_apic_write(SERAPH_APIC_ESR, 0);
    return seraph_apic_read(SERAPH_APIC_ESR);
}

void seraph_apic_clear_errors(void) {
    if (apic_base == NULL) return;
    seraph_apic_write(SERAPH_APIC_ESR, 0);
    seraph_apic_write(SERAPH_APIC_ESR, 0);
}
