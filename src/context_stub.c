/**
 * @file context_stub.c
 * @brief SERAPH Context - Stub Implementation for Testing
 *
 * This file provides stub implementations of the context switching functions
 * that are normally implemented in assembly. These stubs allow the test suite
 * to link properly without requiring actual hardware context switching.
 *
 * NOTE: These stubs are NOT suitable for production use - they are only
 * for testing purposes.
 */

#include "seraph/context.h"
#include <string.h>

/*============================================================================
 * Stub Implementations
 *============================================================================*/

/**
 * @brief Stub: Save CPU context
 */
void seraph_context_save(Seraph_CPU_Context* ctx) {
    if (ctx == NULL) return;
    /* Stub - no actual context save in test environment */
    ctx->context_gen++;
}

/**
 * @brief Stub: Restore CPU context (noreturn)
 *
 * In the stub implementation, this does NOT actually return.
 * It loops forever to satisfy the noreturn attribute.
 */
void seraph_context_restore(const Seraph_CPU_Context* ctx) {
    (void)ctx;
    /* Stub - in test environment, just abort */
    while (1) { /* noreturn stub */ }
}

/**
 * @brief Stub: Switch between contexts
 */
void seraph_context_switch(Seraph_CPU_Context* old_ctx,
                            const Seraph_CPU_Context* new_ctx) {
    if (old_ctx != NULL) {
        old_ctx->context_gen++;
    }
    (void)new_ctx;
    /* Stub - no actual context switch */
}

/**
 * @brief Stub: Save FPU state
 */
void seraph_context_save_fpu(Seraph_CPU_Context* ctx) {
    if (ctx == NULL) return;
    ctx->fpu_valid = 1;
}

/**
 * @brief Stub: Restore FPU state
 */
void seraph_context_restore_fpu(const Seraph_CPU_Context* ctx) {
    (void)ctx;
    /* Stub - no actual FPU restore */
}

/**
 * @brief Stub: Initialize context for new thread
 */
void seraph_context_init(Seraph_CPU_Context* ctx,
                          void (*entry_point)(void*),
                          void* stack_top,
                          void* arg,
                          uint64_t cr3) {
    if (ctx == NULL) return;

    memset(ctx, 0, sizeof(Seraph_CPU_Context));

    /* Set up minimal context */
    ctx->rip = (uint64_t)entry_point;
    ctx->rsp = (uint64_t)stack_top;
    ctx->rdi = (uint64_t)arg;  /* First argument in x86-64 ABI */
    ctx->cr3 = cr3;
    ctx->context_gen = 1;

    /* Set up default segment selectors */
    ctx->cs = SERAPH_USER_CS;
    ctx->ss = SERAPH_USER_DS;

    /* Enable interrupts by default */
    ctx->rflags = SERAPH_RFLAGS_USER;
}

/**
 * @brief Stub: Initialize context for kernel thread
 */
void seraph_context_init_kernel(Seraph_CPU_Context* ctx,
                                 void (*entry_point)(void*),
                                 void* stack_top,
                                 void* arg) {
    if (ctx == NULL) return;

    memset(ctx, 0, sizeof(Seraph_CPU_Context));

    /* Set up minimal context */
    ctx->rip = (uint64_t)entry_point;
    ctx->rsp = (uint64_t)stack_top;
    ctx->rdi = (uint64_t)arg;
    ctx->cr3 = 0;  /* Use current page tables */
    ctx->context_gen = 1;

    /* Set up kernel segment selectors */
    ctx->cs = SERAPH_KERNEL_CS;
    ctx->ss = SERAPH_KERNEL_DS;

    /* Enable interrupts by default */
    ctx->rflags = SERAPH_RFLAGS_KERNEL;
}

/**
 * @brief Stub: Clone context with new stack
 */
void seraph_context_clone(Seraph_CPU_Context* dst,
                           const Seraph_CPU_Context* src,
                           void* new_stack_top) {
    if (dst == NULL || src == NULL) return;

    /* Copy entire context */
    *dst = *src;

    /* Update stack pointer */
    dst->rsp = (uint64_t)new_stack_top;

    /* Increment generation */
    dst->context_gen = src->context_gen + 1;
}

/**
 * @brief Stub: Validate context
 */
bool seraph_context_valid(const Seraph_CPU_Context* ctx) {
    if (ctx == NULL) return false;

    /* Basic sanity checks */
    if (ctx->rsp == 0) return false;
    if (ctx->rip == 0) return false;

    /* Check segment selectors have valid privilege levels */
    /* (In stub, just accept anything) */

    return true;
}
