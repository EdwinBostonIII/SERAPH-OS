/**
 * @file context.h
 * @brief CPU Context Structure for Context Switching
 *
 * MC13/27: The Pulse - Preemptive Scheduler
 *
 * Defines the CPU context structure used for saving and restoring
 * execution state during context switches. This structure captures
 * all relevant CPU registers including:
 * - General purpose registers (callee-saved and caller-saved)
 * - Instruction pointer and flags
 * - Page table pointer (CR3)
 * - FPU/SSE state
 * - Generation counter for temporal safety
 *
 * The context switch routines are implemented in assembly for
 * optimal performance and correctness.
 */

#ifndef SERAPH_CONTEXT_H
#define SERAPH_CONTEXT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * CPU Context Structure
 *============================================================================*/

/**
 * @brief Complete CPU context for context switching
 *
 * This structure must be kept in sync with the assembly routines.
 * Layout is optimized for efficient save/restore operations.
 *
 * The structure is 16-byte aligned for SSE state compatibility.
 */
typedef struct __attribute__((packed, aligned(16))) {
    /*
     * Callee-saved registers (System V AMD64 ABI)
     * These must be preserved across function calls
     */
    uint64_t r15;           /* Offset 0x00 */
    uint64_t r14;           /* Offset 0x08 */
    uint64_t r13;           /* Offset 0x10 */
    uint64_t r12;           /* Offset 0x18 */
    uint64_t rbx;           /* Offset 0x20 */
    uint64_t rbp;           /* Offset 0x28 - Frame pointer */

    /*
     * Caller-saved registers (used for interrupt frames)
     * These need only be saved on interrupt/exception
     */
    uint64_t r11;           /* Offset 0x30 */
    uint64_t r10;           /* Offset 0x38 */
    uint64_t r9;            /* Offset 0x40 */
    uint64_t r8;            /* Offset 0x48 */
    uint64_t rax;           /* Offset 0x50 - Return value */
    uint64_t rcx;           /* Offset 0x58 - 4th argument */
    uint64_t rdx;           /* Offset 0x60 - 3rd argument */
    uint64_t rsi;           /* Offset 0x68 - 2nd argument */
    uint64_t rdi;           /* Offset 0x70 - 1st argument */

    /*
     * Instruction pointer and execution state
     */
    uint64_t rip;           /* Offset 0x78 - Instruction pointer */
    uint64_t cs;            /* Offset 0x80 - Code segment */
    uint64_t rflags;        /* Offset 0x88 - CPU flags */
    uint64_t rsp;           /* Offset 0x90 - Stack pointer */
    uint64_t ss;            /* Offset 0x98 - Stack segment */

    /*
     * Page table pointer for address space switching
     */
    uint64_t cr3;           /* Offset 0xA0 - Page table base */

    /*
     * FPU/SSE state (512 bytes for FXSAVE area)
     * Must be 16-byte aligned
     */
    uint8_t fpu_state[512] __attribute__((aligned(16)));

    /*
     * FPU state validity flag
     * Set to 1 if fpu_state contains valid data
     */
    uint8_t fpu_valid;

    /*
     * Generation counter for temporal safety
     * Incremented on each context reuse to detect stale references
     */
    uint64_t context_gen;

    /*
     * Reserved for future use / alignment padding
     */
    uint8_t pad[7];

} Seraph_CPU_Context;

/*============================================================================
 * Context Structure Offsets (for assembly)
 *============================================================================*/

/* Offset definitions for use in assembly routines */
#define SERAPH_CTX_OFF_R15      0x00
#define SERAPH_CTX_OFF_R14      0x08
#define SERAPH_CTX_OFF_R13      0x10
#define SERAPH_CTX_OFF_R12      0x18
#define SERAPH_CTX_OFF_RBX      0x20
#define SERAPH_CTX_OFF_RBP      0x28
#define SERAPH_CTX_OFF_R11      0x30
#define SERAPH_CTX_OFF_R10      0x38
#define SERAPH_CTX_OFF_R9       0x40
#define SERAPH_CTX_OFF_R8       0x48
#define SERAPH_CTX_OFF_RAX      0x50
#define SERAPH_CTX_OFF_RCX      0x58
#define SERAPH_CTX_OFF_RDX      0x60
#define SERAPH_CTX_OFF_RSI      0x68
#define SERAPH_CTX_OFF_RDI      0x70
#define SERAPH_CTX_OFF_RIP      0x78
#define SERAPH_CTX_OFF_CS       0x80
#define SERAPH_CTX_OFF_RFLAGS   0x88
#define SERAPH_CTX_OFF_RSP      0x90
#define SERAPH_CTX_OFF_SS       0x98
#define SERAPH_CTX_OFF_CR3      0xA0
#define SERAPH_CTX_OFF_FPU      0xA8

/*============================================================================
 * Interrupt Frame Structure
 *============================================================================*/

/**
 * @brief Minimal CPU state pushed by interrupt/exception
 *
 * When an interrupt or exception occurs, the CPU pushes this
 * information onto the stack before calling the handler.
 * This is the minimal frame - see interrupts.h for full frame
 * with saved registers.
 */
typedef struct __attribute__((packed)) {
    uint64_t rip;           /**< Instruction pointer */
    uint64_t cs;            /**< Code segment */
    uint64_t rflags;        /**< CPU flags */
    uint64_t rsp;           /**< Stack pointer */
    uint64_t ss;            /**< Stack segment */
} Seraph_MinimalInterruptFrame;

/**
 * @brief Extended interrupt frame with error code
 *
 * Some exceptions (page fault, GPF, etc.) push an error code.
 */
typedef struct __attribute__((packed)) {
    uint64_t error_code;    /**< Error code pushed by CPU */
    uint64_t rip;           /**< Instruction pointer */
    uint64_t cs;            /**< Code segment */
    uint64_t rflags;        /**< CPU flags */
    uint64_t rsp;           /**< Stack pointer */
    uint64_t ss;            /**< Stack segment */
} Seraph_InterruptFrameError;

/*============================================================================
 * Context Operations (Assembly Implementations)
 *============================================================================*/

/**
 * @brief Save current CPU context
 *
 * Saves all general-purpose registers, flags, and optionally FPU state
 * into the provided context structure.
 *
 * @param ctx Context structure to save into
 *
 * Note: This function uses special calling conventions. After returning,
 * execution continues normally. When the context is later restored,
 * execution resumes at the point after this call.
 */
void seraph_context_save(Seraph_CPU_Context* ctx);

/**
 * @brief Restore CPU context
 *
 * Restores all CPU state from the provided context structure.
 * This function does NOT return - execution continues at the
 * saved instruction pointer.
 *
 * @param ctx Context structure to restore from
 */
void seraph_context_restore(const Seraph_CPU_Context* ctx)
    __attribute__((noreturn));

/**
 * @brief Switch between two contexts
 *
 * Atomically saves the current context and restores a new one.
 * This is the core context switch operation.
 *
 * @param old_ctx Where to save the current context
 * @param new_ctx Context to switch to
 */
void seraph_context_switch(Seraph_CPU_Context* old_ctx,
                            const Seraph_CPU_Context* new_ctx);

/**
 * @brief Save FPU/SSE state
 *
 * Saves the FPU/SSE state to the context's fpu_state buffer.
 *
 * @param ctx Context to save FPU state into
 */
void seraph_context_save_fpu(Seraph_CPU_Context* ctx);

/**
 * @brief Restore FPU/SSE state
 *
 * Restores FPU/SSE state from the context's fpu_state buffer.
 *
 * @param ctx Context to restore FPU state from
 */
void seraph_context_restore_fpu(const Seraph_CPU_Context* ctx);

/*============================================================================
 * Context Initialization
 *============================================================================*/

/**
 * @brief Initialize a context for a new thread
 *
 * Sets up a context structure so that when restored, execution
 * begins at the specified entry point with the given stack.
 *
 * @param ctx Context to initialize
 * @param entry_point Function to start executing
 * @param stack_top Top of the stack to use
 * @param arg Argument to pass to entry function
 * @param cr3 Page table base (0 to use current)
 */
void seraph_context_init(Seraph_CPU_Context* ctx,
                          void (*entry_point)(void*),
                          void* stack_top,
                          void* arg,
                          uint64_t cr3);

/**
 * @brief Initialize a context for kernel thread
 *
 * Like seraph_context_init but sets up kernel-mode selectors.
 *
 * @param ctx Context to initialize
 * @param entry_point Function to start executing
 * @param stack_top Top of the stack
 * @param arg Argument to pass to entry function
 */
void seraph_context_init_kernel(Seraph_CPU_Context* ctx,
                                 void (*entry_point)(void*),
                                 void* stack_top,
                                 void* arg);

/**
 * @brief Clone a context
 *
 * Creates a copy of a context with a new stack.
 *
 * @param dst Destination context
 * @param src Source context
 * @param new_stack_top New stack top for the clone
 */
void seraph_context_clone(Seraph_CPU_Context* dst,
                           const Seraph_CPU_Context* src,
                           void* new_stack_top);

/*============================================================================
 * Context Validation
 *============================================================================*/

/**
 * @brief Validate a context structure
 *
 * Checks that the context contains valid values.
 *
 * @param ctx Context to validate
 * @return true if valid, false otherwise
 */
bool seraph_context_valid(const Seraph_CPU_Context* ctx);

/**
 * @brief Check if context has valid FPU state
 *
 * @param ctx Context to check
 * @return true if FPU state is valid
 */
static inline bool seraph_context_has_fpu(const Seraph_CPU_Context* ctx) {
    return ctx != NULL && ctx->fpu_valid;
}

/**
 * @brief Get context generation
 *
 * @param ctx Context to query
 * @return Generation counter value
 */
static inline uint64_t seraph_context_generation(const Seraph_CPU_Context* ctx) {
    return ctx != NULL ? ctx->context_gen : 0;
}

/*============================================================================
 * Kernel/User Mode Segment Selectors
 *============================================================================*/

/* GDT segment selectors */
#define SERAPH_KERNEL_CS    0x08    /* Kernel code segment */
#define SERAPH_KERNEL_DS    0x10    /* Kernel data segment */
#define SERAPH_USER_CS      0x1B    /* User code segment (RPL=3) */
#define SERAPH_USER_DS      0x23    /* User data segment (RPL=3) */

/* RFLAGS bits */
#define SERAPH_RFLAGS_IF    (1ULL << 9)     /* Interrupt enable */
#define SERAPH_RFLAGS_IOPL  (3ULL << 12)    /* I/O privilege level */
#define SERAPH_RFLAGS_RESERVED (1ULL << 1)  /* Always set */

/* Default RFLAGS for new threads */
#define SERAPH_RFLAGS_DEFAULT   (SERAPH_RFLAGS_IF | SERAPH_RFLAGS_RESERVED)
#define SERAPH_RFLAGS_KERNEL    (SERAPH_RFLAGS_DEFAULT)
#define SERAPH_RFLAGS_USER      (SERAPH_RFLAGS_DEFAULT)

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_CONTEXT_H */
