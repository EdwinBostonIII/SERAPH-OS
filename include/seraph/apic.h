/**
 * @file apic.h
 * @brief Local APIC Interface for Preemptive Scheduling
 *
 * MC13/27: The Pulse - Preemptive Scheduler
 *
 * The Local APIC (Advanced Programmable Interrupt Controller) provides
 * per-CPU interrupt handling and a timer for preemptive multitasking.
 *
 * Key Features:
 * - Periodic timer for preemption
 * - IPI (Inter-Processor Interrupt) for SMP
 * - Interrupt prioritization
 *
 * The APIC timer is calibrated at boot to determine its frequency,
 * then configured for periodic interrupts at the desired preemption rate.
 */

#ifndef SERAPH_APIC_H
#define SERAPH_APIC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * APIC Register Offsets
 *============================================================================*/

/* Base address of Local APIC (mapped to physical 0xFEE00000) */
#define SERAPH_APIC_BASE            0xFEE00000ULL

/* APIC register offsets from base */
#define SERAPH_APIC_ID              0x020   /* Local APIC ID */
#define SERAPH_APIC_VERSION         0x030   /* Version */
#define SERAPH_APIC_TPR             0x080   /* Task Priority Register */
#define SERAPH_APIC_APR             0x090   /* Arbitration Priority */
#define SERAPH_APIC_PPR             0x0A0   /* Processor Priority */
#define SERAPH_APIC_EOI             0x0B0   /* End of Interrupt */
#define SERAPH_APIC_RRD             0x0C0   /* Remote Read */
#define SERAPH_APIC_LDR             0x0D0   /* Logical Destination */
#define SERAPH_APIC_DFR             0x0E0   /* Destination Format */
#define SERAPH_APIC_SPURIOUS        0x0F0   /* Spurious Interrupt Vector */
#define SERAPH_APIC_ISR             0x100   /* In-Service Register (8 x 32-bit) */
#define SERAPH_APIC_TMR             0x180   /* Trigger Mode Register */
#define SERAPH_APIC_IRR             0x200   /* Interrupt Request Register */
#define SERAPH_APIC_ESR             0x280   /* Error Status Register */
#define SERAPH_APIC_ICR_LOW         0x300   /* Interrupt Command (bits 0-31) */
#define SERAPH_APIC_ICR_HIGH        0x310   /* Interrupt Command (bits 32-63) */
#define SERAPH_APIC_LVT_TIMER       0x320   /* LVT Timer */
#define SERAPH_APIC_LVT_THERMAL     0x330   /* LVT Thermal Sensor */
#define SERAPH_APIC_LVT_PERF        0x340   /* LVT Performance Monitoring */
#define SERAPH_APIC_LVT_LINT0       0x350   /* LVT LINT0 */
#define SERAPH_APIC_LVT_LINT1       0x360   /* LVT LINT1 */
#define SERAPH_APIC_LVT_ERROR       0x370   /* LVT Error */
#define SERAPH_APIC_TIMER_INIT      0x380   /* Timer Initial Count */
#define SERAPH_APIC_TIMER_CURRENT   0x390   /* Timer Current Count */
#define SERAPH_APIC_TIMER_DIVIDE    0x3E0   /* Timer Divide Configuration */

/*============================================================================
 * APIC Timer Modes
 *============================================================================*/

/* Timer mode flags for LVT Timer register */
#define SERAPH_APIC_TIMER_ONESHOT   0x00000     /* One-shot mode */
#define SERAPH_APIC_TIMER_PERIODIC  0x20000     /* Periodic mode */
#define SERAPH_APIC_TIMER_TSC_DL    0x40000     /* TSC-Deadline mode */

/* Timer mask (disable) flag */
#define SERAPH_APIC_TIMER_MASKED    0x10000

/* Timer divide values (APIC_TIMER_DIVIDE register) */
#define SERAPH_APIC_DIVIDE_1        0x0B        /* Divide by 1 */
#define SERAPH_APIC_DIVIDE_2        0x00        /* Divide by 2 */
#define SERAPH_APIC_DIVIDE_4        0x01        /* Divide by 4 */
#define SERAPH_APIC_DIVIDE_8        0x02        /* Divide by 8 */
#define SERAPH_APIC_DIVIDE_16       0x03        /* Divide by 16 */
#define SERAPH_APIC_DIVIDE_32       0x08        /* Divide by 32 */
#define SERAPH_APIC_DIVIDE_64       0x09        /* Divide by 64 */
#define SERAPH_APIC_DIVIDE_128      0x0A        /* Divide by 128 */

/*============================================================================
 * Interrupt Vectors
 *============================================================================*/

/* APIC interrupt vector assignments */
#define SERAPH_INT_TIMER            0x20        /* Timer interrupt */
#define SERAPH_INT_SPURIOUS         0xFF        /* Spurious interrupt */
#define SERAPH_INT_IPI_RESCHEDULE   0x21        /* IPI for rescheduling */
#define SERAPH_INT_IPI_TLB_FLUSH    0x22        /* IPI for TLB flush */
#define SERAPH_INT_IPI_PANIC        0x23        /* IPI for system panic */

/*============================================================================
 * APIC Configuration
 *============================================================================*/

/**
 * @brief APIC configuration structure
 */
typedef struct {
    uint64_t base_address;          /**< APIC base address */
    uint32_t timer_frequency_hz;    /**< Timer frequency in Hz */
    uint32_t timer_initial_count;   /**< Initial count for timer */
    uint32_t preemption_hz;         /**< Desired preemption rate */
    uint8_t  timer_vector;          /**< Timer interrupt vector */
    bool     enabled;               /**< APIC enabled? */
    bool     timer_running;         /**< Timer currently running? */
} Seraph_APIC_Config;

/*============================================================================
 * APIC Initialization
 *============================================================================*/

/**
 * @brief Initialize the Local APIC
 *
 * Enables the APIC, calibrates the timer, and sets up
 * interrupt vectors.
 *
 * @return true if initialization succeeded
 */
bool seraph_apic_init(void);

/**
 * @brief Check if APIC is available
 *
 * @return true if CPU has a Local APIC
 */
bool seraph_apic_available(void);

/**
 * @brief Enable the Local APIC
 *
 * Sets the APIC enable bit in the spurious interrupt vector register.
 */
void seraph_apic_enable(void);

/**
 * @brief Disable the Local APIC
 */
void seraph_apic_disable(void);

/**
 * @brief Get APIC configuration
 *
 * @return Pointer to current APIC configuration
 */
const Seraph_APIC_Config* seraph_apic_get_config(void);

/*============================================================================
 * APIC Register Access
 *============================================================================*/

/**
 * @brief Read APIC register
 *
 * @param offset Register offset from APIC base
 * @return Register value
 */
uint32_t seraph_apic_read(uint32_t offset);

/**
 * @brief Write APIC register
 *
 * @param offset Register offset from APIC base
 * @param value Value to write
 */
void seraph_apic_write(uint32_t offset, uint32_t value);

/**
 * @brief Get Local APIC ID
 *
 * @return This CPU's APIC ID
 */
uint32_t seraph_apic_id(void);

/**
 * @brief Get APIC version
 *
 * @return APIC version number
 */
uint32_t seraph_apic_version(void);

/*============================================================================
 * APIC Timer
 *============================================================================*/

/**
 * @brief Calibrate the APIC timer
 *
 * Determines the APIC timer frequency by timing against
 * a known reference (PIT or TSC).
 *
 * @return Timer frequency in Hz, or 0 on failure
 */
uint32_t seraph_apic_timer_calibrate(void);

/**
 * @brief Start the APIC timer
 *
 * Configures the timer for periodic interrupts at the
 * configured preemption rate.
 *
 * @param ticks Number of ticks between interrupts
 */
void seraph_apic_timer_start(uint32_t ticks);

/**
 * @brief Start timer for specific frequency
 *
 * @param hz Desired interrupt frequency in Hz
 */
void seraph_apic_timer_start_hz(uint32_t hz);

/**
 * @brief Stop the APIC timer
 */
void seraph_apic_timer_stop(void);

/**
 * @brief Set timer to one-shot mode
 *
 * @param ticks Ticks until interrupt
 */
void seraph_apic_timer_oneshot(uint32_t ticks);

/**
 * @brief Get current timer count
 *
 * @return Current countdown value
 */
uint32_t seraph_apic_timer_current(void);

/**
 * @brief Check if timer is running
 *
 * @return true if timer is active
 */
bool seraph_apic_timer_running(void);

/*============================================================================
 * End of Interrupt
 *============================================================================*/

/**
 * @brief Send End of Interrupt signal
 *
 * Must be called at the end of every APIC interrupt handler.
 */
void seraph_apic_eoi(void);

/*============================================================================
 * Inter-Processor Interrupts (IPI)
 *============================================================================*/

/**
 * @brief IPI destination modes
 */
typedef enum {
    SERAPH_IPI_DEST_SINGLE      = 0,    /**< Send to specific APIC ID */
    SERAPH_IPI_DEST_SELF        = 1,    /**< Send to self */
    SERAPH_IPI_DEST_ALL         = 2,    /**< Send to all (including self) */
    SERAPH_IPI_DEST_ALL_BUT_SELF = 3,   /**< Send to all except self */
} Seraph_IPI_Dest;

/**
 * @brief Send an IPI
 *
 * @param dest_apic_id Destination APIC ID (for DEST_SINGLE)
 * @param vector Interrupt vector to send
 * @param dest Destination mode
 */
void seraph_apic_send_ipi(uint32_t dest_apic_id,
                           uint8_t vector,
                           Seraph_IPI_Dest dest);

/**
 * @brief Send reschedule IPI
 *
 * @param dest_apic_id Destination CPU
 */
void seraph_apic_send_reschedule(uint32_t dest_apic_id);

/**
 * @brief Send reschedule to all CPUs
 */
void seraph_apic_broadcast_reschedule(void);

/**
 * @brief Wait for IPI delivery
 *
 * Waits until the last IPI has been delivered.
 */
void seraph_apic_ipi_wait(void);

/*============================================================================
 * Error Handling
 *============================================================================*/

/**
 * @brief Get APIC error status
 *
 * @return Error status register value
 */
uint32_t seraph_apic_error_status(void);

/**
 * @brief Clear APIC errors
 */
void seraph_apic_clear_errors(void);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_APIC_H */
