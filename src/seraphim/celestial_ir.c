/**
 * @file celestial_ir.c
 * @brief MC28: Celestial IR Implementation
 *
 * The heart of SERAPH's native compiler. Celestial IR encodes
 * SERAPH's unique semantics - VOID, Capabilities, Substrates,
 * Galactic numbers - as first-class concepts.
 *
 * Every operation here reflects SERAPH's philosophy:
 * - Safety over speed
 * - VOID as wisdom, not error
 * - Capabilities as the foundation of trust
 * - Effects as explicit contracts
 */

#include "seraph/seraphim/celestial_ir.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*============================================================================
 * Module Management
 *============================================================================*/

Celestial_Module* celestial_module_create(const char* name, Seraph_Arena* arena) {
    if (arena == NULL) return NULL;

    Celestial_Module* mod = (Celestial_Module*)seraph_arena_alloc(arena,
                                                                   sizeof(Celestial_Module),
                                                                   _Alignof(Celestial_Module));
    if (mod == NULL) return NULL;

    memset(mod, 0, sizeof(Celestial_Module));

    if (name != NULL) {
        mod->name_len = strlen(name);
        char* name_copy = (char*)seraph_arena_alloc(arena, mod->name_len + 1, 1);
        if (name_copy != NULL) {
            memcpy(name_copy, name, mod->name_len + 1);
            mod->name = name_copy;
        }
    }

    mod->arena = arena;
    return mod;
}

void celestial_module_free(Celestial_Module* module) {
    /* Module memory is in arena - arena handles deallocation */
    (void)module;
}

/*============================================================================
 * Type Creation
 *============================================================================*/

/**
 * @brief Cached primitive types
 */
static Celestial_Type g_primitive_types[] = {
    { .kind = CIR_TYPE_VOID },
    { .kind = CIR_TYPE_BOOL },
    { .kind = CIR_TYPE_I8 },
    { .kind = CIR_TYPE_I16 },
    { .kind = CIR_TYPE_I32 },
    { .kind = CIR_TYPE_I64 },
    { .kind = CIR_TYPE_U8 },
    { .kind = CIR_TYPE_U16 },
    { .kind = CIR_TYPE_U32 },
    { .kind = CIR_TYPE_U64 },
    { .kind = CIR_TYPE_SCALAR },
    { .kind = CIR_TYPE_DUAL },
    { .kind = CIR_TYPE_GALACTIC },
    { .kind = CIR_TYPE_CAPABILITY },
    { .kind = CIR_TYPE_SUBSTRATE },
};

Celestial_Type* celestial_type_primitive(Celestial_Module* mod,
                                          Celestial_Type_Kind kind) {
    (void)mod;  /* Primitives are statically allocated */

    switch (kind) {
        case CIR_TYPE_VOID:      return &g_primitive_types[0];
        case CIR_TYPE_BOOL:      return &g_primitive_types[1];
        case CIR_TYPE_I8:        return &g_primitive_types[2];
        case CIR_TYPE_I16:       return &g_primitive_types[3];
        case CIR_TYPE_I32:       return &g_primitive_types[4];
        case CIR_TYPE_I64:       return &g_primitive_types[5];
        case CIR_TYPE_U8:        return &g_primitive_types[6];
        case CIR_TYPE_U16:       return &g_primitive_types[7];
        case CIR_TYPE_U32:       return &g_primitive_types[8];
        case CIR_TYPE_U64:       return &g_primitive_types[9];
        case CIR_TYPE_SCALAR:    return &g_primitive_types[10];
        case CIR_TYPE_DUAL:      return &g_primitive_types[11];
        case CIR_TYPE_GALACTIC:  return &g_primitive_types[12];
        case CIR_TYPE_CAPABILITY: return &g_primitive_types[13];
        case CIR_TYPE_SUBSTRATE: return &g_primitive_types[14];
        default:                 return NULL;
    }
}

Celestial_Type* celestial_type_voidable(Celestial_Module* mod,
                                         Celestial_Type* inner) {
    if (mod == NULL || inner == NULL) return NULL;

    Celestial_Type* type = (Celestial_Type*)seraph_arena_alloc(mod->arena,
                                                                sizeof(Celestial_Type),
                                                                _Alignof(Celestial_Type));
    if (type == NULL) return NULL;

    type->kind = CIR_TYPE_VOIDABLE;
    type->voidable_type.inner_type = inner;
    return type;
}

Celestial_Type* celestial_type_capability(Celestial_Module* mod) {
    (void)mod;
    return &g_primitive_types[13];
}

Celestial_Type* celestial_type_struct(Celestial_Module* mod,
                                       const char* name,
                                       size_t name_len,
                                       Celestial_Type** fields,
                                       const char** field_names,
                                       size_t field_count) {
    if (mod == NULL) return NULL;

    Celestial_Type* type = (Celestial_Type*)seraph_arena_alloc(mod->arena,
                                                                sizeof(Celestial_Type),
                                                                _Alignof(Celestial_Type));
    if (type == NULL) return NULL;

    type->kind = CIR_TYPE_STRUCT;

    /* Copy name */
    if (name != NULL && name_len > 0) {
        char* name_copy = (char*)seraph_arena_alloc(mod->arena, name_len + 1, 1);
        if (name_copy != NULL) {
            memcpy(name_copy, name, name_len);
            name_copy[name_len] = '\0';
            type->struct_type.name = name_copy;
            type->struct_type.name_len = name_len;
        }
    }

    /* Copy fields */
    if (field_count > 0 && fields != NULL) {
        type->struct_type.fields = (Celestial_Type**)seraph_arena_alloc(
            mod->arena, field_count * sizeof(Celestial_Type*), _Alignof(Celestial_Type*));

        type->struct_type.field_names = (const char**)seraph_arena_alloc(
            mod->arena, field_count * sizeof(char*), _Alignof(char*));

        if (type->struct_type.fields != NULL) {
            memcpy(type->struct_type.fields, fields,
                   field_count * sizeof(Celestial_Type*));
        }

        if (type->struct_type.field_names != NULL && field_names != NULL) {
            for (size_t i = 0; i < field_count; i++) {
                if (field_names[i] != NULL) {
                    size_t len = strlen(field_names[i]);
                    char* copy = (char*)seraph_arena_alloc(mod->arena, len + 1, 1);
                    if (copy != NULL) {
                        memcpy(copy, field_names[i], len + 1);
                        type->struct_type.field_names[i] = copy;
                    }
                }
            }
        }

        type->struct_type.field_count = field_count;
    }

    /* Register type in module's types array for later lookup */
    size_t new_count = mod->type_count + 1;
    Celestial_Type** new_types = (Celestial_Type**)seraph_arena_alloc(
        mod->arena, new_count * sizeof(Celestial_Type*), _Alignof(Celestial_Type*));
    if (new_types != NULL) {
        if (mod->types != NULL && mod->type_count > 0) {
            memcpy(new_types, mod->types, mod->type_count * sizeof(Celestial_Type*));
        }
        new_types[mod->type_count] = type;
        mod->types = new_types;
        mod->type_count = new_count;
    }

    return type;
}

Celestial_Type* celestial_type_array(Celestial_Module* mod,
                                      Celestial_Type* elem,
                                      size_t length) {
    if (mod == NULL || elem == NULL) return NULL;

    Celestial_Type* type = (Celestial_Type*)seraph_arena_alloc(mod->arena,
                                                                sizeof(Celestial_Type),
                                                                _Alignof(Celestial_Type));
    if (type == NULL) return NULL;

    type->kind = CIR_TYPE_ARRAY;
    type->array_type.elem_type = elem;
    type->array_type.length = length;
    return type;
}

Celestial_Type* celestial_type_str(Celestial_Module* mod) {
    if (mod == NULL) return NULL;

    Celestial_Type* type = (Celestial_Type*)seraph_arena_alloc(mod->arena,
                                                                sizeof(Celestial_Type),
                                                                _Alignof(Celestial_Type));
    if (type == NULL) return NULL;

    type->kind = CIR_TYPE_STR;
    return type;
}

Celestial_Type* celestial_type_enum(Celestial_Module* mod,
                                     const char* name,
                                     size_t name_len,
                                     const char** variant_names,
                                     size_t* variant_name_lens,
                                     Celestial_Type** variant_types,
                                     size_t variant_count) {
    if (mod == NULL) return NULL;

    Celestial_Type* type = (Celestial_Type*)seraph_arena_alloc(mod->arena,
                                                                sizeof(Celestial_Type),
                                                                _Alignof(Celestial_Type));
    if (type == NULL) return NULL;

    type->kind = CIR_TYPE_ENUM;

    /* Copy name */
    if (name != NULL && name_len > 0) {
        char* name_copy = (char*)seraph_arena_alloc(mod->arena, name_len + 1, 1);
        if (name_copy != NULL) {
            memcpy(name_copy, name, name_len);
            name_copy[name_len] = '\0';
            type->enum_type.name = name_copy;
            type->enum_type.name_len = name_len;
        }
    }

    type->enum_type.variant_count = variant_count;

    if (variant_count > 0) {
        /* Allocate variant arrays */
        type->enum_type.variant_names = (const char**)seraph_arena_alloc(
            mod->arena, variant_count * sizeof(char*), _Alignof(char*));
        type->enum_type.variant_name_lens = (size_t*)seraph_arena_alloc(
            mod->arena, variant_count * sizeof(size_t), _Alignof(size_t));
        type->enum_type.variant_types = (Celestial_Type**)seraph_arena_alloc(
            mod->arena, variant_count * sizeof(Celestial_Type*), _Alignof(Celestial_Type*));

        if (type->enum_type.variant_names && variant_names) {
            for (size_t i = 0; i < variant_count; i++) {
                if (variant_names[i] && variant_name_lens[i] > 0) {
                    size_t vlen = variant_name_lens[i];
                    char* vcopy = (char*)seraph_arena_alloc(mod->arena, vlen + 1, 1);
                    if (vcopy) {
                        memcpy(vcopy, variant_names[i], vlen);
                        vcopy[vlen] = '\0';
                        type->enum_type.variant_names[i] = vcopy;
                        type->enum_type.variant_name_lens[i] = vlen;
                    }
                }
                if (type->enum_type.variant_types && variant_types) {
                    type->enum_type.variant_types[i] = variant_types[i];
                }
            }
        }
    }

    /* Register in module's type list */
    size_t new_count = mod->type_count + 1;
    Celestial_Type** new_types = (Celestial_Type**)seraph_arena_alloc(
        mod->arena, new_count * sizeof(Celestial_Type*), _Alignof(Celestial_Type*));
    if (new_types != NULL) {
        if (mod->types != NULL && mod->type_count > 0) {
            memcpy(new_types, mod->types, mod->type_count * sizeof(Celestial_Type*));
        }
        new_types[mod->type_count] = type;
        mod->types = new_types;
        mod->type_count = new_count;
    }

    return type;
}

/**
 * @brief Process escape sequences in a string
 * @return Processed length (may be less than input length)
 */
static size_t process_escape_sequences(const char* src, size_t src_len,
                                        char* dst, size_t dst_cap) {
    size_t di = 0;
    for (size_t si = 0; si < src_len && di < dst_cap; ) {
        if (src[si] == '\\' && si + 1 < src_len) {
            si++;
            switch (src[si]) {
                case 'n':  dst[di++] = '\n'; break;
                case 'r':  dst[di++] = '\r'; break;
                case 't':  dst[di++] = '\t'; break;
                case '\\': dst[di++] = '\\'; break;
                case '"':  dst[di++] = '"';  break;
                case '\'': dst[di++] = '\''; break;
                case '0':  dst[di++] = '\0'; break;
                case 'x':
                    /* Hex escape \xNN */
                    if (si + 2 < src_len) {
                        char hex[3] = {src[si+1], src[si+2], '\0'};
                        dst[di++] = (char)strtol(hex, NULL, 16);
                        si += 2;
                    }
                    break;
                default:
                    /* Unknown escape, copy as-is */
                    dst[di++] = src[si];
            }
            si++;
        } else {
            dst[di++] = src[si++];
        }
    }
    return di;
}

Celestial_String_Const* celestial_add_string_const(Celestial_Module* mod,
                                                    const char* data,
                                                    size_t len) {
    if (mod == NULL || data == NULL) return NULL;

    /* Allocate string constant entry */
    Celestial_String_Const* sc = (Celestial_String_Const*)seraph_arena_alloc(
        mod->arena, sizeof(Celestial_String_Const), _Alignof(Celestial_String_Const));
    if (sc == NULL) return NULL;

    /* Allocate buffer for processed string (escape sequences may shrink it) */
    char* buf = (char*)seraph_arena_alloc(mod->arena, len + 1, 1);
    if (buf == NULL) return NULL;

    /* Process escape sequences */
    size_t processed_len = process_escape_sequences(data, len, buf, len);
    buf[processed_len] = '\0';  /* Null-terminate for safety */

    sc->data = buf;
    sc->len = processed_len;
    sc->id = (uint32_t)mod->string_count;
    sc->next = mod->strings;
    mod->strings = sc;
    mod->string_count++;

    return sc;
}

Celestial_Type* celestial_type_function(Celestial_Module* mod,
                                         Celestial_Type* ret,
                                         Celestial_Type** params,
                                         size_t param_count,
                                         uint32_t effects) {
    if (mod == NULL) return NULL;

    Celestial_Type* type = (Celestial_Type*)seraph_arena_alloc(mod->arena,
                                                                sizeof(Celestial_Type),
                                                                _Alignof(Celestial_Type));
    if (type == NULL) return NULL;

    type->kind = CIR_TYPE_FUNCTION;
    type->func_type.ret_type = ret;
    type->func_type.effects = effects;

    if (param_count > 0 && params != NULL) {
        type->func_type.param_types = (Celestial_Type**)seraph_arena_alloc(
            mod->arena, param_count * sizeof(Celestial_Type*), _Alignof(Celestial_Type*));

        if (type->func_type.param_types != NULL) {
            memcpy(type->func_type.param_types, params,
                   param_count * sizeof(Celestial_Type*));
        }
        type->func_type.param_count = param_count;
    }

    return type;
}

Celestial_Type* celestial_type_pointer(Celestial_Module* mod,
                                        Celestial_Type* pointee) {
    if (mod == NULL) return NULL;

    Celestial_Type* type = (Celestial_Type*)seraph_arena_alloc(mod->arena,
                                                                sizeof(Celestial_Type),
                                                                _Alignof(Celestial_Type));
    if (type == NULL) return NULL;

    type->kind = CIR_TYPE_POINTER;
    type->pointer_type.pointee_type = pointee;

    return type;
}

size_t celestial_type_size(Celestial_Type* type) {
    if (type == NULL) return 0;

    switch (type->kind) {
        case CIR_TYPE_VOID:      return 0;
        case CIR_TYPE_BOOL:      return 1;
        case CIR_TYPE_I8:
        case CIR_TYPE_U8:        return 1;
        case CIR_TYPE_I16:
        case CIR_TYPE_U16:       return 2;
        case CIR_TYPE_I32:
        case CIR_TYPE_U32:       return 4;
        case CIR_TYPE_I64:
        case CIR_TYPE_U64:       return 8;
        case CIR_TYPE_SCALAR:    return 16;   /* Q64.64 = 128 bits */
        case CIR_TYPE_DUAL:      return 32;   /* 2 x Scalar */
        case CIR_TYPE_GALACTIC:  return 64;   /* 4 x Scalar */
        case CIR_TYPE_CAPABILITY: return 32;  /* base + len + gen + perms */
        case CIR_TYPE_SUBSTRATE: return 8;    /* Context handle */
        case CIR_TYPE_POINTER:   return 8;    /* Raw pointer (64-bit) */

        case CIR_TYPE_VOIDABLE:
            return celestial_type_size(type->voidable_type.inner_type);

        case CIR_TYPE_STRUCT: {
            size_t total = 0;
            for (size_t i = 0; i < type->struct_type.field_count; i++) {
                size_t field_size = celestial_type_size(type->struct_type.fields[i]);
                size_t field_align = celestial_type_align(type->struct_type.fields[i]);
                /* Align */
                total = (total + field_align - 1) & ~(field_align - 1);
                total += field_size;
            }
            /* Align to struct alignment */
            size_t align = celestial_type_align(type);
            total = (total + align - 1) & ~(align - 1);
            return total;
        }

        case CIR_TYPE_ARRAY:
            return celestial_type_size(type->array_type.elem_type) *
                   type->array_type.length;

        case CIR_TYPE_SLICE:     return 16;   /* ptr + len */
        case CIR_TYPE_STR:       return 16;   /* ptr + len (fat pointer) */

        case CIR_TYPE_ENUM: {
            /* Enum size = discriminant (4 bytes) + max payload size */
            size_t max_payload = 0;
            for (size_t i = 0; i < type->enum_type.variant_count; i++) {
                if (type->enum_type.variant_types && type->enum_type.variant_types[i]) {
                    size_t payload_size = celestial_type_size(type->enum_type.variant_types[i]);
                    if (payload_size > max_payload) max_payload = payload_size;
                }
            }
            /* Align payload to 8 bytes after discriminant */
            return 8 + ((max_payload + 7) & ~7ULL);
        }

        case CIR_TYPE_FUNCTION:  return 8;    /* Function pointer */

        default: return 0;
    }
}

size_t celestial_type_align(Celestial_Type* type) {
    if (type == NULL) return 1;

    switch (type->kind) {
        case CIR_TYPE_VOID:      return 1;
        case CIR_TYPE_BOOL:      return 1;
        case CIR_TYPE_I8:
        case CIR_TYPE_U8:        return 1;
        case CIR_TYPE_I16:
        case CIR_TYPE_U16:       return 2;
        case CIR_TYPE_I32:
        case CIR_TYPE_U32:       return 4;
        case CIR_TYPE_I64:
        case CIR_TYPE_U64:       return 8;
        case CIR_TYPE_SCALAR:    return 16;
        case CIR_TYPE_DUAL:      return 16;
        case CIR_TYPE_GALACTIC:  return 16;
        case CIR_TYPE_CAPABILITY: return 8;
        case CIR_TYPE_SUBSTRATE: return 8;
        case CIR_TYPE_POINTER:   return 8;

        case CIR_TYPE_VOIDABLE:
            return celestial_type_align(type->voidable_type.inner_type);

        case CIR_TYPE_STRUCT: {
            size_t max_align = 1;
            for (size_t i = 0; i < type->struct_type.field_count; i++) {
                size_t field_align = celestial_type_align(type->struct_type.fields[i]);
                if (field_align > max_align) max_align = field_align;
            }
            return max_align;
        }

        case CIR_TYPE_ARRAY:
            return celestial_type_align(type->array_type.elem_type);

        case CIR_TYPE_SLICE:     return 8;
        case CIR_TYPE_STR:       return 8;   /* Pointer alignment */
        case CIR_TYPE_ENUM:      return 8;   /* Align to max payload or discriminant */
        case CIR_TYPE_FUNCTION:  return 8;

        default: return 1;
    }
}

/*============================================================================
 * Function Creation
 *============================================================================*/

Celestial_Function* celestial_function_create(Celestial_Module* mod,
                                               const char* name,
                                               Celestial_Type* type) {
    if (mod == NULL || type == NULL) return NULL;
    if (type->kind != CIR_TYPE_FUNCTION) return NULL;

    Celestial_Function* fn = (Celestial_Function*)seraph_arena_alloc(
        mod->arena, sizeof(Celestial_Function), _Alignof(Celestial_Function));
    if (fn == NULL) return NULL;

    memset(fn, 0, sizeof(Celestial_Function));

    /* Copy name */
    if (name != NULL) {
        fn->name_len = strlen(name);
        char* name_copy = (char*)seraph_arena_alloc(mod->arena, fn->name_len + 1, 1);
        if (name_copy != NULL) {
            memcpy(name_copy, name, fn->name_len + 1);
            fn->name = name_copy;
        }
    }

    fn->type = type;
    fn->declared_effects = type->func_type.effects;
    fn->next_vreg_id = 0;
    fn->next_block_id = 0;

    /* Create parameter values */
    size_t param_count = type->func_type.param_count;
    if (param_count > 0) {
        fn->params = (Celestial_Value**)seraph_arena_alloc(
            mod->arena, param_count * sizeof(Celestial_Value*),
            _Alignof(Celestial_Value*));

        for (size_t i = 0; i < param_count; i++) {
            Celestial_Value* param = (Celestial_Value*)seraph_arena_alloc(
                mod->arena, sizeof(Celestial_Value), _Alignof(Celestial_Value));
            if (param != NULL) {
                memset(param, 0, sizeof(Celestial_Value));
                param->kind = CIR_VALUE_PARAM;
                param->type = type->func_type.param_types[i];
                param->id = fn->next_vreg_id++;
                param->param.index = (uint32_t)i;
                param->may_be_void = SERAPH_VBIT_VOID;  /* Params might be VOID */
                fn->params[i] = param;
            }
        }
        fn->param_count = param_count;
    }

    /* Link into module */
    fn->next = mod->functions;
    mod->functions = fn;
    mod->function_count++;

    return fn;
}

Celestial_Block* celestial_function_entry(Celestial_Function* fn) {
    if (fn == NULL) return NULL;
    return fn->entry;
}

Celestial_Block* celestial_block_create(Celestial_Function* fn,
                                         const char* name) {
    if (fn == NULL) return NULL;

    /* For now, use a global allocation - TODO: fix this */
    Celestial_Block* block = (Celestial_Block*)malloc(sizeof(Celestial_Block));
    if (block == NULL) return NULL;

    memset(block, 0, sizeof(Celestial_Block));
    block->id = fn->next_block_id++;

    if (name != NULL) {
        size_t len = strlen(name);
        char* name_copy = (char*)malloc(len + 1);
        if (name_copy != NULL) {
            memcpy(name_copy, name, len + 1);
            block->name = name_copy;
        }
    }

    block->substrate = CIR_SUBSTRATE_VOLATILE;  /* Default substrate */

    /* Link into function */
    if (fn->blocks == NULL) {
        fn->blocks = block;
        fn->entry = block;
    } else {
        /* Append to end */
        Celestial_Block* last = fn->blocks;
        while (last->next != NULL) last = last->next;
        last->next = block;
        block->prev = last;
    }
    fn->block_count++;

    return block;
}

/*============================================================================
 * Value Creation
 *============================================================================*/

static Celestial_Value* celestial_value_create(Celestial_Module* mod,
                                                Celestial_Value_Kind kind,
                                                Celestial_Type* type) {
    Celestial_Value* value = (Celestial_Value*)seraph_arena_alloc(
        mod->arena, sizeof(Celestial_Value), _Alignof(Celestial_Value));
    if (value == NULL) return NULL;

    memset(value, 0, sizeof(Celestial_Value));
    value->kind = kind;
    value->type = type;
    value->may_be_void = SERAPH_VBIT_FALSE;  /* Constants aren't VOID by default */
    return value;
}

Celestial_Value* celestial_const_i64(Celestial_Module* mod, int64_t value) {
    Celestial_Value* v = celestial_value_create(mod, CIR_VALUE_CONST,
                                                 celestial_type_primitive(mod, CIR_TYPE_I64));
    if (v != NULL) {
        v->constant.i64 = value;
    }
    return v;
}

Celestial_Value* celestial_const_u64(Celestial_Module* mod, uint64_t value) {
    Celestial_Value* v = celestial_value_create(mod, CIR_VALUE_CONST,
                                                 celestial_type_primitive(mod, CIR_TYPE_U64));
    if (v != NULL) {
        v->constant.u64 = value;
    }
    return v;
}

Celestial_Value* celestial_const_i32(Celestial_Module* mod, int32_t value) {
    Celestial_Value* v = celestial_value_create(mod, CIR_VALUE_CONST,
                                                 celestial_type_primitive(mod, CIR_TYPE_I32));
    if (v != NULL) {
        v->constant.i64 = value;
    }
    return v;
}

Celestial_Value* celestial_const_u32(Celestial_Module* mod, uint32_t value) {
    Celestial_Value* v = celestial_value_create(mod, CIR_VALUE_CONST,
                                                 celestial_type_primitive(mod, CIR_TYPE_U32));
    if (v != NULL) {
        v->constant.u64 = value;
    }
    return v;
}

Celestial_Value* celestial_const_i16(Celestial_Module* mod, int16_t value) {
    Celestial_Value* v = celestial_value_create(mod, CIR_VALUE_CONST,
                                                 celestial_type_primitive(mod, CIR_TYPE_I16));
    if (v != NULL) {
        v->constant.i64 = value;
    }
    return v;
}

Celestial_Value* celestial_const_u16(Celestial_Module* mod, uint16_t value) {
    Celestial_Value* v = celestial_value_create(mod, CIR_VALUE_CONST,
                                                 celestial_type_primitive(mod, CIR_TYPE_U16));
    if (v != NULL) {
        v->constant.u64 = value;
    }
    return v;
}

Celestial_Value* celestial_const_i8(Celestial_Module* mod, int8_t value) {
    Celestial_Value* v = celestial_value_create(mod, CIR_VALUE_CONST,
                                                 celestial_type_primitive(mod, CIR_TYPE_I8));
    if (v != NULL) {
        v->constant.i64 = value;
    }
    return v;
}

Celestial_Value* celestial_const_u8(Celestial_Module* mod, uint8_t value) {
    Celestial_Value* v = celestial_value_create(mod, CIR_VALUE_CONST,
                                                 celestial_type_primitive(mod, CIR_TYPE_U8));
    if (v != NULL) {
        v->constant.u64 = value;
    }
    return v;
}

Celestial_Value* celestial_const_bool(Celestial_Module* mod, int value) {
    Celestial_Value* v = celestial_value_create(mod, CIR_VALUE_CONST,
                                                 celestial_type_primitive(mod, CIR_TYPE_BOOL));
    if (v != NULL) {
        v->constant.i64 = value ? 1 : 0;
    }
    return v;
}

Celestial_Value* celestial_const_void(Celestial_Module* mod,
                                       Celestial_Type* type) {
    Celestial_Value* v = celestial_value_create(mod, CIR_VALUE_VOID_CONST, type);
    if (v != NULL) {
        v->may_be_void = SERAPH_VBIT_TRUE;  /* This IS VOID */

        /* Set the VOID bit pattern based on type */
        switch (type->kind) {
            case CIR_TYPE_I8:
            case CIR_TYPE_U8:
                v->constant.u64 = 0xFF;
                break;
            case CIR_TYPE_I16:
            case CIR_TYPE_U16:
                v->constant.u64 = 0xFFFF;
                break;
            case CIR_TYPE_I32:
            case CIR_TYPE_U32:
                v->constant.u64 = 0xFFFFFFFF;
                break;
            case CIR_TYPE_I64:
            case CIR_TYPE_U64:
            default:
                v->constant.u64 = 0xFFFFFFFFFFFFFFFFULL;
                break;
        }
    }
    return v;
}

Celestial_Value* celestial_const_galactic(Celestial_Module* mod,
                                           int64_t w, int64_t x,
                                           int64_t y, int64_t z) {
    Celestial_Value* v = celestial_value_create(mod, CIR_VALUE_CONST,
                                                 celestial_type_primitive(mod, CIR_TYPE_GALACTIC));
    if (v != NULL) {
        v->constant.galactic.w = w;
        v->constant.galactic.x = x;
        v->constant.galactic.y = y;
        v->constant.galactic.z = z;
    }
    return v;
}

Celestial_Value* celestial_const_string(Celestial_Module* mod,
                                         Celestial_String_Const* str_const) {
    if (mod == NULL || str_const == NULL) return NULL;

    Celestial_Value* v = celestial_value_create(mod, CIR_VALUE_STRING,
                                                 celestial_type_str(mod));
    if (v != NULL) {
        v->string.str_const = str_const;
    }
    return v;
}

/*============================================================================
 * Builder
 *============================================================================*/

void celestial_builder_init(Celestial_Builder* builder,
                            Celestial_Module* mod) {
    if (builder == NULL) return;

    memset(builder, 0, sizeof(Celestial_Builder));
    builder->module = mod;
}

void celestial_builder_position(Celestial_Builder* builder,
                                Celestial_Block* block) {
    if (builder == NULL) return;

    builder->block = block;
    builder->insert_point = NULL;  /* Insert at end */
}

/*============================================================================
 * Instruction Building Helpers
 *============================================================================*/

static Celestial_Instr* celestial_instr_create(Celestial_Builder* b,
                                                Celestial_Opcode opcode,
                                                Celestial_Type* result_type,
                                                size_t operand_count) {
    if (b == NULL || b->module == NULL) return NULL;

    Celestial_Instr* instr = (Celestial_Instr*)seraph_arena_alloc(
        b->module->arena, sizeof(Celestial_Instr), _Alignof(Celestial_Instr));
    if (instr == NULL) return NULL;

    memset(instr, 0, sizeof(Celestial_Instr));
    instr->opcode = opcode;

    /* Create result value if not void */
    if (result_type != NULL && result_type->kind != CIR_TYPE_VOID) {
        instr->result = (Celestial_Value*)seraph_arena_alloc(
            b->module->arena, sizeof(Celestial_Value), _Alignof(Celestial_Value));
        if (instr->result != NULL) {
            memset(instr->result, 0, sizeof(Celestial_Value));
            instr->result->kind = CIR_VALUE_VREG;
            instr->result->type = result_type;
            instr->result->id = b->function->next_vreg_id++;
            instr->result->vreg.def = instr;
            instr->result->may_be_void = SERAPH_VBIT_VOID;
        }
    }

    /* Allocate operand array */
    if (operand_count > 0) {
        instr->operands = (Celestial_Value**)seraph_arena_alloc(
            b->module->arena, operand_count * sizeof(Celestial_Value*),
            _Alignof(Celestial_Value*));
        instr->operand_count = operand_count;
    }

    /* Insert into block */
    if (b->block != NULL) {
        if (b->insert_point != NULL) {
            /* Insert before insert_point */
            instr->next = b->insert_point;
            instr->prev = b->insert_point->prev;
            if (b->insert_point->prev != NULL) {
                b->insert_point->prev->next = instr;
            } else {
                b->block->first = instr;
            }
            b->insert_point->prev = instr;
        } else {
            /* Append to end */
            if (b->block->last != NULL) {
                b->block->last->next = instr;
                instr->prev = b->block->last;
            } else {
                b->block->first = instr;
            }
            b->block->last = instr;
        }
        b->block->instr_count++;
    }

    return instr;
}

/*============================================================================
 * Arithmetic Instructions
 *============================================================================*/

Celestial_Value* celestial_build_add(Celestial_Builder* b,
                                      Celestial_Value* lhs,
                                      Celestial_Value* rhs,
                                      const char* name) {
    if (b == NULL || lhs == NULL || rhs == NULL) return NULL;
    (void)name;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_ADD, lhs->type, 2);
    if (instr == NULL) return NULL;

    instr->operands[0] = lhs;
    instr->operands[1] = rhs;

    /* VOID propagation: if either operand might be VOID, result might be VOID */
    if (lhs->may_be_void == SERAPH_VBIT_TRUE ||
        rhs->may_be_void == SERAPH_VBIT_TRUE) {
        instr->result->may_be_void = SERAPH_VBIT_TRUE;
    } else if (lhs->may_be_void == SERAPH_VBIT_VOID ||
               rhs->may_be_void == SERAPH_VBIT_VOID) {
        instr->result->may_be_void = SERAPH_VBIT_VOID;
    } else {
        instr->result->may_be_void = SERAPH_VBIT_FALSE;
    }

    return instr->result;
}

Celestial_Value* celestial_build_sub(Celestial_Builder* b,
                                      Celestial_Value* lhs,
                                      Celestial_Value* rhs,
                                      const char* name) {
    if (b == NULL || lhs == NULL || rhs == NULL) return NULL;
    (void)name;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_SUB, lhs->type, 2);
    if (instr == NULL) return NULL;

    instr->operands[0] = lhs;
    instr->operands[1] = rhs;

    /* VOID propagation */
    if (lhs->may_be_void == SERAPH_VBIT_TRUE ||
        rhs->may_be_void == SERAPH_VBIT_TRUE) {
        instr->result->may_be_void = SERAPH_VBIT_TRUE;
    } else if (lhs->may_be_void == SERAPH_VBIT_VOID ||
               rhs->may_be_void == SERAPH_VBIT_VOID) {
        instr->result->may_be_void = SERAPH_VBIT_VOID;
    }

    return instr->result;
}

Celestial_Value* celestial_build_mul(Celestial_Builder* b,
                                      Celestial_Value* lhs,
                                      Celestial_Value* rhs,
                                      const char* name) {
    if (b == NULL || lhs == NULL || rhs == NULL) return NULL;
    (void)name;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_MUL, lhs->type, 2);
    if (instr == NULL) return NULL;

    instr->operands[0] = lhs;
    instr->operands[1] = rhs;

    /* VOID propagation */
    if (lhs->may_be_void == SERAPH_VBIT_TRUE ||
        rhs->may_be_void == SERAPH_VBIT_TRUE) {
        instr->result->may_be_void = SERAPH_VBIT_TRUE;
    } else if (lhs->may_be_void == SERAPH_VBIT_VOID ||
               rhs->may_be_void == SERAPH_VBIT_VOID) {
        instr->result->may_be_void = SERAPH_VBIT_VOID;
    }

    return instr->result;
}

Celestial_Value* celestial_build_div(Celestial_Builder* b,
                                      Celestial_Value* lhs,
                                      Celestial_Value* rhs,
                                      const char* name) {
    if (b == NULL || lhs == NULL || rhs == NULL) return NULL;
    (void)name;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_DIV, lhs->type, 2);
    if (instr == NULL) return NULL;

    instr->operands[0] = lhs;
    instr->operands[1] = rhs;
    instr->effects = CIR_EFFECT_VOID;  /* Division can produce VOID */

    /* Division result might be VOID (divide by zero) */
    instr->result->may_be_void = SERAPH_VBIT_VOID;

    return instr->result;
}

Celestial_Value* celestial_build_mod(Celestial_Builder* b,
                                      Celestial_Value* lhs,
                                      Celestial_Value* rhs,
                                      const char* name) {
    if (b == NULL || lhs == NULL || rhs == NULL) return NULL;
    (void)name;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_MOD, lhs->type, 2);
    if (instr == NULL) return NULL;

    instr->operands[0] = lhs;
    instr->operands[1] = rhs;
    instr->effects = CIR_EFFECT_VOID;  /* Modulo can produce VOID */

    /* Modulo result might be VOID (mod by zero) */
    instr->result->may_be_void = SERAPH_VBIT_VOID;

    return instr->result;
}

Celestial_Value* celestial_build_neg(Celestial_Builder* b,
                                      Celestial_Value* val,
                                      const char* name) {
    if (b == NULL || val == NULL) return NULL;
    (void)name;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_NEG, val->type, 1);
    if (instr == NULL) return NULL;

    instr->operands[0] = val;

    return instr->result;
}

/*============================================================================
 * Bitwise Instructions
 *============================================================================*/

Celestial_Value* celestial_build_and(Celestial_Builder* b,
                                      Celestial_Value* lhs,
                                      Celestial_Value* rhs,
                                      const char* name) {
    if (b == NULL || lhs == NULL || rhs == NULL) return NULL;
    (void)name;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_AND, lhs->type, 2);
    if (instr == NULL) return NULL;

    instr->operands[0] = lhs;
    instr->operands[1] = rhs;

    return instr->result;
}

Celestial_Value* celestial_build_or(Celestial_Builder* b,
                                     Celestial_Value* lhs,
                                     Celestial_Value* rhs,
                                     const char* name) {
    if (b == NULL || lhs == NULL || rhs == NULL) return NULL;
    (void)name;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_OR, lhs->type, 2);
    if (instr == NULL) return NULL;

    instr->operands[0] = lhs;
    instr->operands[1] = rhs;

    return instr->result;
}

Celestial_Value* celestial_build_xor(Celestial_Builder* b,
                                      Celestial_Value* lhs,
                                      Celestial_Value* rhs,
                                      const char* name) {
    if (b == NULL || lhs == NULL || rhs == NULL) return NULL;
    (void)name;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_XOR, lhs->type, 2);
    if (instr == NULL) return NULL;

    instr->operands[0] = lhs;
    instr->operands[1] = rhs;

    return instr->result;
}

Celestial_Value* celestial_build_not(Celestial_Builder* b,
                                      Celestial_Value* val,
                                      const char* name) {
    if (b == NULL || val == NULL) return NULL;
    (void)name;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_NOT, val->type, 1);
    if (instr == NULL) return NULL;

    instr->operands[0] = val;

    return instr->result;
}

Celestial_Value* celestial_build_shl(Celestial_Builder* b,
                                      Celestial_Value* lhs,
                                      Celestial_Value* rhs,
                                      const char* name) {
    if (b == NULL || lhs == NULL || rhs == NULL) return NULL;
    (void)name;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_SHL, lhs->type, 2);
    if (instr == NULL) return NULL;

    instr->operands[0] = lhs;
    instr->operands[1] = rhs;

    return instr->result;
}

Celestial_Value* celestial_build_shr(Celestial_Builder* b,
                                      Celestial_Value* lhs,
                                      Celestial_Value* rhs,
                                      const char* name) {
    if (b == NULL || lhs == NULL || rhs == NULL) return NULL;
    (void)name;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_SHR, lhs->type, 2);
    if (instr == NULL) return NULL;

    instr->operands[0] = lhs;
    instr->operands[1] = rhs;

    return instr->result;
}

/*============================================================================
 * Type Conversion Instructions
 *============================================================================*/

Celestial_Value* celestial_build_trunc(Celestial_Builder* b,
                                        Celestial_Value* value,
                                        Celestial_Type* target_type,
                                        const char* name) {
    if (b == NULL || value == NULL || target_type == NULL) return NULL;
    (void)name;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_TRUNC, target_type, 1);
    if (instr == NULL) return NULL;

    instr->operands[0] = value;

    return instr->result;
}

Celestial_Value* celestial_build_zext(Celestial_Builder* b,
                                       Celestial_Value* value,
                                       Celestial_Type* target_type,
                                       const char* name) {
    if (b == NULL || value == NULL || target_type == NULL) return NULL;
    (void)name;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_ZEXT, target_type, 1);
    if (instr == NULL) return NULL;

    instr->operands[0] = value;

    return instr->result;
}

Celestial_Value* celestial_build_sext(Celestial_Builder* b,
                                       Celestial_Value* value,
                                       Celestial_Type* target_type,
                                       const char* name) {
    if (b == NULL || value == NULL || target_type == NULL) return NULL;
    (void)name;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_SEXT, target_type, 1);
    if (instr == NULL) return NULL;

    instr->operands[0] = value;

    return instr->result;
}

/*============================================================================
 * VOID Instructions
 *============================================================================*/

Celestial_Value* celestial_build_void_test(Celestial_Builder* b,
                                            Celestial_Value* value,
                                            const char* name) {
    if (b == NULL || value == NULL) return NULL;
    (void)name;

    Celestial_Type* bool_type = celestial_type_primitive(b->module, CIR_TYPE_BOOL);
    Celestial_Instr* instr = celestial_instr_create(b, CIR_VOID_TEST, bool_type, 1);
    if (instr == NULL) return NULL;

    instr->operands[0] = value;
    instr->result->may_be_void = SERAPH_VBIT_FALSE;  /* Result is boolean, not VOID */

    return instr->result;
}

Celestial_Value* celestial_build_void_prop(Celestial_Builder* b,
                                            Celestial_Value* value,
                                            const char* name) {
    if (b == NULL || value == NULL) return NULL;
    (void)name;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_VOID_PROP, value->type, 1);
    if (instr == NULL) return NULL;

    instr->operands[0] = value;
    instr->effects = CIR_EFFECT_VOID;

    /* After propagation, result is guaranteed non-VOID (or we returned) */
    instr->result->may_be_void = SERAPH_VBIT_FALSE;

    return instr->result;
}

Celestial_Value* celestial_build_void_assert(Celestial_Builder* b,
                                              Celestial_Value* value,
                                              const char* name) {
    if (b == NULL || value == NULL) return NULL;
    (void)name;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_VOID_ASSERT, value->type, 1);
    if (instr == NULL) return NULL;

    instr->operands[0] = value;
    instr->effects = CIR_EFFECT_PANIC;  /* May panic if VOID */

    /* After assertion, result is guaranteed non-VOID (or we panicked) */
    instr->result->may_be_void = SERAPH_VBIT_FALSE;

    return instr->result;
}

Celestial_Value* celestial_build_void_coalesce(Celestial_Builder* b,
                                                Celestial_Value* value,
                                                Celestial_Value* default_val,
                                                const char* name) {
    if (b == NULL || value == NULL || default_val == NULL) return NULL;
    (void)name;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_VOID_COALESCE, value->type, 2);
    if (instr == NULL) return NULL;

    instr->operands[0] = value;
    instr->operands[1] = default_val;

    /* Result is non-VOID if default is non-VOID */
    instr->result->may_be_void = default_val->may_be_void;

    return instr->result;
}

/*============================================================================
 * Capability Instructions
 *============================================================================*/

Celestial_Value* celestial_build_cap_create(Celestial_Builder* b,
                                             Celestial_Value* base,
                                             Celestial_Value* length,
                                             Celestial_Value* generation,
                                             Celestial_Value* perms,
                                             const char* name) {
    if (b == NULL) return NULL;
    (void)name;

    Celestial_Type* cap_type = celestial_type_capability(b->module);
    Celestial_Instr* instr = celestial_instr_create(b, CIR_CAP_CREATE, cap_type, 4);
    if (instr == NULL) return NULL;

    instr->operands[0] = base;
    instr->operands[1] = length;
    instr->operands[2] = generation;
    instr->operands[3] = perms;
    instr->result->may_be_void = SERAPH_VBIT_FALSE;

    return instr->result;
}

Celestial_Value* celestial_build_cap_load(Celestial_Builder* b,
                                           Celestial_Value* cap,
                                           Celestial_Value* offset,
                                           Celestial_Type* type,
                                           const char* name) {
    if (b == NULL || cap == NULL || offset == NULL || type == NULL) return NULL;
    (void)name;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_CAP_LOAD, type, 2);
    if (instr == NULL) return NULL;

    instr->operands[0] = cap;
    instr->operands[1] = offset;
    instr->effects = CIR_EFFECT_READ | CIR_EFFECT_VOID;

    /* Load through capability might return VOID (if bounds violated) */
    instr->result->may_be_void = SERAPH_VBIT_VOID;

    return instr->result;
}

void celestial_build_cap_store(Celestial_Builder* b,
                               Celestial_Value* cap,
                               Celestial_Value* offset,
                               Celestial_Value* value) {
    if (b == NULL || cap == NULL || offset == NULL || value == NULL) return;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_CAP_STORE, NULL, 3);
    if (instr == NULL) return;

    instr->operands[0] = cap;
    instr->operands[1] = offset;
    instr->operands[2] = value;
    instr->effects = CIR_EFFECT_WRITE;
}

/*============================================================================
 * Substrate Instructions
 *============================================================================*/

Celestial_Value* celestial_build_substrate_enter(Celestial_Builder* b,
                                                  Celestial_Substrate_Kind kind,
                                                  const char* name) {
    if (b == NULL) return NULL;
    (void)name;

    Celestial_Type* sub_type = celestial_type_primitive(b->module, CIR_TYPE_SUBSTRATE);
    Celestial_Instr* instr = celestial_instr_create(b, CIR_SUBSTRATE_ENTER, sub_type, 0);
    if (instr == NULL) return NULL;

    /* Store substrate kind in effects field temporarily */
    instr->effects = (uint32_t)kind;

    if (kind == CIR_SUBSTRATE_ATLAS) {
        instr->effects |= CIR_EFFECT_PERSIST;
    } else if (kind == CIR_SUBSTRATE_AETHER) {
        instr->effects |= CIR_EFFECT_NETWORK;
    }

    return instr->result;
}

void celestial_build_substrate_exit(Celestial_Builder* b,
                                    Celestial_Value* context) {
    if (b == NULL || context == NULL) return;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_SUBSTRATE_EXIT, NULL, 1);
    if (instr == NULL) return;

    instr->operands[0] = context;
}

Celestial_Value* celestial_build_atlas_begin(Celestial_Builder* b,
                                              const char* name) {
    if (b == NULL) return NULL;
    (void)name;

    Celestial_Type* sub_type = celestial_type_primitive(b->module, CIR_TYPE_SUBSTRATE);
    Celestial_Instr* instr = celestial_instr_create(b, CIR_ATLAS_BEGIN, sub_type, 0);
    if (instr == NULL) return NULL;

    instr->effects = CIR_EFFECT_PERSIST;
    return instr->result;
}

void celestial_build_atlas_commit(Celestial_Builder* b,
                                  Celestial_Value* tx) {
    if (b == NULL || tx == NULL) return;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_ATLAS_COMMIT, NULL, 1);
    if (instr == NULL) return;

    instr->operands[0] = tx;
    instr->effects = CIR_EFFECT_PERSIST;
}

/*============================================================================
 * Galactic Instructions
 *============================================================================*/

Celestial_Value* celestial_build_galactic_add(Celestial_Builder* b,
                                               Celestial_Value* lhs,
                                               Celestial_Value* rhs,
                                               const char* name) {
    if (b == NULL || lhs == NULL || rhs == NULL) return NULL;
    (void)name;

    Celestial_Type* gal_type = celestial_type_primitive(b->module, CIR_TYPE_GALACTIC);
    Celestial_Instr* instr = celestial_instr_create(b, CIR_GALACTIC_ADD, gal_type, 2);
    if (instr == NULL) return NULL;

    instr->operands[0] = lhs;
    instr->operands[1] = rhs;

    return instr->result;
}

Celestial_Value* celestial_build_galactic_mul(Celestial_Builder* b,
                                               Celestial_Value* lhs,
                                               Celestial_Value* rhs,
                                               const char* name) {
    if (b == NULL || lhs == NULL || rhs == NULL) return NULL;
    (void)name;

    Celestial_Type* gal_type = celestial_type_primitive(b->module, CIR_TYPE_GALACTIC);
    Celestial_Instr* instr = celestial_instr_create(b, CIR_GALACTIC_MUL, gal_type, 2);
    if (instr == NULL) return NULL;

    instr->operands[0] = lhs;
    instr->operands[1] = rhs;

    return instr->result;
}

Celestial_Value* celestial_build_galactic_predict(Celestial_Builder* b,
                                                   Celestial_Value* galactic,
                                                   Celestial_Value* delta_t,
                                                   const char* name) {
    if (b == NULL || galactic == NULL || delta_t == NULL) return NULL;
    (void)name;

    Celestial_Type* gal_type = celestial_type_primitive(b->module, CIR_TYPE_GALACTIC);
    Celestial_Instr* instr = celestial_instr_create(b, CIR_GALACTIC_PREDICT, gal_type, 2);
    if (instr == NULL) return NULL;

    instr->operands[0] = galactic;
    instr->operands[1] = delta_t;

    return instr->result;
}

/*============================================================================
 * Control Flow Instructions
 *============================================================================*/

void celestial_build_jump(Celestial_Builder* b, Celestial_Block* target) {
    if (b == NULL || target == NULL) return;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_JUMP, NULL, 0);
    if (instr == NULL) return;

    instr->target1 = target;
}

void celestial_build_branch(Celestial_Builder* b,
                            Celestial_Value* cond,
                            Celestial_Block* then_block,
                            Celestial_Block* else_block) {
    if (b == NULL || cond == NULL || then_block == NULL || else_block == NULL) return;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_BRANCH, NULL, 1);
    if (instr == NULL) return;

    instr->operands[0] = cond;
    instr->target1 = then_block;
    instr->target2 = else_block;
}

void celestial_build_return(Celestial_Builder* b, Celestial_Value* value) {
    if (b == NULL) return;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_RETURN, NULL,
                                                     value != NULL ? 1 : 0);
    if (instr == NULL) return;

    if (value != NULL) {
        instr->operands[0] = value;
    }
}

Celestial_Value* celestial_build_call(Celestial_Builder* b,
                                       Celestial_Function* callee,
                                       Celestial_Value** args,
                                       size_t arg_count,
                                       const char* name) {
    if (b == NULL || callee == NULL) return NULL;
    (void)name;

    Celestial_Type* ret_type = callee->type->func_type.ret_type;
    Celestial_Instr* instr = celestial_instr_create(b, CIR_CALL, ret_type, arg_count);
    if (instr == NULL) return NULL;

    instr->callee = callee;

    for (size_t i = 0; i < arg_count; i++) {
        instr->operands[i] = args[i];
    }

    /* Effects from callee */
    instr->effects = callee->declared_effects;

    /* Return value might be VOID if callee has VOID effect */
    if (callee->declared_effects & CIR_EFFECT_VOID) {
        instr->result->may_be_void = SERAPH_VBIT_VOID;
    }

    return instr->result;
}

Celestial_Value* celestial_build_call_indirect(Celestial_Builder* b,
                                                Celestial_Value* fn_ptr,
                                                Celestial_Value** args,
                                                size_t arg_count,
                                                const char* name) {
    if (b == NULL || fn_ptr == NULL) return NULL;
    if (fn_ptr->type == NULL || fn_ptr->type->kind != CIR_TYPE_FUNCTION) return NULL;
    (void)name;

    Celestial_Type* ret_type = fn_ptr->type->func_type.ret_type;

    /* Create instruction with fn_ptr as operand[0], then args */
    Celestial_Instr* instr = celestial_instr_create(b, CIR_CALL_INDIRECT, ret_type,
                                                     arg_count + 1);
    if (instr == NULL) return NULL;

    /* First operand is the function pointer */
    instr->operands[0] = fn_ptr;

    /* Copy arguments */
    for (size_t i = 0; i < arg_count; i++) {
        instr->operands[i + 1] = args[i];
    }

    /* Conservative effects for indirect call */
    instr->effects = CIR_EFFECT_IO | CIR_EFFECT_VOID;

    if (instr->result) {
        instr->result->may_be_void = SERAPH_VBIT_VOID;
    }

    return instr->result;
}

Celestial_Value* celestial_build_syscall(Celestial_Builder* b,
                                          Celestial_Value* syscall_num,
                                          Celestial_Value** args,
                                          size_t arg_count,
                                          const char* name) {
    if (b == NULL || syscall_num == NULL) return NULL;
    if (arg_count > 6) return NULL;  /* Max 6 syscall arguments */
    (void)name;

    /* Syscall returns i64 */
    Celestial_Type* ret_type = celestial_type_primitive(b->module, CIR_TYPE_I64);

    /* Create instruction with syscall_num as operand[0], then args */
    Celestial_Instr* instr = celestial_instr_create(b, CIR_SYSCALL, ret_type,
                                                     arg_count + 1);
    if (instr == NULL) return NULL;

    /* First operand is the syscall number */
    instr->operands[0] = syscall_num;

    /* Copy arguments */
    for (size_t i = 0; i < arg_count; i++) {
        instr->operands[i + 1] = args[i];
    }

    /* Syscalls have IO effects */
    instr->effects = CIR_EFFECT_IO;

    return instr->result;
}

Celestial_Value* celestial_get_fn_ptr(Celestial_Module* mod,
                                       Celestial_Function* fn) {
    if (mod == NULL || fn == NULL) return NULL;

    Celestial_Value* v = celestial_value_create(mod, CIR_VALUE_FNPTR, fn->type);
    if (v != NULL) {
        v->fnptr.fn = fn;
        v->may_be_void = SERAPH_VBIT_FALSE;
    }
    return v;
}

/*============================================================================
 * Comparison Instructions
 *============================================================================*/

Celestial_Value* celestial_build_eq(Celestial_Builder* b,
                                     Celestial_Value* lhs,
                                     Celestial_Value* rhs,
                                     const char* name) {
    if (b == NULL || lhs == NULL || rhs == NULL) return NULL;
    (void)name;

    Celestial_Type* bool_type = celestial_type_primitive(b->module, CIR_TYPE_BOOL);
    Celestial_Instr* instr = celestial_instr_create(b, CIR_EQ, bool_type, 2);
    if (instr == NULL) return NULL;

    instr->operands[0] = lhs;
    instr->operands[1] = rhs;
    instr->result->may_be_void = SERAPH_VBIT_FALSE;

    return instr->result;
}

Celestial_Value* celestial_build_lt(Celestial_Builder* b,
                                     Celestial_Value* lhs,
                                     Celestial_Value* rhs,
                                     const char* name) {
    if (b == NULL || lhs == NULL || rhs == NULL) return NULL;
    (void)name;

    Celestial_Type* bool_type = celestial_type_primitive(b->module, CIR_TYPE_BOOL);
    Celestial_Instr* instr = celestial_instr_create(b, CIR_LT, bool_type, 2);
    if (instr == NULL) return NULL;

    instr->operands[0] = lhs;
    instr->operands[1] = rhs;
    instr->result->may_be_void = SERAPH_VBIT_FALSE;

    return instr->result;
}

Celestial_Value* celestial_build_le(Celestial_Builder* b,
                                     Celestial_Value* lhs,
                                     Celestial_Value* rhs,
                                     const char* name) {
    if (b == NULL || lhs == NULL || rhs == NULL) return NULL;
    (void)name;

    Celestial_Type* bool_type = celestial_type_primitive(b->module, CIR_TYPE_BOOL);
    Celestial_Instr* instr = celestial_instr_create(b, CIR_LE, bool_type, 2);
    if (instr == NULL) return NULL;

    instr->operands[0] = lhs;
    instr->operands[1] = rhs;
    instr->result->may_be_void = SERAPH_VBIT_FALSE;

    return instr->result;
}

Celestial_Value* celestial_build_gt(Celestial_Builder* b,
                                     Celestial_Value* lhs,
                                     Celestial_Value* rhs,
                                     const char* name) {
    if (b == NULL || lhs == NULL || rhs == NULL) return NULL;
    (void)name;

    Celestial_Type* bool_type = celestial_type_primitive(b->module, CIR_TYPE_BOOL);
    Celestial_Instr* instr = celestial_instr_create(b, CIR_GT, bool_type, 2);
    if (instr == NULL) return NULL;

    instr->operands[0] = lhs;
    instr->operands[1] = rhs;
    instr->result->may_be_void = SERAPH_VBIT_FALSE;

    return instr->result;
}

Celestial_Value* celestial_build_ge(Celestial_Builder* b,
                                     Celestial_Value* lhs,
                                     Celestial_Value* rhs,
                                     const char* name) {
    if (b == NULL || lhs == NULL || rhs == NULL) return NULL;
    (void)name;

    Celestial_Type* bool_type = celestial_type_primitive(b->module, CIR_TYPE_BOOL);
    Celestial_Instr* instr = celestial_instr_create(b, CIR_GE, bool_type, 2);
    if (instr == NULL) return NULL;

    instr->operands[0] = lhs;
    instr->operands[1] = rhs;
    instr->result->may_be_void = SERAPH_VBIT_FALSE;

    return instr->result;
}

Celestial_Value* celestial_build_ne(Celestial_Builder* b,
                                     Celestial_Value* lhs,
                                     Celestial_Value* rhs,
                                     const char* name) {
    if (b == NULL || lhs == NULL || rhs == NULL) return NULL;
    (void)name;

    Celestial_Type* bool_type = celestial_type_primitive(b->module, CIR_TYPE_BOOL);
    Celestial_Instr* instr = celestial_instr_create(b, CIR_NE, bool_type, 2);
    if (instr == NULL) return NULL;

    instr->operands[0] = lhs;
    instr->operands[1] = rhs;
    instr->result->may_be_void = SERAPH_VBIT_FALSE;

    return instr->result;
}

/*============================================================================
 * Memory Instructions
 *============================================================================*/

Celestial_Value* celestial_build_alloca(Celestial_Builder* b,
                                         Celestial_Type* type,
                                         const char* name) {
    if (b == NULL || type == NULL) return NULL;
    (void)name;

    /* alloca returns a pointer (capability) to the allocated space */
    Celestial_Type* cap_type = celestial_type_capability(b->module);
    Celestial_Instr* instr = celestial_instr_create(b, CIR_ALLOCA, cap_type, 0);
    if (instr == NULL) return NULL;

    /* Store the allocated type for later use (array indexing, field access) */
    instr->result->alloca_type = type;

    instr->effects = CIR_EFFECT_ALLOC;
    return instr->result;
}

Celestial_Value* celestial_build_load(Celestial_Builder* b,
                                       Celestial_Value* ptr,
                                       Celestial_Type* type,
                                       const char* name) {
    if (b == NULL || ptr == NULL || type == NULL) return NULL;
    (void)name;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_LOAD, type, 1);
    if (instr == NULL) return NULL;

    instr->operands[0] = ptr;
    instr->effects = CIR_EFFECT_READ;

    return instr->result;
}

void celestial_build_store(Celestial_Builder* b,
                           Celestial_Value* ptr,
                           Celestial_Value* value) {
    if (b == NULL || ptr == NULL || value == NULL) return;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_STORE, NULL, 2);
    if (instr == NULL) return;

    instr->operands[0] = ptr;
    instr->operands[1] = value;
    instr->effects = CIR_EFFECT_WRITE;
}

/*============================================================================
 * Type Field Offset
 *============================================================================*/

size_t celestial_type_field_offset(Celestial_Type* struct_type, size_t field_idx) {
    if (struct_type == NULL || struct_type->kind != CIR_TYPE_STRUCT) return 0;
    if (field_idx >= struct_type->struct_type.field_count) return 0;

    size_t offset = 0;
    for (size_t i = 0; i < field_idx; i++) {
        Celestial_Type* field_type = struct_type->struct_type.fields[i];
        size_t field_size = celestial_type_size(field_type);
        size_t field_align = celestial_type_align(field_type);
        /* Align to field's natural alignment */
        offset = (offset + field_align - 1) & ~(field_align - 1);
        offset += field_size;
    }
    /* Align to the target field's alignment */
    Celestial_Type* target_field = struct_type->struct_type.fields[field_idx];
    size_t target_align = celestial_type_align(target_field);
    return (offset + target_align - 1) & ~(target_align - 1);
}

/*============================================================================
 * Struct/Array Operations
 *============================================================================*/

Celestial_Value* celestial_build_gep(Celestial_Builder* b,
                                      Celestial_Value* struct_ptr,
                                      Celestial_Type* struct_type,
                                      size_t field_idx,
                                      const char* name) {
    if (b == NULL || struct_ptr == NULL || struct_type == NULL) return NULL;
    if (struct_type->kind != CIR_TYPE_STRUCT) return NULL;
    if (field_idx >= struct_type->struct_type.field_count) return NULL;
    (void)name;

    /* Calculate field offset */
    size_t offset = celestial_type_field_offset(struct_type, field_idx);

    /* Get the field type */
    Celestial_Type* field_type = struct_type->struct_type.fields[field_idx];

    /* Create a pointer type (capability) for the result */
    Celestial_Type* result_type = celestial_type_capability(b->module);

    /* Create GEP instruction with 2 operands: base ptr and offset */
    Celestial_Instr* instr = celestial_instr_create(b, CIR_GEP, result_type, 2);
    if (instr == NULL) return NULL;

    instr->operands[0] = struct_ptr;
    instr->operands[1] = celestial_const_i64(b->module, (int64_t)offset);

    /* Store field type info in a way backends can use */
    /* We'll use the unused callee field to store the struct type */
    /* This is a hack - in a real implementation we'd have a proper field */
    (void)field_type;

    return instr->result;
}

Celestial_Value* celestial_build_extractfield(Celestial_Builder* b,
                                               Celestial_Value* struct_val,
                                               size_t field_idx,
                                               const char* name) {
    if (b == NULL || struct_val == NULL) return NULL;
    if (struct_val->type == NULL || struct_val->type->kind != CIR_TYPE_STRUCT) return NULL;
    if (field_idx >= struct_val->type->struct_type.field_count) return NULL;
    (void)name;

    Celestial_Type* field_type = struct_val->type->struct_type.fields[field_idx];

    Celestial_Instr* instr = celestial_instr_create(b, CIR_EXTRACTFIELD, field_type, 2);
    if (instr == NULL) return NULL;

    instr->operands[0] = struct_val;
    instr->operands[1] = celestial_const_i64(b->module, (int64_t)field_idx);

    return instr->result;
}

Celestial_Value* celestial_build_insertfield(Celestial_Builder* b,
                                              Celestial_Value* struct_val,
                                              Celestial_Value* field_val,
                                              size_t field_idx,
                                              const char* name) {
    if (b == NULL || struct_val == NULL || field_val == NULL) return NULL;
    if (struct_val->type == NULL || struct_val->type->kind != CIR_TYPE_STRUCT) return NULL;
    if (field_idx >= struct_val->type->struct_type.field_count) return NULL;
    (void)name;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_INSERTFIELD, struct_val->type, 3);
    if (instr == NULL) return NULL;

    instr->operands[0] = struct_val;
    instr->operands[1] = field_val;
    instr->operands[2] = celestial_const_i64(b->module, (int64_t)field_idx);

    return instr->result;
}

/*============================================================================
 * Array Operations
 *============================================================================*/

Celestial_Value* celestial_build_array_gep(Celestial_Builder* b,
                                            Celestial_Value* array_ptr,
                                            Celestial_Type* array_type,
                                            Celestial_Value* index,
                                            const char* name) {
    if (b == NULL || array_ptr == NULL || array_type == NULL || index == NULL) return NULL;
    if (array_type->kind != CIR_TYPE_ARRAY) return NULL;
    (void)name;

    /* Get element type and size */
    Celestial_Type* elem_type = array_type->array_type.elem_type;
    size_t elem_size = celestial_type_size(elem_type);

    /* Result is a pointer (capability) to the element */
    Celestial_Type* result_type = celestial_type_capability(b->module);

    /* Create instruction with 3 operands: base ptr, index, element size */
    Celestial_Instr* instr = celestial_instr_create(b, CIR_GEP, result_type, 3);
    if (instr == NULL) return NULL;

    instr->operands[0] = array_ptr;
    instr->operands[1] = index;
    instr->operands[2] = celestial_const_i64(b->module, (int64_t)elem_size);

    /* Mark this as array GEP by setting operand_count to 3 */
    /* (struct GEP has 2 operands) */

    return instr->result;
}

Celestial_Value* celestial_build_extractelem(Celestial_Builder* b,
                                              Celestial_Value* array_val,
                                              Celestial_Value* index,
                                              const char* name) {
    if (b == NULL || array_val == NULL || index == NULL) return NULL;
    if (array_val->type == NULL || array_val->type->kind != CIR_TYPE_ARRAY) return NULL;
    (void)name;

    Celestial_Type* elem_type = array_val->type->array_type.elem_type;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_EXTRACTELEM, elem_type, 2);
    if (instr == NULL) return NULL;

    instr->operands[0] = array_val;
    instr->operands[1] = index;

    return instr->result;
}

Celestial_Value* celestial_build_insertelem(Celestial_Builder* b,
                                             Celestial_Value* array_val,
                                             Celestial_Value* elem_val,
                                             Celestial_Value* index,
                                             const char* name) {
    if (b == NULL || array_val == NULL || elem_val == NULL || index == NULL) return NULL;
    if (array_val->type == NULL || array_val->type->kind != CIR_TYPE_ARRAY) return NULL;
    (void)name;

    Celestial_Instr* instr = celestial_instr_create(b, CIR_INSERTELEM, array_val->type, 3);
    if (instr == NULL) return NULL;

    instr->operands[0] = array_val;
    instr->operands[1] = elem_val;
    instr->operands[2] = index;

    return instr->result;
}

/*============================================================================
 * Verification
 *============================================================================*/

Seraph_Vbit celestial_verify_module(Celestial_Module* mod) {
    if (mod == NULL) return SERAPH_VBIT_FALSE;

    for (Celestial_Function* fn = mod->functions; fn != NULL; fn = fn->next) {
        if (celestial_verify_function(fn) != SERAPH_VBIT_TRUE) {
            return SERAPH_VBIT_FALSE;
        }
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit celestial_verify_function(Celestial_Function* fn) {
    if (fn == NULL) {
        fprintf(stderr, "verify: function is NULL\n");
        return SERAPH_VBIT_FALSE;
    }

    /* Check that function has at least one block */
    if (fn->blocks == NULL) {
        fprintf(stderr, "verify: function '%.*s' has no blocks\n",
                (int)fn->name_len, fn->name);
        return SERAPH_VBIT_FALSE;
    }

    /* Check that each block has a terminator */
    for (Celestial_Block* block = fn->blocks; block != NULL; block = block->next) {
        if (block->last == NULL) {
            fprintf(stderr, "verify: block '%s' in function '%.*s' has no instructions\n",
                    block->name ? block->name : "<anon>",
                    (int)fn->name_len, fn->name);
            return SERAPH_VBIT_FALSE;
        }

        /* Last instruction must be a terminator */
        Celestial_Opcode op = block->last->opcode;
        if (op != CIR_JUMP && op != CIR_BRANCH && op != CIR_RETURN &&
            op != CIR_UNREACHABLE && op != CIR_SWITCH) {
            fprintf(stderr, "verify: block '%s' in function '%.*s' doesn't end with terminator (op=%d)\n",
                    block->name ? block->name : "<anon>",
                    (int)fn->name_len, fn->name, op);
            return SERAPH_VBIT_FALSE;
        }
    }

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * Debug Output
 *============================================================================*/

static const char* celestial_opcode_name(Celestial_Opcode op) {
    switch (op) {
        case CIR_ADD:           return "add";
        case CIR_SUB:           return "sub";
        case CIR_MUL:           return "mul";
        case CIR_DIV:           return "div";
        case CIR_MOD:           return "mod";
        case CIR_NEG:           return "neg";
        case CIR_AND:           return "and";
        case CIR_OR:            return "or";
        case CIR_XOR:           return "xor";
        case CIR_NOT:           return "not";
        case CIR_SHL:           return "shl";
        case CIR_SHR:           return "shr";
        case CIR_SAR:           return "sar";
        case CIR_EQ:            return "eq";
        case CIR_NE:            return "ne";
        case CIR_LT:            return "lt";
        case CIR_LE:            return "le";
        case CIR_GT:            return "gt";
        case CIR_GE:            return "ge";
        case CIR_VOID_TEST:     return "void.test";
        case CIR_VOID_PROP:     return "void.prop";
        case CIR_VOID_ASSERT:   return "void.assert";
        case CIR_VOID_COALESCE: return "void.coalesce";
        case CIR_CAP_CREATE:    return "cap.create";
        case CIR_CAP_LOAD:      return "cap.load";
        case CIR_CAP_STORE:     return "cap.store";
        case CIR_SUBSTRATE_ENTER: return "substrate.enter";
        case CIR_SUBSTRATE_EXIT:  return "substrate.exit";
        case CIR_ATLAS_BEGIN:   return "atlas.begin";
        case CIR_ATLAS_COMMIT:  return "atlas.commit";
        case CIR_GALACTIC_ADD:  return "galactic.add";
        case CIR_GALACTIC_MUL:  return "galactic.mul";
        case CIR_GALACTIC_PREDICT: return "galactic.predict";
        case CIR_JUMP:          return "jump";
        case CIR_BRANCH:        return "branch";
        case CIR_CALL:          return "call";
        case CIR_SYSCALL:       return "syscall";
        case CIR_RETURN:        return "return";
        case CIR_LOAD:          return "load";
        case CIR_STORE:         return "store";
        case CIR_ALLOCA:        return "alloca";
        case CIR_GEP:           return "gep";
        case CIR_EXTRACTFIELD:  return "extractfield";
        case CIR_INSERTFIELD:   return "insertfield";
        case CIR_CALL_INDIRECT: return "call.indirect";
        case CIR_ZEXT:          return "zext";
        case CIR_SEXT:          return "sext";
        case CIR_TRUNC:         return "trunc";
        case CIR_BITCAST:       return "bitcast";
        default:                return "unknown";
    }
}

void celestial_print_function(Celestial_Function* fn, FILE* out) {
    if (fn == NULL || out == NULL) return;

    fprintf(out, "fn %.*s {\n", (int)fn->name_len, fn->name);

    for (Celestial_Block* block = fn->blocks; block != NULL; block = block->next) {
        fprintf(out, "  block_%u:\n", block->id);

        for (Celestial_Instr* instr = block->first; instr != NULL; instr = instr->next) {
            fprintf(out, "    ");

            if (instr->result != NULL) {
                fprintf(out, "%%v%u = ", instr->result->id);
            }

            fprintf(out, "%s", celestial_opcode_name(instr->opcode));

            for (size_t i = 0; i < instr->operand_count; i++) {
                Celestial_Value* op = instr->operands[i];
                if (op->kind == CIR_VALUE_VREG) {
                    fprintf(out, " %%v%u", op->id);
                } else if (op->kind == CIR_VALUE_CONST) {
                    fprintf(out, " %lld", (long long)op->constant.i64);
                } else if (op->kind == CIR_VALUE_PARAM) {
                    fprintf(out, " %%arg%u", op->param.index);
                }
                if (i < instr->operand_count - 1) fprintf(out, ",");
            }

            if (instr->target1 != NULL) {
                fprintf(out, " -> block_%u", instr->target1->id);
            }
            if (instr->target2 != NULL) {
                fprintf(out, ", block_%u", instr->target2->id);
            }

            fprintf(out, "\n");
        }
    }

    fprintf(out, "}\n\n");
}

void celestial_print_module(Celestial_Module* mod, FILE* out) {
    if (mod == NULL || out == NULL) return;

    fprintf(out, "; Celestial IR Module: %.*s\n", (int)mod->name_len, mod->name);
    fprintf(out, "; SERAPH Native Compiler\n\n");

    for (Celestial_Function* fn = mod->functions; fn != NULL; fn = fn->next) {
        celestial_print_function(fn, out);
    }
}

/*============================================================================
 * Optimization Passes
 *============================================================================*/

/**
 * @brief Check if a value is a compile-time constant
 */
static int is_constant(Celestial_Value* val) {
    return val != NULL && val->kind == CIR_VALUE_CONST;
}

/**
 * @brief Try to fold a binary operation on constants
 *
 * Returns a new constant value if both operands are constants,
 * or NULL if folding is not possible.
 */
static Celestial_Value* try_fold_binary(Celestial_Module* mod,
                                         Celestial_Opcode op,
                                         Celestial_Value* left,
                                         Celestial_Value* right) {
    if (!is_constant(left) || !is_constant(right)) return NULL;

    /* Only fold integer operations for now */
    if (left->type->kind < CIR_TYPE_I8 || left->type->kind > CIR_TYPE_U64) return NULL;
    if (right->type->kind < CIR_TYPE_I8 || right->type->kind > CIR_TYPE_U64) return NULL;

    int64_t l = left->constant.i64;
    int64_t r = right->constant.i64;
    int64_t result = 0;

    switch (op) {
        case CIR_ADD:
            result = l + r;
            break;
        case CIR_SUB:
            result = l - r;
            break;
        case CIR_MUL:
            result = l * r;
            break;
        case CIR_DIV:
            if (r == 0) return NULL;  /* Cannot fold division by zero */
            result = l / r;
            break;
        case CIR_MOD:
            if (r == 0) return NULL;
            result = l % r;
            break;
        case CIR_AND:
            result = l & r;
            break;
        case CIR_OR:
            result = l | r;
            break;
        case CIR_XOR:
            result = l ^ r;
            break;
        case CIR_SHL:
            result = l << r;
            break;
        case CIR_SHR:
            result = l >> r;
            break;
        case CIR_EQ:
            return celestial_const_bool(mod, l == r);
        case CIR_NE:
            return celestial_const_bool(mod, l != r);
        case CIR_LT:
            return celestial_const_bool(mod, l < r);
        case CIR_LE:
            return celestial_const_bool(mod, l <= r);
        case CIR_GT:
            return celestial_const_bool(mod, l > r);
        case CIR_GE:
            return celestial_const_bool(mod, l >= r);
        default:
            return NULL;  /* Cannot fold this operation */
    }

    return celestial_const_i64(mod, result);
}

/**
 * @brief Try to fold a unary operation on a constant
 */
static Celestial_Value* try_fold_unary(Celestial_Module* mod,
                                        Celestial_Opcode op,
                                        Celestial_Value* operand) {
    if (!is_constant(operand)) return NULL;

    int64_t val = operand->constant.i64;
    int64_t result = 0;

    switch (op) {
        case CIR_NEG:
            result = -val;
            break;
        case CIR_NOT:
            result = ~val;
            break;
        default:
            return NULL;
    }

    return celestial_const_i64(mod, result);
}

/**
 * @brief Perform constant folding on a single instruction
 *
 * Returns 1 if the instruction was modified, 0 otherwise.
 */
static int fold_instruction(Celestial_Module* mod, Celestial_Instr* instr) {
    if (instr == NULL || instr->result == NULL) return 0;

    Celestial_Value* folded = NULL;

    /* Try to fold binary operations */
    if (instr->operand_count == 2) {
        folded = try_fold_binary(mod, instr->opcode,
                                  instr->operands[0], instr->operands[1]);
    }
    /* Try to fold unary operations */
    else if (instr->operand_count == 1) {
        folded = try_fold_unary(mod, instr->opcode, instr->operands[0]);
    }

    if (folded != NULL) {
        /* Replace the instruction's result with the folded constant */
        /* Copy the constant value into the existing result */
        instr->result->kind = CIR_VALUE_CONST;
        instr->result->constant.i64 = folded->constant.i64;
        instr->result->type = folded->type;

        /* Mark instruction as a no-op by changing to a NOP opcode */
        instr->opcode = CIR_NOP;
        return 1;
    }

    return 0;
}

/**
 * @brief Run constant folding optimization on a module
 *
 * This pass evaluates constant expressions at compile time,
 * replacing operations like `2 + 3` with the result `5`.
 *
 * Returns the number of instructions folded.
 */
int celestial_fold_constants(Celestial_Module* mod) {
    if (mod == NULL) return 0;

    int folded_count = 0;

    /* Iterate through all functions */
    for (Celestial_Function* fn = mod->functions; fn != NULL; fn = fn->next) {
        /* Iterate through all blocks */
        for (Celestial_Block* block = fn->blocks; block != NULL; block = block->next) {
            /* Iterate through all instructions */
            for (Celestial_Instr* instr = block->first; instr != NULL; instr = instr->next) {
                if (fold_instruction(mod, instr)) {
                    folded_count++;
                }
            }
        }
    }

    return folded_count;
}

/**
 * @brief Check if an instruction has side effects
 */
static int has_side_effects(Celestial_Instr* instr) {
    if (instr == NULL) return 0;

    /* Check explicit effect flags */
    if (instr->effects != 0) return 1;

    /* Check opcode for inherent side effects */
    switch (instr->opcode) {
        /* Control flow */
        case CIR_JUMP:
        case CIR_BRANCH:
        case CIR_RETURN:
        case CIR_CALL:
        case CIR_CALL_INDIRECT:
        case CIR_SYSCALL:
        case CIR_TAIL_CALL:
        case CIR_TRAP:
        case CIR_UNREACHABLE:
            return 1;

        /* Memory writes */
        case CIR_STORE:
        case CIR_CAP_STORE:
        case CIR_ATLAS_STORE:
        case CIR_AETHER_STORE:
        case CIR_MEMCPY:
        case CIR_MEMSET:
            return 1;

        /* Substrate and Atlas */
        case CIR_SUBSTRATE_ENTER:
        case CIR_SUBSTRATE_EXIT:
        case CIR_ATLAS_BEGIN:
        case CIR_ATLAS_COMMIT:
        case CIR_ATLAS_ROLLBACK:
        case CIR_AETHER_SYNC:
            return 1;

        /* Capability operations with side effects */
        case CIR_CAP_REVOKE:
            return 1;

        default:
            return 0;
    }
}

/**
 * @brief Mark a value as live and propagate to its operands
 */
static void mark_live(Celestial_Value** live_set, size_t* live_count,
                      size_t live_capacity, Celestial_Value* val) {
    if (val == NULL) return;
    if (val->kind == CIR_VALUE_CONST) return;  /* Constants don't need marking */

    /* Check if already marked */
    for (size_t i = 0; i < *live_count; i++) {
        if (live_set[i] == val) return;
    }

    /* Add to live set */
    if (*live_count < live_capacity) {
        live_set[(*live_count)++] = val;
    }
}

/**
 * @brief Run dead code elimination on a module
 *
 * This pass removes instructions whose results are never used
 * and have no side effects.
 *
 * Returns the number of instructions eliminated.
 */
int celestial_eliminate_dead_code(Celestial_Module* mod) {
    if (mod == NULL) return 0;

    int eliminated_count = 0;

    /* Process each function */
    for (Celestial_Function* fn = mod->functions; fn != NULL; fn = fn->next) {
        /* Allocate live set (simple array for now) */
        size_t live_capacity = 4096;
        Celestial_Value** live_set = malloc(live_capacity * sizeof(Celestial_Value*));
        if (!live_set) continue;
        size_t live_count = 0;

        /* First pass: mark operands of essential instructions as live */
        for (Celestial_Block* block = fn->blocks; block != NULL; block = block->next) {
            for (Celestial_Instr* instr = block->first; instr != NULL; instr = instr->next) {
                /* Skip already-eliminated instructions */
                if (instr->opcode == CIR_NOP) continue;

                /* If instruction has side effects, its operands are live */
                if (has_side_effects(instr)) {
                    for (size_t i = 0; i < instr->operand_count; i++) {
                        mark_live(live_set, &live_count, live_capacity,
                                  instr->operands[i]);
                    }
                }
            }
        }

        /* Second pass: propagate liveness - iterate until no changes */
        int changed = 1;
        while (changed) {
            changed = 0;

            for (Celestial_Block* block = fn->blocks; block != NULL; block = block->next) {
                for (Celestial_Instr* instr = block->first; instr != NULL; instr = instr->next) {
                    if (instr->opcode == CIR_NOP) continue;
                    if (instr->result == NULL) continue;

                    /* Check if result is live */
                    int result_live = 0;
                    for (size_t i = 0; i < live_count; i++) {
                        if (live_set[i] == instr->result) {
                            result_live = 1;
                            break;
                        }
                    }

                    /* If result is live, operands become live */
                    if (result_live) {
                        for (size_t i = 0; i < instr->operand_count; i++) {
                            size_t old_count = live_count;
                            mark_live(live_set, &live_count, live_capacity,
                                      instr->operands[i]);
                            if (live_count > old_count) changed = 1;
                        }
                    }
                }
            }
        }

        /* Third pass: eliminate dead instructions */
        for (Celestial_Block* block = fn->blocks; block != NULL; block = block->next) {
            for (Celestial_Instr* instr = block->first; instr != NULL; instr = instr->next) {
                if (instr->opcode == CIR_NOP) continue;
                if (instr->result == NULL) continue;  /* No result = side effect or terminator */
                if (has_side_effects(instr)) continue;

                /* Check if result is dead */
                int result_live = 0;
                for (size_t i = 0; i < live_count; i++) {
                    if (live_set[i] == instr->result) {
                        result_live = 1;
                        break;
                    }
                }

                if (!result_live) {
                    /* Dead instruction - mark as NOP */
                    instr->opcode = CIR_NOP;
                    eliminated_count++;
                }
            }
        }

        free(live_set);
    }

    return eliminated_count;
}
