/**
 * @file sbf_writer.h
 * @brief SERAPH Binary Format (SBF) Writer API
 *
 * This module provides the API for creating SBF binaries from compiled
 * Celestial IR. The writer handles:
 * - Section layout with proper alignment
 * - Proof table generation with Merkle tree
 * - Capability template generation
 * - Effect table generation
 * - SHA-256 hashing for integrity verification
 * - Manifest creation with sovereign requirements
 *
 * Usage:
 *   SBF_Writer* writer = sbf_writer_create();
 *   sbf_writer_set_architecture(writer, SBF_ARCH_X64);
 *   sbf_writer_set_code(writer, code_data, code_size);
 *   sbf_writer_set_rodata(writer, rodata_data, rodata_size);
 *   sbf_writer_set_data(writer, data_data, data_size);
 *   sbf_writer_set_bss_size(writer, bss_size);
 *   sbf_writer_add_proof(writer, &proof);
 *   sbf_writer_add_capability(writer, &cap);
 *   sbf_writer_add_effect(writer, &effect);
 *   sbf_writer_add_string(writer, "debug_name");
 *   sbf_writer_configure_manifest(writer, &manifest_config);
 *   sbf_writer_finalize(writer);  // Computes hashes, builds Merkle tree
 *   sbf_writer_write_file(writer, "output.sbf");
 *   sbf_writer_destroy(writer);
 */

#ifndef SERAPH_SBF_WRITER_H
#define SERAPH_SBF_WRITER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "seraph/sbf.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Forward Declarations
 *============================================================================*/

typedef struct SBF_Writer SBF_Writer;
typedef struct SBF_Writer_Config SBF_Writer_Config;
typedef struct SBF_Manifest_Config SBF_Manifest_Config;

/* Forward declarations for Celestial IR integration */
struct Celestial_Function;
struct Celestial_Module;
struct Seraph_Proof;
struct Seraph_Proof_Table;

/*============================================================================
 * Writer Error Codes
 *============================================================================*/

typedef enum {
    SBF_WRITE_OK                = 0,
    SBF_WRITE_ERR_ALLOC         = 1,    /**< Memory allocation failed */
    SBF_WRITE_ERR_NO_CODE       = 2,    /**< No code section provided */
    SBF_WRITE_ERR_TOO_LARGE     = 3,    /**< Binary exceeds size limits */
    SBF_WRITE_ERR_HASH_FAIL     = 4,    /**< Hash computation failed */
    SBF_WRITE_ERR_IO            = 5,    /**< File I/O error */
    SBF_WRITE_ERR_ALIGNMENT     = 6,    /**< Alignment requirements violated */
    SBF_WRITE_ERR_OVERLAP       = 7,    /**< Sections overlap */
    SBF_WRITE_ERR_INVALID_PROOF = 8,    /**< Invalid proof entry */
    SBF_WRITE_ERR_INVALID_CAP   = 9,    /**< Invalid capability template */
    SBF_WRITE_ERR_INVALID_EFFECT= 10,   /**< Invalid effect entry */
    SBF_WRITE_ERR_STRING_FULL   = 11,   /**< String table full */
    SBF_WRITE_ERR_NOT_FINALIZED = 12,   /**< Must call finalize first */
    SBF_WRITE_ERR_ALREADY_FINAL = 13,   /**< Already finalized, cannot modify */
    SBF_WRITE_ERR_SIGN_FAIL     = 14,   /**< Signing operation failed */
} SBF_Write_Error;

/*============================================================================
 * Writer Configuration
 *============================================================================*/

/**
 * @brief Configuration for SBF writer
 */
struct SBF_Writer_Config {
    uint32_t flags;                     /**< SBF_FLAG_* */
    SBF_Architecture architecture;      /**< Target architecture */
    uint64_t entry_point;               /**< Entry point virtual address */

    /* Size limits */
    size_t max_code_size;               /**< Max code section (0 = unlimited) */
    size_t max_data_size;               /**< Max data section (0 = unlimited) */
    size_t max_proofs;                  /**< Max proof entries (0 = unlimited) */
    size_t max_caps;                    /**< Max capability templates (0 = unlimited) */
    size_t max_effects;                 /**< Max effect entries (0 = unlimited) */
    size_t max_string_size;             /**< Max string table size (0 = default 64KB) */

    /* Signing configuration */
    const uint8_t* author_private_key;  /**< Ed25519 private key (64 bytes, NULL = unsigned) */
    const uint8_t* author_public_key;   /**< Ed25519 public key (32 bytes) */
};

/**
 * @brief Configuration for manifest generation
 */
struct SBF_Manifest_Config {
    /* Version requirements */
    uint32_t kernel_min_version;        /**< Minimum kernel version */
    uint32_t kernel_max_version;        /**< Maximum kernel version (0 = any) */

    /* Sovereign configuration */
    uint32_t sovereign_flags;           /**< SBF_SOV_FLAG_* */
    uint32_t strand_count_min;          /**< Minimum Strands needed */
    uint32_t strand_count_max;          /**< Maximum Strands requested */
    uint32_t strand_flags;              /**< Strand creation flags */

    /* Memory requirements */
    uint64_t stack_size;                /**< Required stack per Strand */
    uint64_t heap_size;                 /**< Required heap size */
    uint64_t memory_limit;              /**< Maximum total memory (0 = unlimited) */

    /* Time requirements */
    uint64_t chronon_budget;            /**< Initial Chronon allocation */
    uint64_t chronon_limit;             /**< Maximum Chronon (0 = unlimited) */
    uint64_t chronon_slice;             /**< Preferred scheduling quantum */

    /* Substrate requirements */
    uint32_t atlas_region_count;        /**< Number of Atlas regions needed */
    uint32_t atlas_flags;               /**< SBF_ATLAS_FLAG_* */
    uint32_t aether_node_count;         /**< Number of Aether connections */
    uint32_t aether_flags;              /**< SBF_AETHER_FLAG_* */

    /* Capability requirements */
    uint32_t cap_slot_count;            /**< Capability slots needed */
    uint64_t priority_class;            /**< Scheduling priority hint */

    /* Identity (optional, generated if not provided) */
    const uint8_t* binary_id;           /**< 32-byte unique ID (NULL = generate) */
};

/*============================================================================
 * Dynamic Array Helpers (internal)
 *============================================================================*/

typedef struct {
    uint8_t* data;
    size_t size;
    size_t capacity;
} SBF_Buffer;

typedef struct {
    SBF_Proof_Entry* entries;
    size_t count;
    size_t capacity;
} SBF_Proof_Array;

typedef struct {
    SBF_Cap_Template* entries;
    size_t count;
    size_t capacity;
} SBF_Cap_Array;

typedef struct {
    SBF_Effect_Entry* entries;
    size_t count;
    size_t capacity;
} SBF_Effect_Array;

/*============================================================================
 * SBF Writer State
 *============================================================================*/

/**
 * @brief SBF Writer context
 *
 * Maintains all state needed to build an SBF binary.
 * The writer accumulates sections and metadata, then finalizes
 * by computing hashes and laying out the binary.
 */
struct SBF_Writer {
    /* Configuration */
    SBF_Writer_Config config;
    SBF_Manifest_Config manifest_config;
    bool manifest_configured;

    /* State flags */
    bool finalized;
    SBF_Write_Error last_error;

    /* Section data */
    SBF_Buffer code;
    SBF_Buffer rodata;
    SBF_Buffer data;
    uint64_t bss_size;

    /* Metadata tables */
    SBF_Proof_Array proofs;
    SBF_Cap_Array caps;
    SBF_Effect_Array effects;
    SBF_Buffer strings;

    /* Computed values (set during finalize) */
    SBF_Header header;
    SBF_Manifest manifest;
    SBF_Proof_Table proof_table_header;
    SBF_Cap_Table cap_table_header;
    SBF_Effect_Table effect_table_header;
    SBF_String_Table string_table_header;

    /* Merkle tree for proofs */
    uint8_t* merkle_nodes;              /**< Array of SHA-256 hashes */
    size_t merkle_node_count;

    /* Final binary */
    uint8_t* output;
    size_t output_size;
};

/*============================================================================
 * Writer Lifecycle
 *============================================================================*/

/**
 * @brief Create a new SBF writer with default configuration
 * @return New writer instance or NULL on failure
 */
SBF_Writer* sbf_writer_create(void);

/**
 * @brief Create a new SBF writer with custom configuration
 * @param config Writer configuration
 * @return New writer instance or NULL on failure
 */
SBF_Writer* sbf_writer_create_with_config(const SBF_Writer_Config* config);

/**
 * @brief Destroy an SBF writer and free all resources
 * @param writer Writer to destroy
 */
void sbf_writer_destroy(SBF_Writer* writer);

/**
 * @brief Get the last error from the writer
 * @param writer Writer instance
 * @return Last error code
 */
SBF_Write_Error sbf_writer_get_error(const SBF_Writer* writer);

/**
 * @brief Get human-readable error message
 * @param error Error code
 * @return Static string describing the error
 */
const char* sbf_write_error_name(SBF_Write_Error error);

/*============================================================================
 * Configuration
 *============================================================================*/

/**
 * @brief Set target architecture
 * @param writer Writer instance
 * @param arch Target architecture
 * @return SBF_WRITE_OK or error
 */
SBF_Write_Error sbf_writer_set_architecture(SBF_Writer* writer, SBF_Architecture arch);

/**
 * @brief Set binary flags
 * @param writer Writer instance
 * @param flags SBF_FLAG_* flags
 * @return SBF_WRITE_OK or error
 */
SBF_Write_Error sbf_writer_set_flags(SBF_Writer* writer, uint32_t flags);

/**
 * @brief Set entry point address
 * @param writer Writer instance
 * @param entry Entry point virtual address
 * @return SBF_WRITE_OK or error
 */
SBF_Write_Error sbf_writer_set_entry(SBF_Writer* writer, uint64_t entry);

/**
 * @brief Configure signing keys
 * @param writer Writer instance
 * @param private_key Ed25519 private key (64 bytes) or NULL to disable
 * @param public_key Ed25519 public key (32 bytes)
 * @return SBF_WRITE_OK or error
 */
SBF_Write_Error sbf_writer_set_signing_keys(
    SBF_Writer* writer,
    const uint8_t* private_key,
    const uint8_t* public_key
);

/**
 * @brief Configure manifest
 * @param writer Writer instance
 * @param config Manifest configuration
 * @return SBF_WRITE_OK or error
 */
SBF_Write_Error sbf_writer_configure_manifest(
    SBF_Writer* writer,
    const SBF_Manifest_Config* config
);

/*============================================================================
 * Section Data
 *============================================================================*/

/**
 * @brief Set code section data
 * @param writer Writer instance
 * @param data Code bytes
 * @param size Size in bytes
 * @return SBF_WRITE_OK or error
 */
SBF_Write_Error sbf_writer_set_code(SBF_Writer* writer, const void* data, size_t size);

/**
 * @brief Set read-only data section
 * @param writer Writer instance
 * @param data RoData bytes
 * @param size Size in bytes
 * @return SBF_WRITE_OK or error
 */
SBF_Write_Error sbf_writer_set_rodata(SBF_Writer* writer, const void* data, size_t size);

/**
 * @brief Set initialized data section
 * @param writer Writer instance
 * @param data Data bytes
 * @param size Size in bytes
 * @return SBF_WRITE_OK or error
 */
SBF_Write_Error sbf_writer_set_data(SBF_Writer* writer, const void* data, size_t size);

/**
 * @brief Set BSS (uninitialized data) size
 * @param writer Writer instance
 * @param size Size in bytes
 * @return SBF_WRITE_OK or error
 */
SBF_Write_Error sbf_writer_set_bss_size(SBF_Writer* writer, uint64_t size);

/*============================================================================
 * Proof Table
 *============================================================================*/

/**
 * @brief Add a proof entry
 * @param writer Writer instance
 * @param entry Proof entry to add
 * @return SBF_WRITE_OK or error
 */
SBF_Write_Error sbf_writer_add_proof(SBF_Writer* writer, const SBF_Proof_Entry* entry);

/**
 * @brief Add proof entry with individual fields
 * @param writer Writer instance
 * @param kind Proof kind
 * @param status Proof status
 * @param code_offset Offset in code section
 * @param location_str Source location string (added to string table)
 * @param description_str Description string (added to string table)
 * @return SBF_WRITE_OK or error
 */
SBF_Write_Error sbf_writer_add_proof_ex(
    SBF_Writer* writer,
    SBF_Proof_Kind kind,
    SBF_Proof_Status status,
    uint64_t code_offset,
    const char* location_str,
    const char* description_str
);

/**
 * @brief Import proofs from Seraphim compiler proof table
 * @param writer Writer instance
 * @param proof_table Compiler's proof table
 * @return SBF_WRITE_OK or error
 */
SBF_Write_Error sbf_writer_import_proofs(
    SBF_Writer* writer,
    const struct Seraph_Proof_Table* proof_table
);

/**
 * @brief Get proof count
 * @param writer Writer instance
 * @return Number of proof entries
 */
size_t sbf_writer_get_proof_count(const SBF_Writer* writer);

/*============================================================================
 * Capability Table
 *============================================================================*/

/**
 * @brief Add a capability template
 * @param writer Writer instance
 * @param cap Capability template to add
 * @return SBF_WRITE_OK or error
 */
SBF_Write_Error sbf_writer_add_capability(SBF_Writer* writer, const SBF_Cap_Template* cap);

/**
 * @brief Add capability template with individual fields
 * @param writer Writer instance
 * @param base Base address (0 = dynamic)
 * @param length Region length
 * @param permissions SBF_CAP_* flags
 * @param name Debug name (added to string table)
 * @return SBF_WRITE_OK or error
 */
SBF_Write_Error sbf_writer_add_capability_ex(
    SBF_Writer* writer,
    uint64_t base,
    uint64_t length,
    uint32_t permissions,
    const char* name
);

/**
 * @brief Add standard capabilities for sections
 *
 * Automatically creates capability templates for:
 * - Code section (R-X)
 * - RoData section (R--)
 * - Data section (RW-)
 * - Stack (RW-)
 *
 * @param writer Writer instance
 * @param stack_size Stack size for stack capability
 * @return SBF_WRITE_OK or error
 */
SBF_Write_Error sbf_writer_add_standard_caps(SBF_Writer* writer, uint64_t stack_size);

/**
 * @brief Get capability count
 * @param writer Writer instance
 * @return Number of capability templates
 */
size_t sbf_writer_get_cap_count(const SBF_Writer* writer);

/*============================================================================
 * Effect Table
 *============================================================================*/

/**
 * @brief Add an effect entry
 * @param writer Writer instance
 * @param entry Effect entry to add
 * @return SBF_WRITE_OK or error
 */
SBF_Write_Error sbf_writer_add_effect(SBF_Writer* writer, const SBF_Effect_Entry* entry);

/**
 * @brief Add effect entry with individual fields
 * @param writer Writer instance
 * @param function_offset Offset in code section
 * @param function_size Size of function
 * @param declared_effects Effects declared by programmer
 * @param verified_effects Effects verified by compiler
 * @param required_caps Required capability types
 * @param name Function name (added to string table)
 * @return SBF_WRITE_OK or error
 */
SBF_Write_Error sbf_writer_add_effect_ex(
    SBF_Writer* writer,
    uint32_t function_offset,
    uint32_t function_size,
    uint32_t declared_effects,
    uint32_t verified_effects,
    uint32_t required_caps,
    const char* name
);

/**
 * @brief Import effects from Celestial module
 * @param writer Writer instance
 * @param module Celestial module
 * @return SBF_WRITE_OK or error
 */
SBF_Write_Error sbf_writer_import_effects(
    SBF_Writer* writer,
    const struct Celestial_Module* module
);

/**
 * @brief Get effect count
 * @param writer Writer instance
 * @return Number of effect entries
 */
size_t sbf_writer_get_effect_count(const SBF_Writer* writer);

/*============================================================================
 * String Table
 *============================================================================*/

/**
 * @brief Add a string to the string table
 * @param writer Writer instance
 * @param str Null-terminated string to add
 * @return Offset in string table, or (uint32_t)-1 on error
 */
uint32_t sbf_writer_add_string(SBF_Writer* writer, const char* str);

/**
 * @brief Get string table size
 * @param writer Writer instance
 * @return Current size of string table in bytes
 */
size_t sbf_writer_get_string_size(const SBF_Writer* writer);

/*============================================================================
 * Finalization
 *============================================================================*/

/**
 * @brief Finalize the SBF binary
 *
 * This computes:
 * - All section offsets and alignments
 * - SHA-256 content hash
 * - Merkle tree of proofs
 * - Manifest signature (if keys provided)
 *
 * After finalization, the binary cannot be modified.
 *
 * @param writer Writer instance
 * @return SBF_WRITE_OK or error
 */
SBF_Write_Error sbf_writer_finalize(SBF_Writer* writer);

/**
 * @brief Check if writer is finalized
 * @param writer Writer instance
 * @return true if finalized
 */
bool sbf_writer_is_finalized(const SBF_Writer* writer);

/*============================================================================
 * Output
 *============================================================================*/

/**
 * @brief Get the finalized binary data
 * @param writer Writer instance (must be finalized)
 * @param out_size Output parameter for size
 * @return Pointer to binary data or NULL if not finalized
 */
const void* sbf_writer_get_binary(const SBF_Writer* writer, size_t* out_size);

/**
 * @brief Write the finalized binary to a file
 * @param writer Writer instance (must be finalized)
 * @param path Output file path
 * @return SBF_WRITE_OK or error
 */
SBF_Write_Error sbf_writer_write_file(const SBF_Writer* writer, const char* path);

/**
 * @brief Write the finalized binary to a memory buffer
 * @param writer Writer instance (must be finalized)
 * @param buffer Output buffer
 * @param buffer_size Size of buffer
 * @param out_written Output parameter for bytes written
 * @return SBF_WRITE_OK or error
 */
SBF_Write_Error sbf_writer_write_buffer(
    const SBF_Writer* writer,
    void* buffer,
    size_t buffer_size,
    size_t* out_written
);

/*============================================================================
 * Header Access (after finalization)
 *============================================================================*/

/**
 * @brief Get the finalized header
 * @param writer Writer instance (must be finalized)
 * @return Pointer to header or NULL if not finalized
 */
const SBF_Header* sbf_writer_get_header(const SBF_Writer* writer);

/**
 * @brief Get the finalized manifest
 * @param writer Writer instance (must be finalized)
 * @return Pointer to manifest or NULL if not finalized
 */
const SBF_Manifest* sbf_writer_get_manifest(const SBF_Writer* writer);

/*============================================================================
 * High-Level Integration
 *============================================================================*/

/**
 * @brief Create SBF from Celestial module
 *
 * High-level function that:
 * 1. Creates writer
 * 2. Generates x64 code from Celestial
 * 3. Extracts proofs, capabilities, effects
 * 4. Configures manifest from module metadata
 * 5. Finalizes and returns binary
 *
 * @param module Celestial module to compile
 * @param config Writer configuration (or NULL for defaults)
 * @param manifest_config Manifest configuration (or NULL for defaults)
 * @param out_size Output parameter for binary size
 * @return Allocated binary data (caller must free) or NULL on error
 */
void* sbf_from_celestial(
    const struct Celestial_Module* module,
    const SBF_Writer_Config* config,
    const SBF_Manifest_Config* manifest_config,
    size_t* out_size
);

/**
 * @brief Create SBF from Celestial module to file
 * @param module Celestial module to compile
 * @param output_path Output file path
 * @param config Writer configuration (or NULL for defaults)
 * @param manifest_config Manifest configuration (or NULL for defaults)
 * @return SBF_WRITE_OK or error
 */
SBF_Write_Error sbf_from_celestial_to_file(
    const struct Celestial_Module* module,
    const char* output_path,
    const SBF_Writer_Config* config,
    const SBF_Manifest_Config* manifest_config
);

/*============================================================================
 * Debug Utilities
 *============================================================================*/

/**
 * @brief Dump writer state for debugging
 * @param writer Writer instance
 * @param buf Output buffer
 * @param buf_size Buffer size
 * @return Number of bytes written (excluding null terminator)
 */
size_t sbf_writer_dump(const SBF_Writer* writer, char* buf, size_t buf_size);

/**
 * @brief Validate internal consistency of writer state
 * @param writer Writer instance
 * @return SBF_WRITE_OK or first error found
 */
SBF_Write_Error sbf_writer_validate(const SBF_Writer* writer);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_SBF_WRITER_H */
