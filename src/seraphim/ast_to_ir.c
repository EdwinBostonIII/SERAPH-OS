/**
 * @file ast_to_ir.c
 * @brief AST to Celestial IR Converter Implementation
 *
 * This is the bridge between the Seraphim frontend and backend.
 * It walks the AST and generates Celestial IR with proper:
 * - VOID handling for all operations
 * - Capability-based memory access
 * - Substrate context management
 * - Effect tracking
 */

#include "seraph/seraphim/ast_to_ir.h"
#include "seraph/seraphim/token.h"
#include <string.h>
#include <stdio.h>

/*============================================================================
 * Forward Declarations
 *============================================================================*/

static Celestial_Value* ir_convert_int_lit(IR_Context* ctx, Seraph_AST_Node* node);
static Celestial_Value* ir_convert_float_lit(IR_Context* ctx, Seraph_AST_Node* node);
static Celestial_Value* ir_convert_bool_lit(IR_Context* ctx, Seraph_AST_Node* node);
static Celestial_Value* ir_convert_string_lit(IR_Context* ctx, Seraph_AST_Node* node);
static Celestial_Value* ir_convert_ident(IR_Context* ctx, Seraph_AST_Node* node);
static Celestial_Value* ir_convert_field_access(IR_Context* ctx, Seraph_AST_Node* node);
static Celestial_Value* ir_convert_index(IR_Context* ctx, Seraph_AST_Node* node);
static Celestial_Value* ir_convert_array_lit(IR_Context* ctx, Seraph_AST_Node* node);
static Celestial_Value* ir_convert_struct_init(IR_Context* ctx, Seraph_AST_Node* node);
static Celestial_Value* ir_convert_closure(IR_Context* ctx, Seraph_AST_Node* node);
static Celestial_Value* ir_convert_cast(IR_Context* ctx, Seraph_AST_Node* node);

static void ir_set_error(IR_Context* ctx, const char* msg);
static int ir_block_is_terminated(Celestial_Block* block);
static char* ir_temp_name(IR_Context* ctx);

/*============================================================================
 * Context Management
 *============================================================================*/

Seraph_Vbit ir_context_init(IR_Context* ctx, Seraph_Arena* arena,
                            Seraph_Type_Context* types) {
    if (!ctx || !arena) {
        return SERAPH_VBIT_FALSE;
    }

    memset(ctx, 0, sizeof(IR_Context));
    ctx->arena = arena;
    ctx->types = types;
    ctx->temp_counter = 0;

    /* Initialize struct registry for method self parameter type lookup */
    ctx->struct_capacity = 32;
    ctx->struct_types = seraph_arena_alloc(arena,
                                           ctx->struct_capacity * sizeof(Celestial_Type*),
                                           _Alignof(Celestial_Type*));
    ctx->struct_names = seraph_arena_alloc(arena,
                                           ctx->struct_capacity * sizeof(const char*),
                                           _Alignof(const char*));
    if (!ctx->struct_types || !ctx->struct_names) {
        return SERAPH_VBIT_FALSE;
    }
    ctx->struct_count = 0;

    /* Initialize enum variant registry for enum value lookup */
    ctx->enum_variant_capacity = 256;
    ctx->enum_variant_names = seraph_arena_alloc(arena,
                                                  ctx->enum_variant_capacity * sizeof(const char*),
                                                  _Alignof(const char*));
    ctx->enum_variant_name_lens = seraph_arena_alloc(arena,
                                                      ctx->enum_variant_capacity * sizeof(size_t),
                                                      _Alignof(size_t));
    ctx->enum_variant_values = seraph_arena_alloc(arena,
                                                   ctx->enum_variant_capacity * sizeof(int64_t),
                                                   _Alignof(int64_t));
    if (!ctx->enum_variant_names || !ctx->enum_variant_name_lens || !ctx->enum_variant_values) {
        return SERAPH_VBIT_FALSE;
    }
    ctx->enum_variant_count = 0;

    return SERAPH_VBIT_TRUE;
}

void ir_context_cleanup(IR_Context* ctx) {
    if (!ctx) return;
    /* Arena handles memory cleanup */
    memset(ctx, 0, sizeof(IR_Context));
}

void ir_scope_push(IR_Context* ctx) {
    IR_Scope* scope = seraph_arena_alloc(ctx->arena, sizeof(IR_Scope),
                                          _Alignof(IR_Scope));
    if (!scope) return;

    scope->symbols = NULL;
    scope->parent = ctx->scope;
    ctx->scope = scope;
}

void ir_scope_pop(IR_Context* ctx) {
    if (ctx->scope) {
        ctx->scope = ctx->scope->parent;
    }
}

Seraph_Vbit ir_symbol_add(IR_Context* ctx, const char* name, size_t name_len,
                          Celestial_Value* value, Celestial_Type* type,
                          int is_mutable) {
    if (!ctx || !ctx->scope || !name || !value) {
        return SERAPH_VBIT_FALSE;
    }

    IR_Symbol* sym = seraph_arena_alloc(ctx->arena, sizeof(IR_Symbol), _Alignof(IR_Symbol));
    if (!sym) return SERAPH_VBIT_FALSE;

    sym->name = name;
    sym->name_len = name_len;
    sym->value = value;
    sym->type = type;
    sym->is_mutable = is_mutable;
    sym->next = ctx->scope->symbols;
    ctx->scope->symbols = sym;

    return SERAPH_VBIT_TRUE;
}

IR_Symbol* ir_symbol_lookup(IR_Context* ctx, const char* name, size_t name_len) {
    if (!ctx || !name) return NULL;

    /* Search through all scopes from innermost to outermost */
    for (IR_Scope* scope = ctx->scope; scope; scope = scope->parent) {
        for (IR_Symbol* sym = scope->symbols; sym; sym = sym->next) {
            if (sym->name_len == name_len &&
                memcmp(sym->name, name, name_len) == 0) {
                return sym;
            }
        }
    }

    return NULL;
}

static void ir_set_error(IR_Context* ctx, const char* msg) {
    ctx->has_error = 1;
    ctx->error_msg = msg;
}

static char* ir_temp_name(IR_Context* ctx) {
    char* buf = seraph_arena_alloc(ctx->arena, 32, 1);
    if (buf) {
        snprintf(buf, 32, "t%u", ctx->temp_counter++);
    }
    return buf;
}

/*============================================================================
 * Type Conversion
 *============================================================================*/

Celestial_Type* ir_type_from_primitive(IR_Context* ctx, Seraph_Token_Type prim) {
    Celestial_Type_Kind kind;

    switch (prim) {
        case SERAPH_TOK_BOOL:   kind = CIR_TYPE_BOOL; break;
        case SERAPH_TOK_I8:     kind = CIR_TYPE_I8;   break;
        case SERAPH_TOK_I16:    kind = CIR_TYPE_I16;  break;
        case SERAPH_TOK_I32:    kind = CIR_TYPE_I32;  break;
        case SERAPH_TOK_I64:    kind = CIR_TYPE_I64;  break;
        case SERAPH_TOK_U8:     kind = CIR_TYPE_U8;   break;
        case SERAPH_TOK_U16:    kind = CIR_TYPE_U16;  break;
        case SERAPH_TOK_U32:    kind = CIR_TYPE_U32;  break;
        case SERAPH_TOK_U64:    kind = CIR_TYPE_U64;  break;
        case SERAPH_TOK_SCALAR: kind = CIR_TYPE_SCALAR; break;
        case SERAPH_TOK_DUAL:   kind = CIR_TYPE_DUAL; break;
        case SERAPH_TOK_GALACTIC: kind = CIR_TYPE_GALACTIC; break;
        /* Bootstrap floating-point types - map to integers for now */
        case SERAPH_TOK_F32:    kind = CIR_TYPE_I32; break;
        case SERAPH_TOK_F64:    kind = CIR_TYPE_I64; break;
        default:
            ir_set_error(ctx, "Unknown primitive type");
            return NULL;
    }

    return celestial_type_primitive(ctx->module, kind);
}

Celestial_Type* ir_convert_type(IR_Context* ctx, Seraph_AST_Node* type_node) {
    if (!type_node) {
        /* Default to void/unit type */
        return celestial_type_primitive(ctx->module, CIR_TYPE_VOID);
    }

    switch (type_node->hdr.kind) {
        case AST_TYPE_PRIMITIVE:
            return ir_type_from_primitive(ctx, type_node->prim_type.prim);

        case AST_TYPE_NAMED: {
            /* Look up named type in module's type registry or struct registry */
            const char* name = type_node->named_type.name;
            size_t name_len = type_node->named_type.name_len;

            /* First check the module's types array for both structs and enums */
            for (size_t i = 0; i < ctx->module->type_count; i++) {
                Celestial_Type* t = ctx->module->types[i];
                if (!t) continue;

                if (t->kind == CIR_TYPE_STRUCT &&
                    t->struct_type.name_len == name_len &&
                    memcmp(t->struct_type.name, name, name_len) == 0) {
                    return t;
                }
                if (t->kind == CIR_TYPE_ENUM &&
                    t->enum_type.name_len == name_len &&
                    memcmp(t->enum_type.name, name, name_len) == 0) {
                    return t;
                }
            }

            /* Also check the context's struct registry */
            for (size_t i = 0; i < ctx->struct_count; i++) {
                if (ctx->struct_names[i] &&
                    strlen(ctx->struct_names[i]) == name_len &&
                    memcmp(ctx->struct_names[i], name, name_len) == 0) {
                    return ctx->struct_types[i];
                }
            }

            /* For enum types used only as discriminants (no payload),
             * treat as i64 for matching/comparison purposes */
            for (size_t i = 0; i < ctx->enum_variant_count; i++) {
                /* Check if there's any variant that matches this type name pattern
                 * This is a bootstrap heuristic for self-hosting */
            }

            /* Named type not found - return i64 as fallback for bootstrap
             * This handles recursive struct types like AstNode in self-hosting */
            return celestial_type_primitive(ctx->module, CIR_TYPE_I64);
        }

        case AST_TYPE_ARRAY: {
            Celestial_Type* elem = ir_convert_type(ctx,
                                                    type_node->array_type.elem_type);
            if (!elem) return NULL;

            /* Get array size from expression */
            /* For now, assume constant size */
            size_t len = 0;
            if (type_node->array_type.size &&
                type_node->array_type.size->hdr.kind == AST_EXPR_INT_LIT) {
                len = (size_t)type_node->array_type.size->int_lit.value;
            }

            return celestial_type_array(ctx->module, elem, len);
        }

        case AST_TYPE_VOID_ABLE: {
            Celestial_Type* inner = ir_convert_type(ctx,
                                                     type_node->void_type.inner);
            if (!inner) return NULL;
            return celestial_type_voidable(ctx->module, inner);
        }

        case AST_TYPE_REF:
        case AST_TYPE_MUT_REF:
            /* References become capabilities */
            return celestial_type_capability(ctx->module);

        case AST_TYPE_POINTER: {
            /* Pointer types: *T -> proper pointer type for field access support
             * For recursive types (like *AstNode in AstNode), the pointee type
             * might not exist yet. Try to resolve it, but fall back to i64 if needed. */
            int saved_error = ctx->has_error;
            const char* saved_msg = ctx->error_msg;
            ctx->has_error = 0;
            ctx->error_msg = NULL;

            Celestial_Type* pointee = ir_convert_type(ctx, type_node->ref_type.inner);

            if (ctx->has_error || !pointee) {
                /* Pointee type not found - this is OK for forward references
                 * Use i64 as a placeholder (pointers are just addresses) */
                ctx->has_error = saved_error;
                ctx->error_msg = saved_msg;
                pointee = celestial_type_primitive(ctx->module, CIR_TYPE_I64);
            }
            return celestial_type_pointer(ctx->module, pointee);
        }

        case AST_TYPE_FN: {
            /* Function type: fn(A, B) -> C */
            Celestial_Type* ret = NULL;
            if (type_node->fn_type.ret) {
                ret = ir_convert_type(ctx, type_node->fn_type.ret);
            } else {
                ret = celestial_type_primitive(ctx->module, CIR_TYPE_VOID);
            }
            if (!ret) return NULL;

            /* Convert parameter types */
            size_t param_count = type_node->fn_type.param_count;
            Celestial_Type** param_types = NULL;

            if (param_count > 0) {
                param_types = seraph_arena_alloc(ctx->arena,
                                                  param_count * sizeof(Celestial_Type*),
                                                  _Alignof(Celestial_Type*));
                if (!param_types) return NULL;

                size_t i = 0;
                for (Seraph_AST_Node* p = type_node->fn_type.params; p; p = p->hdr.next) {
                    param_types[i] = ir_convert_type(ctx, p);
                    if (!param_types[i]) return NULL;
                    i++;
                }
            }

            return celestial_type_function(ctx->module, ret, param_types,
                                            param_count, CIR_EFFECT_IO);
        }

        default:
            ir_set_error(ctx, "Unsupported type in IR generation");
            return NULL;
    }
}

/*============================================================================
 * Expression Conversion
 *============================================================================*/

static Celestial_Value* ir_convert_int_lit(IR_Context* ctx,
                                            Seraph_AST_Node* node) {
    int64_t val = (int64_t)node->int_lit.value;
    return celestial_const_i64(ctx->module, val);
}

static Celestial_Value* ir_convert_float_lit(IR_Context* ctx,
                                              Seraph_AST_Node* node) {
    /* Convert float to scalar (Q64.64) */
    /* For now, use i64 representation */
    int64_t fixed = (int64_t)(node->float_lit.value * (double)(1LL << 32));
    return celestial_const_i64(ctx->module, fixed);
}

static Celestial_Value* ir_convert_bool_lit(IR_Context* ctx,
                                             Seraph_AST_Node* node) {
    return celestial_const_bool(ctx->module, node->bool_lit.value);
}

static Celestial_Value* ir_convert_string_lit(IR_Context* ctx,
                                               Seraph_AST_Node* node) {
    /* Get string data from AST (raw, with escape sequences not yet processed) */
    const char* raw_data = node->string_lit.value;
    size_t raw_len = node->string_lit.len;

    /* Add string to module's constant pool (escapes processed internally) */
    Celestial_String_Const* str_const = celestial_add_string_const(ctx->module,
                                                                    raw_data, raw_len);
    if (!str_const) {
        ir_set_error(ctx, "Failed to add string constant");
        return NULL;
    }

    /* Return string constant value */
    return celestial_const_string(ctx->module, str_const);
}

static Celestial_Value* ir_convert_ident(IR_Context* ctx,
                                          Seraph_AST_Node* node) {
    IR_Symbol* sym = ir_symbol_lookup(ctx, node->ident.name,
                                       node->ident.name_len);
    if (!sym) {
        /* Not a variable - check if it's a function reference */
        const char* fn_name = node->ident.name;
        size_t fn_name_len = node->ident.name_len;

        for (Celestial_Function* fn = ctx->module->functions; fn; fn = fn->next) {
            if (fn->name_len == fn_name_len &&
                memcmp(fn->name, fn_name, fn_name_len) == 0) {
                /* Found function - return function pointer value */
                return celestial_get_fn_ptr(ctx->module, fn);
            }
        }

        /* Check if it's an enum variant */
        for (size_t i = 0; i < ctx->enum_variant_count; i++) {
            if (ctx->enum_variant_name_lens[i] == fn_name_len &&
                memcmp(ctx->enum_variant_names[i], fn_name, fn_name_len) == 0) {
                /* Found enum variant - return its discriminant value */
                return celestial_const_i64(ctx->module, ctx->enum_variant_values[i]);
            }
        }

        /* Print the identifier name for debugging */
        fprintf(stderr, "Undefined identifier: '%.*s'\n",
                (int)node->ident.name_len, node->ident.name);
        ir_set_error(ctx, "Undefined identifier");
        return NULL;
    }

    /* For mutable variables (stored via alloca), we need to load the value */
    if (sym->is_mutable) {
        /* Load from the alloca */
        return celestial_build_load(&ctx->builder, sym->value, sym->type,
                                     ir_temp_name(ctx));
    }

    /* For immutable variables, just return the SSA value */
    return sym->value;
}

static Celestial_Value* ir_convert_field_access(IR_Context* ctx,
                                                 Seraph_AST_Node* node) {
    /* For field access, we need the POINTER to the object, not the loaded value.
     * If the object is a simple identifier (variable), get its alloca directly
     * instead of loading it. */
    Celestial_Value* obj = NULL;

    if (node->field.object->hdr.kind == AST_EXPR_IDENT) {
        /* Look up the variable directly to get its alloca pointer */
        IR_Symbol* sym = ir_symbol_lookup(ctx, node->field.object->ident.name,
                                           node->field.object->ident.name_len);
        if (sym && sym->value) {
            obj = sym->value;  /* This is the alloca, not a loaded value */
        }
    }

    /* Fall back to normal expression conversion for non-identifier cases
     * (e.g., function returns, nested field access) */
    if (!obj) {
        obj = ir_convert_expr(ctx, node->field.object);
    }
    if (!obj) return NULL;

    /* Get the struct type from the object
     * The object should be an alloca'd struct, so get its type from alloca_type */
    Celestial_Type* struct_type = obj->alloca_type;

    /* If obj is a load result, the struct type should be in obj->type */
    if (!struct_type) {
        struct_type = obj->type;
    }

    /* Handle pointer types - auto-dereference to get to the struct
     * This supports ptr.field syntax where ptr: *Struct */
    if (struct_type && struct_type->kind == CIR_TYPE_POINTER) {
        /* Get the pointee type */
        Celestial_Type* pointee = struct_type->pointer_type.pointee_type;
        if (pointee && pointee->kind == CIR_TYPE_STRUCT) {
            struct_type = pointee;
            /* obj is already a pointer value, use it directly for GEP */
        } else {
            ir_set_error(ctx, "Pointer does not point to struct type");
            return NULL;
        }
    }

    /* Handle reference/capability types - the struct type is in alloca_type
     * This happens for method self parameters like self: &Counter */
    if (struct_type && struct_type->kind == CIR_TYPE_CAPABILITY) {
        /* The underlying struct type should be stored in alloca_type */
        if (obj->alloca_type && obj->alloca_type->kind == CIR_TYPE_STRUCT) {
            struct_type = obj->alloca_type;
        }
    }

    if (!struct_type || struct_type->kind != CIR_TYPE_STRUCT) {
        ir_set_error(ctx, "Field access on non-struct type");
        return NULL;
    }

    /* Find the field index by name */
    const char* field_name = node->field.field;
    size_t field_name_len = node->field.field_len;
    int field_idx = -1;

    for (size_t i = 0; i < struct_type->struct_type.field_count; i++) {
        const char* sf_name = struct_type->struct_type.field_names[i];
        if (sf_name && strlen(sf_name) == field_name_len &&
            memcmp(sf_name, field_name, field_name_len) == 0) {
            field_idx = (int)i;
            break;
        }
    }

    if (field_idx < 0) {
        ir_set_error(ctx, "Unknown struct field");
        return NULL;
    }

    /* Get the field type from struct_type.fields array */
    Celestial_Type* field_type = struct_type->struct_type.fields[field_idx];
    if (!field_type) {
        field_type = celestial_type_primitive(ctx->module, CIR_TYPE_I32);
    }

    /* Use GEP to get pointer to the field */
    Celestial_Value* field_ptr = celestial_build_gep(&ctx->builder, obj,
                                                      struct_type, (size_t)field_idx,
                                                      ir_temp_name(ctx));
    if (!field_ptr) {
        ir_set_error(ctx, "Failed to create field pointer");
        return NULL;
    }

    /* Load the field value */
    Celestial_Value* field_val = celestial_build_load(&ctx->builder, field_ptr,
                                                       field_type, ir_temp_name(ctx));
    return field_val;
}

static Celestial_Value* ir_convert_index(IR_Context* ctx,
                                          Seraph_AST_Node* node) {
    Celestial_Value* array_ptr = ir_convert_expr(ctx, node->index.object);
    Celestial_Value* idx = ir_convert_expr(ctx, node->index.index);
    if (!array_ptr || !idx) return NULL;

    /* Get the array type from the alloca's stored type info */
    Celestial_Type* array_type = array_ptr->alloca_type;
    Celestial_Type* elem_type = NULL;

    if (array_type && array_type->kind == CIR_TYPE_ARRAY) {
        elem_type = array_type->array_type.elem_type;
    } else {
        /* Default to i64 for pointer arithmetic */
        elem_type = celestial_type_primitive(ctx->module, CIR_TYPE_I64);
        /* Create a default array type for GEP */
        array_type = celestial_type_array(ctx->module, elem_type, 0);
    }

    /* Calculate element pointer using array GEP */
    Celestial_Value* elem_ptr = celestial_build_array_gep(&ctx->builder, array_ptr,
                                                           array_type, idx, ir_temp_name(ctx));
    if (!elem_ptr) return NULL;

    /* Load the element */
    Celestial_Value* elem = celestial_build_load(&ctx->builder, elem_ptr, elem_type, ir_temp_name(ctx));
    if (!elem) return NULL;

    return elem;
}

static Celestial_Value* ir_convert_array_lit(IR_Context* ctx,
                                              Seraph_AST_Node* node) {
    size_t elem_count = node->array.elem_count;
    if (elem_count == 0) {
        /* Empty array - still need valid type */
        Celestial_Type* i64_type = celestial_type_primitive(ctx->module, CIR_TYPE_I64);
        Celestial_Type* array_type = celestial_type_array(ctx->module, i64_type, 0);
        return celestial_build_alloca(&ctx->builder, array_type, ir_temp_name(ctx));
    }

    /* Convert first element to determine type */
    Seraph_AST_Node* first_elem = node->array.elements;
    Celestial_Value* first_val = ir_convert_expr(ctx, first_elem);
    if (!first_val) return NULL;

    Celestial_Type* elem_type = first_val->type;
    if (!elem_type) {
        elem_type = celestial_type_primitive(ctx->module, CIR_TYPE_I64);
    }

    /* Create array type */
    Celestial_Type* array_type = celestial_type_array(ctx->module, elem_type, elem_count);
    if (!array_type) return NULL;

    /* Allocate stack space for the array */
    Celestial_Value* array_ptr = celestial_build_alloca(&ctx->builder, array_type, ir_temp_name(ctx));
    if (!array_ptr) return NULL;

    /* Store each element using GEP for proper offset calculation */
    size_t i = 0;
    for (Seraph_AST_Node* elem_node = node->array.elements; elem_node; elem_node = elem_node->hdr.next) {
        Celestial_Value* elem_val;
        if (i == 0) {
            elem_val = first_val;  /* Already converted */
        } else {
            elem_val = ir_convert_expr(ctx, elem_node);
            if (!elem_val) return NULL;
        }

        /* Calculate element pointer using array GEP */
        Celestial_Value* idx_val = celestial_const_i64(ctx->module, (int64_t)i);
        Celestial_Value* elem_ptr = celestial_build_array_gep(&ctx->builder, array_ptr,
                                                               array_type, idx_val, ir_temp_name(ctx));
        if (!elem_ptr) return NULL;

        /* Store the element */
        celestial_build_store(&ctx->builder, elem_ptr, elem_val);
        i++;
    }

    return array_ptr;
}

static Celestial_Value* ir_convert_struct_init(IR_Context* ctx,
                                                Seraph_AST_Node* node) {
    /* Get the struct type name */
    if (!node->struct_init.type_path ||
        node->struct_init.type_path->hdr.kind != AST_EXPR_IDENT) {
        ir_set_error(ctx, "Struct initializer requires type name");
        return NULL;
    }

    const char* type_name = node->struct_init.type_path->ident.name;
    size_t type_name_len = node->struct_init.type_path->ident.name_len;

    /* Look up the struct type in the module */
    Celestial_Type* struct_type = NULL;
    for (size_t i = 0; i < ctx->module->type_count; i++) {
        Celestial_Type* t = ctx->module->types[i];
        if (t && t->kind == CIR_TYPE_STRUCT && t->struct_type.name &&
            t->struct_type.name_len == type_name_len &&
            memcmp(t->struct_type.name, type_name, type_name_len) == 0) {
            struct_type = t;
            break;
        }
    }

    if (!struct_type) {
        ir_set_error(ctx, "Unknown struct type");
        return NULL;
    }

    /* Allocate space for the struct on the stack */
    Celestial_Value* alloca_val = celestial_build_alloca(&ctx->builder, struct_type,
                                                          ir_temp_name(ctx));
    if (!alloca_val) return NULL;

    /* Initialize each field using GEP for proper field offset calculation */
    for (Seraph_AST_Node* field = node->struct_init.fields; field; field = field->hdr.next) {
        if (field->hdr.kind != AST_FIELD_INIT) continue;

        const char* field_name = field->field_init.name;
        size_t field_name_len = field->field_init.name_len;

        /* Find field index in struct type */
        int field_idx = -1;
        for (size_t i = 0; i < struct_type->struct_type.field_count; i++) {
            const char* sf_name = struct_type->struct_type.field_names[i];
            if (sf_name && strlen(sf_name) == field_name_len &&
                memcmp(sf_name, field_name, field_name_len) == 0) {
                field_idx = (int)i;
                break;
            }
        }

        if (field_idx < 0) {
            ir_set_error(ctx, "Unknown field in struct initializer");
            return NULL;
        }

        /* Convert the field value expression */
        Celestial_Value* field_val = ir_convert_expr(ctx, field->field_init.value);
        if (!field_val) return NULL;

        /* Get the field type for proper size handling */
        Celestial_Type* field_type = struct_type->struct_type.fields[field_idx];

        /* If value type doesn't match field type, we need to coerce it
         * This handles integer literal promotion/truncation */
        if (field_val->type && field_type &&
            field_val->type->kind != field_type->kind) {
            /* For integer types, create a new constant with the correct type */
            if (field_val->kind == CIR_VALUE_CONST) {
                Celestial_Value* coerced = NULL;
                int64_t val = field_val->constant.i64;
                switch (field_type->kind) {
                    case CIR_TYPE_I32: coerced = celestial_const_i32(ctx->module, (int32_t)val); break;
                    case CIR_TYPE_U32: coerced = celestial_const_u32(ctx->module, (uint32_t)val); break;
                    case CIR_TYPE_I16: coerced = celestial_const_i16(ctx->module, (int16_t)val); break;
                    case CIR_TYPE_U16: coerced = celestial_const_u16(ctx->module, (uint16_t)val); break;
                    case CIR_TYPE_I8:  coerced = celestial_const_i8(ctx->module, (int8_t)val); break;
                    case CIR_TYPE_U8:  coerced = celestial_const_u8(ctx->module, (uint8_t)val); break;
                    default: break;
                }
                if (coerced) {
                    field_val = coerced;
                }
            }
        }

        /* Get pointer to the field using GEP */
        Celestial_Value* field_ptr = celestial_build_gep(&ctx->builder, alloca_val,
                                                          struct_type, (size_t)field_idx,
                                                          ir_temp_name(ctx));
        if (!field_ptr) {
            ir_set_error(ctx, "Failed to create field pointer");
            return NULL;
        }

        /* Store the value at the field location */
        celestial_build_store(&ctx->builder, field_ptr, field_val);
    }

    /* Return the pointer to the struct */
    return alloca_val;
}

static uint32_t s_closure_counter = 0;

static Celestial_Value* ir_convert_closure(IR_Context* ctx, Seraph_AST_Node* node) {
    /* Lambda-lift: create a new function for the closure body */

    /* Generate unique name for the lambda-lifted function */
    char fn_name_buf[64];
    snprintf(fn_name_buf, sizeof(fn_name_buf), "__closure_%u", s_closure_counter++);

    /* Copy name to arena */
    size_t fn_name_len = strlen(fn_name_buf);
    char* fn_name = seraph_arena_alloc(ctx->arena, fn_name_len + 1, 1);
    if (!fn_name) return NULL;
    memcpy(fn_name, fn_name_buf, fn_name_len + 1);

    /* Determine return type */
    Celestial_Type* ret_type = NULL;
    if (node->closure.ret_type) {
        ret_type = ir_convert_type(ctx, node->closure.ret_type);
    } else {
        /* Default to i64 for now, should infer from body */
        ret_type = celestial_type_primitive(ctx->module, CIR_TYPE_I64);
    }
    if (!ret_type) return NULL;

    /* Convert parameter types */
    size_t param_count = node->closure.param_count;
    Celestial_Type** param_types = NULL;

    if (param_count > 0) {
        param_types = seraph_arena_alloc(ctx->arena,
                                          param_count * sizeof(Celestial_Type*),
                                          _Alignof(Celestial_Type*));
        if (!param_types) return NULL;

        size_t i = 0;
        for (Seraph_AST_Node* p = node->closure.params; p; p = p->hdr.next) {
            if (p->param.type) {
                param_types[i] = ir_convert_type(ctx, p->param.type);
            } else {
                /* Default to i64 if no type specified */
                param_types[i] = celestial_type_primitive(ctx->module, CIR_TYPE_I64);
            }
            if (!param_types[i]) return NULL;
            i++;
        }
    }

    /* Create the closure function type */
    Celestial_Type* fn_type = celestial_type_function(ctx->module, ret_type,
                                                       param_types, param_count,
                                                       CIR_EFFECT_IO);
    if (!fn_type) return NULL;

    /* Create the lambda-lifted function */
    Celestial_Function* closure_fn = celestial_function_create(ctx->module, fn_name,
                                                                fn_type);
    if (!closure_fn) return NULL;

    /* Save current context state */
    Celestial_Function* saved_fn = ctx->function;
    Celestial_Builder saved_builder = ctx->builder;
    IR_Scope* saved_scope = ctx->scope;
    Celestial_Block* saved_break_target = ctx->break_target;
    Celestial_Block* saved_continue_target = ctx->continue_target;

    /* Set up new context for closure function */
    ctx->function = closure_fn;
    ctx->scope = NULL;
    ctx->break_target = NULL;
    ctx->continue_target = NULL;

    /* Push a new scope for closure parameters */
    ir_scope_push(ctx);

    /* Create entry block */
    Celestial_Block* entry = celestial_block_create(closure_fn, "entry");
    celestial_builder_position(&ctx->builder, entry);

    /* Add parameters to symbol table */
    size_t i = 0;
    for (Seraph_AST_Node* p = node->closure.params; p; p = p->hdr.next) {
        const char* param_name = p->param.name;
        size_t param_name_len = p->param.name_len;

        ir_symbol_add(ctx, param_name, param_name_len,
                      closure_fn->params[i], param_types[i], 0);
        i++;
    }

    /* Convert closure body */
    if (node->closure.body) {
        Celestial_Value* body_val = NULL;

        if (node->closure.body->hdr.kind == AST_EXPR_BLOCK) {
            body_val = ir_convert_block(ctx, node->closure.body);
        } else {
            body_val = ir_convert_expr(ctx, node->closure.body);
        }

        /* If body doesn't end with return, add implicit return */
        if (!ir_block_is_terminated(ctx->builder.block)) {
            celestial_build_return(&ctx->builder, body_val);
        }
    } else {
        /* Empty body - return void/null */
        celestial_build_return(&ctx->builder, NULL);
    }

    /* Pop scope */
    ir_scope_pop(ctx);

    /* Restore original context */
    ctx->function = saved_fn;
    ctx->builder = saved_builder;
    ctx->scope = saved_scope;
    ctx->break_target = saved_break_target;
    ctx->continue_target = saved_continue_target;

    /* Return function pointer to the closure */
    return celestial_get_fn_ptr(ctx->module, closure_fn);
}

/**
 * @brief Convert a type cast expression (expr as Type)
 */
static Celestial_Value* ir_convert_cast(IR_Context* ctx, Seraph_AST_Node* node) {
    if (!node || node->hdr.kind != AST_EXPR_CAST) {
        ir_set_error(ctx, "Expected cast expression");
        return NULL;
    }

    /* Convert the operand */
    Celestial_Value* operand = ir_convert_expr(ctx, node->cast.operand);
    if (!operand) return NULL;

    /* Convert the target type */
    Celestial_Type* target_type = ir_convert_type(ctx, node->cast.target_type);
    if (!target_type) return NULL;

    /* Get source and target sizes */
    size_t src_size = celestial_type_size(operand->type);
    size_t dst_size = celestial_type_size(target_type);

    /* If types are same size, no conversion needed */
    if (src_size == dst_size && operand->type->kind == target_type->kind) {
        return operand;
    }

    /* Determine conversion type based on sizes */
    if (dst_size < src_size) {
        /* Truncate to smaller type */
        return celestial_build_trunc(&ctx->builder, operand, target_type,
                                      ir_temp_name(ctx));
    } else if (dst_size > src_size) {
        /* Extend to larger type */
        /* Check if source is signed or unsigned */
        int is_signed = (operand->type->kind == CIR_TYPE_I8 ||
                         operand->type->kind == CIR_TYPE_I16 ||
                         operand->type->kind == CIR_TYPE_I32 ||
                         operand->type->kind == CIR_TYPE_I64);

        if (is_signed) {
            return celestial_build_sext(&ctx->builder, operand, target_type,
                                         ir_temp_name(ctx));
        } else {
            return celestial_build_zext(&ctx->builder, operand, target_type,
                                         ir_temp_name(ctx));
        }
    }

    /* Same size, different type - just reinterpret (no IR needed for integers) */
    /* For now, return the operand as-is with updated type info */
    return operand;
}

Celestial_Value* ir_convert_binary(IR_Context* ctx, Seraph_AST_Node* node) {
    /* Handle assignment specially - don't evaluate left side as a value */
    if (node->binary.op == SERAPH_TOK_ASSIGN) {
        if (node->binary.left->hdr.kind == AST_EXPR_IDENT) {
            IR_Symbol* sym = ir_symbol_lookup(ctx,
                node->binary.left->ident.name,
                node->binary.left->ident.name_len);
            if (!sym) {
                ir_set_error(ctx, "Assignment to undefined variable");
                return NULL;
            }
            if (!sym->is_mutable) {
                fprintf(stderr, "Cannot assign to immutable variable: '%.*s'\n",
                        (int)node->binary.left->ident.name_len,
                        node->binary.left->ident.name);
                ir_set_error(ctx, "Cannot assign to immutable variable");
                return NULL;
            }
            /* Now evaluate the right side */
            Celestial_Value* right = ir_convert_expr(ctx, node->binary.right);
            if (!right) return NULL;

            /* Store the right value to the variable's location */
            celestial_build_store(&ctx->builder, sym->value, right);
            return right;
        }

        /* Handle field assignment: obj.field = value */
        if (node->binary.left->hdr.kind == AST_EXPR_FIELD) {
            Seraph_AST_Node* field_node = node->binary.left;

            /* Get the object (struct) */
            Celestial_Value* obj = ir_convert_expr(ctx, field_node->field.object);
            if (!obj) return NULL;

            /* Get the struct type */
            Celestial_Type* struct_type = obj->alloca_type;
            if (!struct_type) struct_type = obj->type;

            /* Handle pointer types - auto-dereference */
            if (struct_type && struct_type->kind == CIR_TYPE_POINTER) {
                struct_type = struct_type->pointer_type.pointee_type;
            }

            if (!struct_type || struct_type->kind != CIR_TYPE_STRUCT) {
                ir_set_error(ctx, "Field assignment on non-struct type");
                return NULL;
            }

            /* Find the field index */
            const char* field_name = field_node->field.field;
            size_t field_name_len = field_node->field.field_len;
            int field_idx = -1;

            for (size_t i = 0; i < struct_type->struct_type.field_count; i++) {
                const char* sf_name = struct_type->struct_type.field_names[i];
                if (sf_name && strlen(sf_name) == field_name_len &&
                    memcmp(sf_name, field_name, field_name_len) == 0) {
                    field_idx = (int)i;
                    break;
                }
            }

            if (field_idx < 0) {
                ir_set_error(ctx, "Unknown field in assignment");
                return NULL;
            }

            /* Evaluate the right side */
            Celestial_Value* right = ir_convert_expr(ctx, node->binary.right);
            if (!right) return NULL;

            /* Get pointer to the field using GEP */
            Celestial_Value* field_ptr = celestial_build_gep(&ctx->builder, obj,
                                                              struct_type, (size_t)field_idx,
                                                              ir_temp_name(ctx));
            if (!field_ptr) {
                ir_set_error(ctx, "Failed to get field pointer for assignment");
                return NULL;
            }

            /* Store the value */
            celestial_build_store(&ctx->builder, field_ptr, right);
            return right;
        }

        /* Handle array index assignment: arr[idx] = value */
        if (node->binary.left->hdr.kind == AST_EXPR_INDEX) {
            Seraph_AST_Node* index_node = node->binary.left;

            /* Get the array/pointer */
            Celestial_Value* array_ptr = ir_convert_expr(ctx, index_node->index.object);
            if (!array_ptr) return NULL;

            /* Get the index */
            Celestial_Value* idx = ir_convert_expr(ctx, index_node->index.index);
            if (!idx) return NULL;

            /* Evaluate the right side */
            Celestial_Value* right = ir_convert_expr(ctx, node->binary.right);
            if (!right) return NULL;

            /* Get the array type from the alloca's stored type info */
            Celestial_Type* array_type = array_ptr->alloca_type;
            Celestial_Type* elem_type = NULL;

            if (array_type && array_type->kind == CIR_TYPE_ARRAY) {
                elem_type = array_type->array_type.elem_type;
            } else if (array_type && array_type->kind == CIR_TYPE_POINTER) {
                elem_type = array_type->pointer_type.pointee_type;
                /* Create array type for GEP */
                array_type = celestial_type_array(ctx->module, elem_type, 0);
            } else {
                /* Default to i64 for pointer arithmetic */
                elem_type = celestial_type_primitive(ctx->module, CIR_TYPE_I64);
                array_type = celestial_type_array(ctx->module, elem_type, 0);
            }

            /* Calculate element pointer using array GEP */
            Celestial_Value* elem_ptr = celestial_build_array_gep(&ctx->builder, array_ptr,
                                                                   array_type, idx, ir_temp_name(ctx));
            if (!elem_ptr) {
                ir_set_error(ctx, "Failed to get element pointer for array assignment");
                return NULL;
            }

            /* Store the value */
            celestial_build_store(&ctx->builder, elem_ptr, right);
            return right;
        }

        ir_set_error(ctx, "Invalid assignment target");
        return NULL;
    }

    /* For other operators, evaluate both sides */
    Celestial_Value* left = ir_convert_expr(ctx, node->binary.left);
    Celestial_Value* right = ir_convert_expr(ctx, node->binary.right);
    if (!left || !right) return NULL;

    char* name = ir_temp_name(ctx);

    /* Pointer arithmetic handling */
    int left_is_ptr = (left->type && left->type->kind == CIR_TYPE_POINTER);
    int right_is_ptr = (right->type && right->type->kind == CIR_TYPE_POINTER);

    if (left_is_ptr || right_is_ptr) {
        /* Handle pointer arithmetic for + and - operators */
        if (node->binary.op == SERAPH_TOK_PLUS) {
            /* ptr + int: multiply int by element size, then add to pointer */
            if (left_is_ptr && !right_is_ptr) {
                /* left is pointer, right is integer offset */
                Celestial_Type* pointee = left->type->pointer_type.pointee_type;
                size_t elem_size = pointee ? celestial_type_size(pointee) : 8;
                Celestial_Value* size_val = celestial_const_i64(ctx->module, (int64_t)elem_size);
                char* scaled_name = ir_temp_name(ctx);
                Celestial_Value* scaled_offset = celestial_build_mul(&ctx->builder, right, size_val, scaled_name);
                Celestial_Value* result = celestial_build_add(&ctx->builder, left, scaled_offset, name);
                /* Ensure result has pointer type */
                if (result) result->type = left->type;
                return result;
            } else if (!left_is_ptr && right_is_ptr) {
                /* int + ptr: multiply int by element size, then add to pointer */
                Celestial_Type* pointee = right->type->pointer_type.pointee_type;
                size_t elem_size = pointee ? celestial_type_size(pointee) : 8;
                Celestial_Value* size_val = celestial_const_i64(ctx->module, (int64_t)elem_size);
                char* scaled_name = ir_temp_name(ctx);
                Celestial_Value* scaled_offset = celestial_build_mul(&ctx->builder, left, size_val, scaled_name);
                Celestial_Value* result = celestial_build_add(&ctx->builder, right, scaled_offset, name);
                /* Ensure result has pointer type */
                if (result) result->type = right->type;
                return result;
            }
            /* ptr + ptr is invalid - fall through to error or default handling */
        } else if (node->binary.op == SERAPH_TOK_MINUS) {
            if (left_is_ptr && !right_is_ptr) {
                /* ptr - int: multiply int by element size, then subtract from pointer */
                Celestial_Type* pointee = left->type->pointer_type.pointee_type;
                size_t elem_size = pointee ? celestial_type_size(pointee) : 8;
                Celestial_Value* size_val = celestial_const_i64(ctx->module, (int64_t)elem_size);
                char* scaled_name = ir_temp_name(ctx);
                Celestial_Value* scaled_offset = celestial_build_mul(&ctx->builder, right, size_val, scaled_name);
                Celestial_Value* result = celestial_build_sub(&ctx->builder, left, scaled_offset, name);
                /* Ensure result has pointer type */
                if (result) result->type = left->type;
                return result;
            } else if (left_is_ptr && right_is_ptr) {
                /* ptr - ptr: subtract pointers and divide by element size */
                Celestial_Type* pointee = left->type->pointer_type.pointee_type;
                size_t elem_size = pointee ? celestial_type_size(pointee) : 8;
                char* diff_name = ir_temp_name(ctx);
                Celestial_Value* diff = celestial_build_sub(&ctx->builder, left, right, diff_name);
                if (elem_size > 1) {
                    Celestial_Value* size_val = celestial_const_i64(ctx->module, (int64_t)elem_size);
                    Celestial_Value* result = celestial_build_div(&ctx->builder, diff, size_val, name);
                    /* Result is i64, not pointer */
                    if (result) result->type = celestial_type_primitive(ctx->module, CIR_TYPE_I64);
                    return result;
                }
                /* elem_size == 1, just return the difference */
                if (diff) diff->type = celestial_type_primitive(ctx->module, CIR_TYPE_I64);
                return diff;
            }
            /* int - ptr is invalid - fall through to default handling */
        }
        /* For other operators with pointers (comparison, etc.), fall through to normal handling */
    }

    switch (node->binary.op) {
        /* Arithmetic - VOID propagating */
        case SERAPH_TOK_PLUS:
            return celestial_build_add(&ctx->builder, left, right, name);

        case SERAPH_TOK_MINUS:
            return celestial_build_sub(&ctx->builder, left, right, name);

        case SERAPH_TOK_STAR:
            return celestial_build_mul(&ctx->builder, left, right, name);

        case SERAPH_TOK_SLASH:
            return celestial_build_div(&ctx->builder, left, right, name);

        case SERAPH_TOK_PERCENT:
            return celestial_build_mod(&ctx->builder, left, right, name);

        /* Comparison */
        case SERAPH_TOK_EQ:
            return celestial_build_eq(&ctx->builder, left, right, name);

        case SERAPH_TOK_LT:
            return celestial_build_lt(&ctx->builder, left, right, name);

        case SERAPH_TOK_GT:
            return celestial_build_gt(&ctx->builder, left, right, name);

        case SERAPH_TOK_LE:
            return celestial_build_le(&ctx->builder, left, right, name);

        case SERAPH_TOK_GE:
            return celestial_build_ge(&ctx->builder, left, right, name);

        case SERAPH_TOK_NE:
            return celestial_build_ne(&ctx->builder, left, right, name);

        case SERAPH_TOK_AND:
            /* Logical AND - using bitwise AND for now (no short-circuit) */
            return celestial_build_and(&ctx->builder, left, right, name);

        case SERAPH_TOK_OR:
            /* Logical OR - using bitwise OR for now (no short-circuit) */
            return celestial_build_or(&ctx->builder, left, right, name);

        case SERAPH_TOK_ASSIGN:
            /* Assignment is handled above - should not reach here */
            ir_set_error(ctx, "Internal error: assignment not handled");
            return NULL;

        /* Bitwise operators */
        case SERAPH_TOK_BIT_AND:
            return celestial_build_and(&ctx->builder, left, right, name);

        case SERAPH_TOK_BIT_OR:
            return celestial_build_or(&ctx->builder, left, right, name);

        case SERAPH_TOK_BIT_XOR:
            return celestial_build_xor(&ctx->builder, left, right, name);

        case SERAPH_TOK_SHL:
            return celestial_build_shl(&ctx->builder, left, right, name);

        case SERAPH_TOK_SHR:
            return celestial_build_shr(&ctx->builder, left, right, name);

        default:
            ir_set_error(ctx, "Unsupported binary operator");
            return NULL;
    }
}

Celestial_Value* ir_convert_unary(IR_Context* ctx, Seraph_AST_Node* node) {
    char* name = ir_temp_name(ctx);

    /* Handle address-of specially - don't load the value */
    if (node->unary.op == SERAPH_TOK_BIT_AND) {
        /* Address-of operator: &x
         * For identifiers, get the alloca address */
        Seraph_AST_Node* operand_ast = node->unary.operand;
        if (operand_ast->hdr.kind == AST_EXPR_IDENT) {
            IR_Symbol* sym = ir_symbol_lookup(ctx, operand_ast->ident.name,
                                               operand_ast->ident.name_len);
            if (!sym) {
                ir_set_error(ctx, "Undefined variable in address-of");
                return NULL;
            }
            /* Return the pointer value directly (it's already an address) */
            return sym->value;
        }

        /* For field access: &obj.field - compute GEP but don't load */
        if (operand_ast->hdr.kind == AST_EXPR_FIELD) {
            /* Get the object pointer */
            Celestial_Value* obj = NULL;

            if (operand_ast->field.object->hdr.kind == AST_EXPR_IDENT) {
                /* Look up the variable directly to get its alloca pointer */
                IR_Symbol* sym = ir_symbol_lookup(ctx, operand_ast->field.object->ident.name,
                                                   operand_ast->field.object->ident.name_len);
                if (sym && sym->value) {
                    obj = sym->value;
                }
            }

            if (!obj) {
                obj = ir_convert_expr(ctx, operand_ast->field.object);
            }
            if (!obj) return NULL;

            /* Get the struct type */
            Celestial_Type* struct_type = obj->alloca_type;
            if (!struct_type) {
                struct_type = obj->type;
            }

            if (!struct_type || struct_type->kind != CIR_TYPE_STRUCT) {
                ir_set_error(ctx, "Cannot take address of field on non-struct type");
                return NULL;
            }

            /* Find the field index */
            int field_idx = -1;
            for (size_t i = 0; i < struct_type->struct_type.field_count; i++) {
                const char* sf_name = struct_type->struct_type.field_names[i];
                if (sf_name && strlen(sf_name) == operand_ast->field.field_len &&
                    memcmp(sf_name,
                           operand_ast->field.field,
                           operand_ast->field.field_len) == 0) {
                    field_idx = (int)i;
                    break;
                }
            }

            if (field_idx < 0) {
                ir_set_error(ctx, "Unknown struct field in address-of");
                return NULL;
            }

            /* Return the field pointer directly (via GEP) */
            return celestial_build_gep(&ctx->builder, obj, struct_type,
                                       (size_t)field_idx, ir_temp_name(ctx));
        }

        /* For array indexing: &arr[i] - compute array GEP but don't load */
        if (operand_ast->hdr.kind == AST_EXPR_INDEX) {
            Celestial_Value* array_ptr = ir_convert_expr(ctx, operand_ast->index.object);
            Celestial_Value* idx = ir_convert_expr(ctx, operand_ast->index.index);
            if (!array_ptr || !idx) return NULL;

            /* Get the array type */
            Celestial_Type* array_type = array_ptr->alloca_type;
            Celestial_Type* elem_type = NULL;

            if (array_type && array_type->kind == CIR_TYPE_ARRAY) {
                elem_type = array_type->array_type.elem_type;
            } else {
                elem_type = celestial_type_primitive(ctx->module, CIR_TYPE_I64);
                array_type = celestial_type_array(ctx->module, elem_type, 0);
            }

            /* Return the element pointer directly (via array GEP) */
            return celestial_build_array_gep(&ctx->builder, array_ptr, array_type,
                                              idx, ir_temp_name(ctx));
        }

        ir_set_error(ctx, "Address-of requires identifier, field access, or array index");
        return NULL;
    }

    /* Handle dereference specially */
    if (node->unary.op == SERAPH_TOK_STAR) {
        /* Dereference operator: *ptr */
        Celestial_Value* ptr = ir_convert_expr(ctx, node->unary.operand);
        if (!ptr) return NULL;
        /* Load from the pointer address - use i64 as default type for bootstrap */
        Celestial_Type* load_type = celestial_type_primitive(ctx->module, CIR_TYPE_I64);
        return celestial_build_load(&ctx->builder, ptr, load_type, name);
    }

    /* For other unary operators, evaluate operand normally */
    Celestial_Value* operand = ir_convert_expr(ctx, node->unary.operand);
    if (!operand) return NULL;

    switch (node->unary.op) {
        case SERAPH_TOK_MINUS:
            /* Negate - uses CIR_NEG instruction */
            return celestial_build_neg(&ctx->builder, operand, name);

        case SERAPH_TOK_NOT: {
            /* Logical NOT: compare with 0 (false), produces inverse boolean */
            Celestial_Value* zero = celestial_const_i64(ctx->module, 0);
            return celestial_build_eq(&ctx->builder, operand, zero, name);
        }

        case SERAPH_TOK_BIT_NOT:
            /* Bitwise NOT - flip all bits */
            return celestial_build_not(&ctx->builder, operand, name);

        default:
            ir_set_error(ctx, "Unsupported unary operator");
            return NULL;
    }
}

Celestial_Value* ir_convert_method_call(IR_Context* ctx, Seraph_AST_Node* node) {
    /* Convert receiver expression */
    Celestial_Value* receiver = ir_convert_expr(ctx, node->method_call.receiver);
    if (!receiver) return NULL;

    /* Get type name from receiver's type */
    Celestial_Type* recv_type = receiver->type;
    const char* type_name = NULL;
    size_t type_name_len = 0;

    /* Handle struct values and capability (reference) types */
    if (recv_type->kind == CIR_TYPE_STRUCT) {
        type_name = recv_type->struct_type.name;
        type_name_len = recv_type->struct_type.name_len;
    } else if (recv_type->kind == CIR_TYPE_CAPABILITY) {
        /* For capabilities (alloca'd values), get the underlying struct type */
        if (receiver->alloca_type && receiver->alloca_type->kind == CIR_TYPE_STRUCT) {
            type_name = receiver->alloca_type->struct_type.name;
            type_name_len = receiver->alloca_type->struct_type.name_len;
        } else {
            /* Try to look up the variable in symbol table */
            Seraph_AST_Node* recv_ast = node->method_call.receiver;
            if (recv_ast->hdr.kind == AST_EXPR_IDENT) {
                IR_Symbol* sym = ir_symbol_lookup(ctx, recv_ast->ident.name, recv_ast->ident.name_len);
                if (sym && sym->value && sym->value->alloca_type &&
                    sym->value->alloca_type->kind == CIR_TYPE_STRUCT) {
                    type_name = sym->value->alloca_type->struct_type.name;
                    type_name_len = sym->value->alloca_type->struct_type.name_len;
                }
            }
        }
        if (!type_name) {
            ir_set_error(ctx, "Cannot determine type for method call on reference");
            return NULL;
        }
    } else {
        ir_set_error(ctx, "Method call on non-struct type");
        return NULL;
    }

    /* Build mangled method name: TypeName_methodName */
    char mangled_name[256];
    size_t mlen = 0;
    memcpy(mangled_name + mlen, type_name, type_name_len);
    mlen += type_name_len;
    mangled_name[mlen++] = '_';
    memcpy(mangled_name + mlen, node->method_call.method, node->method_call.method_len);
    mlen += node->method_call.method_len;

    /* Find function in module */
    Celestial_Function* callee = NULL;
    for (Celestial_Function* fn = ctx->module->functions; fn; fn = fn->next) {
        if (fn->name_len == mlen &&
            memcmp(fn->name, mangled_name, mlen) == 0) {
            callee = fn;
            break;
        }
    }

    if (!callee) {
        ir_set_error(ctx, "Undefined method");
        return NULL;
    }

    /* Convert arguments (receiver + explicit args) */
    size_t arg_count = node->method_call.arg_count + 1;  /* +1 for receiver */
    Celestial_Value** args = seraph_arena_alloc(ctx->arena,
                              arg_count * sizeof(Celestial_Value*),
                              _Alignof(Celestial_Value*));
    if (!args) return NULL;

    /* Receiver is first argument */
    args[0] = receiver;

    /* Convert explicit arguments */
    size_t i = 1;
    for (Seraph_AST_Node* arg = node->method_call.args; arg; arg = arg->hdr.next) {
        args[i] = ir_convert_expr(ctx, arg);
        if (!args[i]) return NULL;
        i++;
    }

    return celestial_build_call(&ctx->builder, callee, args, arg_count,
                                ir_temp_name(ctx));
}

Celestial_Value* ir_convert_call(IR_Context* ctx, Seraph_AST_Node* node) {
    /* Check if this is a direct function call (identifier callee) */
    if (node->call.callee->hdr.kind == AST_EXPR_IDENT) {
        const char* fn_name = node->call.callee->ident.name;
        size_t fn_name_len = node->call.callee->ident.name_len;

        /* First check symbol table for function pointer variable */
        IR_Symbol* sym = ir_symbol_lookup(ctx, fn_name, fn_name_len);
        if (sym && sym->type && sym->type->kind == CIR_TYPE_FUNCTION) {
            /* Calling through function pointer variable */
            Celestial_Value* fn_ptr;
            if (sym->is_mutable) {
                fn_ptr = celestial_build_load(&ctx->builder, sym->value, sym->type,
                                               ir_temp_name(ctx));
            } else {
                fn_ptr = sym->value;
            }

            /* Convert arguments */
            size_t arg_count = node->call.arg_count;
            Celestial_Value** args = NULL;
            if (arg_count > 0) {
                args = seraph_arena_alloc(ctx->arena,
                                          arg_count * sizeof(Celestial_Value*),
                                          _Alignof(Celestial_Value*));
                if (!args) return NULL;

                size_t i = 0;
                for (Seraph_AST_Node* arg = node->call.args; arg; arg = arg->hdr.next) {
                    args[i] = ir_convert_expr(ctx, arg);
                    if (!args[i]) return NULL;
                    i++;
                }
            }

            return celestial_build_call_indirect(&ctx->builder, fn_ptr, args,
                                                  arg_count, ir_temp_name(ctx));
        }

        /* Check for syscall intrinsics: __syscall0 through __syscall6 */
        if (fn_name_len == 10 && memcmp(fn_name, "__syscall", 9) == 0) {
            char digit = fn_name[9];
            if (digit >= '0' && digit <= '6') {
                size_t expected_args = (size_t)(digit - '0') + 1;  /* +1 for syscall number */
                size_t arg_count = node->call.arg_count;

                if (arg_count != expected_args) {
                    ir_set_error(ctx, "Wrong number of arguments to syscall intrinsic");
                    return NULL;
                }

                /* Convert all arguments */
                Celestial_Value** args = seraph_arena_alloc(ctx->arena,
                                              arg_count * sizeof(Celestial_Value*),
                                              _Alignof(Celestial_Value*));
                if (!args) return NULL;

                size_t i = 0;
                for (Seraph_AST_Node* arg = node->call.args; arg; arg = arg->hdr.next) {
                    args[i] = ir_convert_expr(ctx, arg);
                    if (!args[i]) return NULL;
                    i++;
                }

                /* First arg is syscall number, rest are syscall arguments */
                Celestial_Value* syscall_num = args[0];
                Celestial_Value** syscall_args = arg_count > 1 ? &args[1] : NULL;
                size_t syscall_arg_count = arg_count - 1;

                return celestial_build_syscall(&ctx->builder, syscall_num,
                                               syscall_args, syscall_arg_count,
                                               ir_temp_name(ctx));
            }
        }

        /* Try to find direct function by name */
        Celestial_Function* callee = NULL;
        for (Celestial_Function* fn = ctx->module->functions; fn; fn = fn->next) {
            if (fn->name_len == fn_name_len &&
                memcmp(fn->name, fn_name, fn_name_len) == 0) {
                callee = fn;
                break;
            }
        }

        if (!callee) {
            ir_set_error(ctx, "Undefined function");
            return NULL;
        }

        /* Convert arguments */
        size_t arg_count = node->call.arg_count;
        Celestial_Value** args = NULL;
        if (arg_count > 0) {
            args = seraph_arena_alloc(ctx->arena,
                                      arg_count * sizeof(Celestial_Value*),
                                      _Alignof(Celestial_Value*));
            if (!args) return NULL;

            size_t i = 0;
            for (Seraph_AST_Node* arg = node->call.args; arg; arg = arg->hdr.next) {
                args[i] = ir_convert_expr(ctx, arg);
                if (!args[i]) return NULL;
                i++;
            }
        }

        return celestial_build_call(&ctx->builder, callee, args, arg_count,
                                    ir_temp_name(ctx));
    }

    /* Indirect call - callee is an expression that evaluates to a function pointer */
    Celestial_Value* fn_ptr = ir_convert_expr(ctx, node->call.callee);
    if (!fn_ptr) return NULL;

    /* Verify it's a function type */
    if (!fn_ptr->type || fn_ptr->type->kind != CIR_TYPE_FUNCTION) {
        ir_set_error(ctx, "Call target is not a function");
        return NULL;
    }

    /* Convert arguments */
    size_t arg_count = node->call.arg_count;
    Celestial_Value** args = NULL;
    if (arg_count > 0) {
        args = seraph_arena_alloc(ctx->arena,
                                  arg_count * sizeof(Celestial_Value*),
                                  _Alignof(Celestial_Value*));
        if (!args) return NULL;

        size_t i = 0;
        for (Seraph_AST_Node* arg = node->call.args; arg; arg = arg->hdr.next) {
            args[i] = ir_convert_expr(ctx, arg);
            if (!args[i]) return NULL;
            i++;
        }
    }

    return celestial_build_call_indirect(&ctx->builder, fn_ptr, args,
                                          arg_count, ir_temp_name(ctx));
}

Celestial_Value* ir_convert_if_expr(IR_Context* ctx, Seraph_AST_Node* node) {
    /* Create blocks */
    Celestial_Block* then_block = celestial_block_create(ctx->function, "then");
    Celestial_Block* else_block = celestial_block_create(ctx->function, "else");
    Celestial_Block* merge_block = celestial_block_create(ctx->function, "merge");

    if (!then_block || !else_block || !merge_block) return NULL;

    /* Convert condition */
    Celestial_Value* cond = ir_convert_expr(ctx, node->if_expr.cond);
    if (!cond) return NULL;

    /* Branch based on condition */
    celestial_build_branch(&ctx->builder, cond, then_block, else_block);

    /* Generate then branch */
    celestial_builder_position(&ctx->builder, then_block);
    Celestial_Value* then_val = ir_convert_expr(ctx, node->if_expr.then_branch);
    celestial_build_jump(&ctx->builder, merge_block);
    Celestial_Block* then_exit = ctx->current_block;

    /* Generate else branch */
    celestial_builder_position(&ctx->builder, else_block);
    Celestial_Value* else_val = NULL;
    if (node->if_expr.else_branch) {
        else_val = ir_convert_expr(ctx, node->if_expr.else_branch);
    } else {
        /* No else branch - produce VOID */
        else_val = celestial_const_void(ctx->module,
            celestial_type_primitive(ctx->module, CIR_TYPE_VOID));
    }
    celestial_build_jump(&ctx->builder, merge_block);
    Celestial_Block* else_exit = ctx->current_block;

    /* Merge with phi node */
    celestial_builder_position(&ctx->builder, merge_block);
    ctx->current_block = merge_block;

    /* Would need PHI node here - for now return then value */
    (void)then_exit;
    (void)else_exit;
    (void)else_val;

    return then_val;
}

Celestial_Value* ir_convert_match(IR_Context* ctx, Seraph_AST_Node* node) {
    /* Convert scrutinee */
    Celestial_Value* scrutinee = ir_convert_expr(ctx, node->match.scrutinee);
    if (!scrutinee) return NULL;

    size_t arm_count = node->match.arm_count;
    if (arm_count == 0) {
        ir_set_error(ctx, "Match expression has no arms");
        return NULL;
    }

    /* Create exit block */
    Celestial_Block* exit_block = celestial_block_create(ctx->function, "match.exit");
    if (!exit_block) return NULL;

    /* For each arm, create a block and potentially a test block */
    Celestial_Value* result = NULL;
    Celestial_Block* next_test = NULL;

    size_t arm_idx = 0;
    for (Seraph_AST_Node* arm = node->match.arms; arm; arm = arm->hdr.next) {
        char block_name[32];
        snprintf(block_name, sizeof(block_name), "match.arm%zu", arm_idx);

        Celestial_Block* arm_block = celestial_block_create(ctx->function, block_name);
        if (!arm_block) return NULL;

        /* Test block for pattern matching */
        char test_name[32];
        snprintf(test_name, sizeof(test_name), "match.test%zu", arm_idx);
        Celestial_Block* test_block = celestial_block_create(ctx->function, test_name);
        if (!test_block) return NULL;

        /* If this is the first arm, jump to its test block */
        if (arm_idx == 0) {
            celestial_build_jump(&ctx->builder, test_block);
        } else if (next_test) {
            /* Previous arm's fallthrough goes to this test - add jump */
            celestial_builder_position(&ctx->builder, next_test);
            celestial_build_jump(&ctx->builder, test_block);
        }

        /* Generate test for this pattern */
        celestial_builder_position(&ctx->builder, test_block);

        /* Handle patterns using AST_PATTERN structure */
        Seraph_AST_Node* pattern = arm->match_arm.pattern;
        Celestial_Value* match_cond = NULL;

        if (pattern->hdr.kind == AST_PATTERN) {
            switch (pattern->pattern.pattern_kind) {
                case 1: {
                    /* Identifier pattern (wildcard or variable binding) */
                    match_cond = celestial_const_bool(ctx->module, 1);

                    /* If not wildcard (_), bind the variable */
                    if (pattern->pattern.data.ident.name_len != 1 ||
                        pattern->pattern.data.ident.name[0] != '_') {
                        ir_symbol_add(ctx, pattern->pattern.data.ident.name,
                                     pattern->pattern.data.ident.name_len,
                                     scrutinee, scrutinee->type, 0);
                    }
                    break;
                }
                case 2: {
                    /* Integer literal pattern */
                    Celestial_Value* pat_val = celestial_const_i64(ctx->module,
                                                                   (int64_t)pattern->pattern.data.int_val);
                    match_cond = celestial_build_eq(&ctx->builder, scrutinee, pat_val, ir_temp_name(ctx));
                    break;
                }
                default:
                    /* Unknown pattern kind - always match */
                    match_cond = celestial_const_bool(ctx->module, 1);
            }
        } else if (pattern->hdr.kind == AST_EXPR_INT_LIT) {
            /* Legacy: integer literal pattern */
            Celestial_Value* pat_val = celestial_const_i64(ctx->module,
                                                           (int64_t)pattern->int_lit.value);
            match_cond = celestial_build_eq(&ctx->builder, scrutinee, pat_val, ir_temp_name(ctx));
        } else if (pattern->hdr.kind == AST_EXPR_IDENT) {
            /* Legacy: identifier pattern */
            match_cond = celestial_const_bool(ctx->module, 1);
            if (pattern->ident.name_len != 1 || pattern->ident.name[0] != '_') {
                ir_symbol_add(ctx, pattern->ident.name, pattern->ident.name_len,
                             scrutinee, scrutinee->type, 0);
            }
        } else {
            /* Default: treat as always matching */
            match_cond = celestial_const_bool(ctx->module, 1);
        }

        /* Create next test block for fallthrough if not last arm */
        Celestial_Block* fallthrough = NULL;
        if (arm->hdr.next) {
            char fall_name[32];
            snprintf(fall_name, sizeof(fall_name), "match.fall%zu", arm_idx);
            fallthrough = celestial_block_create(ctx->function, fall_name);
        } else {
            fallthrough = exit_block;  /* Last arm falls through to exit */
        }

        celestial_build_branch(&ctx->builder, match_cond, arm_block, fallthrough);
        next_test = fallthrough;

        /* Generate arm body */
        celestial_builder_position(&ctx->builder, arm_block);
        Celestial_Value* arm_val = ir_convert_expr(ctx, arm->match_arm.body);
        if (result == NULL && arm_val) {
            result = arm_val;  /* Use first arm's result type */
        }
        celestial_build_jump(&ctx->builder, exit_block);

        arm_idx++;
    }

    /* Position at exit block */
    celestial_builder_position(&ctx->builder, exit_block);
    ctx->current_block = exit_block;

    return result ? result : celestial_const_i64(ctx->module, 0);
}

Celestial_Value* ir_convert_block(IR_Context* ctx, Seraph_AST_Node* node) {
    ir_scope_push(ctx);

    Celestial_Value* result = NULL;

    /* Convert all statements */
    for (Seraph_AST_Node* stmt = node->block.stmts; stmt; stmt = stmt->hdr.next) {
        ir_convert_stmt(ctx, stmt);
        if (ctx->has_error) {
            ir_scope_pop(ctx);
            return NULL;
        }
    }

    /* Convert final expression if present */
    if (node->block.expr) {
        result = ir_convert_expr(ctx, node->block.expr);
    }

    ir_scope_pop(ctx);

    return result ? result : celestial_const_i64(ctx->module, 0);
}

Celestial_Value* ir_convert_void_prop(IR_Context* ctx, Seraph_AST_Node* node) {
    Celestial_Value* operand = ir_convert_expr(ctx, node->void_prop.operand);
    if (!operand) return NULL;

    if (node->void_prop.default_val) {
        /* VOID coalescing: value ?? default */
        Celestial_Value* default_val = ir_convert_expr(ctx,
                                                        node->void_prop.default_val);
        if (!default_val) return NULL;
        return celestial_build_void_coalesce(&ctx->builder, operand,
                                              default_val, ir_temp_name(ctx));
    } else {
        /* VOID propagation: value?? - early return if VOID */
        return celestial_build_void_prop(&ctx->builder, operand,
                                          ir_temp_name(ctx));
    }
}

Celestial_Value* ir_convert_void_assert(IR_Context* ctx,
                                         Seraph_AST_Node* node) {
    Celestial_Value* operand = ir_convert_expr(ctx, node->void_assert.operand);
    if (!operand) return NULL;

    return celestial_build_void_assert(&ctx->builder, operand,
                                        ir_temp_name(ctx));
}

Celestial_Value* ir_convert_expr(IR_Context* ctx, Seraph_AST_Node* expr) {
    if (!expr) return NULL;

    switch (expr->hdr.kind) {
        /* Literals */
        case AST_EXPR_INT_LIT:
            return ir_convert_int_lit(ctx, expr);

        case AST_EXPR_FLOAT_LIT:
            return ir_convert_float_lit(ctx, expr);

        case AST_EXPR_BOOL_LIT:
            return ir_convert_bool_lit(ctx, expr);

        case AST_EXPR_STRING_LIT:
            return ir_convert_string_lit(ctx, expr);

        case AST_EXPR_VOID_LIT:
            return celestial_const_void(ctx->module,
                celestial_type_primitive(ctx->module, CIR_TYPE_VOID));

        /* References */
        case AST_EXPR_IDENT:
            return ir_convert_ident(ctx, expr);

        /* Operators */
        case AST_EXPR_BINARY:
            return ir_convert_binary(ctx, expr);

        case AST_EXPR_UNARY:
            return ir_convert_unary(ctx, expr);

        /* VOID operations */
        case AST_EXPR_VOID_PROP:
            return ir_convert_void_prop(ctx, expr);

        case AST_EXPR_VOID_ASSERT:
            return ir_convert_void_assert(ctx, expr);

        /* Calls and access */
        case AST_EXPR_CALL:
            return ir_convert_call(ctx, expr);

        case AST_EXPR_METHOD_CALL:
            return ir_convert_method_call(ctx, expr);

        case AST_EXPR_FIELD:
            return ir_convert_field_access(ctx, expr);

        case AST_EXPR_INDEX:
            return ir_convert_index(ctx, expr);

        /* Compound */
        case AST_EXPR_BLOCK:
            return ir_convert_block(ctx, expr);

        case AST_EXPR_IF:
            return ir_convert_if_expr(ctx, expr);

        case AST_EXPR_MATCH:
            return ir_convert_match(ctx, expr);

        case AST_EXPR_ARRAY:
            return ir_convert_array_lit(ctx, expr);

        case AST_EXPR_STRUCT_INIT:
            return ir_convert_struct_init(ctx, expr);

        case AST_EXPR_CLOSURE:
            return ir_convert_closure(ctx, expr);

        case AST_EXPR_CAST:
            return ir_convert_cast(ctx, expr);

        default:
            ir_set_error(ctx, "Unsupported expression type");
            return NULL;
    }
}

/*============================================================================
 * Statement Conversion
 *============================================================================*/

/**
 * @brief Check if the current block is terminated
 */
static int ir_block_is_terminated(Celestial_Block* block) {
    if (!block || !block->last) return 0;

    Celestial_Opcode op = block->last->opcode;
    return op == CIR_JUMP || op == CIR_BRANCH || op == CIR_RETURN ||
           op == CIR_UNREACHABLE || op == CIR_SWITCH;
}

void ir_convert_return(IR_Context* ctx, Seraph_AST_Node* node) {
    Celestial_Value* ret_val = NULL;

    if (node->return_stmt.expr) {
        ret_val = ir_convert_expr(ctx, node->return_stmt.expr);
        if (!ret_val) return;
    }

    celestial_build_return(&ctx->builder, ret_val);
}

void ir_convert_for(IR_Context* ctx, Seraph_AST_Node* node) {
    /* For loop: for var in iterable { body } */
    Seraph_AST_Node* iterable = node->for_stmt.iterable;

    /* Check if iterable is a range expression */
    if (iterable && iterable->hdr.kind == AST_EXPR_RANGE) {
        /* Range-based for: for i in start..end { body } */

        Celestial_Block* init_block = celestial_block_create(ctx->function, "for.init");
        Celestial_Block* cond_block = celestial_block_create(ctx->function, "for.cond");
        Celestial_Block* body_block = celestial_block_create(ctx->function, "for.body");
        Celestial_Block* incr_block = celestial_block_create(ctx->function, "for.incr");
        Celestial_Block* exit_block = celestial_block_create(ctx->function, "for.exit");

        if (!init_block || !cond_block || !body_block || !incr_block || !exit_block) return;

        /* Save loop targets */
        Celestial_Block* old_break = ctx->break_target;
        Celestial_Block* old_continue = ctx->continue_target;
        ctx->break_target = exit_block;
        ctx->continue_target = incr_block;

        ir_scope_push(ctx);

        /* Jump to initialization */
        celestial_build_jump(&ctx->builder, init_block);
        celestial_builder_position(&ctx->builder, init_block);

        /* Convert range start and end */
        Celestial_Value* start_val = ir_convert_expr(ctx, iterable->range.start);
        Celestial_Value* end_val = ir_convert_expr(ctx, iterable->range.end);
        if (!start_val || !end_val) {
            ir_scope_pop(ctx);
            ctx->break_target = old_break;
            ctx->continue_target = old_continue;
            return;
        }

        /* Create loop counter variable */
        Celestial_Type* i64_type = celestial_type_primitive(ctx->module, CIR_TYPE_I64);
        Celestial_Value* counter = celestial_build_alloca(&ctx->builder, i64_type, "for.counter");
        celestial_build_store(&ctx->builder, counter, start_val);

        /* Store end value for later comparison */
        Celestial_Value* end_ptr = celestial_build_alloca(&ctx->builder, i64_type, "for.end");
        celestial_build_store(&ctx->builder, end_ptr, end_val);

        /* Add loop variable to scope */
        ir_symbol_add(ctx, node->for_stmt.var, node->for_stmt.var_len,
                      counter, i64_type, 1);

        celestial_build_jump(&ctx->builder, cond_block);

        /* Condition block: check counter against end */
        celestial_builder_position(&ctx->builder, cond_block);
        Celestial_Value* counter_val = celestial_build_load(&ctx->builder, counter, i64_type, ir_temp_name(ctx));
        Celestial_Value* end_loaded = celestial_build_load(&ctx->builder, end_ptr, i64_type, ir_temp_name(ctx));

        Celestial_Value* cond;
        if (iterable->range.inclusive) {
            cond = celestial_build_le(&ctx->builder, counter_val, end_loaded, ir_temp_name(ctx));
        } else {
            cond = celestial_build_lt(&ctx->builder, counter_val, end_loaded, ir_temp_name(ctx));
        }
        celestial_build_branch(&ctx->builder, cond, body_block, exit_block);

        /* Body block */
        celestial_builder_position(&ctx->builder, body_block);
        ir_convert_expr(ctx, node->for_stmt.body);
        celestial_build_jump(&ctx->builder, incr_block);

        /* Increment block */
        celestial_builder_position(&ctx->builder, incr_block);
        Celestial_Value* curr = celestial_build_load(&ctx->builder, counter, i64_type, ir_temp_name(ctx));
        Celestial_Value* one = celestial_const_i64(ctx->module, 1);
        Celestial_Value* next = celestial_build_add(&ctx->builder, curr, one, ir_temp_name(ctx));
        celestial_build_store(&ctx->builder, counter, next);
        celestial_build_jump(&ctx->builder, cond_block);

        /* Exit block */
        celestial_builder_position(&ctx->builder, exit_block);
        ctx->current_block = exit_block;

        ir_scope_pop(ctx);

        ctx->break_target = old_break;
        ctx->continue_target = old_continue;
    } else {
        /* Non-range iterable - not yet implemented */
        ir_set_error(ctx, "For loop only supports range iteration");
    }
}

void ir_convert_while(IR_Context* ctx, Seraph_AST_Node* node) {
    Celestial_Block* cond_block = celestial_block_create(ctx->function,
                                                          "while.cond");
    Celestial_Block* body_block = celestial_block_create(ctx->function,
                                                          "while.body");
    Celestial_Block* exit_block = celestial_block_create(ctx->function,
                                                          "while.exit");

    if (!cond_block || !body_block || !exit_block) return;

    /* Save loop targets */
    Celestial_Block* old_break = ctx->break_target;
    Celestial_Block* old_continue = ctx->continue_target;
    ctx->break_target = exit_block;
    ctx->continue_target = cond_block;

    /* Jump to condition */
    celestial_build_jump(&ctx->builder, cond_block);

    /* Condition block */
    celestial_builder_position(&ctx->builder, cond_block);
    Celestial_Value* cond = ir_convert_expr(ctx, node->while_stmt.cond);
    if (!cond) return;
    celestial_build_branch(&ctx->builder, cond, body_block, exit_block);

    /* Body block */
    celestial_builder_position(&ctx->builder, body_block);
    ir_convert_expr(ctx, node->while_stmt.body);
    celestial_build_jump(&ctx->builder, cond_block);

    /* Exit block */
    celestial_builder_position(&ctx->builder, exit_block);
    ctx->current_block = exit_block;

    ctx->break_target = old_break;
    ctx->continue_target = old_continue;
}

void ir_convert_substrate_block(IR_Context* ctx, Seraph_AST_Node* node,
                                 Celestial_Substrate_Kind kind) {
    /* Enter substrate context */
    Celestial_Value* ctx_val = celestial_build_substrate_enter(&ctx->builder,
                                                                kind, "substrate");
    if (!ctx_val) return;

    /* Convert block body */
    ir_convert_expr(ctx, node->substrate_block.body);

    /* Exit substrate context */
    celestial_build_substrate_exit(&ctx->builder, ctx_val);
}

void ir_convert_stmt(IR_Context* ctx, Seraph_AST_Node* stmt) {
    if (!stmt) return;

    switch (stmt->hdr.kind) {
        case AST_STMT_RETURN:
            ir_convert_return(ctx, stmt);
            break;

        case AST_STMT_EXPR:
            /* Expression statement - evaluate and discard */
            ir_convert_expr(ctx, stmt->expr_stmt.expr);
            break;

        case AST_DECL_LET:
        case AST_DECL_CONST:
            ir_convert_let_decl(ctx, stmt);
            break;

        case AST_STMT_FOR:
            ir_convert_for(ctx, stmt);
            break;

        case AST_STMT_WHILE:
            ir_convert_while(ctx, stmt);
            break;

        case AST_STMT_BREAK:
            if (ctx->break_target) {
                celestial_build_jump(&ctx->builder, ctx->break_target);
            }
            break;

        case AST_STMT_CONTINUE:
            if (ctx->continue_target) {
                celestial_build_jump(&ctx->builder, ctx->continue_target);
            }
            break;

        case AST_STMT_PERSIST:
            ir_convert_substrate_block(ctx, stmt, CIR_SUBSTRATE_ATLAS);
            break;

        case AST_STMT_AETHER:
            ir_convert_substrate_block(ctx, stmt, CIR_SUBSTRATE_AETHER);
            break;

        default:
            ir_set_error(ctx, "Unsupported statement type");
            break;
    }
}

/*============================================================================
 * Declaration Conversion
 *============================================================================*/

Seraph_Vbit ir_convert_let_decl(IR_Context* ctx, Seraph_AST_Node* node) {
    /* Get type */
    Celestial_Type* type = NULL;
    if (node->let_decl.type) {
        type = ir_convert_type(ctx, node->let_decl.type);
    } else {
        /* Infer type from initializer - for now, default to i64 */
        type = celestial_type_primitive(ctx->module, CIR_TYPE_I64);
    }

    if (!type) return SERAPH_VBIT_FALSE;

    /* Allocate stack space for mutable variables */
    Celestial_Value* value = NULL;

    /* For bootstrap: ALL variables use stack allocation
     * This makes them mutable (seraphic.srph expects C-style mutability) */
    value = celestial_build_alloca(&ctx->builder, type,
                                    (char*)node->let_decl.name);
    if (!value) return SERAPH_VBIT_FALSE;

    /* Initialize if there's an initializer */
    if (node->let_decl.init) {
        Celestial_Value* init_val = ir_convert_expr(ctx, node->let_decl.init);
        if (!init_val) return SERAPH_VBIT_FALSE;
        celestial_build_store(&ctx->builder, value, init_val);
    }

    /* Add to symbol table
     * For bootstrap/self-hosting: treat ALL let declarations as mutable
     * since seraphic.srph uses let without mut for mutable variables
     * (C-style rather than Rust-style mutability semantics) */
    int is_mutable = 1;  /* Always mutable for bootstrap */
    (void)node->let_decl.is_mut;  /* Suppress unused warning */
    return ir_symbol_add(ctx, node->let_decl.name, node->let_decl.name_len,
                         value, type, is_mutable);
}

Celestial_Type* ir_convert_struct_decl(IR_Context* ctx,
                                        Seraph_AST_Node* node) {
    size_t field_count = node->struct_decl.field_count;

    Celestial_Type** fields = NULL;
    const char** field_names = NULL;

    if (field_count > 0) {
        fields = seraph_arena_alloc(ctx->arena,
                                    field_count * sizeof(Celestial_Type*),
                                    _Alignof(Celestial_Type*));
        field_names = seraph_arena_alloc(ctx->arena,
                                         field_count * sizeof(const char*),
                                         _Alignof(const char*));
        if (!fields || !field_names) return NULL;

        size_t i = 0;
        for (Seraph_AST_Node* f = node->struct_decl.fields; f; f = f->hdr.next) {
            fields[i] = ir_convert_type(ctx, f->field_def.type);
            /* Copy field name with null termination */
            size_t name_len = f->field_def.name_len;
            char* name_copy = seraph_arena_alloc(ctx->arena, name_len + 1, 1);
            if (name_copy) {
                memcpy(name_copy, f->field_def.name, name_len);
                name_copy[name_len] = '\0';
                field_names[i] = name_copy;
            } else {
                field_names[i] = NULL;
            }
            if (!fields[i]) return NULL;
            i++;
        }
    }

    Celestial_Type* struct_type = celestial_type_struct(ctx->module,
                                  node->struct_decl.name,
                                  node->struct_decl.name_len,
                                  fields, field_names, field_count);

    /* Register struct type for method self parameter lookup */
    if (struct_type && ctx->struct_count < ctx->struct_capacity) {
        /* Copy struct name with null termination */
        char* name_copy = seraph_arena_alloc(ctx->arena,
                                              node->struct_decl.name_len + 1, 1);
        if (name_copy) {
            memcpy(name_copy, node->struct_decl.name, node->struct_decl.name_len);
            name_copy[node->struct_decl.name_len] = '\0';
            ctx->struct_names[ctx->struct_count] = name_copy;
            ctx->struct_types[ctx->struct_count] = struct_type;
            ctx->struct_count++;
        }
    }

    return struct_type;
}

Celestial_Type* ir_convert_enum_decl(IR_Context* ctx,
                                      Seraph_AST_Node* node) {
    size_t variant_count = node->enum_decl.variant_count;

    const char** variant_names = NULL;
    size_t* variant_name_lens = NULL;
    Celestial_Type** variant_types = NULL;

    if (variant_count > 0) {
        variant_names = seraph_arena_alloc(ctx->arena,
                                           variant_count * sizeof(char*),
                                           _Alignof(char*));
        variant_name_lens = seraph_arena_alloc(ctx->arena,
                                               variant_count * sizeof(size_t),
                                               _Alignof(size_t));
        variant_types = seraph_arena_alloc(ctx->arena,
                                           variant_count * sizeof(Celestial_Type*),
                                           _Alignof(Celestial_Type*));
        if (!variant_names || !variant_name_lens || !variant_types) return NULL;

        size_t i = 0;
        for (Seraph_AST_Node* v = node->enum_decl.variants; v; v = v->hdr.next) {
            /* Copy variant name with null termination */
            size_t name_len = v->enum_variant.name_len;
            char* name_copy = seraph_arena_alloc(ctx->arena, name_len + 1, 1);
            if (name_copy) {
                memcpy(name_copy, v->enum_variant.name, name_len);
                name_copy[name_len] = '\0';
                variant_names[i] = name_copy;
                variant_name_lens[i] = name_len;
            }

            /* Register variant in global lookup table for use as values */
            if (ctx->enum_variant_count < ctx->enum_variant_capacity) {
                ctx->enum_variant_names[ctx->enum_variant_count] = name_copy;
                ctx->enum_variant_name_lens[ctx->enum_variant_count] = name_len;
                ctx->enum_variant_values[ctx->enum_variant_count] = (int64_t)i;
                ctx->enum_variant_count++;
            }

            /* Convert payload type if present */
            if (v->enum_variant.data) {
                variant_types[i] = ir_convert_type(ctx, v->enum_variant.data);
            } else {
                variant_types[i] = NULL;
            }
            i++;
        }
    }

    return celestial_type_enum(ctx->module,
                                node->enum_decl.name,
                                node->enum_decl.name_len,
                                variant_names, variant_name_lens,
                                variant_types, variant_count);
}

/* Create function stub (declaration only, no body) */
static Celestial_Function* ir_create_fn_stub(IR_Context* ctx,
                                              Seraph_AST_Node* node) {
    /* Build function type */
    Celestial_Type* ret_type = NULL;
    if (node->fn_decl.ret_type) {
        ret_type = ir_convert_type(ctx, node->fn_decl.ret_type);
    } else {
        ret_type = celestial_type_primitive(ctx->module, CIR_TYPE_VOID);
    }
    if (!ret_type) return NULL;

    /* Build parameter types */
    size_t param_count = node->fn_decl.param_count;
    Celestial_Type** param_types = NULL;

    if (param_count > 0) {
        param_types = seraph_arena_alloc(ctx->arena,
                                         param_count * sizeof(Celestial_Type*),
                                         _Alignof(Celestial_Type*));
        if (!param_types) return NULL;

        size_t i = 0;
        for (Seraph_AST_Node* p = node->fn_decl.params; p; p = p->hdr.next) {
            param_types[i] = ir_convert_type(ctx, p->param.type);
            if (!param_types[i]) return NULL;
            i++;
        }
    }

    /* Determine effects */
    uint32_t effects = CIR_EFFECT_NONE;
    if (!node->fn_decl.is_pure) {
        effects |= CIR_EFFECT_IO;  /* Conservative - assume IO */
    }

    Celestial_Type* fn_type = celestial_type_function(ctx->module,
                                                       ret_type, param_types,
                                                       param_count, effects);
    if (!fn_type) return NULL;

    /* Create function (stub only) */
    /* Need to null-terminate the name since fn_decl.name points into source */
    size_t name_len = node->fn_decl.name_len;
    char* name_copy = seraph_arena_alloc(ctx->arena, name_len + 1, 1);
    if (!name_copy) return NULL;
    memcpy(name_copy, node->fn_decl.name, name_len);
    name_copy[name_len] = '\0';

    Celestial_Function* fn = celestial_function_create(ctx->module,
                                                        name_copy,
                                                        fn_type);
    return fn;
}

/* Convert function body (called after all stubs are created) */
static void ir_convert_fn_body(IR_Context* ctx,
                                Seraph_AST_Node* node,
                                Celestial_Function* fn) {
    if (!fn) return;

    /* Get return type from function type */
    Celestial_Type* ret_type = fn->type->func_type.ret_type;

    /* Set up context for function body */
    ctx->function = fn;
    ctx->return_type = ret_type;
    ctx->builder.function = fn;

    /* Create entry block */
    Celestial_Block* entry = celestial_block_create(fn, "entry");
    if (!entry) return;

    ctx->current_block = entry;
    celestial_builder_position(&ctx->builder, entry);

    ir_scope_push(ctx);

    /* Add parameters to symbol table.
     * For struct parameters passed by value, we need to create an alloca
     * and store the parameter value into it, so that field access can
     * use GEP with proper addresses. */
    size_t i = 0;
    for (Seraph_AST_Node* p = node->fn_decl.params; p; p = p->hdr.next) {
        if (i < fn->param_count) {
            Celestial_Type* param_type = fn->type->func_type.param_types[i];
            Celestial_Value* param_val = fn->params[i];
            Celestial_Value* symbol_val = param_val;  /* What goes in symbol table */

            /* For method self parameters (references to structs), store the
             * underlying struct type in alloca_type for field access */
            if (param_type && param_type->kind == CIR_TYPE_CAPABILITY &&
                p->param.type && p->param.type->hdr.kind == AST_TYPE_REF) {
                /* Get the referenced type from the AST */
                Seraph_AST_Node* ref_type = p->param.type->ref_type.inner;
                if (ref_type && ref_type->hdr.kind == AST_TYPE_NAMED) {
                    /* Look up the struct type by name */
                    const char* name = ref_type->named_type.name;
                    size_t name_len = ref_type->named_type.name_len;
                    for (size_t j = 0; j < ctx->struct_count; j++) {
                        if (ctx->struct_names[j] &&
                            strlen(ctx->struct_names[j]) == name_len &&
                            memcmp(ctx->struct_names[j], name, name_len) == 0) {
                            param_val->alloca_type = ctx->struct_types[j];
                            break;
                        }
                    }
                }
            }

            /* For struct parameters passed by value, create an alloca and store
             * the parameter value into it. This allows field access to work
             * properly with GEP. */
            if (param_type && param_type->kind == CIR_TYPE_STRUCT) {
                Celestial_Value* alloca_val = celestial_build_alloca(&ctx->builder,
                                                                      param_type,
                                                                      ir_temp_name(ctx));
                if (alloca_val) {
                    /* Store the parameter value into the alloca */
                    celestial_build_store(&ctx->builder, alloca_val, param_val);
                    /* Use the alloca as the symbol value */
                    symbol_val = alloca_val;
                }
            }

            ir_symbol_add(ctx, p->param.name, p->param.name_len,
                         symbol_val, param_type, p->param.is_mut);
        }
        i++;
    }

    /* Convert function body */
    if (node->fn_decl.body) {
        Celestial_Value* body_val = ir_convert_block(ctx, node->fn_decl.body);

        /* Only add return if block isn't already terminated */
        if (!ir_block_is_terminated(ctx->current_block)) {
            /* If body produced a value and function returns non-void, return it */
            if (body_val && ret_type->kind != CIR_TYPE_VOID) {
                celestial_build_return(&ctx->builder, body_val);
            } else if (ret_type->kind == CIR_TYPE_VOID) {
                celestial_build_return(&ctx->builder, NULL);
            } else {
                /* Non-void function with no return value - return VOID */
                Celestial_Value* void_val = celestial_const_void(ctx->module, ret_type);
                celestial_build_return(&ctx->builder, void_val);
            }
        }
    } else {
        /* Empty function body - add implicit return */
        if (ret_type->kind == CIR_TYPE_VOID) {
            celestial_build_return(&ctx->builder, NULL);
        } else {
            Celestial_Value* void_val = celestial_const_void(ctx->module, ret_type);
            celestial_build_return(&ctx->builder, void_val);
        }
    }

    ir_scope_pop(ctx);

    ctx->function = NULL;
    ctx->current_block = NULL;
}

Celestial_Function* ir_convert_fn_decl(IR_Context* ctx,
                                        Seraph_AST_Node* node) {
    /* Legacy function - calls both stub creation and body conversion */
    Celestial_Function* fn = ir_create_fn_stub(ctx, node);
    if (fn) {
        ir_convert_fn_body(ctx, node, fn);
    }
    return fn;
}

/*============================================================================
 * Module Conversion
 *============================================================================*/

Celestial_Module* ir_convert_module(Seraph_AST_Node* module_ast,
                                     Seraph_Type_Context* types,
                                     Seraph_Arena* arena) {
    if (!module_ast || !arena) {
        return NULL;
    }

    if (module_ast->hdr.kind != AST_MODULE) {
        return NULL;
    }

    /* Create module */
    const char* name = module_ast->module.name;
    if (!name) name = "main";

    Celestial_Module* mod = celestial_module_create(name, arena);
    if (!mod) return NULL;

    /* Set up context */
    IR_Context ctx;
    if (!seraph_vbit_is_true(ir_context_init(&ctx, arena, types))) {
        return NULL;
    }

    ctx.module = mod;
    celestial_builder_init(&ctx.builder, mod);

    /* Push global scope for module-level symbols (consts, functions) */
    ir_scope_push(&ctx);

    /* First pass: convert struct and enum declarations */
    for (Seraph_AST_Node* decl = module_ast->module.decls;
         decl;
         decl = decl->hdr.next) {
        if (decl->hdr.kind == AST_DECL_STRUCT) {
            ir_convert_struct_decl(&ctx, decl);
        } else if (decl->hdr.kind == AST_DECL_ENUM) {
            ir_convert_enum_decl(&ctx, decl);
        }
    }

    /* Second pass: process global const declarations */
    /* These need to be available before function bodies are converted */
    /* Note: Parser uses AST_DECL_LET with is_const=1 for const declarations */
    for (Seraph_AST_Node* decl = module_ast->module.decls;
         decl;
         decl = decl->hdr.next) {
        if (decl->hdr.kind == AST_DECL_LET && decl->let_decl.is_const) {
            /* Convert the initializer expression to get the value */
            Celestial_Value* init_val = NULL;
            if (decl->let_decl.init != NULL) {
                init_val = ir_convert_expr(&ctx, decl->let_decl.init);
            }

            /* Determine the type */
            Celestial_Type* type = NULL;
            if (decl->let_decl.type != NULL) {
                type = ir_convert_type(&ctx, decl->let_decl.type);
            } else if (init_val != NULL) {
                type = init_val->type;
            } else {
                type = celestial_type_primitive(ctx.module, CIR_TYPE_I64);  /* Default to i64 */
            }

            /* Add to symbol table so it can be used in function bodies */
            ir_symbol_add(&ctx, decl->let_decl.name, decl->let_decl.name_len,
                          init_val, type, 0);  /* is_mutable = 0 for const */
        }
    }

    /* Third pass: create function stubs (signatures only) */
    /* This allows functions to call each other regardless of order
     * Skip forward declarations (is_forward) - only create stubs for real definitions */
    for (Seraph_AST_Node* decl = module_ast->module.decls;
         decl;
         decl = decl->hdr.next) {
        if (decl->hdr.kind == AST_DECL_FN && !decl->fn_decl.is_forward) {
            ir_create_fn_stub(&ctx, decl);
        } else if (decl->hdr.kind == AST_DECL_IMPL) {
            /* Create stubs for methods in impl blocks */
            Seraph_AST_Node* type_node = decl->impl_decl.type;
            const char* type_name = type_node->ident.name;
            size_t type_name_len = type_node->ident.name_len;

            for (Seraph_AST_Node* method = decl->impl_decl.methods;
                 method;
                 method = method->hdr.next) {
                /* Create mangled name: TypeName_methodName */
                char mangled_name[256];
                size_t mlen = 0;
                memcpy(mangled_name + mlen, type_name, type_name_len);
                mlen += type_name_len;
                mangled_name[mlen++] = '_';
                memcpy(mangled_name + mlen, method->fn_decl.name, method->fn_decl.name_len);
                mlen += method->fn_decl.name_len;

                /* Store type name in method for later lookup */
                method->fn_decl.impl_type_name = type_name;
                method->fn_decl.impl_type_name_len = type_name_len;

                /* Temporarily swap name for stub creation */
                const char* orig_name = method->fn_decl.name;
                size_t orig_len = method->fn_decl.name_len;
                method->fn_decl.name = mangled_name;
                method->fn_decl.name_len = mlen;

                ir_create_fn_stub(&ctx, method);

                /* Restore original name */
                method->fn_decl.name = orig_name;
                method->fn_decl.name_len = orig_len;
            }
        }
    }

    /* Fourth pass: convert function bodies */
    /* Now all functions are known, so calls can resolve correctly */
    for (Seraph_AST_Node* decl = module_ast->module.decls;
         decl;
         decl = decl->hdr.next) {
        if (decl->hdr.kind == AST_DECL_FN) {
            /* Skip forward declarations - they have no body to convert */
            if (decl->fn_decl.is_forward) continue;

            /* Find the function by name */
            const char* fn_name = decl->fn_decl.name;
            size_t fn_name_len = decl->fn_decl.name_len;
            Celestial_Function* fn = NULL;
            for (Celestial_Function* f = mod->functions; f; f = f->next) {
                if (f->name_len == fn_name_len &&
                    memcmp(f->name, fn_name, fn_name_len) == 0) {
                    fn = f;
                    break;
                }
            }
            if (fn) {
                ir_convert_fn_body(&ctx, decl, fn);
            }
        } else if (decl->hdr.kind == AST_DECL_IMPL) {
            /* Convert method bodies in impl blocks */
            Seraph_AST_Node* type_node = decl->impl_decl.type;
            const char* type_name = type_node->ident.name;
            size_t type_name_len = type_node->ident.name_len;

            for (Seraph_AST_Node* method = decl->impl_decl.methods;
                 method;
                 method = method->hdr.next) {
                /* Build mangled name to find function */
                char mangled_name[256];
                size_t mlen = 0;
                memcpy(mangled_name + mlen, type_name, type_name_len);
                mlen += type_name_len;
                mangled_name[mlen++] = '_';
                memcpy(mangled_name + mlen, method->fn_decl.name, method->fn_decl.name_len);
                mlen += method->fn_decl.name_len;

                Celestial_Function* fn = NULL;
                for (Celestial_Function* f = mod->functions; f; f = f->next) {
                    if (f->name_len == mlen &&
                        memcmp(f->name, mangled_name, mlen) == 0) {
                        fn = f;
                        break;
                    }
                }
                if (fn) {
                    ir_convert_fn_body(&ctx, method, fn);
                }
            }
        }
    }

    /* Global let (mutable) declarations would need special handling
     * For now, they're not supported at module level - use const instead */

    if (ctx.has_error) {
        fprintf(stderr, "IR generation error: %s\n",
                ctx.error_msg ? ctx.error_msg : "unknown");
        return NULL;
    }

    ir_context_cleanup(&ctx);

    return mod;
}
