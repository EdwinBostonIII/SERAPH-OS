/**
 * @file sbf_loader.c
 * @brief SERAPH Binary Format (SBF) Loader Implementation
 *
 * Loads and validates SBF binaries for execution.
 */

#include "seraph/seraphim/sbf_loader.h"
#include "seraph/crypto/sha256.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*============================================================================
 * Default Configuration
 *============================================================================*/

static const SBF_Loader_Config default_config = {
    .verify_content_hash = true,
    .verify_proof_root = true,
    .verify_signature = true,
    .reject_failed_proofs = true,
    .require_signed = false,
    .min_kernel_version = 0,
    .max_kernel_version = 0,
};

/*============================================================================
 * Loader Lifecycle
 *============================================================================*/

SBF_Loader* sbf_loader_create(void) {
    return sbf_loader_create_with_config(&default_config);
}

SBF_Loader* sbf_loader_create_with_config(const SBF_Loader_Config* config) {
    if (config == NULL) config = &default_config;

    SBF_Loader* loader = (SBF_Loader*)calloc(1, sizeof(SBF_Loader));
    if (loader == NULL) return NULL;

    loader->config = *config;
    loader->loaded = false;
    loader->last_error = SBF_LOAD_OK;
    loader->validation_result = SBF_VALID;

    return loader;
}

void sbf_loader_destroy(SBF_Loader* loader) {
    if (loader == NULL) return;

    sbf_loader_unload(loader);
    memset(loader, 0, sizeof(*loader));
    free(loader);
}

SBF_Load_Error sbf_loader_get_error(const SBF_Loader* loader) {
    if (loader == NULL) return SBF_LOAD_ERR_ALLOC;
    return loader->last_error;
}

const char* sbf_load_error_name(SBF_Load_Error error) {
    switch (error) {
        case SBF_LOAD_OK:               return "OK";
        case SBF_LOAD_ERR_ALLOC:        return "Memory allocation failed";
        case SBF_LOAD_ERR_IO:           return "File I/O error";
        case SBF_LOAD_ERR_TRUNCATED:    return "File truncated";
        case SBF_LOAD_ERR_INVALID_MAGIC:return "Invalid magic number";
        case SBF_LOAD_ERR_INVALID_VERSION:return "Unsupported version";
        case SBF_LOAD_ERR_HASH_MISMATCH:return "Content hash mismatch";
        case SBF_LOAD_ERR_PROOF_ROOT:   return "Proof Merkle root mismatch";
        case SBF_LOAD_ERR_PROOF_FAILED: return "Binary has failed proofs";
        case SBF_LOAD_ERR_MANIFEST:     return "Invalid manifest";
        case SBF_LOAD_ERR_SIGNATURE:    return "Signature verification failed";
        case SBF_LOAD_ERR_SECTION:      return "Invalid section bounds";
        case SBF_LOAD_ERR_NOT_LOADED:   return "No binary loaded";
        case SBF_LOAD_ERR_ALREADY_LOADED:return "Binary already loaded";
        default:                         return "Unknown error";
    }
}

/*============================================================================
 * Loading
 *============================================================================*/

SBF_Load_Error sbf_loader_load_file(SBF_Loader* loader, const char* path) {
    if (loader == NULL) return SBF_LOAD_ERR_ALLOC;
    if (loader->loaded) return (loader->last_error = SBF_LOAD_ERR_ALREADY_LOADED);
    if (path == NULL) return (loader->last_error = SBF_LOAD_ERR_IO);

    /* Open file */
    FILE* f = fopen(path, "rb");
    if (f == NULL) return (loader->last_error = SBF_LOAD_ERR_IO);

    /* Get file size */
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return (loader->last_error = SBF_LOAD_ERR_IO);
    }
    long size = ftell(f);
    if (size < 0 || (size_t)size < SBF_HEADER_SIZE) {
        fclose(f);
        return (loader->last_error = SBF_LOAD_ERR_TRUNCATED);
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return (loader->last_error = SBF_LOAD_ERR_IO);
    }

    /* Allocate buffer */
    loader->data = (uint8_t*)malloc((size_t)size);
    if (loader->data == NULL) {
        fclose(f);
        return (loader->last_error = SBF_LOAD_ERR_ALLOC);
    }

    /* Read file */
    size_t read = fread(loader->data, 1, (size_t)size, f);
    fclose(f);

    if (read != (size_t)size) {
        free(loader->data);
        loader->data = NULL;
        return (loader->last_error = SBF_LOAD_ERR_IO);
    }

    loader->data_size = (size_t)size;
    loader->owns_data = true;

    /* Parse pointers */
    return sbf_loader_load_buffer(loader, loader->data, loader->data_size, false);
}

static bool parse_sections(SBF_Loader* loader) {
    const SBF_Header* hdr = loader->header;

    /* Validate section bounds */
    #define CHECK_SECTION(offset, size) \
        if ((offset) != 0 && ((offset) + (size) > loader->data_size)) return false

    CHECK_SECTION(hdr->manifest_offset, hdr->manifest_size);
    CHECK_SECTION(hdr->code_offset, hdr->code_size);
    CHECK_SECTION(hdr->rodata_offset, hdr->rodata_size);
    CHECK_SECTION(hdr->data_offset, hdr->data_size);
    CHECK_SECTION(hdr->proofs_offset, hdr->proofs_size);
    CHECK_SECTION(hdr->caps_offset, hdr->caps_size);
    CHECK_SECTION(hdr->effects_offset, hdr->effects_size);
    CHECK_SECTION(hdr->strings_offset, hdr->strings_size);
    #undef CHECK_SECTION

    /* Parse manifest */
    if (hdr->manifest_offset != 0 && hdr->manifest_size >= sizeof(SBF_Manifest)) {
        loader->manifest = (const SBF_Manifest*)(loader->data + hdr->manifest_offset);
        if (loader->manifest->magic != SBF_MANIFEST_MAGIC) return false;
    }

    /* Parse code */
    if (hdr->code_offset != 0 && hdr->code_size > 0) {
        loader->code = loader->data + hdr->code_offset;
    }

    /* Parse rodata */
    if (hdr->rodata_offset != 0 && hdr->rodata_size > 0) {
        loader->rodata = loader->data + hdr->rodata_offset;
    }

    /* Parse data */
    if (hdr->data_offset != 0 && hdr->data_size > 0) {
        loader->data_section = loader->data + hdr->data_offset;
    }

    /* Parse proof table */
    if (hdr->proofs_offset != 0 && hdr->proofs_size >= sizeof(SBF_Proof_Table)) {
        loader->proof_table = (const SBF_Proof_Table*)(loader->data + hdr->proofs_offset);
        if (loader->proof_table->magic != SBF_PROOF_MAGIC) return false;
        if (loader->proof_table->entry_count > 0) {
            loader->proofs = (const SBF_Proof_Entry*)(
                (const uint8_t*)loader->proof_table + sizeof(SBF_Proof_Table));
        }
    }

    /* Parse capability table */
    if (hdr->caps_offset != 0 && hdr->caps_size >= sizeof(SBF_Cap_Table)) {
        loader->cap_table = (const SBF_Cap_Table*)(loader->data + hdr->caps_offset);
        if (loader->cap_table->magic != SBF_CAP_MAGIC) return false;
        if (loader->cap_table->entry_count > 0) {
            loader->caps = (const SBF_Cap_Template*)(
                (const uint8_t*)loader->cap_table + sizeof(SBF_Cap_Table));
        }
    }

    /* Parse effect table */
    if (hdr->effects_offset != 0 && hdr->effects_size >= sizeof(SBF_Effect_Table)) {
        loader->effect_table = (const SBF_Effect_Table*)(loader->data + hdr->effects_offset);
        if (loader->effect_table->magic != SBF_EFFECT_MAGIC) return false;
        if (loader->effect_table->entry_count > 0) {
            loader->effects = (const SBF_Effect_Entry*)(
                (const uint8_t*)loader->effect_table + sizeof(SBF_Effect_Table));
        }
    }

    /* Parse string table */
    if (hdr->strings_offset != 0 && hdr->strings_size >= sizeof(SBF_String_Table)) {
        loader->string_table = (const SBF_String_Table*)(loader->data + hdr->strings_offset);
        if (loader->string_table->magic != SBF_STRING_MAGIC) return false;
        loader->strings = (const char*)(
            (const uint8_t*)loader->string_table + sizeof(SBF_String_Table));
    }

    return true;
}

SBF_Load_Error sbf_loader_load_buffer(
    SBF_Loader* loader,
    const void* data,
    size_t size,
    bool copy
) {
    if (loader == NULL) return SBF_LOAD_ERR_ALLOC;
    if (data == NULL) return (loader->last_error = SBF_LOAD_ERR_IO);
    if (size < SBF_HEADER_SIZE) return (loader->last_error = SBF_LOAD_ERR_TRUNCATED);

    /* Handle case where we already have data (called from load_file) */
    if (loader->data == NULL) {
        if (loader->loaded) return (loader->last_error = SBF_LOAD_ERR_ALREADY_LOADED);

        if (copy) {
            loader->data = (uint8_t*)malloc(size);
            if (loader->data == NULL) return (loader->last_error = SBF_LOAD_ERR_ALLOC);
            memcpy(loader->data, data, size);
            loader->owns_data = true;
        } else {
            loader->data = (uint8_t*)data;  /* Cast away const - we won't modify */
            loader->owns_data = false;
        }
        loader->data_size = size;
    }

    /* Parse header */
    loader->header = (const SBF_Header*)loader->data;

    /* Quick validation */
    if (loader->header->magic != SBF_MAGIC) {
        sbf_loader_unload(loader);
        return (loader->last_error = SBF_LOAD_ERR_INVALID_MAGIC);
    }
    if ((loader->header->version >> 16) != SBF_VERSION_MAJOR) {
        sbf_loader_unload(loader);
        return (loader->last_error = SBF_LOAD_ERR_INVALID_VERSION);
    }
    if (loader->header->total_size > size) {
        sbf_loader_unload(loader);
        return (loader->last_error = SBF_LOAD_ERR_TRUNCATED);
    }

    /* Parse sections */
    if (!parse_sections(loader)) {
        sbf_loader_unload(loader);
        return (loader->last_error = SBF_LOAD_ERR_SECTION);
    }

    loader->loaded = true;
    loader->last_error = SBF_LOAD_OK;
    return SBF_LOAD_OK;
}

void sbf_loader_unload(SBF_Loader* loader) {
    if (loader == NULL) return;

    if (loader->owns_data && loader->data != NULL) {
        memset(loader->data, 0, loader->data_size);
        free(loader->data);
    }

    loader->data = NULL;
    loader->data_size = 0;
    loader->owns_data = false;
    loader->loaded = false;
    loader->header = NULL;
    loader->manifest = NULL;
    loader->code = NULL;
    loader->rodata = NULL;
    loader->data_section = NULL;
    loader->proof_table = NULL;
    loader->proofs = NULL;
    loader->cap_table = NULL;
    loader->caps = NULL;
    loader->effect_table = NULL;
    loader->effects = NULL;
    loader->string_table = NULL;
    loader->strings = NULL;
    memset(loader->computed_content_hash, 0, sizeof(loader->computed_content_hash));
    memset(loader->computed_proof_root, 0, sizeof(loader->computed_proof_root));
}

bool sbf_loader_is_loaded(const SBF_Loader* loader) {
    if (loader == NULL) return false;
    return loader->loaded;
}

/*============================================================================
 * Validation
 *============================================================================*/

SBF_Validation_Result sbf_loader_validate(SBF_Loader* loader) {
    if (loader == NULL) return SBF_ERR_INVALID_MAGIC;
    if (!loader->loaded) {
        loader->last_error = SBF_LOAD_ERR_NOT_LOADED;
        return SBF_ERR_INVALID_MAGIC;
    }

    /* Quick validation first */
    SBF_Validation_Result result = sbf_loader_validate_quick(loader);
    if (result != SBF_VALID) {
        loader->validation_result = result;
        return result;
    }

    /* Verify manifest */
    if (loader->manifest == NULL) {
        loader->validation_result = SBF_ERR_MANIFEST_INVALID;
        loader->last_error = SBF_LOAD_ERR_MANIFEST;
        return SBF_ERR_MANIFEST_INVALID;
    }

    /* Check kernel version requirements */
    if (loader->config.min_kernel_version > 0) {
        if (loader->manifest->kernel_max_version > 0 &&
            loader->manifest->kernel_max_version < loader->config.min_kernel_version) {
            loader->validation_result = SBF_ERR_INVALID_VERSION;
            loader->last_error = SBF_LOAD_ERR_INVALID_VERSION;
            return SBF_ERR_INVALID_VERSION;
        }
    }
    if (loader->config.max_kernel_version > 0) {
        if (loader->manifest->kernel_min_version > loader->config.max_kernel_version) {
            loader->validation_result = SBF_ERR_INVALID_VERSION;
            loader->last_error = SBF_LOAD_ERR_INVALID_VERSION;
            return SBF_ERR_INVALID_VERSION;
        }
    }

    /* Verify content hash if configured */
    if (loader->config.verify_content_hash) {
        if (!sbf_loader_verify_content_hash(loader)) {
            loader->validation_result = SBF_ERR_HASH_MISMATCH;
            loader->last_error = SBF_LOAD_ERR_HASH_MISMATCH;
            return SBF_ERR_HASH_MISMATCH;
        }
    }

    /* Verify proof Merkle root if configured */
    if (loader->config.verify_proof_root && loader->proof_table != NULL) {
        if (!sbf_loader_verify_proof_root(loader)) {
            loader->validation_result = SBF_ERR_PROOF_ROOT_MISMATCH;
            loader->last_error = SBF_LOAD_ERR_PROOF_ROOT;
            return SBF_ERR_PROOF_ROOT_MISMATCH;
        }
    }

    /* Check for failed proofs if configured */
    if (loader->config.reject_failed_proofs && sbf_loader_has_failed_proofs(loader)) {
        loader->validation_result = SBF_ERR_PROOF_FAILED;
        loader->last_error = SBF_LOAD_ERR_PROOF_FAILED;
        return SBF_ERR_PROOF_FAILED;
    }

    /* Require signature if configured */
    if (loader->config.require_signed) {
        if (!(loader->header->flags & SBF_FLAG_SIGNED)) {
            loader->validation_result = SBF_ERR_SIGNATURE_INVALID;
            loader->last_error = SBF_LOAD_ERR_SIGNATURE;
            return SBF_ERR_SIGNATURE_INVALID;
        }
    }

    /* TODO: Verify signature if configured and binary is signed */
    if (loader->config.verify_signature && (loader->header->flags & SBF_FLAG_SIGNED)) {
        /* Ed25519 verification would go here */
    }

    loader->validation_result = SBF_VALID;
    return SBF_VALID;
}

SBF_Validation_Result sbf_loader_validate_quick(const SBF_Loader* loader) {
    if (loader == NULL) return SBF_ERR_INVALID_MAGIC;
    if (!loader->loaded || loader->header == NULL) return SBF_ERR_INVALID_MAGIC;
    return sbf_validate_header_quick(loader->header);
}

bool sbf_loader_verify_content_hash(SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded) return false;
    if (loader->data_size <= SBF_HEADER_SIZE) return false;

    /* Compute hash of everything after header */
    sha256(loader->data + SBF_HEADER_SIZE,
           loader->data_size - SBF_HEADER_SIZE,
           loader->computed_content_hash);

    return sha256_equal(loader->computed_content_hash, loader->header->content_hash);
}

bool sbf_loader_verify_proof_root(SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded) return false;
    if (loader->proof_table == NULL) {
        /* No proofs - root should be zeros */
        return sha256_is_zero(loader->header->proof_root);
    }

    uint32_t count = loader->proof_table->entry_count;
    if (count == 0) {
        return sha256_is_zero(loader->header->proof_root);
    }

    /* Allocate leaf array */
    uint8_t* leaves = (uint8_t*)malloc(count * SHA256_DIGEST_SIZE);
    if (leaves == NULL) return false;

    /* Copy proof hashes */
    for (uint32_t i = 0; i < count; i++) {
        memcpy(&leaves[i * SHA256_DIGEST_SIZE], loader->proofs[i].hash, SHA256_DIGEST_SIZE);
    }

    /* Compute Merkle root */
    int result = sha256_merkle_root_alloc(leaves, count, loader->computed_proof_root);
    free(leaves);

    if (result != 1) return false;

    /* Compare with stored root */
    return sha256_equal(loader->computed_proof_root, loader->header->proof_root);
}

bool sbf_loader_has_failed_proofs(const SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded || loader->proof_table == NULL) return false;
    return loader->proof_table->failed_count > 0;
}

SBF_Validation_Result sbf_loader_get_validation_result(const SBF_Loader* loader) {
    if (loader == NULL) return SBF_ERR_INVALID_MAGIC;
    return loader->validation_result;
}

/*============================================================================
 * Header Access
 *============================================================================*/

const SBF_Header* sbf_loader_get_header(const SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded) return NULL;
    return loader->header;
}

const SBF_Manifest* sbf_loader_get_manifest(const SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded) return NULL;
    return loader->manifest;
}

SBF_Architecture sbf_loader_get_architecture(const SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded || loader->header == NULL) return SBF_ARCH_X64;
    return (SBF_Architecture)loader->header->architecture;
}

uint32_t sbf_loader_get_flags(const SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded || loader->header == NULL) return 0;
    return loader->header->flags;
}

uint64_t sbf_loader_get_entry_point(const SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded || loader->header == NULL) return 0;
    return loader->header->entry_point;
}

/*============================================================================
 * Section Access
 *============================================================================*/

const void* sbf_loader_get_code(const SBF_Loader* loader, size_t* out_size) {
    if (loader == NULL || !loader->loaded) {
        if (out_size) *out_size = 0;
        return NULL;
    }
    if (out_size) *out_size = (size_t)loader->header->code_size;
    return loader->code;
}

const void* sbf_loader_get_rodata(const SBF_Loader* loader, size_t* out_size) {
    if (loader == NULL || !loader->loaded) {
        if (out_size) *out_size = 0;
        return NULL;
    }
    if (out_size) *out_size = (size_t)loader->header->rodata_size;
    return loader->rodata;
}

const void* sbf_loader_get_data(const SBF_Loader* loader, size_t* out_size) {
    if (loader == NULL || !loader->loaded) {
        if (out_size) *out_size = 0;
        return NULL;
    }
    if (out_size) *out_size = (size_t)loader->header->data_size;
    return loader->data_section;
}

uint64_t sbf_loader_get_bss_size(const SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded || loader->header == NULL) return 0;
    return loader->header->bss_size;
}

/*============================================================================
 * Proof Table Access
 *============================================================================*/

const SBF_Proof_Table* sbf_loader_get_proof_table(const SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded) return NULL;
    return loader->proof_table;
}

const SBF_Proof_Entry* sbf_loader_get_proof(const SBF_Loader* loader, size_t index) {
    if (loader == NULL || !loader->loaded || loader->proofs == NULL) return NULL;
    if (loader->proof_table == NULL || index >= loader->proof_table->entry_count) return NULL;
    return &loader->proofs[index];
}

size_t sbf_loader_get_proof_count(const SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded || loader->proof_table == NULL) return 0;
    return loader->proof_table->entry_count;
}

size_t sbf_loader_get_proven_count(const SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded || loader->proof_table == NULL) return 0;
    return loader->proof_table->proven_count;
}

size_t sbf_loader_get_failed_count(const SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded || loader->proof_table == NULL) return 0;
    return loader->proof_table->failed_count;
}

/*============================================================================
 * Capability Table Access
 *============================================================================*/

const SBF_Cap_Table* sbf_loader_get_cap_table(const SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded) return NULL;
    return loader->cap_table;
}

const SBF_Cap_Template* sbf_loader_get_capability(const SBF_Loader* loader, size_t index) {
    if (loader == NULL || !loader->loaded || loader->caps == NULL) return NULL;
    if (loader->cap_table == NULL || index >= loader->cap_table->entry_count) return NULL;
    return &loader->caps[index];
}

size_t sbf_loader_get_cap_count(const SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded || loader->cap_table == NULL) return 0;
    return loader->cap_table->entry_count;
}

const SBF_Cap_Template* sbf_loader_get_code_cap(const SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded || loader->cap_table == NULL) return NULL;
    return sbf_loader_get_capability(loader, loader->cap_table->code_cap_idx);
}

const SBF_Cap_Template* sbf_loader_get_data_cap(const SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded || loader->cap_table == NULL) return NULL;
    return sbf_loader_get_capability(loader, loader->cap_table->data_cap_idx);
}

const SBF_Cap_Template* sbf_loader_get_stack_cap(const SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded || loader->cap_table == NULL) return NULL;
    return sbf_loader_get_capability(loader, loader->cap_table->stack_cap_idx);
}

/*============================================================================
 * Effect Table Access
 *============================================================================*/

const SBF_Effect_Table* sbf_loader_get_effect_table(const SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded) return NULL;
    return loader->effect_table;
}

const SBF_Effect_Entry* sbf_loader_get_effect(const SBF_Loader* loader, size_t index) {
    if (loader == NULL || !loader->loaded || loader->effects == NULL) return NULL;
    if (loader->effect_table == NULL || index >= loader->effect_table->entry_count) return NULL;
    return &loader->effects[index];
}

size_t sbf_loader_get_effect_count(const SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded || loader->effect_table == NULL) return 0;
    return loader->effect_table->entry_count;
}

uint32_t sbf_loader_get_effect_mask(const SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded || loader->manifest == NULL) return 0;
    return loader->manifest->effect_mask;
}

/*============================================================================
 * String Table Access
 *============================================================================*/

const char* sbf_loader_get_string(const SBF_Loader* loader, uint32_t offset) {
    if (loader == NULL || !loader->loaded || loader->strings == NULL) return NULL;
    if (loader->string_table == NULL) return NULL;

    /* Check bounds */
    size_t string_data_size = loader->string_table->total_size - sizeof(SBF_String_Table);
    if (offset >= string_data_size) return NULL;

    return &loader->strings[offset];
}

const char* sbf_loader_get_proof_location(const SBF_Loader* loader, const SBF_Proof_Entry* proof) {
    if (proof == NULL || proof->location == 0) return NULL;
    return sbf_loader_get_string(loader, proof->location);
}

const char* sbf_loader_get_proof_description(const SBF_Loader* loader, const SBF_Proof_Entry* proof) {
    if (proof == NULL || proof->description == 0) return NULL;
    return sbf_loader_get_string(loader, proof->description);
}

const char* sbf_loader_get_cap_name(const SBF_Loader* loader, const SBF_Cap_Template* cap) {
    if (cap == NULL || cap->name_offset == 0) return NULL;
    return sbf_loader_get_string(loader, cap->name_offset);
}

const char* sbf_loader_get_effect_name(const SBF_Loader* loader, const SBF_Effect_Entry* effect) {
    if (effect == NULL || effect->name_offset == 0) return NULL;
    return sbf_loader_get_string(loader, effect->name_offset);
}

/*============================================================================
 * Manifest Requirements
 *============================================================================*/

uint64_t sbf_loader_get_required_stack(const SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded || loader->manifest == NULL) return 0;
    return loader->manifest->stack_size;
}

uint64_t sbf_loader_get_required_heap(const SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded || loader->manifest == NULL) return 0;
    return loader->manifest->heap_size;
}

uint64_t sbf_loader_get_chronon_budget(const SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded || loader->manifest == NULL) return 0;
    return loader->manifest->chronon_budget;
}

bool sbf_loader_requires_atlas(const SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded || loader->manifest == NULL) return false;
    return (loader->manifest->atlas_flags & SBF_ATLAS_FLAG_REQUIRED) != 0;
}

bool sbf_loader_requires_aether(const SBF_Loader* loader) {
    if (loader == NULL || !loader->loaded || loader->manifest == NULL) return false;
    return (loader->manifest->aether_flags & SBF_AETHER_FLAG_REQUIRED) != 0;
}

/*============================================================================
 * Debug Utilities
 *============================================================================*/

size_t sbf_loader_dump(const SBF_Loader* loader, char* buf, size_t buf_size) {
    if (loader == NULL || buf == NULL || buf_size == 0) return 0;

    if (!loader->loaded) {
        return (size_t)snprintf(buf, buf_size, "SBF Loader: Not loaded\n");
    }

    char version_str[12];
    sbf_version_to_string(loader->header->version, version_str, sizeof(version_str));

    int n = snprintf(buf, buf_size,
        "SBF Loader State:\n"
        "  Version: %s\n"
        "  Architecture: %s\n"
        "  Flags: 0x%08X\n"
        "  Entry Point: 0x%016llX\n"
        "  Total Size: %llu bytes\n"
        "  Code: %llu bytes @ 0x%llX\n"
        "  RoData: %llu bytes @ 0x%llX\n"
        "  Data: %llu bytes @ 0x%llX\n"
        "  BSS: %llu bytes\n"
        "  Proofs: %zu entries (%zu proven, %zu failed)\n"
        "  Capabilities: %zu templates\n"
        "  Effects: %zu entries\n"
        "  Validation: %s\n",
        version_str,
        sbf_arch_name(sbf_loader_get_architecture(loader)),
        loader->header->flags,
        (unsigned long long)loader->header->entry_point,
        (unsigned long long)loader->header->total_size,
        (unsigned long long)loader->header->code_size,
        (unsigned long long)loader->header->code_offset,
        (unsigned long long)loader->header->rodata_size,
        (unsigned long long)loader->header->rodata_offset,
        (unsigned long long)loader->header->data_size,
        (unsigned long long)loader->header->data_offset,
        (unsigned long long)loader->header->bss_size,
        sbf_loader_get_proof_count(loader),
        sbf_loader_get_proven_count(loader),
        sbf_loader_get_failed_count(loader),
        sbf_loader_get_cap_count(loader),
        sbf_loader_get_effect_count(loader),
        sbf_validation_result_name(loader->validation_result)
    );

    return n < 0 ? 0 : (size_t)n;
}

size_t sbf_loader_dump_proofs(const SBF_Loader* loader, char* buf, size_t buf_size) {
    if (loader == NULL || buf == NULL || buf_size == 0) return 0;
    if (!loader->loaded || loader->proof_table == NULL) {
        return (size_t)snprintf(buf, buf_size, "No proofs\n");
    }

    size_t pos = 0;
    int n = snprintf(buf, buf_size, "Proof Table (%u entries):\n", loader->proof_table->entry_count);
    if (n > 0) pos += (size_t)n;

    for (uint32_t i = 0; i < loader->proof_table->entry_count && pos < buf_size; i++) {
        const SBF_Proof_Entry* p = &loader->proofs[i];
        const char* loc = sbf_loader_get_proof_location(loader, p);
        const char* status_str;
        switch (p->status) {
            case SBF_PROOF_PROVEN:  status_str = "PROVEN"; break;
            case SBF_PROOF_ASSUMED: status_str = "ASSUMED"; break;
            case SBF_PROOF_RUNTIME: status_str = "RUNTIME"; break;
            case SBF_PROOF_FAILED:  status_str = "FAILED"; break;
            default:                status_str = "UNKNOWN"; break;
        }
        n = snprintf(buf + pos, buf_size - pos,
            "  [%u] %s: %s @ 0x%llX %s\n",
            i, sbf_proof_kind_name((SBF_Proof_Kind)p->kind), status_str,
            (unsigned long long)p->code_offset,
            loc ? loc : "");
        if (n > 0) pos += (size_t)n;
    }

    return pos;
}

size_t sbf_loader_dump_caps(const SBF_Loader* loader, char* buf, size_t buf_size) {
    if (loader == NULL || buf == NULL || buf_size == 0) return 0;
    if (!loader->loaded || loader->cap_table == NULL) {
        return (size_t)snprintf(buf, buf_size, "No capabilities\n");
    }

    size_t pos = 0;
    int n = snprintf(buf, buf_size, "Capability Table (%u entries):\n", loader->cap_table->entry_count);
    if (n > 0) pos += (size_t)n;

    for (uint32_t i = 0; i < loader->cap_table->entry_count && pos < buf_size; i++) {
        const SBF_Cap_Template* c = &loader->caps[i];
        const char* name = sbf_loader_get_cap_name(loader, c);
        char perms[8] = "-------";
        if (c->permissions & SBF_CAP_READ)  perms[0] = 'R';
        if (c->permissions & SBF_CAP_WRITE) perms[1] = 'W';
        if (c->permissions & SBF_CAP_EXEC)  perms[2] = 'X';
        if (c->permissions & SBF_CAP_DERIVE) perms[3] = 'D';

        n = snprintf(buf + pos, buf_size - pos,
            "  [%u] %s base=0x%llX len=%llu %s\n",
            i, perms,
            (unsigned long long)c->base,
            (unsigned long long)c->length,
            name ? name : "");
        if (n > 0) pos += (size_t)n;
    }

    return pos;
}

size_t sbf_loader_dump_effects(const SBF_Loader* loader, char* buf, size_t buf_size) {
    if (loader == NULL || buf == NULL || buf_size == 0) return 0;
    if (!loader->loaded || loader->effect_table == NULL) {
        return (size_t)snprintf(buf, buf_size, "No effects\n");
    }

    size_t pos = 0;
    int n = snprintf(buf, buf_size, "Effect Table (%u entries, %u pure):\n",
        loader->effect_table->entry_count,
        loader->effect_table->pure_count);
    if (n > 0) pos += (size_t)n;

    for (uint32_t i = 0; i < loader->effect_table->entry_count && pos < buf_size; i++) {
        const SBF_Effect_Entry* e = &loader->effects[i];
        const char* name = sbf_loader_get_effect_name(loader, e);

        n = snprintf(buf + pos, buf_size - pos,
            "  [%u] %s @ 0x%X (%u bytes) effects=0x%02X\n",
            i, name ? name : "(unnamed)",
            e->function_offset, e->function_size,
            e->declared_effects);
        if (n > 0) pos += (size_t)n;
    }

    return pos;
}
