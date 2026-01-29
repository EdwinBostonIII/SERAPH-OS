/**
 * @file codegen.c
 * @brief Seraphim Compiler - C Code Generator Implementation
 *
 * MC26: Seraphim Language Code Generator
 *
 * Generates C code from Seraphim AST with proper VOID handling,
 * substrate block translation, and proof embedding.
 */

#include "seraph/seraphim/codegen.h"
#include "seraph/vbit.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/*============================================================================
 * Initialization
 *============================================================================*/

Seraph_Vbit seraph_codegen_init(Seraph_Codegen* gen,
                                 FILE* output,
                                 Seraph_Arena* arena) {
    if (gen == NULL || output == NULL || arena == NULL) {
        return SERAPH_VBIT_FALSE;
    }

    memset(gen, 0, sizeof(Seraph_Codegen));
    gen->output = output;
    gen->arena = arena;
    gen->indent_level = 0;
    gen->options = SERAPH_CODEGEN_OPT_RUNTIME_CHECK;

    return SERAPH_VBIT_TRUE;
}

void seraph_codegen_set_proofs(Seraph_Codegen* gen,
                                const Seraph_Proof_Table* proofs) {
    if (gen != NULL) {
        gen->proofs = proofs;
    }
}

void seraph_codegen_set_types(Seraph_Codegen* gen,
                               Seraph_Type_Context* types) {
    if (gen != NULL) {
        gen->types = types;
    }
}

void seraph_codegen_set_options(Seraph_Codegen* gen,
                                 Seraph_Codegen_Options options) {
    if (gen != NULL) {
        gen->options = options;
    }
}

/*============================================================================
 * Utility Functions
 *============================================================================*/

void seraph_codegen_indent(Seraph_Codegen* gen) {
    if (gen == NULL || gen->output == NULL) return;
    for (int i = 0; i < gen->indent_level; i++) {
        fprintf(gen->output, "    ");
    }
}

void seraph_codegen_indent_inc(Seraph_Codegen* gen) {
    if (gen != NULL) gen->indent_level++;
}

void seraph_codegen_indent_dec(Seraph_Codegen* gen) {
    if (gen != NULL && gen->indent_level > 0) gen->indent_level--;
}

void seraph_codegen_writeln(Seraph_Codegen* gen, const char* format, ...) {
    if (gen == NULL || gen->output == NULL) return;

    seraph_codegen_indent(gen);

    va_list args;
    va_start(args, format);
    vfprintf(gen->output, format, args);
    va_end(args);

    fprintf(gen->output, "\n");
}

void seraph_codegen_write(Seraph_Codegen* gen, const char* format, ...) {
    if (gen == NULL || gen->output == NULL) return;

    va_list args;
    va_start(args, format);
    vfprintf(gen->output, format, args);
    va_end(args);
}

void seraph_codegen_line_directive(Seraph_Codegen* gen, Seraph_Source_Loc loc) {
    if (gen == NULL || gen->output == NULL) return;
    if (!(gen->options & SERAPH_CODEGEN_OPT_LINE_DIRECTIVES)) return;

    fprintf(gen->output, "#line %u \"%s\"\n",
            loc.line, loc.filename ? loc.filename : "<unknown>");
}

size_t seraph_codegen_temp_name(Seraph_Codegen* gen, char* buf, size_t buf_size) {
    if (gen == NULL || buf == NULL || buf_size == 0) return 0;
    return snprintf(buf, buf_size, "__tmp_%u", gen->temp_counter++);
}

size_t seraph_codegen_label_name(Seraph_Codegen* gen, char* buf, size_t buf_size) {
    if (gen == NULL || buf == NULL || buf_size == 0) return 0;
    return snprintf(buf, buf_size, "__label_%u", gen->label_counter++);
}

/*============================================================================
 * Pointer Tracking for Auto-Deref Field Access
 *
 * Seraph uses `.` for field access on both values and pointers (like Rust).
 * C requires `->` for pointer field access. We track which identifiers are
 * pointers so we can emit the correct operator.
 *============================================================================*/

/**
 * @brief Check if a type node represents a pointer type
 */
static int seraph_type_is_pointer(Seraph_AST_Node* type_node) {
    if (type_node == NULL) return 0;
    switch (type_node->hdr.kind) {
        case AST_TYPE_POINTER:
        case AST_TYPE_REF:
        case AST_TYPE_MUT_REF:
        case AST_TYPE_SUBSTRATE_REF:
            return 1;
        default:
            return 0;
    }
}

/**
 * @brief Add a name to the pointer tracking list
 */
static void seraph_codegen_add_ptr_name(Seraph_Codegen* gen, const char* name, size_t name_len) {
    if (gen == NULL || gen->arena == NULL || name == NULL) return;

    Seraph_PtrName_Entry* entry = seraph_arena_alloc(gen->arena, sizeof(Seraph_PtrName_Entry), _Alignof(Seraph_PtrName_Entry));
    if (entry == NULL) return;

    entry->name = name;
    entry->name_len = name_len;
    entry->next = gen->ptr_names;
    gen->ptr_names = entry;
}

/**
 * @brief Check if a name is in the pointer tracking list
 */
static int seraph_codegen_is_ptr_name(Seraph_Codegen* gen, const char* name, size_t name_len) {
    if (gen == NULL || name == NULL) return 0;

    for (Seraph_PtrName_Entry* e = gen->ptr_names; e != NULL; e = e->next) {
        if (e->name_len == name_len && memcmp(e->name, name, name_len) == 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Clear the pointer tracking list (called at function entry)
 */
static void seraph_codegen_clear_ptr_names(Seraph_Codegen* gen) {
    if (gen == NULL) return;
    gen->ptr_names = NULL;  /* Arena-allocated, no need to free */
}

/*============================================================================
 * Type Generation
 *============================================================================*/

const char* seraph_codegen_prim_type_str(Seraph_Token_Type tok_type) {
    switch (tok_type) {
        case SERAPH_TOK_U8:      return "uint8_t";
        case SERAPH_TOK_U16:     return "uint16_t";
        case SERAPH_TOK_U32:     return "uint32_t";
        case SERAPH_TOK_U64:     return "uint64_t";
        case SERAPH_TOK_I8:      return "int8_t";
        case SERAPH_TOK_I16:     return "int16_t";
        case SERAPH_TOK_I32:     return "int32_t";
        case SERAPH_TOK_I64:     return "int64_t";
        case SERAPH_TOK_BOOL:    return "bool";
        case SERAPH_TOK_CHAR:    return "char";
        case SERAPH_TOK_SCALAR:  return "Seraph_Scalar";
        case SERAPH_TOK_DUAL:    return "Seraph_Dual";
        case SERAPH_TOK_GALACTIC: return "Seraph_Galactic";
        default:                 return "void";
    }
}

void seraph_codegen_type(Seraph_Codegen* gen, Seraph_AST_Node* type_node) {
    if (gen == NULL || type_node == NULL) {
        seraph_codegen_write(gen, "void");
        return;
    }

    switch (type_node->hdr.kind) {
        case AST_TYPE_PRIMITIVE:
            seraph_codegen_write(gen, "%s",
                                  seraph_codegen_prim_type_str(type_node->prim_type.prim));
            break;

        case AST_TYPE_NAMED:
            /* Handle built-in type aliases that aren't in the primitive token list */
            if (type_node->named_type.name_len == 3 &&
                memcmp(type_node->named_type.name, "str", 3) == 0) {
                /* str -> const char* (string type) */
                seraph_codegen_write(gen, "const char*");
            } else {
                /* For named types (structs, enums, typedefs), emit just the type name.
                 * The typedef'd name works for both structs (typedef struct X X;) and enums.
                 * Using just the name allows forward-declared types to work correctly. */
                seraph_codegen_write(gen, "%.*s",
                                      (int)type_node->named_type.name_len,
                                      type_node->named_type.name);
            }
            break;

        case AST_TYPE_VOID_ABLE:
            /* VOID-able types use the same C type - VOID is a special value */
            seraph_codegen_type(gen, type_node->void_type.inner);
            break;

        case AST_TYPE_REF:
        case AST_TYPE_MUT_REF:
        case AST_TYPE_POINTER:
            /* References and pointers all become C pointers */
            seraph_codegen_type(gen, type_node->ref_type.inner);
            seraph_codegen_write(gen, "*");
            break;

        case AST_TYPE_ARRAY:
            seraph_codegen_type(gen, type_node->array_type.elem_type);
            seraph_codegen_write(gen, "*");
            break;

        case AST_TYPE_SLICE:
            /* Slice becomes pointer + length struct */
            seraph_codegen_write(gen, "Seraph_Slice");
            break;

        case AST_TYPE_FN:
            {
                /* Function pointer type: fn(args...) -> ret
                 * When used standalone (without name), emit return type (*)(params...)
                 * This is only used for anonymous contexts; named contexts use seraph_codegen_type_with_name */
                if (type_node->fn_type.ret != NULL) {
                    seraph_codegen_type(gen, type_node->fn_type.ret);
                } else {
                    seraph_codegen_write(gen, "void");
                }
                seraph_codegen_write(gen, " (*)(");
                int first = 1;
                for (Seraph_AST_Node* p = type_node->fn_type.params; p != NULL; p = p->hdr.next) {
                    if (!first) seraph_codegen_write(gen, ", ");
                    seraph_codegen_type(gen, p);
                    first = 0;
                }
                if (first) seraph_codegen_write(gen, "void");  /* No params: fn() -> T becomes T (*)(void) */
                seraph_codegen_write(gen, ")");
            }
            break;

        default:
            seraph_codegen_write(gen, "void");
            break;
    }
}

/**
 * @brief Emit a type declaration with an associated identifier name.
 *
 * This handles the C syntax complexity where function pointers need the name
 * embedded in the type: `ret_type (*name)(params...)` rather than `type name`.
 */
void seraph_codegen_type_with_name(Seraph_Codegen* gen, Seraph_AST_Node* type_node,
                                    const char* name, size_t name_len) {
    if (gen == NULL || type_node == NULL) {
        seraph_codegen_write(gen, "void %.*s", (int)name_len, name);
        return;
    }

    if (type_node->hdr.kind == AST_TYPE_FN) {
        /* Function pointer: ret_type (*name)(params...) */
        if (type_node->fn_type.ret != NULL) {
            seraph_codegen_type(gen, type_node->fn_type.ret);
        } else {
            seraph_codegen_write(gen, "void");
        }
        seraph_codegen_write(gen, " (*%.*s)(", (int)name_len, name);
        int first = 1;
        for (Seraph_AST_Node* p = type_node->fn_type.params; p != NULL; p = p->hdr.next) {
            if (!first) seraph_codegen_write(gen, ", ");
            seraph_codegen_type(gen, p);
            first = 0;
        }
        if (first) seraph_codegen_write(gen, "void");  /* No params */
        seraph_codegen_write(gen, ")");
    } else if (type_node->hdr.kind == AST_TYPE_ARRAY) {
        /* Array type: elem_type name[size] */
        seraph_codegen_type(gen, type_node->array_type.elem_type);
        seraph_codegen_write(gen, " %.*s[", (int)name_len, name);
        if (type_node->array_type.size != NULL &&
            type_node->array_type.size->hdr.kind == AST_EXPR_INT_LIT) {
            seraph_codegen_write(gen, "%llu", (unsigned long long)type_node->array_type.size->int_lit.value);
        }
        seraph_codegen_write(gen, "]");
    } else {
        /* Normal type: type name */
        seraph_codegen_type(gen, type_node);
        seraph_codegen_write(gen, " %.*s", (int)name_len, name);
    }
}

/*============================================================================
 * Preamble Generation
 *============================================================================*/

void seraph_codegen_preamble(Seraph_Codegen* gen) {
    if (gen == NULL || gen->output == NULL) return;

    fprintf(gen->output, "/**\n");
    fprintf(gen->output, " * Generated by Seraphim Compiler\n");
    fprintf(gen->output, " * SERAPH Operating System - MC26\n");
    fprintf(gen->output, " */\n\n");

    fprintf(gen->output, "#include <stdint.h>\n");
    fprintf(gen->output, "#include <stdbool.h>\n");
    fprintf(gen->output, "#include <stddef.h>\n");
    fprintf(gen->output, "#include <setjmp.h>\n");
    fprintf(gen->output, "\n");

    fprintf(gen->output, "/* SERAPH VOID value definitions */\n");
    fprintf(gen->output, "#define SERAPH_VOID_U8   ((uint8_t)0xFF)\n");
    fprintf(gen->output, "#define SERAPH_VOID_U16  ((uint16_t)0xFFFF)\n");
    fprintf(gen->output, "#define SERAPH_VOID_U32  ((uint32_t)0xFFFFFFFF)\n");
    fprintf(gen->output, "#define SERAPH_VOID_U64  ((uint64_t)0xFFFFFFFFFFFFFFFF)\n");
    fprintf(gen->output, "#define SERAPH_VOID_I8   ((int8_t)-1)\n");
    fprintf(gen->output, "#define SERAPH_VOID_I16  ((int16_t)-1)\n");
    fprintf(gen->output, "#define SERAPH_VOID_I32  ((int32_t)-1)\n");
    fprintf(gen->output, "#define SERAPH_VOID_I64  ((int64_t)-1)\n");
    fprintf(gen->output, "#define SERAPH_VOID_PTR  ((void*)0)\n");
    fprintf(gen->output, "\n");

    fprintf(gen->output, "/* VOID checking macros */\n");
    fprintf(gen->output, "#define SERAPH_IS_VOID_U8(x)  ((x) == SERAPH_VOID_U8)\n");
    fprintf(gen->output, "#define SERAPH_IS_VOID_U16(x) ((x) == SERAPH_VOID_U16)\n");
    fprintf(gen->output, "#define SERAPH_IS_VOID_U32(x) ((x) == SERAPH_VOID_U32)\n");
    fprintf(gen->output, "#define SERAPH_IS_VOID_U64(x) ((x) == SERAPH_VOID_U64)\n");
    fprintf(gen->output, "#define SERAPH_IS_VOID_I8(x)  ((x) == SERAPH_VOID_I8)\n");
    fprintf(gen->output, "#define SERAPH_IS_VOID_I16(x) ((x) == SERAPH_VOID_I16)\n");
    fprintf(gen->output, "#define SERAPH_IS_VOID_I32(x) ((x) == SERAPH_VOID_I32)\n");
    fprintf(gen->output, "#define SERAPH_IS_VOID_I64(x) ((x) == SERAPH_VOID_I64)\n");
    fprintf(gen->output, "#define SERAPH_IS_VOID_PTR(x) ((x) == SERAPH_VOID_PTR)\n");
    fprintf(gen->output, "\n");

    fprintf(gen->output, "/* Generic VOID check (uses _Generic in C11) */\n");
    fprintf(gen->output, "#define SERAPH_IS_VOID(x) _Generic((x), \\\n");
    fprintf(gen->output, "    uint8_t:  SERAPH_IS_VOID_U8(x), \\\n");
    fprintf(gen->output, "    uint16_t: SERAPH_IS_VOID_U16(x), \\\n");
    fprintf(gen->output, "    uint32_t: SERAPH_IS_VOID_U32(x), \\\n");
    fprintf(gen->output, "    uint64_t: SERAPH_IS_VOID_U64(x), \\\n");
    fprintf(gen->output, "    int8_t:   SERAPH_IS_VOID_I8(x), \\\n");
    fprintf(gen->output, "    int16_t:  SERAPH_IS_VOID_I16(x), \\\n");
    fprintf(gen->output, "    int32_t:  SERAPH_IS_VOID_I32(x), \\\n");
    fprintf(gen->output, "    int64_t:  SERAPH_IS_VOID_I64(x), \\\n");
    fprintf(gen->output, "    default:  SERAPH_IS_VOID_PTR(x))\n");
    fprintf(gen->output, "\n");

    fprintf(gen->output, "/* Panic function */\n");
    fprintf(gen->output, "extern void seraph_panic(const char* msg) __attribute__((noreturn));\n");
    fprintf(gen->output, "\n");

    fprintf(gen->output, "/* Atlas (persistent storage) types */\n");
    fprintf(gen->output, "typedef struct Seraph_Atlas Seraph_Atlas;\n");
    fprintf(gen->output, "typedef struct Seraph_Atlas_Transaction Seraph_Atlas_Transaction;\n");
    fprintf(gen->output, "extern Seraph_Atlas __atlas;\n");
    fprintf(gen->output, "extern Seraph_Atlas_Transaction* seraph_atlas_begin(Seraph_Atlas* atlas);\n");
    fprintf(gen->output, "extern void seraph_atlas_commit(Seraph_Atlas* atlas, Seraph_Atlas_Transaction* tx);\n");
    fprintf(gen->output, "extern void seraph_atlas_rollback(Seraph_Atlas* atlas, Seraph_Atlas_Transaction* tx);\n");
    fprintf(gen->output, "\n");

    fprintf(gen->output, "/* Aether (network) types */\n");
    fprintf(gen->output, "typedef struct Seraph_Aether Seraph_Aether;\n");
    fprintf(gen->output, "typedef struct Seraph_Aether_Context Seraph_Aether_Context;\n");
    fprintf(gen->output, "extern Seraph_Aether __aether;\n");
    fprintf(gen->output, "extern Seraph_Aether_Context* seraph_aether_begin(Seraph_Aether* aether);\n");
    fprintf(gen->output, "extern void seraph_aether_end(Seraph_Aether* aether, Seraph_Aether_Context* ctx);\n");
    fprintf(gen->output, "\n");

    fprintf(gen->output, "/* Slice type */\n");
    fprintf(gen->output, "typedef struct { void* data; size_t len; } Seraph_Slice;\n");
    fprintf(gen->output, "\n");

    fprintf(gen->output, "/* Fixed-point types */\n");
    fprintf(gen->output, "typedef int64_t Seraph_Scalar;  /* Q32.32 fixed-point */\n");
    fprintf(gen->output, "typedef struct { int64_t real; int64_t dual; } Seraph_Dual;\n");
    fprintf(gen->output, "typedef struct { int64_t w; int64_t x; int64_t y; int64_t z; } Seraph_Galactic;\n");
    fprintf(gen->output, "\n");

    /* Emit proof annotations if enabled */
    if ((gen->options & SERAPH_CODEGEN_OPT_PROOFS) && gen->proofs != NULL) {
        seraph_proof_emit_comments(gen->proofs, gen->output);
    }
}

/*============================================================================
 * Forward Declarations
 *============================================================================*/

void seraph_codegen_forward_decls(Seraph_Codegen* gen, Seraph_AST_Node* module) {
    if (gen == NULL || module == NULL) return;
    if (module->hdr.kind != AST_MODULE) return;

    fprintf(gen->output, "/* Forward declarations */\n");

    /* First pass: type forward declarations (structs and enums) */
    for (Seraph_AST_Node* decl = module->module.decls; decl != NULL;
         decl = decl->hdr.next) {
        switch (decl->hdr.kind) {
            case AST_DECL_STRUCT:
                fprintf(gen->output, "typedef struct %.*s %.*s;\n",
                        (int)decl->struct_decl.name_len, decl->struct_decl.name,
                        (int)decl->struct_decl.name_len, decl->struct_decl.name);
                break;

            case AST_DECL_ENUM:
                /* Forward-declare enum type - this allows functions to use the
                 * enum type name before the full definition is emitted */
                fprintf(gen->output, "typedef enum %.*s %.*s;\n",
                        (int)decl->enum_decl.name_len, decl->enum_decl.name,
                        (int)decl->enum_decl.name_len, decl->enum_decl.name);
                break;

            default:
                break;
        }
    }

    /* Second pass: function forward declarations */
    for (Seraph_AST_Node* decl = module->module.decls; decl != NULL;
         decl = decl->hdr.next) {
        switch (decl->hdr.kind) {
            case AST_DECL_FN:
                /* Function prototype */
                {
                    /* Check if this is main() - C requires main to return int */
                    int is_main = (decl->fn_decl.name_len == 4 &&
                                   memcmp(decl->fn_decl.name, "main", 4) == 0);

                    if (is_main) {
                        seraph_codegen_write(gen, "int");
                    } else if (decl->fn_decl.ret_type != NULL) {
                        seraph_codegen_type(gen, decl->fn_decl.ret_type);
                    } else {
                        seraph_codegen_write(gen, "void");
                    }
                    fprintf(gen->output, " %.*s(",
                            (int)decl->fn_decl.name_len, decl->fn_decl.name);

                    int first = 1;
                    for (Seraph_AST_Node* param = decl->fn_decl.params; param != NULL;
                         param = param->hdr.next) {
                        if (param->hdr.kind == AST_PARAM) {
                            if (!first) fprintf(gen->output, ", ");
                            seraph_codegen_type_with_name(gen, param->param.type,
                                                           param->param.name, param->param.name_len);
                            first = 0;
                        }
                    }
                    if (first) fprintf(gen->output, "void");
                    fprintf(gen->output, ");\n");
                }
                break;

            default:
                break;
        }
    }
    fprintf(gen->output, "\n");
}

/*============================================================================
 * Module Generation
 *============================================================================*/

void seraph_codegen_module(Seraph_Codegen* gen, Seraph_AST_Node* module) {
    if (gen == NULL || module == NULL) return;
    if (module->hdr.kind != AST_MODULE) return;

    /* Preamble */
    seraph_codegen_preamble(gen);

    /* Forward declarations */
    seraph_codegen_forward_decls(gen, module);

    /* Generate all declarations */
    for (Seraph_AST_Node* decl = module->module.decls; decl != NULL;
         decl = decl->hdr.next) {
        switch (decl->hdr.kind) {
            case AST_DECL_STRUCT:
                seraph_codegen_struct_decl(gen, decl);
                break;

            case AST_DECL_ENUM:
                seraph_codegen_enum_decl(gen, decl);
                break;

            case AST_DECL_FN:
                seraph_codegen_fn_decl(gen, decl);
                break;

            default:
                break;
        }
    }
}

/*============================================================================
 * Declaration Generation
 *============================================================================*/

void seraph_codegen_struct_decl(Seraph_Codegen* gen, Seraph_AST_Node* struct_decl) {
    if (gen == NULL || struct_decl == NULL) return;
    if (struct_decl->hdr.kind != AST_DECL_STRUCT) return;

    seraph_codegen_line_directive(gen, struct_decl->hdr.loc);

    fprintf(gen->output, "struct %.*s {\n",
            (int)struct_decl->struct_decl.name_len,
            struct_decl->struct_decl.name);

    seraph_codegen_indent_inc(gen);

    for (Seraph_AST_Node* field = struct_decl->struct_decl.fields; field != NULL;
         field = field->hdr.next) {
        if (field->hdr.kind == AST_FIELD_DEF) {
            seraph_codegen_indent(gen);
            seraph_codegen_type(gen, field->field_def.type);
            fprintf(gen->output, " %.*s;\n",
                    (int)field->field_def.name_len,
                    field->field_def.name);
        }
    }

    seraph_codegen_indent_dec(gen);
    fprintf(gen->output, "};\n\n");
}

void seraph_codegen_enum_decl(Seraph_Codegen* gen, Seraph_AST_Node* enum_decl) {
    if (gen == NULL || enum_decl == NULL) return;
    if (enum_decl->hdr.kind != AST_DECL_ENUM) return;

    seraph_codegen_line_directive(gen, enum_decl->hdr.loc);

    /* Use tagged enum form: enum EnumName { ... };
     * This pairs with the forward declaration: typedef enum EnumName EnumName;
     * Together they allow the enum type to be used before definition. */
    fprintf(gen->output, "enum %.*s {\n",
            (int)enum_decl->enum_decl.name_len,
            enum_decl->enum_decl.name);

    seraph_codegen_indent_inc(gen);

    int idx = 0;
    for (Seraph_AST_Node* variant = enum_decl->enum_decl.variants; variant != NULL;
         variant = variant->hdr.next, idx++) {
        if (variant->hdr.kind == AST_ENUM_VARIANT) {
            seraph_codegen_indent(gen);
            /* Emit variant name without prefix - SERAPH enum variants are
             * global names, matching Rust-style usage: `let x = TokIf;`
             * not `let x = TokenType::TokIf;` */
            fprintf(gen->output, "%.*s = %d",
                    (int)variant->enum_variant.name_len,
                    variant->enum_variant.name,
                    idx);
            if (variant->hdr.next != NULL) fprintf(gen->output, ",");
            fprintf(gen->output, "\n");
        }
    }

    seraph_codegen_indent_dec(gen);
    fprintf(gen->output, "};\n\n");
}

void seraph_codegen_fn_decl(Seraph_Codegen* gen, Seraph_AST_Node* fn_decl) {
    if (gen == NULL || fn_decl == NULL) return;
    if (fn_decl->hdr.kind != AST_DECL_FN) return;

    seraph_codegen_line_directive(gen, fn_decl->hdr.loc);

    /* Clear pointer tracking for new function scope */
    seraph_codegen_clear_ptr_names(gen);

    /* Save current function context */
    gen->current_fn_name = fn_decl->fn_decl.name;
    gen->current_fn_name_len = fn_decl->fn_decl.name_len;

    /* Check if this is main() - C requires main to return int */
    int is_main = (fn_decl->fn_decl.name_len == 4 &&
                   memcmp(fn_decl->fn_decl.name, "main", 4) == 0);

    /* Return type */
    if (is_main) {
        /* main() must return int in C for standards compliance */
        seraph_codegen_write(gen, "int");
    } else if (fn_decl->fn_decl.ret_type != NULL) {
        seraph_codegen_type(gen, fn_decl->fn_decl.ret_type);
    } else {
        seraph_codegen_write(gen, "void");
    }

    /* Function name */
    fprintf(gen->output, " %.*s(",
            (int)fn_decl->fn_decl.name_len, fn_decl->fn_decl.name);

    /* Parameters - track which ones are pointers */
    int first = 1;
    for (Seraph_AST_Node* param = fn_decl->fn_decl.params; param != NULL;
         param = param->hdr.next) {
        if (param->hdr.kind == AST_PARAM) {
            if (!first) fprintf(gen->output, ", ");
            seraph_codegen_type_with_name(gen, param->param.type,
                                           param->param.name, param->param.name_len);

            /* Track pointer parameters for auto-deref field access */
            if (seraph_type_is_pointer(param->param.type)) {
                seraph_codegen_add_ptr_name(gen, param->param.name, param->param.name_len);
            }
            first = 0;
        }
    }
    if (first) fprintf(gen->output, "void");
    fprintf(gen->output, ")");

    /* Body */
    if (fn_decl->fn_decl.body != NULL) {
        fprintf(gen->output, " ");
        seraph_codegen_block(gen, fn_decl->fn_decl.body);
    } else {
        fprintf(gen->output, ";\n");
    }
    fprintf(gen->output, "\n");
}

/*============================================================================
 * Expression Generation
 *============================================================================*/

void seraph_codegen_expr(Seraph_Codegen* gen, Seraph_AST_Node* expr) {
    if (gen == NULL || expr == NULL) return;

    switch (expr->hdr.kind) {
        /* Literals */
        case AST_EXPR_INT_LIT:
            fprintf(gen->output, "%lldLL", (long long)expr->int_lit.value);
            break;

        case AST_EXPR_FLOAT_LIT:
            fprintf(gen->output, "%g", expr->float_lit.value);
            break;

        case AST_EXPR_BOOL_LIT:
            fprintf(gen->output, "%s", expr->bool_lit.value ? "true" : "false");
            break;

        case AST_EXPR_CHAR_LIT:
            fprintf(gen->output, "'%c'", expr->char_lit.value);
            break;

        case AST_EXPR_STRING_LIT:
            fprintf(gen->output, "\"%.*s\"",
                    (int)expr->string_lit.len, expr->string_lit.value);
            break;

        case AST_EXPR_VOID_LIT:
            /* Use appropriate VOID constant based on context */
            fprintf(gen->output, "SERAPH_VOID_U64");
            break;

        /* Identifier */
        case AST_EXPR_IDENT:
            fprintf(gen->output, "%.*s",
                    (int)expr->ident.name_len, expr->ident.name);
            break;

        /* Binary operators */
        case AST_EXPR_BINARY:
            seraph_codegen_write(gen, "(");
            seraph_codegen_expr(gen, expr->binary.left);
            switch (expr->binary.op) {
                case SERAPH_TOK_PLUS:    seraph_codegen_write(gen, " + "); break;
                case SERAPH_TOK_MINUS:   seraph_codegen_write(gen, " - "); break;
                case SERAPH_TOK_STAR:    seraph_codegen_write(gen, " * "); break;
                case SERAPH_TOK_SLASH:   seraph_codegen_write(gen, " / "); break;
                case SERAPH_TOK_PERCENT: seraph_codegen_write(gen, " %% "); break;
                case SERAPH_TOK_EQ:      seraph_codegen_write(gen, " == "); break;
                case SERAPH_TOK_NE:      seraph_codegen_write(gen, " != "); break;
                case SERAPH_TOK_LT:      seraph_codegen_write(gen, " < "); break;
                case SERAPH_TOK_LE:      seraph_codegen_write(gen, " <= "); break;
                case SERAPH_TOK_GT:      seraph_codegen_write(gen, " > "); break;
                case SERAPH_TOK_GE:      seraph_codegen_write(gen, " >= "); break;
                case SERAPH_TOK_AND:     seraph_codegen_write(gen, " && "); break;
                case SERAPH_TOK_OR:      seraph_codegen_write(gen, " || "); break;
                case SERAPH_TOK_BIT_AND: seraph_codegen_write(gen, " & "); break;
                case SERAPH_TOK_BIT_OR:  seraph_codegen_write(gen, " | "); break;
                case SERAPH_TOK_BIT_XOR: seraph_codegen_write(gen, " ^ "); break;
                case SERAPH_TOK_SHL:     seraph_codegen_write(gen, " << "); break;
                case SERAPH_TOK_SHR:     seraph_codegen_write(gen, " >> "); break;
                case SERAPH_TOK_ASSIGN:  seraph_codegen_write(gen, " = "); break;
                default: seraph_codegen_write(gen, " ? "); break;
            }
            seraph_codegen_expr(gen, expr->binary.right);
            seraph_codegen_write(gen, ")");
            break;

        /* Unary operators */
        case AST_EXPR_UNARY:
            switch (expr->unary.op) {
                case SERAPH_TOK_MINUS:     seraph_codegen_write(gen, "-"); break;
                case SERAPH_TOK_NOT:       seraph_codegen_write(gen, "!"); break;
                case SERAPH_TOK_BIT_NOT:   seraph_codegen_write(gen, "~"); break;
                case SERAPH_TOK_AMPERSAND: seraph_codegen_write(gen, "&"); break;  /* reference */
                case SERAPH_TOK_BIT_AND:   seraph_codegen_write(gen, "&"); break;  /* address-of */
                case SERAPH_TOK_STAR:      seraph_codegen_write(gen, "*"); break;  /* dereference */
                default: break;
            }
            seraph_codegen_expr(gen, expr->unary.operand);
            break;

        /* VOID propagation */
        case AST_EXPR_VOID_PROP:
            seraph_codegen_void_prop(gen, expr);
            break;

        /* VOID assertion */
        case AST_EXPR_VOID_ASSERT:
            seraph_codegen_void_assert(gen, expr);
            break;

        /* Function call */
        case AST_EXPR_CALL:
            seraph_codegen_expr(gen, expr->call.callee);
            seraph_codegen_write(gen, "(");
            {
                int first = 1;
                for (Seraph_AST_Node* arg = expr->call.args; arg != NULL;
                     arg = arg->hdr.next) {
                    if (!first) seraph_codegen_write(gen, ", ");
                    seraph_codegen_expr(gen, arg);
                    first = 0;
                }
            }
            seraph_codegen_write(gen, ")");
            break;

        /* Field access - auto-deref for pointers like Rust/Seraph */
        case AST_EXPR_FIELD:
            {
                /* Check if object is a pointer identifier - use -> instead of . */
                int use_arrow = 0;
                if (expr->field.object != NULL &&
                    expr->field.object->hdr.kind == AST_EXPR_IDENT) {
                    use_arrow = seraph_codegen_is_ptr_name(gen,
                        expr->field.object->ident.name,
                        expr->field.object->ident.name_len);
                }

                seraph_codegen_expr(gen, expr->field.object);
                if (use_arrow) {
                    seraph_codegen_write(gen, "->%.*s",
                                          (int)expr->field.field_len, expr->field.field);
                } else {
                    seraph_codegen_write(gen, ".%.*s",
                                          (int)expr->field.field_len, expr->field.field);
                }
            }
            break;

        /* Index access */
        case AST_EXPR_INDEX:
            seraph_codegen_expr(gen, expr->index.object);
            seraph_codegen_write(gen, "[");
            seraph_codegen_expr(gen, expr->index.index);
            seraph_codegen_write(gen, "]");
            break;

        /* Block as expression */
        case AST_EXPR_BLOCK:
            seraph_codegen_block(gen, expr);
            break;

        /* If expression */
        case AST_EXPR_IF:
            if (gen->in_expression) {
                /* Ternary operator for expression context */
                seraph_codegen_write(gen, "(");
                seraph_codegen_expr(gen, expr->if_expr.cond);
                seraph_codegen_write(gen, " ? ");
                if (expr->if_expr.then_branch != NULL) {
                    gen->in_expression = 1;
                    seraph_codegen_expr(gen, expr->if_expr.then_branch);
                }
                seraph_codegen_write(gen, " : ");
                if (expr->if_expr.else_branch != NULL) {
                    seraph_codegen_expr(gen, expr->if_expr.else_branch);
                } else {
                    seraph_codegen_write(gen, "0");
                }
                seraph_codegen_write(gen, ")");
            } else {
                /* Statement context */
                seraph_codegen_write(gen, "if (");
                seraph_codegen_expr(gen, expr->if_expr.cond);
                seraph_codegen_write(gen, ") ");
                if (expr->if_expr.then_branch != NULL) {
                    seraph_codegen_block(gen, expr->if_expr.then_branch);
                }
                if (expr->if_expr.else_branch != NULL) {
                    seraph_codegen_write(gen, " else ");
                    seraph_codegen_block(gen, expr->if_expr.else_branch);
                }
            }
            break;

        /* Match expression - convert to nested ternary chain
         *
         * SERAPH match expressions are exhaustive and always produce a value.
         * We generate nested ternary operators for proper C value semantics:
         *
         *   match x {
         *       1 => 10,
         *       2 => 20,
         *       _ => 0,
         *   }
         *
         * Becomes:
         *   ({ __auto_type __tmp = x;
         *      (__tmp == 1) ? (10) :
         *      ((__tmp == 2) ? (20) :
         *      (0)); })
         *
         * This ensures the match expression produces a value that can be
         * assigned, returned, or used in any expression context.
         */
        case AST_EXPR_MATCH:
            {
                char temp[32];
                seraph_codegen_temp_name(gen, temp, sizeof(temp));

                /* Start statement expression with scrutinee evaluation */
                seraph_codegen_write(gen, "({ __auto_type %s = ", temp);
                seraph_codegen_expr(gen, expr->match.scrutinee);
                seraph_codegen_write(gen, "; ");

                /* Count arms and find default (wildcard) arm if any */
                int arm_count = 0;
                Seraph_AST_Node* default_arm = NULL;
                for (Seraph_AST_Node* arm = expr->match.arms; arm != NULL;
                     arm = arm->hdr.next) {
                    if (arm->hdr.kind == AST_MATCH_ARM) {
                        arm_count++;
                        /* Check for wildcard pattern (pattern_kind == 1 or name == "_") */
                        if (arm->match_arm.pattern != NULL &&
                            arm->match_arm.pattern->hdr.kind == AST_PATTERN) {
                            int pk = arm->match_arm.pattern->pattern.pattern_kind;
                            if (pk == 1) { /* Wildcard */
                                default_arm = arm;
                            }
                        }
                    }
                }

                /* Generate nested ternary expressions */
                int nesting = 0;
                for (Seraph_AST_Node* arm = expr->match.arms; arm != NULL;
                     arm = arm->hdr.next) {
                    if (arm->hdr.kind == AST_MATCH_ARM) {
                        /* Skip default arm - handle at end */
                        if (arm == default_arm) continue;

                        /* Generate condition: (__tmp == pattern) ? (body) : */
                        seraph_codegen_write(gen, "(%s == ", temp);

                        /* Generate pattern value
                         * Pattern kinds:
                         *   0 = identifier binding
                         *   1 = wildcard (_)
                         *   2 = integer literal
                         */
                        if (arm->match_arm.pattern != NULL &&
                            arm->match_arm.pattern->hdr.kind == AST_PATTERN) {
                            int pk = arm->match_arm.pattern->pattern.pattern_kind;
                            if (pk == 2) {
                                /* Int literal pattern */
                                seraph_codegen_write(gen, "%lld",
                                    (long long)arm->match_arm.pattern->pattern.data.int_val);
                            } else if (pk == 0 && arm->match_arm.pattern->pattern.data.ident.name != NULL) {
                                /* Identifier pattern - bind value to name
                                 * For comparison, we can't directly compare - this would be
                                 * handled differently in full implementation. For now treat as wildcard. */
                                seraph_codegen_write(gen, "0 /* binding: %.*s */",
                                    (int)arm->match_arm.pattern->pattern.data.ident.name_len,
                                    arm->match_arm.pattern->pattern.data.ident.name);
                            } else {
                                /* Unknown or unhandled pattern - use 0 */
                                seraph_codegen_write(gen, "0");
                            }
                        } else {
                            seraph_codegen_write(gen, "0");
                        }

                        seraph_codegen_write(gen, ") ? (");
                        seraph_codegen_expr(gen, arm->match_arm.body);
                        seraph_codegen_write(gen, ") : (");
                        nesting++;
                    }
                }

                /* Generate default case (or panic if no default) */
                if (default_arm != NULL) {
                    seraph_codegen_expr(gen, default_arm->match_arm.body);
                } else {
                    /* No default - this is an error in SERAPH (non-exhaustive match)
                     * but for now generate a zero value. A proper implementation
                     * would emit a compile error during type checking. */
                    seraph_codegen_write(gen, "0");
                }

                /* Close all nested ternaries */
                for (int i = 0; i < nesting; i++) {
                    seraph_codegen_write(gen, ")");
                }

                /* Close statement expression */
                seraph_codegen_write(gen, "; })");
            }
            break;

        /* Array literal */
        case AST_EXPR_ARRAY:
            seraph_codegen_write(gen, "{ ");
            {
                int first = 1;
                for (Seraph_AST_Node* elem = expr->array.elements; elem != NULL;
                     elem = elem->hdr.next) {
                    if (!first) seraph_codegen_write(gen, ", ");
                    seraph_codegen_expr(gen, elem);
                    first = 0;
                }
            }
            seraph_codegen_write(gen, " }");
            break;

        /* Struct initializer: StructName { field: value, ... } */
        case AST_EXPR_STRUCT_INIT:
            {
                /* Get type name and length */
                const char* type_name = "unknown";
                int type_name_len = 7;
                if (expr->struct_init.type_path &&
                    expr->struct_init.type_path->hdr.kind == AST_EXPR_IDENT) {
                    type_name = expr->struct_init.type_path->ident.name;
                    type_name_len = (int)expr->struct_init.type_path->ident.name_len;
                }

                /* Output as C99 compound literal: (struct TypeName){ .field = val, ... } */
                seraph_codegen_write(gen, "(struct %.*s){ ", type_name_len, type_name);

                int first = 1;
                for (Seraph_AST_Node* field = expr->struct_init.fields;
                     field != NULL; field = field->hdr.next) {
                    if (field->hdr.kind != AST_FIELD_INIT) continue;

                    if (!first) seraph_codegen_write(gen, ", ");
                    seraph_codegen_write(gen, ".%.*s = ",
                        (int)field->field_init.name_len, field->field_init.name);
                    seraph_codegen_expr(gen, field->field_init.value);
                    first = 0;
                }

                seraph_codegen_write(gen, " }");
            }
            break;

        /* Type cast: expr as Type */
        case AST_EXPR_CAST:
            seraph_codegen_write(gen, "((");
            seraph_codegen_type(gen, expr->cast.target_type);
            seraph_codegen_write(gen, ")(");
            seraph_codegen_expr(gen, expr->cast.operand);
            seraph_codegen_write(gen, "))");
            break;

        default:
            seraph_codegen_write(gen, "/* unhandled expr */");
            break;
    }
}

/*============================================================================
 * VOID Operator Generation
 *============================================================================*/

void seraph_codegen_void_prop(Seraph_Codegen* gen, Seraph_AST_Node* node) {
    if (gen == NULL || node == NULL) return;
    if (node->hdr.kind != AST_EXPR_VOID_PROP) return;

    /*
     * ?? operator (VOID propagation):
     * ({ __auto_type __tmp = (expr);
     *    if (SERAPH_IS_VOID(__tmp)) return SERAPH_VOID_<TYPE>;
     *    __tmp; })
     */
    char temp[32];
    seraph_codegen_temp_name(gen, temp, sizeof(temp));

    seraph_codegen_write(gen, "({ __auto_type %s = (", temp);
    seraph_codegen_expr(gen, node->void_prop.operand);
    seraph_codegen_write(gen, "); ");
    seraph_codegen_write(gen, "if (SERAPH_IS_VOID(%s)) return SERAPH_VOID_U64; ", temp);
    seraph_codegen_write(gen, "%s; })", temp);
}

void seraph_codegen_void_assert(Seraph_Codegen* gen, Seraph_AST_Node* node) {
    if (gen == NULL || node == NULL) return;
    if (node->hdr.kind != AST_EXPR_VOID_ASSERT) return;

    /*
     * !! operator (VOID assertion):
     * ({ __auto_type __tmp = (expr);
     *    if (SERAPH_IS_VOID(__tmp)) seraph_panic("VOID assertion failed at ...");
     *    __tmp; })
     */
    char temp[32];
    seraph_codegen_temp_name(gen, temp, sizeof(temp));

    seraph_codegen_write(gen, "({ __auto_type %s = (", temp);
    seraph_codegen_expr(gen, node->void_assert.operand);
    seraph_codegen_write(gen, "); ");
    seraph_codegen_write(gen, "if (SERAPH_IS_VOID(%s)) seraph_panic(\"VOID assertion failed at %s:%u\"); ",
                          temp,
                          node->hdr.loc.filename ? node->hdr.loc.filename : "unknown",
                          node->hdr.loc.line);
    seraph_codegen_write(gen, "%s; })", temp);
}

/*============================================================================
 * Statement Generation
 *============================================================================*/

void seraph_codegen_stmt(Seraph_Codegen* gen, Seraph_AST_Node* stmt) {
    if (gen == NULL || stmt == NULL) return;

    switch (stmt->hdr.kind) {
        case AST_STMT_EXPR:
            seraph_codegen_indent(gen);
            seraph_codegen_expr(gen, stmt->expr_stmt.expr);
            seraph_codegen_write(gen, ";\n");
            break;

        case AST_STMT_RETURN:
            seraph_codegen_indent(gen);
            seraph_codegen_write(gen, "return");
            if (stmt->return_stmt.expr != NULL) {
                /* Check if we're in main() - need to cast return value to int */
                int in_main = (gen->current_fn_name != NULL &&
                               gen->current_fn_name_len == 4 &&
                               memcmp(gen->current_fn_name, "main", 4) == 0);
                if (in_main) {
                    seraph_codegen_write(gen, " (int)");
                } else {
                    seraph_codegen_write(gen, " ");
                }
                seraph_codegen_expr(gen, stmt->return_stmt.expr);
            }
            seraph_codegen_write(gen, ";\n");
            break;

        case AST_STMT_BREAK:
            seraph_codegen_writeln(gen, "break;");
            break;

        case AST_STMT_CONTINUE:
            seraph_codegen_writeln(gen, "continue;");
            break;

        case AST_STMT_WHILE:
            seraph_codegen_indent(gen);
            seraph_codegen_write(gen, "while (");
            seraph_codegen_expr(gen, stmt->while_stmt.cond);
            seraph_codegen_write(gen, ") ");
            seraph_codegen_block(gen, stmt->while_stmt.body);
            seraph_codegen_write(gen, "\n");
            break;

        case AST_STMT_FOR:
            {
                Seraph_AST_Node* iterable = stmt->for_stmt.iterable;

                /* Check if iterable is a range expression */
                if (iterable != NULL && iterable->hdr.kind == AST_EXPR_RANGE) {
                    /* Range-based for: for i in start..end { body }
                     * Emit: for (int64_t i = start; i < end; i++) { ... } */
                    seraph_codegen_indent(gen);
                    seraph_codegen_write(gen, "for (int64_t %.*s = ",
                                          (int)stmt->for_stmt.var_len, stmt->for_stmt.var);
                    seraph_codegen_expr(gen, iterable->range.start);
                    seraph_codegen_write(gen, "; %.*s < ",
                                          (int)stmt->for_stmt.var_len, stmt->for_stmt.var);
                    seraph_codegen_expr(gen, iterable->range.end);
                    seraph_codegen_write(gen, "; %.*s++) ",
                                          (int)stmt->for_stmt.var_len, stmt->for_stmt.var);
                    seraph_codegen_block(gen, stmt->for_stmt.body);
                    seraph_codegen_write(gen, "\n");
                } else {
                    /* Array/slice iteration: for i in arr { body }
                     * Emit: { __auto_type __tmp = arr; for (size_t __i = 0; __i < sizeof(__tmp)/sizeof(__tmp[0]); __i++) { __auto_type i = __tmp[__i]; ... } } */
                    char iter[32];
                    seraph_codegen_temp_name(gen, iter, sizeof(iter));

                    seraph_codegen_indent(gen);
                    seraph_codegen_write(gen, "{ __auto_type %s = ", iter);
                    seraph_codegen_expr(gen, iterable);
                    seraph_codegen_write(gen, "; ");
                    seraph_codegen_write(gen, "for (size_t __i = 0; __i < sizeof(%s)/sizeof(%s[0]); __i++) ",
                                          iter, iter);
                    seraph_codegen_write(gen, "{ __auto_type %.*s = %s[__i]; ",
                                          (int)stmt->for_stmt.var_len, stmt->for_stmt.var, iter);
                    seraph_codegen_indent_inc(gen);
                    seraph_codegen_write(gen, "\n");
                    for (Seraph_AST_Node* s = stmt->for_stmt.body->block.stmts; s != NULL;
                         s = s->hdr.next) {
                        seraph_codegen_stmt(gen, s);
                    }
                    seraph_codegen_indent_dec(gen);
                    seraph_codegen_writeln(gen, "} }");
                }
            }
            break;

        case AST_DECL_LET:
        case AST_DECL_CONST:
            seraph_codegen_indent(gen);
            if (stmt->let_decl.type != NULL) {
                seraph_codegen_type_with_name(gen, stmt->let_decl.type,
                                               stmt->let_decl.name, stmt->let_decl.name_len);
                /* Track pointer local variables for auto-deref field access */
                if (seraph_type_is_pointer(stmt->let_decl.type)) {
                    seraph_codegen_add_ptr_name(gen, stmt->let_decl.name, stmt->let_decl.name_len);
                }
            } else {
                seraph_codegen_write(gen, "__auto_type %.*s",
                                      (int)stmt->let_decl.name_len, stmt->let_decl.name);
            }
            if (stmt->let_decl.init != NULL) {
                seraph_codegen_write(gen, " = ");
                seraph_codegen_expr(gen, stmt->let_decl.init);
            }
            seraph_codegen_write(gen, ";\n");
            break;

        case AST_STMT_PERSIST:
            seraph_codegen_persist(gen, stmt);
            break;

        case AST_STMT_AETHER:
            seraph_codegen_aether(gen, stmt);
            break;

        case AST_STMT_RECOVER:
            seraph_codegen_recover(gen, stmt);
            break;

        case AST_EXPR_IF:
            seraph_codegen_indent(gen);
            seraph_codegen_write(gen, "if (");
            seraph_codegen_expr(gen, stmt->if_expr.cond);
            seraph_codegen_write(gen, ") ");
            if (stmt->if_expr.then_branch != NULL) {
                seraph_codegen_block(gen, stmt->if_expr.then_branch);
            }
            if (stmt->if_expr.else_branch != NULL) {
                seraph_codegen_write(gen, " else ");
                seraph_codegen_block(gen, stmt->if_expr.else_branch);
            }
            seraph_codegen_write(gen, "\n");
            break;

        default:
            break;
    }
}

void seraph_codegen_block(Seraph_Codegen* gen, Seraph_AST_Node* block) {
    if (gen == NULL || block == NULL) return;
    if (block->hdr.kind != AST_EXPR_BLOCK) {
        /* Treat as single expression */
        seraph_codegen_expr(gen, block);
        return;
    }

    seraph_codegen_write(gen, "{\n");
    seraph_codegen_indent_inc(gen);

    /* Statements */
    for (Seraph_AST_Node* stmt = block->block.stmts; stmt != NULL;
         stmt = stmt->hdr.next) {
        seraph_codegen_stmt(gen, stmt);
    }

    /* Result expression */
    if (block->block.expr != NULL) {
        seraph_codegen_indent(gen);
        seraph_codegen_expr(gen, block->block.expr);
        seraph_codegen_write(gen, ";\n");
    }

    seraph_codegen_indent_dec(gen);
    seraph_codegen_indent(gen);
    seraph_codegen_write(gen, "}");
}

/*============================================================================
 * Substrate Block Generation
 *============================================================================*/

void seraph_codegen_persist(Seraph_Codegen* gen, Seraph_AST_Node* node) {
    if (gen == NULL || node == NULL) return;
    if (node->hdr.kind != AST_STMT_PERSIST) return;

    /*
     * persist { body }  ->
     *   { Seraph_Atlas_Transaction* __tx = seraph_atlas_begin(&__atlas);
     *     <body>
     *     seraph_atlas_commit(&__atlas, __tx); }
     */
    seraph_codegen_writeln(gen, "{ /* persist block */");
    seraph_codegen_indent_inc(gen);

    seraph_codegen_writeln(gen, "Seraph_Atlas_Transaction* __tx = seraph_atlas_begin(&__atlas);");
    seraph_codegen_writeln(gen, "volatile int __persist_ok = 1;");

    if (node->substrate_block.body != NULL) {
        seraph_codegen_writeln(gen, "if (1) {");
        seraph_codegen_indent_inc(gen);
        for (Seraph_AST_Node* stmt = node->substrate_block.body->block.stmts;
             stmt != NULL; stmt = stmt->hdr.next) {
            seraph_codegen_stmt(gen, stmt);
        }
        seraph_codegen_indent_dec(gen);
        seraph_codegen_writeln(gen, "}");
    }

    seraph_codegen_writeln(gen, "if (__persist_ok) seraph_atlas_commit(&__atlas, __tx);");
    seraph_codegen_writeln(gen, "else seraph_atlas_rollback(&__atlas, __tx);");

    seraph_codegen_indent_dec(gen);
    seraph_codegen_writeln(gen, "} /* end persist */");
}

void seraph_codegen_aether(Seraph_Codegen* gen, Seraph_AST_Node* node) {
    if (gen == NULL || node == NULL) return;
    if (node->hdr.kind != AST_STMT_AETHER) return;

    /*
     * aether { body }  ->
     *   { Seraph_Aether_Context* __actx = seraph_aether_begin(&__aether);
     *     <body>
     *     seraph_aether_end(&__aether, __actx); }
     */
    seraph_codegen_writeln(gen, "{ /* aether block */");
    seraph_codegen_indent_inc(gen);

    seraph_codegen_writeln(gen, "Seraph_Aether_Context* __actx = seraph_aether_begin(&__aether);");

    if (node->substrate_block.body != NULL) {
        for (Seraph_AST_Node* stmt = node->substrate_block.body->block.stmts;
             stmt != NULL; stmt = stmt->hdr.next) {
            seraph_codegen_stmt(gen, stmt);
        }
    }

    seraph_codegen_writeln(gen, "seraph_aether_end(&__aether, __actx);");

    seraph_codegen_indent_dec(gen);
    seraph_codegen_writeln(gen, "} /* end aether */");
}

void seraph_codegen_recover(Seraph_Codegen* gen, Seraph_AST_Node* node) {
    if (gen == NULL || node == NULL) return;
    if (node->hdr.kind != AST_STMT_RECOVER) return;

    /*
     * recover { try } else { else }  ->
     *   { jmp_buf __recover_buf_N;
     *     if (setjmp(__recover_buf_N) == 0) { <try> }
     *     else { <else> } }
     */
    uint32_t id = gen->recover_counter++;

    seraph_codegen_writeln(gen, "{ /* recover block */");
    seraph_codegen_indent_inc(gen);

    seraph_codegen_writeln(gen, "jmp_buf __recover_buf_%u;", id);
    seraph_codegen_writeln(gen, "if (setjmp(__recover_buf_%u) == 0) {", id);

    /* Try body */
    seraph_codegen_indent_inc(gen);
    gen->in_recover = 1;
    gen->recover_depth++;

    if (node->recover_stmt.try_body != NULL) {
        for (Seraph_AST_Node* stmt = node->recover_stmt.try_body->block.stmts;
             stmt != NULL; stmt = stmt->hdr.next) {
            seraph_codegen_stmt(gen, stmt);
        }
    }

    gen->recover_depth--;
    gen->in_recover = gen->recover_depth > 0;

    seraph_codegen_indent_dec(gen);
    seraph_codegen_writeln(gen, "} else {");

    /* Else body */
    seraph_codegen_indent_inc(gen);

    if (node->recover_stmt.else_body != NULL) {
        for (Seraph_AST_Node* stmt = node->recover_stmt.else_body->block.stmts;
             stmt != NULL; stmt = stmt->hdr.next) {
            seraph_codegen_stmt(gen, stmt);
        }
    }

    seraph_codegen_indent_dec(gen);
    seraph_codegen_writeln(gen, "}");

    seraph_codegen_indent_dec(gen);
    seraph_codegen_writeln(gen, "} /* end recover */");
}
