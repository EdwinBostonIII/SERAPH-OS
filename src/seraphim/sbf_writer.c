/**
 * @file sbf_writer.c
 * @brief SERAPH Binary Format (SBF) Writer Implementation
 *
 * Creates SBF binaries from compiled code and metadata.
 */

#include "seraph/seraphim/sbf_writer.h"
#include "seraph/crypto/sha256.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/*============================================================================
 * Default Configuration
 *============================================================================*/

#define DEFAULT_STRING_CAPACITY     (64 * 1024)     /* 64KB default string table */
#define DEFAULT_PROOF_CAPACITY      256
#define DEFAULT_CAP_CAPACITY        64
#define DEFAULT_EFFECT_CAPACITY     256
#define DEFAULT_CODE_CAPACITY       (256 * 1024)    /* 256KB initial code buffer */
#define DEFAULT_DATA_CAPACITY       (64 * 1024)     /* 64KB initial data buffer */

/*============================================================================
 * Buffer Management
 *============================================================================*/

static bool buffer_init(SBF_Buffer* buf, size_t initial_capacity) {
    buf->data = (uint8_t*)calloc(1, initial_capacity);
    if (buf->data == NULL) return false;
    buf->size = 0;
    buf->capacity = initial_capacity;
    return true;
}

static void buffer_free(SBF_Buffer* buf) {
    if (buf->data != NULL) {
        memset(buf->data, 0, buf->capacity);
        free(buf->data);
    }
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;
}

static bool buffer_ensure(SBF_Buffer* buf, size_t needed) {
    if (buf->capacity >= needed) return true;

    size_t new_cap = buf->capacity;
    while (new_cap < needed) {
        new_cap = new_cap * 2;
        if (new_cap < buf->capacity) return false; /* Overflow check */
    }

    uint8_t* new_data = (uint8_t*)realloc(buf->data, new_cap);
    if (new_data == NULL) return false;

    /* Zero new memory */
    memset(new_data + buf->capacity, 0, new_cap - buf->capacity);
    buf->data = new_data;
    buf->capacity = new_cap;
    return true;
}

static bool buffer_append(SBF_Buffer* buf, const void* data, size_t len) {
    if (!buffer_ensure(buf, buf->size + len)) return false;
    memcpy(buf->data + buf->size, data, len);
    buf->size += len;
    return true;
}

static bool buffer_set(SBF_Buffer* buf, const void* data, size_t len) {
    if (!buffer_ensure(buf, len)) return false;
    memcpy(buf->data, data, len);
    buf->size = len;
    return true;
}

/*============================================================================
 * Array Management
 *============================================================================*/

static bool proof_array_init(SBF_Proof_Array* arr, size_t capacity) {
    arr->entries = (SBF_Proof_Entry*)calloc(capacity, sizeof(SBF_Proof_Entry));
    if (arr->entries == NULL) return false;
    arr->count = 0;
    arr->capacity = capacity;
    return true;
}

static void proof_array_free(SBF_Proof_Array* arr) {
    if (arr->entries != NULL) free(arr->entries);
    arr->entries = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static bool proof_array_append(SBF_Proof_Array* arr, const SBF_Proof_Entry* entry) {
    if (arr->count >= arr->capacity) {
        size_t new_cap = arr->capacity * 2;
        SBF_Proof_Entry* new_entries = (SBF_Proof_Entry*)realloc(
            arr->entries, new_cap * sizeof(SBF_Proof_Entry));
        if (new_entries == NULL) return false;
        arr->entries = new_entries;
        arr->capacity = new_cap;
    }
    arr->entries[arr->count++] = *entry;
    return true;
}

static bool cap_array_init(SBF_Cap_Array* arr, size_t capacity) {
    arr->entries = (SBF_Cap_Template*)calloc(capacity, sizeof(SBF_Cap_Template));
    if (arr->entries == NULL) return false;
    arr->count = 0;
    arr->capacity = capacity;
    return true;
}

static void cap_array_free(SBF_Cap_Array* arr) {
    if (arr->entries != NULL) free(arr->entries);
    arr->entries = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static bool cap_array_append(SBF_Cap_Array* arr, const SBF_Cap_Template* cap) {
    if (arr->count >= arr->capacity) {
        size_t new_cap = arr->capacity * 2;
        SBF_Cap_Template* new_entries = (SBF_Cap_Template*)realloc(
            arr->entries, new_cap * sizeof(SBF_Cap_Template));
        if (new_entries == NULL) return false;
        arr->entries = new_entries;
        arr->capacity = new_cap;
    }
    arr->entries[arr->count++] = *cap;
    return true;
}

static bool effect_array_init(SBF_Effect_Array* arr, size_t capacity) {
    arr->entries = (SBF_Effect_Entry*)calloc(capacity, sizeof(SBF_Effect_Entry));
    if (arr->entries == NULL) return false;
    arr->count = 0;
    arr->capacity = capacity;
    return true;
}

static void effect_array_free(SBF_Effect_Array* arr) {
    if (arr->entries != NULL) free(arr->entries);
    arr->entries = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static bool effect_array_append(SBF_Effect_Array* arr, const SBF_Effect_Entry* entry) {
    if (arr->count >= arr->capacity) {
        size_t new_cap = arr->capacity * 2;
        SBF_Effect_Entry* new_entries = (SBF_Effect_Entry*)realloc(
            arr->entries, new_cap * sizeof(SBF_Effect_Entry));
        if (new_entries == NULL) return false;
        arr->entries = new_entries;
        arr->capacity = new_cap;
    }
    arr->entries[arr->count++] = *entry;
    return true;
}

/*============================================================================
 * Writer Lifecycle
 *============================================================================*/

SBF_Writer* sbf_writer_create(void) {
    SBF_Writer_Config config = {0};
    config.architecture = SBF_ARCH_X64;
    config.max_string_size = DEFAULT_STRING_CAPACITY;
    return sbf_writer_create_with_config(&config);
}

SBF_Writer* sbf_writer_create_with_config(const SBF_Writer_Config* config) {
    if (config == NULL) return NULL;

    SBF_Writer* writer = (SBF_Writer*)calloc(1, sizeof(SBF_Writer));
    if (writer == NULL) return NULL;

    writer->config = *config;
    writer->finalized = false;
    writer->manifest_configured = false;
    writer->last_error = SBF_WRITE_OK;

    /* Initialize string table with empty string at offset 0 */
    size_t string_cap = config->max_string_size > 0 ? config->max_string_size : DEFAULT_STRING_CAPACITY;
    if (!buffer_init(&writer->strings, string_cap)) goto fail;
    uint8_t zero = 0;
    buffer_append(&writer->strings, &zero, 1);

    /* Initialize other buffers */
    if (!buffer_init(&writer->code, DEFAULT_CODE_CAPACITY)) goto fail;
    if (!buffer_init(&writer->rodata, DEFAULT_DATA_CAPACITY)) goto fail;
    if (!buffer_init(&writer->data, DEFAULT_DATA_CAPACITY)) goto fail;

    /* Initialize arrays */
    size_t proof_cap = config->max_proofs > 0 ? config->max_proofs : DEFAULT_PROOF_CAPACITY;
    size_t cap_cap = config->max_caps > 0 ? config->max_caps : DEFAULT_CAP_CAPACITY;
    size_t effect_cap = config->max_effects > 0 ? config->max_effects : DEFAULT_EFFECT_CAPACITY;

    if (!proof_array_init(&writer->proofs, proof_cap)) goto fail;
    if (!cap_array_init(&writer->caps, cap_cap)) goto fail;
    if (!effect_array_init(&writer->effects, effect_cap)) goto fail;

    return writer;

fail:
    sbf_writer_destroy(writer);
    return NULL;
}

void sbf_writer_destroy(SBF_Writer* writer) {
    if (writer == NULL) return;

    buffer_free(&writer->code);
    buffer_free(&writer->rodata);
    buffer_free(&writer->data);
    buffer_free(&writer->strings);

    proof_array_free(&writer->proofs);
    cap_array_free(&writer->caps);
    effect_array_free(&writer->effects);

    if (writer->merkle_nodes != NULL) {
        free(writer->merkle_nodes);
    }
    if (writer->output != NULL) {
        memset(writer->output, 0, writer->output_size);
        free(writer->output);
    }

    memset(writer, 0, sizeof(*writer));
    free(writer);
}

SBF_Write_Error sbf_writer_get_error(const SBF_Writer* writer) {
    if (writer == NULL) return SBF_WRITE_ERR_ALLOC;
    return writer->last_error;
}

const char* sbf_write_error_name(SBF_Write_Error error) {
    switch (error) {
        case SBF_WRITE_OK:              return "OK";
        case SBF_WRITE_ERR_ALLOC:       return "Memory allocation failed";
        case SBF_WRITE_ERR_NO_CODE:     return "No code section provided";
        case SBF_WRITE_ERR_TOO_LARGE:   return "Binary exceeds size limits";
        case SBF_WRITE_ERR_HASH_FAIL:   return "Hash computation failed";
        case SBF_WRITE_ERR_IO:          return "File I/O error";
        case SBF_WRITE_ERR_ALIGNMENT:   return "Alignment requirements violated";
        case SBF_WRITE_ERR_OVERLAP:     return "Sections overlap";
        case SBF_WRITE_ERR_INVALID_PROOF: return "Invalid proof entry";
        case SBF_WRITE_ERR_INVALID_CAP:   return "Invalid capability template";
        case SBF_WRITE_ERR_INVALID_EFFECT:return "Invalid effect entry";
        case SBF_WRITE_ERR_STRING_FULL:   return "String table full";
        case SBF_WRITE_ERR_NOT_FINALIZED: return "Must finalize first";
        case SBF_WRITE_ERR_ALREADY_FINAL: return "Already finalized";
        case SBF_WRITE_ERR_SIGN_FAIL:     return "Signing failed";
        default:                          return "Unknown error";
    }
}

/*============================================================================
 * Configuration
 *============================================================================*/

SBF_Write_Error sbf_writer_set_architecture(SBF_Writer* writer, SBF_Architecture arch) {
    if (writer == NULL) return SBF_WRITE_ERR_ALLOC;
    if (writer->finalized) return (writer->last_error = SBF_WRITE_ERR_ALREADY_FINAL);
    writer->config.architecture = arch;
    return SBF_WRITE_OK;
}

SBF_Write_Error sbf_writer_set_flags(SBF_Writer* writer, uint32_t flags) {
    if (writer == NULL) return SBF_WRITE_ERR_ALLOC;
    if (writer->finalized) return (writer->last_error = SBF_WRITE_ERR_ALREADY_FINAL);
    writer->config.flags = flags;
    return SBF_WRITE_OK;
}

SBF_Write_Error sbf_writer_set_entry(SBF_Writer* writer, uint64_t entry) {
    if (writer == NULL) return SBF_WRITE_ERR_ALLOC;
    if (writer->finalized) return (writer->last_error = SBF_WRITE_ERR_ALREADY_FINAL);
    writer->config.entry_point = entry;
    return SBF_WRITE_OK;
}

SBF_Write_Error sbf_writer_set_signing_keys(
    SBF_Writer* writer,
    const uint8_t* private_key,
    const uint8_t* public_key
) {
    if (writer == NULL) return SBF_WRITE_ERR_ALLOC;
    if (writer->finalized) return (writer->last_error = SBF_WRITE_ERR_ALREADY_FINAL);
    writer->config.author_private_key = private_key;
    writer->config.author_public_key = public_key;
    return SBF_WRITE_OK;
}

SBF_Write_Error sbf_writer_configure_manifest(
    SBF_Writer* writer,
    const SBF_Manifest_Config* config
) {
    if (writer == NULL) return SBF_WRITE_ERR_ALLOC;
    if (config == NULL) return SBF_WRITE_ERR_ALLOC;
    if (writer->finalized) return (writer->last_error = SBF_WRITE_ERR_ALREADY_FINAL);

    writer->manifest_config = *config;
    writer->manifest_configured = true;
    return SBF_WRITE_OK;
}

/*============================================================================
 * Section Data
 *============================================================================*/

SBF_Write_Error sbf_writer_set_code(SBF_Writer* writer, const void* data, size_t size) {
    if (writer == NULL) return SBF_WRITE_ERR_ALLOC;
    if (writer->finalized) return (writer->last_error = SBF_WRITE_ERR_ALREADY_FINAL);
    if (data == NULL || size == 0) return SBF_WRITE_OK;

    if (writer->config.max_code_size > 0 && size > writer->config.max_code_size) {
        return (writer->last_error = SBF_WRITE_ERR_TOO_LARGE);
    }

    if (!buffer_set(&writer->code, data, size)) {
        return (writer->last_error = SBF_WRITE_ERR_ALLOC);
    }
    return SBF_WRITE_OK;
}

SBF_Write_Error sbf_writer_set_rodata(SBF_Writer* writer, const void* data, size_t size) {
    if (writer == NULL) return SBF_WRITE_ERR_ALLOC;
    if (writer->finalized) return (writer->last_error = SBF_WRITE_ERR_ALREADY_FINAL);
    if (data == NULL || size == 0) return SBF_WRITE_OK;

    if (!buffer_set(&writer->rodata, data, size)) {
        return (writer->last_error = SBF_WRITE_ERR_ALLOC);
    }
    return SBF_WRITE_OK;
}

SBF_Write_Error sbf_writer_set_data(SBF_Writer* writer, const void* data, size_t size) {
    if (writer == NULL) return SBF_WRITE_ERR_ALLOC;
    if (writer->finalized) return (writer->last_error = SBF_WRITE_ERR_ALREADY_FINAL);
    if (data == NULL || size == 0) return SBF_WRITE_OK;

    if (writer->config.max_data_size > 0 && size > writer->config.max_data_size) {
        return (writer->last_error = SBF_WRITE_ERR_TOO_LARGE);
    }

    if (!buffer_set(&writer->data, data, size)) {
        return (writer->last_error = SBF_WRITE_ERR_ALLOC);
    }
    return SBF_WRITE_OK;
}

SBF_Write_Error sbf_writer_set_bss_size(SBF_Writer* writer, uint64_t size) {
    if (writer == NULL) return SBF_WRITE_ERR_ALLOC;
    if (writer->finalized) return (writer->last_error = SBF_WRITE_ERR_ALREADY_FINAL);
    writer->bss_size = size;
    return SBF_WRITE_OK;
}

/*============================================================================
 * String Table
 *============================================================================*/

uint32_t sbf_writer_add_string(SBF_Writer* writer, const char* str) {
    if (writer == NULL || str == NULL) return (uint32_t)-1;
    if (writer->finalized) {
        writer->last_error = SBF_WRITE_ERR_ALREADY_FINAL;
        return (uint32_t)-1;
    }

    size_t len = strlen(str);
    size_t needed = writer->strings.size + len + 1;

    if (writer->config.max_string_size > 0 && needed > writer->config.max_string_size) {
        writer->last_error = SBF_WRITE_ERR_STRING_FULL;
        return (uint32_t)-1;
    }

    uint32_t offset = (uint32_t)writer->strings.size;

    if (!buffer_append(&writer->strings, str, len + 1)) {
        writer->last_error = SBF_WRITE_ERR_ALLOC;
        return (uint32_t)-1;
    }

    return offset;
}

size_t sbf_writer_get_string_size(const SBF_Writer* writer) {
    if (writer == NULL) return 0;
    return writer->strings.size;
}

/*============================================================================
 * Proof Table
 *============================================================================*/

SBF_Write_Error sbf_writer_add_proof(SBF_Writer* writer, const SBF_Proof_Entry* entry) {
    if (writer == NULL) return SBF_WRITE_ERR_ALLOC;
    if (entry == NULL) return SBF_WRITE_ERR_INVALID_PROOF;
    if (writer->finalized) return (writer->last_error = SBF_WRITE_ERR_ALREADY_FINAL);

    if (!proof_array_append(&writer->proofs, entry)) {
        return (writer->last_error = SBF_WRITE_ERR_ALLOC);
    }
    return SBF_WRITE_OK;
}

SBF_Write_Error sbf_writer_add_proof_ex(
    SBF_Writer* writer,
    SBF_Proof_Kind kind,
    SBF_Proof_Status status,
    uint64_t code_offset,
    const char* location_str,
    const char* description_str
) {
    if (writer == NULL) return SBF_WRITE_ERR_ALLOC;
    if (writer->finalized) return (writer->last_error = SBF_WRITE_ERR_ALREADY_FINAL);

    SBF_Proof_Entry entry = {0};
    entry.kind = kind;
    entry.status = status;
    entry.code_offset = code_offset;

    if (location_str != NULL) {
        entry.location = sbf_writer_add_string(writer, location_str);
        if (entry.location == (uint32_t)-1) return writer->last_error;
    }

    if (description_str != NULL) {
        entry.description = sbf_writer_add_string(writer, description_str);
        if (entry.description == (uint32_t)-1) return writer->last_error;
    }

    /* Compute hash of proof data */
    SHA256_Context ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, &kind, sizeof(kind));
    sha256_update(&ctx, &status, sizeof(status));
    sha256_update(&ctx, &code_offset, sizeof(code_offset));
    if (location_str) sha256_update(&ctx, location_str, strlen(location_str));
    if (description_str) sha256_update(&ctx, description_str, strlen(description_str));
    sha256_final(&ctx, entry.hash);

    return sbf_writer_add_proof(writer, &entry);
}

size_t sbf_writer_get_proof_count(const SBF_Writer* writer) {
    if (writer == NULL) return 0;
    return writer->proofs.count;
}

/*============================================================================
 * Capability Table
 *============================================================================*/

SBF_Write_Error sbf_writer_add_capability(SBF_Writer* writer, const SBF_Cap_Template* cap) {
    if (writer == NULL) return SBF_WRITE_ERR_ALLOC;
    if (cap == NULL) return SBF_WRITE_ERR_INVALID_CAP;
    if (writer->finalized) return (writer->last_error = SBF_WRITE_ERR_ALREADY_FINAL);

    if (!cap_array_append(&writer->caps, cap)) {
        return (writer->last_error = SBF_WRITE_ERR_ALLOC);
    }
    return SBF_WRITE_OK;
}

SBF_Write_Error sbf_writer_add_capability_ex(
    SBF_Writer* writer,
    uint64_t base,
    uint64_t length,
    uint32_t permissions,
    const char* name
) {
    if (writer == NULL) return SBF_WRITE_ERR_ALLOC;
    if (writer->finalized) return (writer->last_error = SBF_WRITE_ERR_ALREADY_FINAL);

    SBF_Cap_Template cap = {0};
    cap.base = base;
    cap.length = length;
    cap.permissions = permissions;
    cap.generation = 0;

    if (name != NULL) {
        cap.name_offset = sbf_writer_add_string(writer, name);
        if (cap.name_offset == (uint32_t)-1) return writer->last_error;
    }

    return sbf_writer_add_capability(writer, &cap);
}

SBF_Write_Error sbf_writer_add_standard_caps(SBF_Writer* writer, uint64_t stack_size) {
    if (writer == NULL) return SBF_WRITE_ERR_ALLOC;
    if (writer->finalized) return (writer->last_error = SBF_WRITE_ERR_ALREADY_FINAL);

    SBF_Write_Error err;

    /* Code capability (R-X) */
    err = sbf_writer_add_capability_ex(writer, 0, writer->code.size,
        SBF_CAP_READ | SBF_CAP_EXEC | SBF_CAP_TYPE_CODE, ".code");
    if (err != SBF_WRITE_OK) return err;

    /* RoData capability (R--) */
    if (writer->rodata.size > 0) {
        err = sbf_writer_add_capability_ex(writer, 0, writer->rodata.size,
            SBF_CAP_READ | SBF_CAP_TYPE_DATA, ".rodata");
        if (err != SBF_WRITE_OK) return err;
    }

    /* Data capability (RW-) */
    if (writer->data.size > 0 || writer->bss_size > 0) {
        err = sbf_writer_add_capability_ex(writer, 0, writer->data.size + writer->bss_size,
            SBF_CAP_READ | SBF_CAP_WRITE | SBF_CAP_TYPE_DATA, ".data");
        if (err != SBF_WRITE_OK) return err;
    }

    /* Stack capability (RW-) */
    if (stack_size > 0) {
        err = sbf_writer_add_capability_ex(writer, 0, stack_size,
            SBF_CAP_READ | SBF_CAP_WRITE | SBF_CAP_TYPE_STACK, ".stack");
        if (err != SBF_WRITE_OK) return err;
    }

    return SBF_WRITE_OK;
}

size_t sbf_writer_get_cap_count(const SBF_Writer* writer) {
    if (writer == NULL) return 0;
    return writer->caps.count;
}

/*============================================================================
 * Effect Table
 *============================================================================*/

SBF_Write_Error sbf_writer_add_effect(SBF_Writer* writer, const SBF_Effect_Entry* entry) {
    if (writer == NULL) return SBF_WRITE_ERR_ALLOC;
    if (entry == NULL) return SBF_WRITE_ERR_INVALID_EFFECT;
    if (writer->finalized) return (writer->last_error = SBF_WRITE_ERR_ALREADY_FINAL);

    if (!effect_array_append(&writer->effects, entry)) {
        return (writer->last_error = SBF_WRITE_ERR_ALLOC);
    }
    return SBF_WRITE_OK;
}

SBF_Write_Error sbf_writer_add_effect_ex(
    SBF_Writer* writer,
    uint32_t function_offset,
    uint32_t function_size,
    uint32_t declared_effects,
    uint32_t verified_effects,
    uint32_t required_caps,
    const char* name
) {
    if (writer == NULL) return SBF_WRITE_ERR_ALLOC;
    if (writer->finalized) return (writer->last_error = SBF_WRITE_ERR_ALREADY_FINAL);

    SBF_Effect_Entry entry = {0};
    entry.function_offset = function_offset;
    entry.function_size = function_size;
    entry.declared_effects = declared_effects;
    entry.verified_effects = verified_effects;
    entry.required_caps = required_caps;

    if (name != NULL) {
        entry.name_offset = sbf_writer_add_string(writer, name);
        if (entry.name_offset == (uint32_t)-1) return writer->last_error;
    }

    return sbf_writer_add_effect(writer, &entry);
}

size_t sbf_writer_get_effect_count(const SBF_Writer* writer) {
    if (writer == NULL) return 0;
    return writer->effects.count;
}

/*============================================================================
 * Merkle Tree Construction
 *============================================================================*/

static bool compute_proof_merkle_root(SBF_Writer* writer) {
    if (writer->proofs.count == 0) {
        /* No proofs - set root to zeros */
        memset(writer->proof_table_header.merkle_root, 0, SHA256_DIGEST_SIZE);
        return true;
    }

    /* Allocate space for leaf hashes */
    size_t leaf_count = writer->proofs.count;
    uint8_t* leaves = (uint8_t*)malloc(leaf_count * SHA256_DIGEST_SIZE);
    if (leaves == NULL) return false;

    /* Copy proof hashes as leaves */
    for (size_t i = 0; i < leaf_count; i++) {
        memcpy(&leaves[i * SHA256_DIGEST_SIZE],
               writer->proofs.entries[i].hash,
               SHA256_DIGEST_SIZE);
    }

    /* Compute Merkle root */
    int result = sha256_merkle_root_alloc(leaves, leaf_count,
        writer->proof_table_header.merkle_root);

    free(leaves);
    return result == 1;
}

/*============================================================================
 * Binary Layout Computation
 *============================================================================*/

static uint64_t align_offset(uint64_t offset, uint64_t align) {
    return (offset + align - 1) & ~(align - 1);
}

static void compute_layout(SBF_Writer* writer) {
    uint64_t offset = 0;

    /* Header at offset 0 */
    offset = SBF_HEADER_SIZE;

    /* Manifest immediately after header */
    writer->header.manifest_offset = offset;
    writer->header.manifest_size = SBF_MANIFEST_SIZE;
    offset += SBF_MANIFEST_SIZE;

    /* Code section (page-aligned) */
    offset = align_offset(offset, SBF_PAGE_SIZE);
    writer->header.code_offset = offset;
    writer->header.code_size = writer->code.size;
    offset += writer->code.size;

    /* RoData section (page-aligned) */
    if (writer->rodata.size > 0) {
        offset = align_offset(offset, SBF_PAGE_SIZE);
        writer->header.rodata_offset = offset;
        writer->header.rodata_size = writer->rodata.size;
        offset += writer->rodata.size;
    } else {
        writer->header.rodata_offset = 0;
        writer->header.rodata_size = 0;
    }

    /* Data section (page-aligned) */
    if (writer->data.size > 0) {
        offset = align_offset(offset, SBF_PAGE_SIZE);
        writer->header.data_offset = offset;
        writer->header.data_size = writer->data.size;
        offset += writer->data.size;
    } else {
        writer->header.data_offset = 0;
        writer->header.data_size = 0;
    }

    /* BSS (not in file, just record size) */
    writer->header.bss_size = writer->bss_size;

    /* Proof table (8-byte aligned) */
    if (writer->proofs.count > 0) {
        offset = align_offset(offset, 8);
        writer->header.proofs_offset = offset;
        writer->header.proofs_size = sizeof(SBF_Proof_Table) +
            writer->proofs.count * sizeof(SBF_Proof_Entry);
        offset += writer->header.proofs_size;
    } else {
        writer->header.proofs_offset = 0;
        writer->header.proofs_size = 0;
    }

    /* Capability table (8-byte aligned) */
    if (writer->caps.count > 0) {
        offset = align_offset(offset, 8);
        writer->header.caps_offset = offset;
        writer->header.caps_size = sizeof(SBF_Cap_Table) +
            writer->caps.count * sizeof(SBF_Cap_Template);
        offset += writer->header.caps_size;
    } else {
        writer->header.caps_offset = 0;
        writer->header.caps_size = 0;
    }

    /* Effect table (8-byte aligned) */
    if (writer->effects.count > 0) {
        offset = align_offset(offset, 8);
        writer->header.effects_offset = offset;
        writer->header.effects_size = sizeof(SBF_Effect_Table) +
            writer->effects.count * sizeof(SBF_Effect_Entry);
        offset += writer->header.effects_size;
    } else {
        writer->header.effects_offset = 0;
        writer->header.effects_size = 0;
    }

    /* String table */
    if (writer->strings.size > 1) {
        offset = align_offset(offset, 8);
        writer->header.strings_offset = offset;
        writer->header.strings_size = sizeof(SBF_String_Table) + writer->strings.size;
        offset += writer->header.strings_size;
    } else {
        writer->header.strings_offset = 0;
        writer->header.strings_size = 0;
    }

    /* Total size */
    writer->header.total_size = offset;
}

/*============================================================================
 * Finalization
 *============================================================================*/

SBF_Write_Error sbf_writer_finalize(SBF_Writer* writer) {
    if (writer == NULL) return SBF_WRITE_ERR_ALLOC;
    if (writer->finalized) return (writer->last_error = SBF_WRITE_ERR_ALREADY_FINAL);
    if (writer->code.size == 0) return (writer->last_error = SBF_WRITE_ERR_NO_CODE);

    /* Initialize header */
    memset(&writer->header, 0, sizeof(writer->header));
    writer->header.magic = SBF_MAGIC;
    writer->header.version = SBF_VERSION;
    writer->header.flags = writer->config.flags;
    writer->header.header_size = SBF_HEADER_SIZE;
    writer->header.entry_point = writer->config.entry_point;
    writer->header.architecture = writer->config.architecture;

    /* Compute layout */
    compute_layout(writer);

    /* Initialize proof table header */
    writer->proof_table_header.magic = SBF_PROOF_MAGIC;
    writer->proof_table_header.entry_count = (uint32_t)writer->proofs.count;
    writer->proof_table_header.proven_count = 0;
    writer->proof_table_header.failed_count = 0;

    /* Count proof statuses */
    for (size_t i = 0; i < writer->proofs.count; i++) {
        switch (writer->proofs.entries[i].status) {
            case SBF_PROOF_PROVEN:  writer->proof_table_header.proven_count++; break;
            case SBF_PROOF_FAILED:  writer->proof_table_header.failed_count++; break;
            default: break;
        }
    }

    /* Compute Merkle root of proofs */
    if (!compute_proof_merkle_root(writer)) {
        return (writer->last_error = SBF_WRITE_ERR_HASH_FAIL);
    }

    /* Copy Merkle root to header */
    memcpy(writer->header.proof_root, writer->proof_table_header.merkle_root, SHA256_DIGEST_SIZE);

    /* Initialize capability table header */
    writer->cap_table_header.magic = SBF_CAP_MAGIC;
    writer->cap_table_header.entry_count = (uint32_t)writer->caps.count;
    /* Set standard cap indices (assuming standard caps were added) */
    writer->cap_table_header.code_cap_idx = 0;
    writer->cap_table_header.rodata_cap_idx = writer->rodata.size > 0 ? 1 : 0;
    writer->cap_table_header.data_cap_idx = writer->caps.count > 2 ? 2 : 0;
    writer->cap_table_header.stack_cap_idx = writer->caps.count > 3 ? 3 : 0;

    /* Initialize effect table header */
    writer->effect_table_header.magic = SBF_EFFECT_MAGIC;
    writer->effect_table_header.entry_count = (uint32_t)writer->effects.count;
    writer->effect_table_header.pure_count = 0;
    writer->effect_table_header.impure_count = 0;

    for (size_t i = 0; i < writer->effects.count; i++) {
        if (writer->effects.entries[i].declared_effects == SBF_EFFECT_NONE) {
            writer->effect_table_header.pure_count++;
        } else {
            writer->effect_table_header.impure_count++;
        }
    }

    /* Initialize string table header */
    writer->string_table_header.magic = SBF_STRING_MAGIC;
    writer->string_table_header.total_size = (uint32_t)(sizeof(SBF_String_Table) + writer->strings.size);

    /* Build manifest */
    memset(&writer->manifest, 0, sizeof(writer->manifest));
    writer->manifest.magic = SBF_MANIFEST_MAGIC;
    writer->manifest.version = SBF_VERSION;

    if (writer->manifest_configured) {
        writer->manifest.kernel_min_version = writer->manifest_config.kernel_min_version;
        writer->manifest.kernel_max_version = writer->manifest_config.kernel_max_version;
        writer->manifest.sovereign_flags = writer->manifest_config.sovereign_flags;
        writer->manifest.strand_count_min = writer->manifest_config.strand_count_min;
        writer->manifest.strand_count_max = writer->manifest_config.strand_count_max;
        writer->manifest.strand_flags = writer->manifest_config.strand_flags;
        writer->manifest.stack_size = writer->manifest_config.stack_size;
        writer->manifest.heap_size = writer->manifest_config.heap_size;
        writer->manifest.memory_limit = writer->manifest_config.memory_limit;
        writer->manifest.chronon_budget = writer->manifest_config.chronon_budget;
        writer->manifest.chronon_limit = writer->manifest_config.chronon_limit;
        writer->manifest.chronon_slice = writer->manifest_config.chronon_slice;
        writer->manifest.atlas_region_count = writer->manifest_config.atlas_region_count;
        writer->manifest.atlas_flags = writer->manifest_config.atlas_flags;
        writer->manifest.aether_node_count = writer->manifest_config.aether_node_count;
        writer->manifest.aether_flags = writer->manifest_config.aether_flags;
        writer->manifest.cap_slot_count = writer->manifest_config.cap_slot_count;
        writer->manifest.priority_class = writer->manifest_config.priority_class;

        if (writer->manifest_config.binary_id != NULL) {
            memcpy(writer->manifest.binary_id, writer->manifest_config.binary_id, SBF_BINARY_ID_SIZE);
        }
    }

    /* Compute declared effect mask from all effects */
    uint32_t effect_mask = 0;
    for (size_t i = 0; i < writer->effects.count; i++) {
        effect_mask |= writer->effects.entries[i].declared_effects;
    }
    writer->manifest.effect_mask = effect_mask;

    /* Set author key if provided */
    if (writer->config.author_public_key != NULL) {
        memcpy(writer->manifest.author_key, writer->config.author_public_key, SBF_PUBKEY_SIZE);
    }

    /* Allocate output buffer */
    writer->output_size = (size_t)writer->header.total_size;
    writer->output = (uint8_t*)calloc(1, writer->output_size);
    if (writer->output == NULL) {
        return (writer->last_error = SBF_WRITE_ERR_ALLOC);
    }

    /* Write header */
    memcpy(writer->output, &writer->header, sizeof(writer->header));

    /* Write manifest */
    memcpy(writer->output + writer->header.manifest_offset, &writer->manifest, sizeof(writer->manifest));

    /* Write code section */
    if (writer->code.size > 0) {
        memcpy(writer->output + writer->header.code_offset, writer->code.data, writer->code.size);
    }

    /* Write rodata section */
    if (writer->rodata.size > 0) {
        memcpy(writer->output + writer->header.rodata_offset, writer->rodata.data, writer->rodata.size);
    }

    /* Write data section */
    if (writer->data.size > 0) {
        memcpy(writer->output + writer->header.data_offset, writer->data.data, writer->data.size);
    }

    /* Write proof table */
    if (writer->proofs.count > 0) {
        uint8_t* proof_ptr = writer->output + writer->header.proofs_offset;
        memcpy(proof_ptr, &writer->proof_table_header, sizeof(writer->proof_table_header));
        memcpy(proof_ptr + sizeof(SBF_Proof_Table),
               writer->proofs.entries,
               writer->proofs.count * sizeof(SBF_Proof_Entry));
    }

    /* Write capability table */
    if (writer->caps.count > 0) {
        uint8_t* cap_ptr = writer->output + writer->header.caps_offset;
        memcpy(cap_ptr, &writer->cap_table_header, sizeof(writer->cap_table_header));
        memcpy(cap_ptr + sizeof(SBF_Cap_Table),
               writer->caps.entries,
               writer->caps.count * sizeof(SBF_Cap_Template));
    }

    /* Write effect table */
    if (writer->effects.count > 0) {
        uint8_t* effect_ptr = writer->output + writer->header.effects_offset;
        memcpy(effect_ptr, &writer->effect_table_header, sizeof(writer->effect_table_header));
        memcpy(effect_ptr + sizeof(SBF_Effect_Table),
               writer->effects.entries,
               writer->effects.count * sizeof(SBF_Effect_Entry));
    }

    /* Write string table */
    if (writer->strings.size > 1) {
        uint8_t* str_ptr = writer->output + writer->header.strings_offset;
        memcpy(str_ptr, &writer->string_table_header, sizeof(writer->string_table_header));
        memcpy(str_ptr + sizeof(SBF_String_Table),
               writer->strings.data,
               writer->strings.size);
    }

    /* Compute content hash (everything after header) */
    if (writer->header.total_size > SBF_HEADER_SIZE) {
        sha256(writer->output + SBF_HEADER_SIZE,
               writer->output_size - SBF_HEADER_SIZE,
               writer->header.content_hash);
        /* Update header in output with content hash */
        memcpy(writer->output, &writer->header, sizeof(writer->header));
    }

    /* TODO: Sign manifest if keys provided */
    if (writer->config.author_private_key != NULL) {
        /* Ed25519 signing would go here */
        writer->header.flags |= SBF_FLAG_SIGNED;
        memcpy(writer->output, &writer->header, sizeof(writer->header));
    }

    writer->finalized = true;
    return SBF_WRITE_OK;
}

bool sbf_writer_is_finalized(const SBF_Writer* writer) {
    if (writer == NULL) return false;
    return writer->finalized;
}

/*============================================================================
 * Output
 *============================================================================*/

const void* sbf_writer_get_binary(const SBF_Writer* writer, size_t* out_size) {
    if (writer == NULL || !writer->finalized) {
        if (out_size) *out_size = 0;
        return NULL;
    }
    if (out_size) *out_size = writer->output_size;
    return writer->output;
}

SBF_Write_Error sbf_writer_write_file(const SBF_Writer* writer, const char* path) {
    if (writer == NULL) return SBF_WRITE_ERR_ALLOC;
    if (!writer->finalized) return SBF_WRITE_ERR_NOT_FINALIZED;
    if (path == NULL) return SBF_WRITE_ERR_IO;

    FILE* f = fopen(path, "wb");
    if (f == NULL) return SBF_WRITE_ERR_IO;

    size_t written = fwrite(writer->output, 1, writer->output_size, f);
    fclose(f);

    if (written != writer->output_size) return SBF_WRITE_ERR_IO;
    return SBF_WRITE_OK;
}

SBF_Write_Error sbf_writer_write_buffer(
    const SBF_Writer* writer,
    void* buffer,
    size_t buffer_size,
    size_t* out_written
) {
    if (writer == NULL) return SBF_WRITE_ERR_ALLOC;
    if (!writer->finalized) return SBF_WRITE_ERR_NOT_FINALIZED;
    if (buffer == NULL) return SBF_WRITE_ERR_IO;

    size_t to_write = writer->output_size;
    if (to_write > buffer_size) to_write = buffer_size;

    memcpy(buffer, writer->output, to_write);
    if (out_written) *out_written = to_write;

    if (to_write < writer->output_size) return SBF_WRITE_ERR_TOO_LARGE;
    return SBF_WRITE_OK;
}

/*============================================================================
 * Header Access
 *============================================================================*/

const SBF_Header* sbf_writer_get_header(const SBF_Writer* writer) {
    if (writer == NULL || !writer->finalized) return NULL;
    return &writer->header;
}

const SBF_Manifest* sbf_writer_get_manifest(const SBF_Writer* writer) {
    if (writer == NULL || !writer->finalized) return NULL;
    return &writer->manifest;
}

/*============================================================================
 * Debug Utilities
 *============================================================================*/

size_t sbf_writer_dump(const SBF_Writer* writer, char* buf, size_t buf_size) {
    if (writer == NULL || buf == NULL || buf_size == 0) return 0;

    int n = snprintf(buf, buf_size,
        "SBF Writer State:\n"
        "  Finalized: %s\n"
        "  Last Error: %s\n"
        "  Architecture: %s\n"
        "  Flags: 0x%08X\n"
        "  Entry Point: 0x%016llX\n"
        "  Code Size: %zu bytes\n"
        "  RoData Size: %zu bytes\n"
        "  Data Size: %zu bytes\n"
        "  BSS Size: %llu bytes\n"
        "  Proofs: %zu entries\n"
        "  Capabilities: %zu templates\n"
        "  Effects: %zu entries\n"
        "  Strings: %zu bytes\n",
        writer->finalized ? "Yes" : "No",
        sbf_write_error_name(writer->last_error),
        sbf_arch_name(writer->config.architecture),
        writer->config.flags,
        (unsigned long long)writer->config.entry_point,
        writer->code.size,
        writer->rodata.size,
        writer->data.size,
        (unsigned long long)writer->bss_size,
        writer->proofs.count,
        writer->caps.count,
        writer->effects.count,
        writer->strings.size
    );

    if (n < 0) return 0;
    if ((size_t)n >= buf_size) return buf_size - 1;

    if (writer->finalized) {
        size_t pos = (size_t)n;
        int m = snprintf(buf + pos, buf_size - pos,
            "\nFinal Binary:\n"
            "  Total Size: %zu bytes\n"
            "  Code @ 0x%llX (%llu bytes)\n"
            "  RoData @ 0x%llX (%llu bytes)\n"
            "  Data @ 0x%llX (%llu bytes)\n"
            "  Proofs @ 0x%llX (%llu bytes)\n"
            "  Caps @ 0x%llX (%llu bytes)\n"
            "  Effects @ 0x%llX (%llu bytes)\n"
            "  Strings @ 0x%llX (%llu bytes)\n",
            writer->output_size,
            (unsigned long long)writer->header.code_offset,
            (unsigned long long)writer->header.code_size,
            (unsigned long long)writer->header.rodata_offset,
            (unsigned long long)writer->header.rodata_size,
            (unsigned long long)writer->header.data_offset,
            (unsigned long long)writer->header.data_size,
            (unsigned long long)writer->header.proofs_offset,
            (unsigned long long)writer->header.proofs_size,
            (unsigned long long)writer->header.caps_offset,
            (unsigned long long)writer->header.caps_size,
            (unsigned long long)writer->header.effects_offset,
            (unsigned long long)writer->header.effects_size,
            (unsigned long long)writer->header.strings_offset,
            (unsigned long long)writer->header.strings_size
        );
        if (m > 0) n += m;
    }

    return (size_t)n;
}

SBF_Write_Error sbf_writer_validate(const SBF_Writer* writer) {
    if (writer == NULL) return SBF_WRITE_ERR_ALLOC;
    if (writer->code.size == 0 && !writer->finalized) return SBF_WRITE_ERR_NO_CODE;
    return SBF_WRITE_OK;
}
