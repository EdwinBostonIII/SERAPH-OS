/**
 * @file sbf.h
 * @brief SERAPH Binary Format (SBF) - Native Executable Format
 *
 * SBF is SERAPH's native binary format, designed from first principles to embody
 * SERAPH's philosophy of security-by-construction, capability-based access control,
 * and proof-embedded verification.
 *
 * Key Design Principles:
 * 1. SELF-DESCRIBING: Binary contains everything needed to understand itself
 * 2. PROOF-EMBEDDED: Compile-time proofs are integral, not optional sections
 * 3. CAPABILITY-NATIVE: Memory access defined by capabilities before execution
 * 4. VOID-AWARE: VOID values have special handling in the format
 * 5. CHRONON-BUDGETED: Time budgets are first-class citizens
 * 6. SUBSTRATE-DECLARED: Atlas/Aether dependencies are explicit
 * 7. CRYPTOGRAPHICALLY-SEALED: SHA-256 hashes make tampering detectable
 * 8. STREAMING-FRIENDLY: Can start verification before file fully loaded
 *
 * File Layout:
 *   [0x000] SBF_Header     (256 bytes) - Fixed header with all offsets
 *   [0x100] SBF_Manifest   (256 bytes) - Sovereign requirements
 *   [0x200] Code Section   (page-aligned, R-X)
 *   [...]   RoData Section (page-aligned, R--)
 *   [...]   Data Section   (page-aligned, RW-)
 *   [...]   Proof Table    (8-byte aligned)
 *   [...]   Cap Table      (8-byte aligned)
 *   [...]   Effect Table   (8-byte aligned)
 *   [...]   String Table   (1-byte aligned)
 *
 * Unlike ELF, SBF:
 * - Has fixed header at the start (not sections at end)
 * - Requires mandatory proof verification before execution
 * - Has no dynamic linking infrastructure
 * - Has no relocation tables
 * - Integrates manifest directly (not separate file)
 */

#ifndef SERAPH_SBF_H
#define SERAPH_SBF_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "seraph/vbit.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * SBF Magic Numbers and Version
 *============================================================================*/

/** SBF file magic: "SBF\0" in little-endian */
#define SBF_MAGIC               0x00464253

/** SBF manifest magic: "SMFN" in little-endian */
#define SBF_MANIFEST_MAGIC      0x4E464D53

/** SBF proof table magic: "SPRF" in little-endian */
#define SBF_PROOF_MAGIC         0x46525053

/** SBF capability table magic: "SCAP" in little-endian */
#define SBF_CAP_MAGIC           0x50414353

/** SBF effect table magic: "SEFF" in little-endian */
#define SBF_EFFECT_MAGIC        0x46464553

/** SBF string table magic: "SSTR" in little-endian */
#define SBF_STRING_MAGIC        0x52545353

/** Current SBF format version: 1.0.0 */
#define SBF_VERSION_MAJOR       1
#define SBF_VERSION_MINOR       0
#define SBF_VERSION_PATCH       0
#define SBF_VERSION             ((SBF_VERSION_MAJOR << 16) | (SBF_VERSION_MINOR << 8) | SBF_VERSION_PATCH)

/** Fixed header size (256 bytes) */
#define SBF_HEADER_SIZE         256

/** Fixed manifest size (256 bytes) */
#define SBF_MANIFEST_SIZE       256

/** Page size for section alignment */
#define SBF_PAGE_SIZE           4096

/** SHA-256 hash size in bytes */
#define SBF_HASH_SIZE           32

/** Ed25519 signature size in bytes */
#define SBF_SIGNATURE_SIZE      64

/** Ed25519 public key size in bytes */
#define SBF_PUBKEY_SIZE         32

/** Binary ID size (unique identifier) */
#define SBF_BINARY_ID_SIZE      32

/*============================================================================
 * SBF Header Flags
 *============================================================================*/

/** Binary is position-independent */
#define SBF_FLAG_PIE            (1 << 0)

/** Binary requires signing verification */
#define SBF_FLAG_SIGNED         (1 << 1)

/** Binary is a kernel module (ring 0) */
#define SBF_FLAG_KERNEL         (1 << 2)

/** Binary is a driver (privileged) */
#define SBF_FLAG_DRIVER         (1 << 3)

/** Binary uses PRISM hypervisor extensions */
#define SBF_FLAG_PRISM          (1 << 4)

/** Binary has debug information */
#define SBF_FLAG_DEBUG          (1 << 5)

/** Binary is stripped (no string table) */
#define SBF_FLAG_STRIPPED       (1 << 6)

/** Binary uses Galactic numbers (autodiff) */
#define SBF_FLAG_GALACTIC       (1 << 7)

/*============================================================================
 * SBF Target Architecture
 *============================================================================*/

typedef enum {
    SBF_ARCH_X64        = 0x01,     /**< x86-64 / AMD64 */
    SBF_ARCH_ARM64      = 0x02,     /**< AArch64 / ARM64 */
    SBF_ARCH_RISCV64    = 0x03,     /**< RISC-V 64-bit */
    SBF_ARCH_SERAPH_VM  = 0xFF,     /**< SERAPH Virtual Machine (future) */
} SBF_Architecture;

/*============================================================================
 * SBF Header (256 bytes)
 *
 * The header is always at offset 0 and contains all information needed
 * to locate and validate every other section of the binary.
 *============================================================================*/

typedef struct __attribute__((packed)) {
    /* Identification (16 bytes) */
    uint32_t magic;                     /**< SBF_MAGIC (0x00464253) */
    uint32_t version;                   /**< Format version (major.minor.patch) */
    uint32_t flags;                     /**< SBF_FLAG_* */
    uint32_t header_size;               /**< Size of this header (for forward compat) */

    /* File structure (16 bytes) */
    uint64_t total_size;                /**< Total file size in bytes */
    uint64_t entry_point;               /**< Entry point virtual address */

    /* Cryptographic integrity (64 bytes) */
    uint8_t  proof_root[SBF_HASH_SIZE]; /**< SHA-256 Merkle root of all proofs */
    uint8_t  content_hash[SBF_HASH_SIZE]; /**< SHA-256 of everything after header */

    /* Section offsets and sizes (80 bytes) */
    uint64_t manifest_offset;           /**< Offset to SBF_Manifest */
    uint64_t manifest_size;             /**< Size of manifest (always 256) */

    uint64_t code_offset;               /**< Offset to executable code */
    uint64_t code_size;                 /**< Size of code section */

    uint64_t rodata_offset;             /**< Offset to read-only data */
    uint64_t rodata_size;               /**< Size of rodata section */

    uint64_t data_offset;               /**< Offset to initialized data */
    uint64_t data_size;                 /**< Size of data section */

    uint64_t bss_size;                  /**< Size of uninitialized data (not in file) */

    /* Metadata section offsets (48 bytes) */
    uint64_t proofs_offset;             /**< Offset to proof table */
    uint64_t proofs_size;               /**< Size of proof table */

    uint64_t caps_offset;               /**< Offset to capability templates */
    uint64_t caps_size;                 /**< Size of capability table */

    uint64_t effects_offset;            /**< Offset to effect declarations */
    uint64_t effects_size;              /**< Size of effect table */

    /* Debug/string information (16 bytes) */
    uint64_t strings_offset;            /**< Offset to string table */
    uint64_t strings_size;              /**< Size of string table */

    /* Architecture (8 bytes) */
    uint32_t architecture;              /**< SBF_Architecture */
    uint32_t arch_flags;                /**< Architecture-specific flags */

    /* Reserved for future expansion (16 bytes to pad to 256) */
    uint64_t reserved[2];
} SBF_Header;

_Static_assert(sizeof(SBF_Header) == SBF_HEADER_SIZE,
               "SBF_Header must be exactly 256 bytes");

/*============================================================================
 * SBF Manifest (256 bytes)
 *
 * The manifest declares what resources the binary needs to execute.
 * The kernel reads this BEFORE granting any resources.
 *============================================================================*/

/** Sovereign creation flags */
#define SBF_SOV_FLAG_ISOLATED       (1 << 0)    /**< No shared memory */
#define SBF_SOV_FLAG_REALTIME       (1 << 1)    /**< Realtime priority */
#define SBF_SOV_FLAG_PERSISTENT     (1 << 2)    /**< Survives reboot (via Atlas) */
#define SBF_SOV_FLAG_NETWORKED      (1 << 3)    /**< Uses Aether networking */
#define SBF_SOV_FLAG_PRIVILEGED     (1 << 4)    /**< Needs elevated permissions */

/** Atlas region requirement flags */
#define SBF_ATLAS_FLAG_REQUIRED     (1 << 0)    /**< Must have Atlas access */
#define SBF_ATLAS_FLAG_EXCLUSIVE    (1 << 1)    /**< Exclusive Atlas region */
#define SBF_ATLAS_FLAG_ENCRYPTED    (1 << 2)    /**< Encrypted Atlas storage */

/** Aether requirement flags */
#define SBF_AETHER_FLAG_REQUIRED    (1 << 0)    /**< Must have network access */
#define SBF_AETHER_FLAG_SERVER      (1 << 1)    /**< Can accept connections */
#define SBF_AETHER_FLAG_BROADCAST   (1 << 2)    /**< Can broadcast */

typedef struct __attribute__((packed)) {
    /* Identification (16 bytes) */
    uint32_t magic;                     /**< SBF_MANIFEST_MAGIC */
    uint32_t version;                   /**< Manifest format version */
    uint32_t kernel_min_version;        /**< Minimum kernel version required */
    uint32_t kernel_max_version;        /**< Maximum kernel version (0 = any) */

    /* Sovereign configuration (16 bytes) */
    uint32_t sovereign_flags;           /**< SBF_SOV_FLAG_* */
    uint32_t strand_count_min;          /**< Minimum Strands needed */
    uint32_t strand_count_max;          /**< Maximum Strands requested */
    uint32_t strand_flags;              /**< Strand creation flags */

    /* Memory requirements (32 bytes) */
    uint64_t stack_size;                /**< Required stack per Strand */
    uint64_t heap_size;                 /**< Required heap size */
    uint64_t memory_limit;              /**< Maximum total memory (0 = unlimited) */
    uint64_t reserved_mem;

    /* Time requirements (32 bytes) */
    uint64_t chronon_budget;            /**< Initial Chronon allocation */
    uint64_t chronon_limit;             /**< Maximum Chronon (0 = unlimited) */
    uint64_t chronon_slice;             /**< Preferred scheduling quantum */
    uint64_t reserved_time;

    /* Substrate requirements (16 bytes) */
    uint32_t atlas_region_count;        /**< Number of Atlas regions needed */
    uint32_t atlas_flags;               /**< SBF_ATLAS_FLAG_* */
    uint32_t aether_node_count;         /**< Number of Aether connections */
    uint32_t aether_flags;              /**< SBF_AETHER_FLAG_* */

    /* Capability requirements (16 bytes) */
    uint32_t cap_slot_count;            /**< Capability slots needed */
    uint32_t effect_mask;               /**< Declared effects (what binary MAY do) */
    uint64_t priority_class;            /**< Scheduling priority hint */

    /* Identity (128 bytes) */
    uint8_t  binary_id[SBF_BINARY_ID_SIZE];   /**< Unique binary identifier */
    uint8_t  author_key[SBF_PUBKEY_SIZE];     /**< Ed25519 public key of author */
    uint8_t  signature[SBF_SIGNATURE_SIZE];    /**< Ed25519 signature of manifest */
    /* 32+32+64 = 128 bytes */
} SBF_Manifest;

_Static_assert(sizeof(SBF_Manifest) == SBF_MANIFEST_SIZE,
               "SBF_Manifest must be exactly 256 bytes");

/*============================================================================
 * SBF Proof Table
 *
 * Contains cryptographic evidence that the compiler verified safety properties.
 * Each proof is hashed and included in a Merkle tree.
 *============================================================================*/

/** Proof kinds (what property is proven) */
typedef enum {
    SBF_PROOF_BOUNDS        = 0x01,     /**< Array bounds checked */
    SBF_PROOF_VOID          = 0x02,     /**< VOID value handled */
    SBF_PROOF_EFFECT        = 0x03,     /**< Effects verified */
    SBF_PROOF_PERMISSION    = 0x04,     /**< Capability permissions valid */
    SBF_PROOF_GENERATION    = 0x05,     /**< Generation (temporal) valid */
    SBF_PROOF_SUBSTRATE     = 0x06,     /**< Substrate access valid */
    SBF_PROOF_TYPE          = 0x07,     /**< Type safety verified */
    SBF_PROOF_INIT          = 0x08,     /**< Variable initialized */
    SBF_PROOF_OVERFLOW      = 0x09,     /**< Arithmetic overflow checked */
    SBF_PROOF_NULL          = 0x0A,     /**< Null pointer checked */
    SBF_PROOF_INVARIANT     = 0x0B,     /**< Loop/data invariant */
    SBF_PROOF_TERMINATION   = 0x0C,     /**< Loop termination */
} SBF_Proof_Kind;

/** Proof status */
typedef enum {
    SBF_PROOF_PROVEN        = 0x01,     /**< Statically verified */
    SBF_PROOF_ASSUMED       = 0x02,     /**< Assumed true (precondition) */
    SBF_PROOF_RUNTIME       = 0x03,     /**< Requires runtime check */
    SBF_PROOF_FAILED        = 0x04,     /**< Could not prove (binary invalid) */
} SBF_Proof_Status;

/** Single proof entry (56 bytes) */
typedef struct __attribute__((packed)) {
    uint32_t kind;                      /**< SBF_Proof_Kind */
    uint32_t status;                    /**< SBF_Proof_Status */
    uint32_t location;                  /**< Offset into strings for source loc */
    uint32_t description;               /**< Offset into strings for description */
    uint64_t code_offset;               /**< Offset in code section */
    uint8_t  hash[SBF_HASH_SIZE];       /**< SHA-256 of proof witness data */
    /* 4+4+4+4+8+32 = 56 bytes */
} SBF_Proof_Entry;

_Static_assert(sizeof(SBF_Proof_Entry) == 56,
               "SBF_Proof_Entry must be exactly 56 bytes");

/** Proof table header (48 bytes) */
typedef struct __attribute__((packed)) {
    uint32_t magic;                     /**< SBF_PROOF_MAGIC */
    uint32_t entry_count;               /**< Number of proof entries */
    uint32_t proven_count;              /**< Entries with status PROVEN */
    uint32_t failed_count;              /**< Entries with status FAILED (should be 0) */
    uint8_t  merkle_root[SBF_HASH_SIZE]; /**< Merkle root (must match header) */
    /* 4+4+4+4+32 = 48 bytes */
    /* Followed by entry_count * SBF_Proof_Entry */
} SBF_Proof_Table;

_Static_assert(sizeof(SBF_Proof_Table) == 48,
               "SBF_Proof_Table header must be exactly 48 bytes");

/*============================================================================
 * SBF Capability Table
 *
 * Defines memory regions and their access permissions.
 * Capabilities are created from these templates before execution begins.
 *============================================================================*/

/** Capability permission flags */
#define SBF_CAP_READ            (1 << 0)    /**< Can read from region */
#define SBF_CAP_WRITE           (1 << 1)    /**< Can write to region */
#define SBF_CAP_EXEC            (1 << 2)    /**< Can execute from region */
#define SBF_CAP_DERIVE          (1 << 3)    /**< Can create sub-capabilities */
#define SBF_CAP_SEAL            (1 << 4)    /**< Can seal (make immutable) */
#define SBF_CAP_UNSEAL          (1 << 5)    /**< Can unseal */
#define SBF_CAP_GLOBAL          (1 << 6)    /**< Survives context switch */
#define SBF_CAP_LOCAL           (1 << 7)    /**< Valid only in current context */

/** Capability type flags */
#define SBF_CAP_TYPE_CODE       (1 << 8)    /**< Code region */
#define SBF_CAP_TYPE_DATA       (1 << 9)    /**< Data region */
#define SBF_CAP_TYPE_STACK      (1 << 10)   /**< Stack region */
#define SBF_CAP_TYPE_HEAP       (1 << 11)   /**< Heap region */
#define SBF_CAP_TYPE_MMIO       (1 << 12)   /**< Memory-mapped I/O */
#define SBF_CAP_TYPE_ATLAS      (1 << 13)   /**< Atlas persistent storage */
#define SBF_CAP_TYPE_AETHER     (1 << 14)   /**< Aether network buffer */

/** Single capability template (32 bytes) */
typedef struct __attribute__((packed)) {
    uint64_t base;                      /**< Base address (0 = dynamically allocate) */
    uint64_t length;                    /**< Region length in bytes */
    uint32_t permissions;               /**< SBF_CAP_* permission and type flags */
    uint32_t generation;                /**< Initial generation number */
    uint32_t name_offset;               /**< Offset into strings for debug name */
    uint32_t reserved;
} SBF_Cap_Template;

_Static_assert(sizeof(SBF_Cap_Template) == 32,
               "SBF_Cap_Template must be exactly 32 bytes");

/** Capability table header (24 bytes) */
typedef struct __attribute__((packed)) {
    uint32_t magic;                     /**< SBF_CAP_MAGIC */
    uint32_t entry_count;               /**< Number of capability templates */
    uint32_t code_cap_idx;              /**< Index of code section capability */
    uint32_t rodata_cap_idx;            /**< Index of rodata section capability */
    uint32_t data_cap_idx;              /**< Index of data section capability */
    uint32_t stack_cap_idx;             /**< Index of stack capability */
    /* Followed by entry_count * SBF_Cap_Template */
} SBF_Cap_Table;

_Static_assert(sizeof(SBF_Cap_Table) == 24,
               "SBF_Cap_Table header must be exactly 24 bytes");

/*============================================================================
 * SBF Effect Table
 *
 * Declares effects (side effects) for each function.
 * Enables per-function capability enforcement.
 *============================================================================*/

/** Effect flags (matches SERAPH_EFFECT_*) */
#define SBF_EFFECT_NONE         0x00        /**< Pure function */
#define SBF_EFFECT_VOID         0x01        /**< May produce VOID */
#define SBF_EFFECT_PERSIST      0x02        /**< Accesses Atlas storage */
#define SBF_EFFECT_NETWORK      0x04        /**< Accesses Aether network */
#define SBF_EFFECT_TIMER        0x08        /**< Uses timers */
#define SBF_EFFECT_IO           0x10        /**< General I/O */
#define SBF_EFFECT_MEMORY       0x20        /**< Dynamic memory allocation */
#define SBF_EFFECT_PANIC        0x40        /**< May panic */
#define SBF_EFFECT_ALL          0xFF        /**< All effects (unsafe) */

/** Required capability types for effects */
#define SBF_EFFECT_CAP_ATLAS    (1 << 0)    /**< Needs Atlas capability */
#define SBF_EFFECT_CAP_AETHER   (1 << 1)    /**< Needs Aether capability */
#define SBF_EFFECT_CAP_IO       (1 << 2)    /**< Needs I/O capability */
#define SBF_EFFECT_CAP_ALLOC    (1 << 3)    /**< Needs allocator capability */

/** Single effect entry (24 bytes) */
typedef struct __attribute__((packed)) {
    uint32_t function_offset;           /**< Offset of function in code section */
    uint32_t function_size;             /**< Size of function in bytes */
    uint32_t declared_effects;          /**< Effects programmer declared */
    uint32_t verified_effects;          /**< Effects compiler verified */
    uint32_t required_caps;             /**< SBF_EFFECT_CAP_* flags */
    uint32_t name_offset;               /**< Function name in strings */
} SBF_Effect_Entry;

_Static_assert(sizeof(SBF_Effect_Entry) == 24,
               "SBF_Effect_Entry must be exactly 24 bytes");

/** Effect table header (16 bytes) */
typedef struct __attribute__((packed)) {
    uint32_t magic;                     /**< SBF_EFFECT_MAGIC */
    uint32_t entry_count;               /**< Number of function entries */
    uint32_t pure_count;                /**< Functions with EFFECT_NONE */
    uint32_t impure_count;              /**< Functions with any effects */
    /* Followed by entry_count * SBF_Effect_Entry */
} SBF_Effect_Table;

_Static_assert(sizeof(SBF_Effect_Table) == 16,
               "SBF_Effect_Table header must be exactly 16 bytes");

/*============================================================================
 * SBF String Table
 *
 * Contains null-terminated strings for debug names, source locations, etc.
 * String offsets in other structures refer to byte offsets from string table start.
 *============================================================================*/

/** String table header (8 bytes) */
typedef struct __attribute__((packed)) {
    uint32_t magic;                     /**< SBF_STRING_MAGIC */
    uint32_t total_size;                /**< Total size including header */
    /* Followed by null-terminated strings */
} SBF_String_Table;

_Static_assert(sizeof(SBF_String_Table) == 8,
               "SBF_String_Table header must be exactly 8 bytes");

/*============================================================================
 * Validation Result
 *============================================================================*/

typedef enum {
    SBF_VALID                   = 0,
    SBF_ERR_INVALID_MAGIC       = 1,
    SBF_ERR_INVALID_VERSION     = 2,
    SBF_ERR_INVALID_SIZE        = 3,
    SBF_ERR_HASH_MISMATCH       = 4,
    SBF_ERR_PROOF_ROOT_MISMATCH = 5,
    SBF_ERR_PROOF_FAILED        = 6,
    SBF_ERR_MANIFEST_INVALID    = 7,
    SBF_ERR_SIGNATURE_INVALID   = 8,
    SBF_ERR_CAPS_INVALID        = 9,
    SBF_ERR_EFFECTS_INVALID     = 10,
    SBF_ERR_SECTION_OVERLAP     = 11,
    SBF_ERR_ALIGNMENT           = 12,
    SBF_ERR_TRUNCATED           = 13,
} SBF_Validation_Result;

/*============================================================================
 * Inline Validation Helpers
 *============================================================================*/

/**
 * @brief Validate SBF header magic and version
 */
static inline SBF_Validation_Result sbf_validate_header_quick(const SBF_Header* hdr) {
    if (hdr == NULL) return SBF_ERR_INVALID_MAGIC;
    if (hdr->magic != SBF_MAGIC) return SBF_ERR_INVALID_MAGIC;
    if ((hdr->version >> 16) != SBF_VERSION_MAJOR) return SBF_ERR_INVALID_VERSION;
    if (hdr->header_size < SBF_HEADER_SIZE) return SBF_ERR_INVALID_SIZE;
    return SBF_VALID;
}

/**
 * @brief Validate manifest magic
 */
static inline SBF_Validation_Result sbf_validate_manifest_quick(const SBF_Manifest* mfst) {
    if (mfst == NULL) return SBF_ERR_MANIFEST_INVALID;
    if (mfst->magic != SBF_MANIFEST_MAGIC) return SBF_ERR_MANIFEST_INVALID;
    return SBF_VALID;
}

/**
 * @brief Check if proof table has failures
 */
static inline bool sbf_proof_table_has_failures(const SBF_Proof_Table* tbl) {
    return tbl != NULL && tbl->failed_count > 0;
}

/**
 * @brief Get version string from packed version
 */
static inline void sbf_version_to_string(uint32_t version, char* buf, size_t buf_size) {
    if (buf == NULL || buf_size < 12) return;
    uint32_t major = (version >> 16) & 0xFF;
    uint32_t minor = (version >> 8) & 0xFF;
    uint32_t patch = version & 0xFF;
    /* Manual formatting to avoid stdio dependency */
    int pos = 0;
    if (major >= 10) buf[pos++] = '0' + (major / 10);
    buf[pos++] = '0' + (major % 10);
    buf[pos++] = '.';
    if (minor >= 10) buf[pos++] = '0' + (minor / 10);
    buf[pos++] = '0' + (minor % 10);
    buf[pos++] = '.';
    if (patch >= 10) buf[pos++] = '0' + (patch / 10);
    buf[pos++] = '0' + (patch % 10);
    buf[pos] = '\0';
}

/**
 * @brief Get human-readable name for validation result
 */
static inline const char* sbf_validation_result_name(SBF_Validation_Result result) {
    switch (result) {
        case SBF_VALID:                     return "Valid";
        case SBF_ERR_INVALID_MAGIC:         return "Invalid magic number";
        case SBF_ERR_INVALID_VERSION:       return "Invalid version";
        case SBF_ERR_INVALID_SIZE:          return "Invalid size";
        case SBF_ERR_HASH_MISMATCH:         return "Content hash mismatch";
        case SBF_ERR_PROOF_ROOT_MISMATCH:   return "Proof root mismatch";
        case SBF_ERR_PROOF_FAILED:          return "Proof verification failed";
        case SBF_ERR_MANIFEST_INVALID:      return "Invalid manifest";
        case SBF_ERR_SIGNATURE_INVALID:     return "Invalid signature";
        case SBF_ERR_CAPS_INVALID:          return "Invalid capabilities";
        case SBF_ERR_EFFECTS_INVALID:       return "Invalid effects";
        case SBF_ERR_SECTION_OVERLAP:       return "Section overlap";
        case SBF_ERR_ALIGNMENT:             return "Alignment error";
        case SBF_ERR_TRUNCATED:             return "File truncated";
        default:                            return "Unknown error";
    }
}

/**
 * @brief Get human-readable name for proof kind
 */
static inline const char* sbf_proof_kind_name(SBF_Proof_Kind kind) {
    switch (kind) {
        case SBF_PROOF_BOUNDS:      return "Bounds";
        case SBF_PROOF_VOID:        return "VOID";
        case SBF_PROOF_EFFECT:      return "Effect";
        case SBF_PROOF_PERMISSION:  return "Permission";
        case SBF_PROOF_GENERATION:  return "Generation";
        case SBF_PROOF_SUBSTRATE:   return "Substrate";
        case SBF_PROOF_TYPE:        return "Type";
        case SBF_PROOF_INIT:        return "Init";
        case SBF_PROOF_OVERFLOW:    return "Overflow";
        case SBF_PROOF_NULL:        return "Null";
        case SBF_PROOF_INVARIANT:   return "Invariant";
        case SBF_PROOF_TERMINATION: return "Termination";
        default:                    return "Unknown";
    }
}

/**
 * @brief Get human-readable name for architecture
 */
static inline const char* sbf_arch_name(SBF_Architecture arch) {
    switch (arch) {
        case SBF_ARCH_X64:          return "x86-64";
        case SBF_ARCH_ARM64:        return "ARM64";
        case SBF_ARCH_RISCV64:      return "RISC-V 64";
        case SBF_ARCH_SERAPH_VM:    return "SERAPH VM";
        default:                    return "Unknown";
    }
}

/**
 * @brief Align offset to specified alignment
 */
static inline uint64_t sbf_align(uint64_t offset, uint64_t alignment) {
    return (offset + alignment - 1) & ~(alignment - 1);
}

/**
 * @brief Align offset to page boundary
 */
static inline uint64_t sbf_page_align(uint64_t offset) {
    return sbf_align(offset, SBF_PAGE_SIZE);
}

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_SBF_H */
