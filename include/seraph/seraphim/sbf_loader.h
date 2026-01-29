/**
 * @file sbf_loader.h
 * @brief SERAPH Binary Format (SBF) Loader API
 *
 * This module provides the API for loading, validating, and preparing
 * SBF binaries for execution. The loader handles:
 * - Header and manifest validation
 * - SHA-256 hash verification
 * - Merkle tree proof verification
 * - Capability template extraction
 * - Effect table extraction
 * - Memory mapping preparation
 *
 * Usage:
 *   SBF_Loader* loader = sbf_loader_create();
 *   sbf_loader_load_file(loader, "program.sbf");
 *   if (sbf_loader_validate(loader) == SBF_VALID) {
 *       const SBF_Header* hdr = sbf_loader_get_header(loader);
 *       const void* code = sbf_loader_get_code(loader);
 *       // Map code to executable memory and run...
 *   }
 *   sbf_loader_destroy(loader);
 */

#ifndef SERAPH_SBF_LOADER_H
#define SERAPH_SBF_LOADER_H

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

typedef struct SBF_Loader SBF_Loader;
typedef struct SBF_Loader_Config SBF_Loader_Config;

/*============================================================================
 * Loader Error Codes
 *============================================================================*/

typedef enum {
    SBF_LOAD_OK                 = 0,
    SBF_LOAD_ERR_ALLOC          = 1,    /**< Memory allocation failed */
    SBF_LOAD_ERR_IO             = 2,    /**< File I/O error */
    SBF_LOAD_ERR_TRUNCATED      = 3,    /**< File is truncated */
    SBF_LOAD_ERR_INVALID_MAGIC  = 4,    /**< Invalid magic number */
    SBF_LOAD_ERR_INVALID_VERSION= 5,    /**< Unsupported version */
    SBF_LOAD_ERR_HASH_MISMATCH  = 6,    /**< Content hash mismatch */
    SBF_LOAD_ERR_PROOF_ROOT     = 7,    /**< Proof Merkle root mismatch */
    SBF_LOAD_ERR_PROOF_FAILED   = 8,    /**< Binary has failed proofs */
    SBF_LOAD_ERR_MANIFEST       = 9,    /**< Invalid manifest */
    SBF_LOAD_ERR_SIGNATURE      = 10,   /**< Signature verification failed */
    SBF_LOAD_ERR_SECTION        = 11,   /**< Invalid section bounds */
    SBF_LOAD_ERR_NOT_LOADED     = 12,   /**< No binary loaded */
    SBF_LOAD_ERR_ALREADY_LOADED = 13,   /**< Binary already loaded */
} SBF_Load_Error;

/*============================================================================
 * Loader Configuration
 *============================================================================*/

/**
 * @brief Configuration for SBF loader
 */
struct SBF_Loader_Config {
    bool verify_content_hash;           /**< Verify content SHA-256 (default: true) */
    bool verify_proof_root;             /**< Verify proof Merkle root (default: true) */
    bool verify_signature;              /**< Verify Ed25519 signature (default: true) */
    bool reject_failed_proofs;          /**< Reject if any proofs failed (default: true) */
    bool require_signed;                /**< Require signed binaries (default: false) */
    uint32_t min_kernel_version;        /**< Minimum kernel version to accept (0 = any) */
    uint32_t max_kernel_version;        /**< Maximum kernel version to accept (0 = any) */
};

/*============================================================================
 * Loader Context
 *============================================================================*/

/**
 * @brief SBF Loader context
 *
 * Maintains state for loading and validating an SBF binary.
 */
struct SBF_Loader {
    /* Configuration */
    SBF_Loader_Config config;

    /* State */
    bool loaded;
    SBF_Load_Error last_error;
    SBF_Validation_Result validation_result;

    /* Raw binary data */
    uint8_t* data;
    size_t data_size;
    bool owns_data;                     /**< True if loader allocated data */

    /* Parsed pointers (point into data) */
    const SBF_Header* header;
    const SBF_Manifest* manifest;
    const uint8_t* code;
    const uint8_t* rodata;
    const uint8_t* data_section;
    const SBF_Proof_Table* proof_table;
    const SBF_Proof_Entry* proofs;
    const SBF_Cap_Table* cap_table;
    const SBF_Cap_Template* caps;
    const SBF_Effect_Table* effect_table;
    const SBF_Effect_Entry* effects;
    const SBF_String_Table* string_table;
    const char* strings;

    /* Computed values */
    uint8_t computed_content_hash[SBF_HASH_SIZE];
    uint8_t computed_proof_root[SBF_HASH_SIZE];
};

/*============================================================================
 * Loader Lifecycle
 *============================================================================*/

/**
 * @brief Create a new SBF loader with default configuration
 * @return New loader instance or NULL on failure
 */
SBF_Loader* sbf_loader_create(void);

/**
 * @brief Create a new SBF loader with custom configuration
 * @param config Loader configuration
 * @return New loader instance or NULL on failure
 */
SBF_Loader* sbf_loader_create_with_config(const SBF_Loader_Config* config);

/**
 * @brief Destroy an SBF loader and free all resources
 * @param loader Loader to destroy
 */
void sbf_loader_destroy(SBF_Loader* loader);

/**
 * @brief Get the last error from the loader
 * @param loader Loader instance
 * @return Last error code
 */
SBF_Load_Error sbf_loader_get_error(const SBF_Loader* loader);

/**
 * @brief Get human-readable error message
 * @param error Error code
 * @return Static string describing the error
 */
const char* sbf_load_error_name(SBF_Load_Error error);

/*============================================================================
 * Loading
 *============================================================================*/

/**
 * @brief Load SBF binary from file
 * @param loader Loader instance
 * @param path File path
 * @return SBF_LOAD_OK or error
 */
SBF_Load_Error sbf_loader_load_file(SBF_Loader* loader, const char* path);

/**
 * @brief Load SBF binary from memory buffer
 *
 * The buffer is copied if copy is true, otherwise the loader
 * takes a reference (buffer must remain valid until loader is destroyed).
 *
 * @param loader Loader instance
 * @param data Binary data
 * @param size Size in bytes
 * @param copy If true, copy the data; if false, reference it
 * @return SBF_LOAD_OK or error
 */
SBF_Load_Error sbf_loader_load_buffer(
    SBF_Loader* loader,
    const void* data,
    size_t size,
    bool copy
);

/**
 * @brief Unload the current binary
 * @param loader Loader instance
 */
void sbf_loader_unload(SBF_Loader* loader);

/**
 * @brief Check if a binary is loaded
 * @param loader Loader instance
 * @return true if loaded
 */
bool sbf_loader_is_loaded(const SBF_Loader* loader);

/*============================================================================
 * Validation
 *============================================================================*/

/**
 * @brief Validate the loaded binary
 *
 * Performs all validation according to configuration:
 * - Magic and version check
 * - Content hash verification (optional)
 * - Proof Merkle root verification (optional)
 * - Signature verification (optional)
 * - Section bounds check
 *
 * @param loader Loader instance
 * @return SBF_VALID or error code
 */
SBF_Validation_Result sbf_loader_validate(SBF_Loader* loader);

/**
 * @brief Quick validation (magic and version only)
 * @param loader Loader instance
 * @return SBF_VALID or error code
 */
SBF_Validation_Result sbf_loader_validate_quick(const SBF_Loader* loader);

/**
 * @brief Verify content hash
 * @param loader Loader instance
 * @return true if hash matches
 */
bool sbf_loader_verify_content_hash(SBF_Loader* loader);

/**
 * @brief Verify proof Merkle root
 * @param loader Loader instance
 * @return true if root matches
 */
bool sbf_loader_verify_proof_root(SBF_Loader* loader);

/**
 * @brief Check if binary has any failed proofs
 * @param loader Loader instance
 * @return true if any proofs failed
 */
bool sbf_loader_has_failed_proofs(const SBF_Loader* loader);

/**
 * @brief Get the last validation result
 * @param loader Loader instance
 * @return Validation result
 */
SBF_Validation_Result sbf_loader_get_validation_result(const SBF_Loader* loader);

/*============================================================================
 * Header Access
 *============================================================================*/

/**
 * @brief Get the binary header
 * @param loader Loader instance
 * @return Pointer to header or NULL if not loaded
 */
const SBF_Header* sbf_loader_get_header(const SBF_Loader* loader);

/**
 * @brief Get the binary manifest
 * @param loader Loader instance
 * @return Pointer to manifest or NULL if not loaded
 */
const SBF_Manifest* sbf_loader_get_manifest(const SBF_Loader* loader);

/**
 * @brief Get target architecture
 * @param loader Loader instance
 * @return Architecture enum
 */
SBF_Architecture sbf_loader_get_architecture(const SBF_Loader* loader);

/**
 * @brief Get binary flags
 * @param loader Loader instance
 * @return Flags
 */
uint32_t sbf_loader_get_flags(const SBF_Loader* loader);

/**
 * @brief Get entry point address
 * @param loader Loader instance
 * @return Entry point
 */
uint64_t sbf_loader_get_entry_point(const SBF_Loader* loader);

/*============================================================================
 * Section Access
 *============================================================================*/

/**
 * @brief Get code section
 * @param loader Loader instance
 * @param out_size Output parameter for size
 * @return Pointer to code or NULL
 */
const void* sbf_loader_get_code(const SBF_Loader* loader, size_t* out_size);

/**
 * @brief Get read-only data section
 * @param loader Loader instance
 * @param out_size Output parameter for size
 * @return Pointer to rodata or NULL
 */
const void* sbf_loader_get_rodata(const SBF_Loader* loader, size_t* out_size);

/**
 * @brief Get data section
 * @param loader Loader instance
 * @param out_size Output parameter for size
 * @return Pointer to data or NULL
 */
const void* sbf_loader_get_data(const SBF_Loader* loader, size_t* out_size);

/**
 * @brief Get BSS size (uninitialized data)
 * @param loader Loader instance
 * @return BSS size in bytes
 */
uint64_t sbf_loader_get_bss_size(const SBF_Loader* loader);

/*============================================================================
 * Proof Table Access
 *============================================================================*/

/**
 * @brief Get proof table header
 * @param loader Loader instance
 * @return Pointer to proof table or NULL
 */
const SBF_Proof_Table* sbf_loader_get_proof_table(const SBF_Loader* loader);

/**
 * @brief Get proof entry by index
 * @param loader Loader instance
 * @param index Proof index
 * @return Pointer to proof entry or NULL if out of bounds
 */
const SBF_Proof_Entry* sbf_loader_get_proof(const SBF_Loader* loader, size_t index);

/**
 * @brief Get number of proofs
 * @param loader Loader instance
 * @return Number of proof entries
 */
size_t sbf_loader_get_proof_count(const SBF_Loader* loader);

/**
 * @brief Get number of proven proofs
 * @param loader Loader instance
 * @return Count of proofs with PROVEN status
 */
size_t sbf_loader_get_proven_count(const SBF_Loader* loader);

/**
 * @brief Get number of failed proofs
 * @param loader Loader instance
 * @return Count of proofs with FAILED status
 */
size_t sbf_loader_get_failed_count(const SBF_Loader* loader);

/*============================================================================
 * Capability Table Access
 *============================================================================*/

/**
 * @brief Get capability table header
 * @param loader Loader instance
 * @return Pointer to capability table or NULL
 */
const SBF_Cap_Table* sbf_loader_get_cap_table(const SBF_Loader* loader);

/**
 * @brief Get capability template by index
 * @param loader Loader instance
 * @param index Capability index
 * @return Pointer to capability template or NULL if out of bounds
 */
const SBF_Cap_Template* sbf_loader_get_capability(const SBF_Loader* loader, size_t index);

/**
 * @brief Get number of capabilities
 * @param loader Loader instance
 * @return Number of capability templates
 */
size_t sbf_loader_get_cap_count(const SBF_Loader* loader);

/**
 * @brief Get code section capability
 * @param loader Loader instance
 * @return Pointer to code capability or NULL
 */
const SBF_Cap_Template* sbf_loader_get_code_cap(const SBF_Loader* loader);

/**
 * @brief Get data section capability
 * @param loader Loader instance
 * @return Pointer to data capability or NULL
 */
const SBF_Cap_Template* sbf_loader_get_data_cap(const SBF_Loader* loader);

/**
 * @brief Get stack capability
 * @param loader Loader instance
 * @return Pointer to stack capability or NULL
 */
const SBF_Cap_Template* sbf_loader_get_stack_cap(const SBF_Loader* loader);

/*============================================================================
 * Effect Table Access
 *============================================================================*/

/**
 * @brief Get effect table header
 * @param loader Loader instance
 * @return Pointer to effect table or NULL
 */
const SBF_Effect_Table* sbf_loader_get_effect_table(const SBF_Loader* loader);

/**
 * @brief Get effect entry by index
 * @param loader Loader instance
 * @param index Effect index
 * @return Pointer to effect entry or NULL if out of bounds
 */
const SBF_Effect_Entry* sbf_loader_get_effect(const SBF_Loader* loader, size_t index);

/**
 * @brief Get number of effects
 * @param loader Loader instance
 * @return Number of effect entries
 */
size_t sbf_loader_get_effect_count(const SBF_Loader* loader);

/**
 * @brief Get declared effect mask for all functions
 * @param loader Loader instance
 * @return Combined effect mask
 */
uint32_t sbf_loader_get_effect_mask(const SBF_Loader* loader);

/*============================================================================
 * String Table Access
 *============================================================================*/

/**
 * @brief Get string from string table by offset
 * @param loader Loader instance
 * @param offset String offset
 * @return Pointer to null-terminated string or NULL
 */
const char* sbf_loader_get_string(const SBF_Loader* loader, uint32_t offset);

/**
 * @brief Get proof location string
 * @param loader Loader instance
 * @param proof Proof entry
 * @return Location string or NULL
 */
const char* sbf_loader_get_proof_location(const SBF_Loader* loader, const SBF_Proof_Entry* proof);

/**
 * @brief Get proof description string
 * @param loader Loader instance
 * @param proof Proof entry
 * @return Description string or NULL
 */
const char* sbf_loader_get_proof_description(const SBF_Loader* loader, const SBF_Proof_Entry* proof);

/**
 * @brief Get capability name string
 * @param loader Loader instance
 * @param cap Capability template
 * @return Name string or NULL
 */
const char* sbf_loader_get_cap_name(const SBF_Loader* loader, const SBF_Cap_Template* cap);

/**
 * @brief Get effect function name string
 * @param loader Loader instance
 * @param effect Effect entry
 * @return Function name or NULL
 */
const char* sbf_loader_get_effect_name(const SBF_Loader* loader, const SBF_Effect_Entry* effect);

/*============================================================================
 * Manifest Requirements
 *============================================================================*/

/**
 * @brief Get required stack size per strand
 * @param loader Loader instance
 * @return Stack size in bytes
 */
uint64_t sbf_loader_get_required_stack(const SBF_Loader* loader);

/**
 * @brief Get required heap size
 * @param loader Loader instance
 * @return Heap size in bytes
 */
uint64_t sbf_loader_get_required_heap(const SBF_Loader* loader);

/**
 * @brief Get chronon budget
 * @param loader Loader instance
 * @return Chronon budget
 */
uint64_t sbf_loader_get_chronon_budget(const SBF_Loader* loader);

/**
 * @brief Check if binary requires Atlas access
 * @param loader Loader instance
 * @return true if Atlas required
 */
bool sbf_loader_requires_atlas(const SBF_Loader* loader);

/**
 * @brief Check if binary requires Aether access
 * @param loader Loader instance
 * @return true if Aether required
 */
bool sbf_loader_requires_aether(const SBF_Loader* loader);

/*============================================================================
 * Debug Utilities
 *============================================================================*/

/**
 * @brief Dump loader state for debugging
 * @param loader Loader instance
 * @param buf Output buffer
 * @param buf_size Buffer size
 * @return Number of bytes written
 */
size_t sbf_loader_dump(const SBF_Loader* loader, char* buf, size_t buf_size);

/**
 * @brief Print proof table summary
 * @param loader Loader instance
 * @param buf Output buffer
 * @param buf_size Buffer size
 * @return Number of bytes written
 */
size_t sbf_loader_dump_proofs(const SBF_Loader* loader, char* buf, size_t buf_size);

/**
 * @brief Print capability table
 * @param loader Loader instance
 * @param buf Output buffer
 * @param buf_size Buffer size
 * @return Number of bytes written
 */
size_t sbf_loader_dump_caps(const SBF_Loader* loader, char* buf, size_t buf_size);

/**
 * @brief Print effect table
 * @param loader Loader instance
 * @param buf Output buffer
 * @param buf_size Buffer size
 * @return Number of bytes written
 */
size_t sbf_loader_dump_effects(const SBF_Loader* loader, char* buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_SBF_LOADER_H */
