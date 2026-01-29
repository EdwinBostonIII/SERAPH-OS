/**
 * @file seraph.h
 * @brief Master include file for the SERAPH Operating System
 *
 * SERAPH: Semantic Extensible Resilient Automatic Persistent Hypervisor
 *
 * Include this single header to access all SERAPH functionality.
 */

#ifndef SERAPH_H
#define SERAPH_H

/*============================================================================
 * Version Information
 *============================================================================*/

#define SERAPH_VERSION_MAJOR 0
#define SERAPH_VERSION_MINOR 1
#define SERAPH_VERSION_PATCH 0
#define SERAPH_VERSION_STRING "0.1.0"

/*============================================================================
 * Foundation Layer (MC0-MC4)
 *============================================================================*/

/* MC0: VOID Semantics - Universal error/nothing representation */
#include "seraph/void.h"

/* MC1: VBIT - Three-valued Kleene logic */
#include "seraph/vbit.h"

/* MC2: Bits - VOID-aware bit operations */
#include "seraph/bits.h"

/* MC3: Semantic Byte - Byte with per-bit validity mask */
#include "seraph/semantic_byte.h"

/* MC4: Integers - Entropic arithmetic (VOID/WRAP/SATURATE) */
#include "seraph/integers.h"

/*============================================================================
 * Numeric Tower (MC5)
 *============================================================================*/

/* MC5: Q128 - Q64.64 fixed-point numbers */
#include "seraph/q128.h"

/* MC5+: Galactic - 256-bit hyper-dual automatic differentiation */
#include "seraph/galactic.h"

/*============================================================================
 * Memory Safety (MC6-MC8)
 *============================================================================*/

/* MC6: Capability - Unforgeable memory access tokens */
#include "seraph/capability.h"

/* MC7: Chronon - Causal/logical time */
#include "seraph/chronon.h"

/* MC8: Arena - Spectral memory allocator */
#include "seraph/arena.h"

/*============================================================================
 * Graphics (MC9)
 *============================================================================*/

/* MC9: Glyph - SDF vector graphics */
#include "seraph/glyph.h"

/*============================================================================
 * Process Model (MC10-MC13)
 *============================================================================*/

/* MC10: Sovereign - Process isolation */
#include "seraph/sovereign.h"

/* MC11: Surface - UI compositor */
#include "seraph/surface.h"

/* MC12: Whisper - Zero-copy IPC */
#include "seraph/whisper.h"

/* MC13: Strand - Green threads */
#include "seraph/strand.h"

/*============================================================================
 * Software-Defined Machine (MC27-MC28)
 *============================================================================*/

/* MC27: Atlas - Single-Level Store (persistent memory) */
#include "seraph/atlas.h"

/* MC28: Aether - Distributed Shared Memory */
#include "seraph/aether.h"

/*============================================================================
 * Seraphim Compiler (MC26)
 *============================================================================*/

/* MC26: Seraphim - Language compiler tokens */
#include "seraph/seraphim/token.h"

/* MC26: Seraphim - Language compiler lexer */
#include "seraph/seraphim/lexer.h"

/* MC26: Seraphim - Abstract Syntax Tree */
#include "seraph/seraphim/ast.h"

/* MC26: Seraphim - Parser */
#include "seraph/seraphim/parser.h"

/*============================================================================
 * Feature Detection Macros
 *============================================================================*/

/* Check if SIMD is available */
#if defined(__AVX2__)
    #define SERAPH_HAS_AVX2 1
#else
    #define SERAPH_HAS_AVX2 0
#endif

#if defined(__SSE2__)
    #define SERAPH_HAS_SSE2 1
#else
    #define SERAPH_HAS_SSE2 0
#endif

/* Check if 128-bit integers are available */
#if defined(__SIZEOF_INT128__)
    #define SERAPH_HAS_INT128 1
#else
    #define SERAPH_HAS_INT128 0
#endif

/*============================================================================
 * Convenience Macros
 *============================================================================*/

/**
 * @brief Assert that a value exists (is not VOID)
 *
 * In debug builds, this will print an error and abort if x is VOID.
 * In release builds, this is a no-op.
 */
#ifdef NDEBUG
    #define SERAPH_ASSERT_EXISTS(x) ((void)0)
#else
    #include <stdio.h>
    #include <stdlib.h>
    #define SERAPH_ASSERT_EXISTS(x) do { \
        if (SERAPH_IS_VOID(x)) { \
            fprintf(stderr, "SERAPH VOID assertion failed: %s at %s:%d\n", \
                    #x, __FILE__, __LINE__); \
            abort(); \
        } \
    } while(0)
#endif

/**
 * @brief Return VOID from current function if expression is VOID
 *
 * Useful for propagating VOID through function chains.
 */
#define SERAPH_TRY(x) do { \
    if (SERAPH_IS_VOID(x)) return SERAPH_VOID_OF(x); \
} while(0)

/**
 * @brief Propagate VOID with custom return value
 */
#define SERAPH_TRY_OR(x, ret) do { \
    if (SERAPH_IS_VOID(x)) return (ret); \
} while(0)

#endif /* SERAPH_H */
