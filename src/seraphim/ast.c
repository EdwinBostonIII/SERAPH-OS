/**
 * @file ast.c
 * @brief Seraphim Compiler - AST Utilities Implementation
 *
 * MC26: Seraphim Language AST Utilities
 */

#include "seraph/seraphim/ast.h"
#include <stdio.h>
#include <string.h>

/*============================================================================
 * AST Kind Names
 *============================================================================*/

const char* seraph_ast_kind_name(Seraph_AST_Kind kind) {
    switch (kind) {
        case AST_VOID:              return "VOID";
        case AST_MODULE:            return "Module";

        /* Declarations */
        case AST_DECL_FN:           return "FnDecl";
        case AST_DECL_LET:          return "LetDecl";
        case AST_DECL_CONST:        return "ConstDecl";
        case AST_DECL_STRUCT:       return "StructDecl";
        case AST_DECL_ENUM:         return "EnumDecl";
        case AST_DECL_TYPE:         return "TypeDecl";
        case AST_DECL_IMPL:         return "ImplDecl";
        case AST_DECL_USE:          return "UseDecl";
        case AST_DECL_FOREIGN:      return "ForeignDecl";

        /* Expressions - Literals */
        case AST_EXPR_INT_LIT:      return "IntLit";
        case AST_EXPR_FLOAT_LIT:    return "FloatLit";
        case AST_EXPR_STRING_LIT:   return "StringLit";
        case AST_EXPR_CHAR_LIT:     return "CharLit";
        case AST_EXPR_BOOL_LIT:     return "BoolLit";
        case AST_EXPR_VOID_LIT:     return "VoidLit";

        /* Expressions - References */
        case AST_EXPR_IDENT:        return "Ident";
        case AST_EXPR_PATH:         return "Path";

        /* Expressions - Operators */
        case AST_EXPR_BINARY:       return "Binary";
        case AST_EXPR_UNARY:        return "Unary";
        case AST_EXPR_VOID_PROP:    return "VoidProp";
        case AST_EXPR_VOID_ASSERT:  return "VoidAssert";

        /* Expressions - Calls and access */
        case AST_EXPR_CALL:         return "Call";
        case AST_EXPR_FIELD:        return "Field";
        case AST_EXPR_INDEX:        return "Index";
        case AST_EXPR_METHOD_CALL:  return "MethodCall";

        /* Expressions - Compound */
        case AST_EXPR_BLOCK:        return "Block";
        case AST_EXPR_IF:           return "If";
        case AST_EXPR_MATCH:        return "Match";
        case AST_EXPR_ARRAY:        return "Array";
        case AST_EXPR_STRUCT_INIT:  return "StructInit";
        case AST_EXPR_CAST:         return "Cast";
        case AST_EXPR_RANGE:        return "Range";
        case AST_EXPR_CLOSURE:      return "Closure";

        /* Statements */
        case AST_STMT_EXPR:         return "ExprStmt";
        case AST_STMT_RETURN:       return "Return";
        case AST_STMT_BREAK:        return "Break";
        case AST_STMT_CONTINUE:     return "Continue";
        case AST_STMT_FOR:          return "For";
        case AST_STMT_WHILE:        return "While";
        case AST_STMT_PERSIST:      return "Persist";
        case AST_STMT_AETHER:       return "Aether";
        case AST_STMT_RECOVER:      return "Recover";

        /* Types */
        case AST_TYPE_PRIMITIVE:    return "PrimType";
        case AST_TYPE_NAMED:        return "NamedType";
        case AST_TYPE_PATH:         return "PathType";
        case AST_TYPE_ARRAY:        return "ArrayType";
        case AST_TYPE_SLICE:        return "SliceType";
        case AST_TYPE_POINTER:      return "PtrType";
        case AST_TYPE_REF:          return "RefType";
        case AST_TYPE_MUT_REF:      return "MutRefType";
        case AST_TYPE_SUBSTRATE_REF: return "SubstrateRef";
        case AST_TYPE_FN:           return "FnType";
        case AST_TYPE_VOID_ABLE:    return "VoidType";
        case AST_TYPE_TUPLE:        return "TupleType";

        /* Auxiliary */
        case AST_PARAM:             return "Param";
        case AST_FIELD_DEF:         return "FieldDef";
        case AST_ENUM_VARIANT:      return "EnumVariant";
        case AST_MATCH_ARM:         return "MatchArm";
        case AST_EFFECT_LIST:       return "EffectList";
        case AST_PATTERN:           return "Pattern";
        case AST_FIELD_INIT:        return "FieldInit";
        case AST_GENERIC_PARAM:     return "GenericParam";

        default:                    return "<unknown>";
    }
}

/*============================================================================
 * VOID Node
 *============================================================================*/

Seraph_AST_Node* seraph_ast_void(Seraph_Arena* arena, Seraph_Source_Loc loc) {
    if (arena == NULL) return NULL;

    Seraph_AST_Node* node = (Seraph_AST_Node*)seraph_arena_alloc(
        arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
    );
    if (node == NULL) return NULL;

    memset(node, 0, sizeof(Seraph_AST_Node));
    node->hdr.kind = AST_VOID;
    node->hdr.loc = loc;
    node->hdr.next = NULL;
    return node;
}

/*============================================================================
 * AST Construction Helpers
 *============================================================================*/

static Seraph_AST_Node* alloc_node(Seraph_Arena* arena, Seraph_AST_Kind kind,
                                    Seraph_Source_Loc loc) {
    if (arena == NULL) return NULL;

    Seraph_AST_Node* node = (Seraph_AST_Node*)seraph_arena_alloc(
        arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
    );
    if (node == NULL) return NULL;

    memset(node, 0, sizeof(Seraph_AST_Node));
    node->hdr.kind = kind;
    node->hdr.loc = loc;
    node->hdr.next = NULL;
    return node;
}

/* Module */
Seraph_AST_Node* seraph_ast_module(Seraph_Arena* arena, Seraph_Source_Loc loc) {
    Seraph_AST_Node* node = alloc_node(arena, AST_MODULE, loc);
    if (node == NULL) return NULL;
    node->module.name = NULL;
    node->module.name_len = 0;
    node->module.decls = NULL;
    node->module.decl_count = 0;
    return node;
}

/* Declarations */
Seraph_AST_Node* seraph_ast_fn_decl(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                     const char* name, size_t name_len) {
    Seraph_AST_Node* node = alloc_node(arena, AST_DECL_FN, loc);
    if (node == NULL) return NULL;
    node->fn_decl.name = name;
    node->fn_decl.name_len = name_len;
    node->fn_decl.params = NULL;
    node->fn_decl.param_count = 0;
    node->fn_decl.ret_type = NULL;
    node->fn_decl.body = NULL;
    node->fn_decl.effects = NULL;
    node->fn_decl.is_pure = 0;
    node->fn_decl.is_foreign = 0;
    node->fn_decl.is_method = 0;
    return node;
}

Seraph_AST_Node* seraph_ast_let_decl(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                      const char* name, size_t name_len,
                                      int is_mut, int is_const) {
    Seraph_AST_Node* node = alloc_node(arena, is_const ? AST_DECL_CONST : AST_DECL_LET, loc);
    if (node == NULL) return NULL;
    node->let_decl.name = name;
    node->let_decl.name_len = name_len;
    node->let_decl.type = NULL;
    node->let_decl.init = NULL;
    node->let_decl.is_mut = is_mut;
    node->let_decl.is_const = is_const;
    return node;
}

Seraph_AST_Node* seraph_ast_struct_decl(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                         const char* name, size_t name_len) {
    Seraph_AST_Node* node = alloc_node(arena, AST_DECL_STRUCT, loc);
    if (node == NULL) return NULL;
    node->struct_decl.name = name;
    node->struct_decl.name_len = name_len;
    node->struct_decl.generics = NULL;
    node->struct_decl.fields = NULL;
    node->struct_decl.field_count = 0;
    return node;
}

/* Expressions */
Seraph_AST_Node* seraph_ast_int_lit(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                     uint64_t value, Seraph_Num_Suffix suffix) {
    Seraph_AST_Node* node = alloc_node(arena, AST_EXPR_INT_LIT, loc);
    if (node == NULL) return NULL;
    node->int_lit.value = value;
    node->int_lit.suffix = suffix;
    return node;
}

Seraph_AST_Node* seraph_ast_float_lit(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                       double value, Seraph_Num_Suffix suffix) {
    Seraph_AST_Node* node = alloc_node(arena, AST_EXPR_FLOAT_LIT, loc);
    if (node == NULL) return NULL;
    node->float_lit.value = value;
    node->float_lit.suffix = suffix;
    return node;
}

Seraph_AST_Node* seraph_ast_string_lit(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                        const char* value, size_t len) {
    Seraph_AST_Node* node = alloc_node(arena, AST_EXPR_STRING_LIT, loc);
    if (node == NULL) return NULL;
    node->string_lit.value = value;
    node->string_lit.len = len;
    return node;
}

Seraph_AST_Node* seraph_ast_bool_lit(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                      int value) {
    Seraph_AST_Node* node = alloc_node(arena, AST_EXPR_BOOL_LIT, loc);
    if (node == NULL) return NULL;
    node->bool_lit.value = value;
    return node;
}

Seraph_AST_Node* seraph_ast_ident(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                   const char* name, size_t name_len) {
    Seraph_AST_Node* node = alloc_node(arena, AST_EXPR_IDENT, loc);
    if (node == NULL) return NULL;
    node->ident.name = name;
    node->ident.name_len = name_len;
    return node;
}

Seraph_AST_Node* seraph_ast_binary(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                    Seraph_Token_Type op,
                                    Seraph_AST_Node* left, Seraph_AST_Node* right) {
    Seraph_AST_Node* node = alloc_node(arena, AST_EXPR_BINARY, loc);
    if (node == NULL) return NULL;
    node->binary.op = op;
    node->binary.left = left;
    node->binary.right = right;
    return node;
}

Seraph_AST_Node* seraph_ast_unary(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                   Seraph_Token_Type op, Seraph_AST_Node* operand) {
    Seraph_AST_Node* node = alloc_node(arena, AST_EXPR_UNARY, loc);
    if (node == NULL) return NULL;
    node->unary.op = op;
    node->unary.operand = operand;
    return node;
}

Seraph_AST_Node* seraph_ast_call(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                  Seraph_AST_Node* callee) {
    Seraph_AST_Node* node = alloc_node(arena, AST_EXPR_CALL, loc);
    if (node == NULL) return NULL;
    node->call.callee = callee;
    node->call.args = NULL;
    node->call.arg_count = 0;
    return node;
}

Seraph_AST_Node* seraph_ast_field(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                   Seraph_AST_Node* object,
                                   const char* field, size_t field_len) {
    Seraph_AST_Node* node = alloc_node(arena, AST_EXPR_FIELD, loc);
    if (node == NULL) return NULL;
    node->field.object = object;
    node->field.field = field;
    node->field.field_len = field_len;
    return node;
}

Seraph_AST_Node* seraph_ast_block(Seraph_Arena* arena, Seraph_Source_Loc loc) {
    Seraph_AST_Node* node = alloc_node(arena, AST_EXPR_BLOCK, loc);
    if (node == NULL) return NULL;
    node->block.stmts = NULL;
    node->block.stmt_count = 0;
    node->block.expr = NULL;
    return node;
}

Seraph_AST_Node* seraph_ast_if(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                Seraph_AST_Node* cond,
                                Seraph_AST_Node* then_branch,
                                Seraph_AST_Node* else_branch) {
    Seraph_AST_Node* node = alloc_node(arena, AST_EXPR_IF, loc);
    if (node == NULL) return NULL;
    node->if_expr.cond = cond;
    node->if_expr.then_branch = then_branch;
    node->if_expr.else_branch = else_branch;
    return node;
}

Seraph_AST_Node* seraph_ast_struct_init(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                         Seraph_AST_Node* type_path) {
    Seraph_AST_Node* node = alloc_node(arena, AST_EXPR_STRUCT_INIT, loc);
    if (node == NULL) return NULL;
    node->struct_init.type_path = type_path;
    node->struct_init.fields = NULL;
    node->struct_init.field_count = 0;
    return node;
}

Seraph_AST_Node* seraph_ast_field_init(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                        const char* name, size_t name_len,
                                        Seraph_AST_Node* value) {
    Seraph_AST_Node* node = alloc_node(arena, AST_FIELD_INIT, loc);
    if (node == NULL) return NULL;
    node->field_init.name = name;
    node->field_init.name_len = name_len;
    node->field_init.value = value;
    return node;
}

Seraph_AST_Node* seraph_ast_cast(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                  Seraph_AST_Node* operand, Seraph_AST_Node* target_type) {
    Seraph_AST_Node* node = alloc_node(arena, AST_EXPR_CAST, loc);
    if (node == NULL) return NULL;
    node->cast.operand = operand;
    node->cast.target_type = target_type;
    return node;
}

static uint32_t s_closure_id_counter = 0;

Seraph_AST_Node* seraph_ast_closure(Seraph_Arena* arena, Seraph_Source_Loc loc) {
    Seraph_AST_Node* node = alloc_node(arena, AST_EXPR_CLOSURE, loc);
    if (node == NULL) return NULL;
    node->closure.params = NULL;
    node->closure.param_count = 0;
    node->closure.ret_type = NULL;
    node->closure.body = NULL;
    node->closure.captures = NULL;
    node->closure.capture_count = 0;
    node->closure.closure_id = s_closure_id_counter++;
    return node;
}

/* Types */
Seraph_AST_Node* seraph_ast_prim_type(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                       Seraph_Token_Type prim) {
    Seraph_AST_Node* node = alloc_node(arena, AST_TYPE_PRIMITIVE, loc);
    if (node == NULL) return NULL;
    node->prim_type.prim = prim;
    return node;
}

Seraph_AST_Node* seraph_ast_named_type(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                        const char* name, size_t name_len) {
    Seraph_AST_Node* node = alloc_node(arena, AST_TYPE_NAMED, loc);
    if (node == NULL) return NULL;
    node->named_type.name = name;
    node->named_type.name_len = name_len;
    node->named_type.generics = NULL;
    return node;
}

Seraph_AST_Node* seraph_ast_ref_type(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                      Seraph_AST_Node* inner, int is_mut) {
    Seraph_AST_Node* node = alloc_node(arena, is_mut ? AST_TYPE_MUT_REF : AST_TYPE_REF, loc);
    if (node == NULL) return NULL;
    node->ref_type.inner = inner;
    node->ref_type.is_mut = is_mut;
    node->ref_type.substrate = 0;
    return node;
}

Seraph_AST_Node* seraph_ast_ptr_type(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                      Seraph_AST_Node* inner) {
    Seraph_AST_Node* node = alloc_node(arena, AST_TYPE_POINTER, loc);
    if (node == NULL) return NULL;
    /* Reuse ref_type union field for pointer - both just wrap an inner type */
    node->ref_type.inner = inner;
    node->ref_type.is_mut = 0;
    node->ref_type.substrate = 0;
    return node;
}

Seraph_AST_Node* seraph_ast_void_type(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                       Seraph_AST_Node* inner) {
    Seraph_AST_Node* node = alloc_node(arena, AST_TYPE_VOID_ABLE, loc);
    if (node == NULL) return NULL;
    node->void_type.inner = inner;
    return node;
}

/* Auxiliary */
Seraph_AST_Node* seraph_ast_param(Seraph_Arena* arena, Seraph_Source_Loc loc,
                                   const char* name, size_t name_len,
                                   Seraph_AST_Node* type) {
    Seraph_AST_Node* node = alloc_node(arena, AST_PARAM, loc);
    if (node == NULL) return NULL;
    node->param.name = name;
    node->param.name_len = name_len;
    node->param.type = type;
    node->param.is_mut = 0;
    return node;
}

/*============================================================================
 * AST List Helpers
 *============================================================================*/

void seraph_ast_append(Seraph_AST_Node** list, Seraph_AST_Node* node) {
    if (list == NULL || node == NULL) return;

    node->hdr.next = NULL;

    if (*list == NULL) {
        *list = node;
        return;
    }

    /* Find end of list */
    Seraph_AST_Node* curr = *list;
    while (curr->hdr.next != NULL) {
        curr = curr->hdr.next;
    }
    curr->hdr.next = node;
}

size_t seraph_ast_count(const Seraph_AST_Node* list) {
    size_t count = 0;
    while (list != NULL) {
        count++;
        list = list->hdr.next;
    }
    return count;
}

/*============================================================================
 * AST Pretty Printer
 *============================================================================*/

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }
}

static void print_name(const char* name, size_t len) {
    if (name == NULL) {
        printf("(null)");
    } else {
        printf("%.*s", (int)len, name);
    }
}

void seraph_ast_print(const Seraph_AST_Node* node, int indent) {
    if (node == NULL) {
        print_indent(indent);
        printf("(null)\n");
        return;
    }

    print_indent(indent);
    printf("%s", seraph_ast_kind_name(node->hdr.kind));

    switch (node->hdr.kind) {
        case AST_VOID:
            printf(" [error]\n");
            break;

        case AST_MODULE:
            printf("\n");
            for (Seraph_AST_Node* d = node->module.decls; d != NULL; d = d->hdr.next) {
                seraph_ast_print(d, indent + 1);
            }
            break;

        case AST_DECL_FN:
            printf(" ");
            print_name(node->fn_decl.name, node->fn_decl.name_len);
            if (node->fn_decl.is_pure) printf(" [pure]");
            printf("\n");
            print_indent(indent + 1);
            printf("params:\n");
            for (Seraph_AST_Node* p = node->fn_decl.params; p != NULL; p = p->hdr.next) {
                seraph_ast_print(p, indent + 2);
            }
            if (node->fn_decl.ret_type) {
                print_indent(indent + 1);
                printf("returns:\n");
                seraph_ast_print(node->fn_decl.ret_type, indent + 2);
            }
            if (node->fn_decl.body) {
                print_indent(indent + 1);
                printf("body:\n");
                seraph_ast_print(node->fn_decl.body, indent + 2);
            }
            break;

        case AST_DECL_LET:
        case AST_DECL_CONST:
            printf(" ");
            print_name(node->let_decl.name, node->let_decl.name_len);
            if (node->let_decl.is_mut) printf(" [mut]");
            printf("\n");
            if (node->let_decl.type) {
                print_indent(indent + 1);
                printf("type:\n");
                seraph_ast_print(node->let_decl.type, indent + 2);
            }
            if (node->let_decl.init) {
                print_indent(indent + 1);
                printf("init:\n");
                seraph_ast_print(node->let_decl.init, indent + 2);
            }
            break;

        case AST_DECL_STRUCT:
            printf(" ");
            print_name(node->struct_decl.name, node->struct_decl.name_len);
            printf("\n");
            for (Seraph_AST_Node* f = node->struct_decl.fields; f != NULL; f = f->hdr.next) {
                seraph_ast_print(f, indent + 1);
            }
            break;

        case AST_EXPR_INT_LIT:
            printf(" %llu\n", (unsigned long long)node->int_lit.value);
            break;

        case AST_EXPR_FLOAT_LIT:
            printf(" %g\n", node->float_lit.value);
            break;

        case AST_EXPR_STRING_LIT:
            printf(" \"");
            print_name(node->string_lit.value, node->string_lit.len);
            printf("\"\n");
            break;

        case AST_EXPR_BOOL_LIT:
            printf(" %s\n", node->bool_lit.value ? "true" : "false");
            break;

        case AST_EXPR_IDENT:
            printf(" ");
            print_name(node->ident.name, node->ident.name_len);
            printf("\n");
            break;

        case AST_EXPR_BINARY:
            printf(" %s\n", seraph_token_type_name(node->binary.op));
            seraph_ast_print(node->binary.left, indent + 1);
            seraph_ast_print(node->binary.right, indent + 1);
            break;

        case AST_EXPR_UNARY:
            printf(" %s\n", seraph_token_type_name(node->unary.op));
            seraph_ast_print(node->unary.operand, indent + 1);
            break;

        case AST_EXPR_CALL:
            printf("\n");
            print_indent(indent + 1);
            printf("callee:\n");
            seraph_ast_print(node->call.callee, indent + 2);
            print_indent(indent + 1);
            printf("args:\n");
            for (Seraph_AST_Node* a = node->call.args; a != NULL; a = a->hdr.next) {
                seraph_ast_print(a, indent + 2);
            }
            break;

        case AST_EXPR_FIELD:
            printf(" .");
            print_name(node->field.field, node->field.field_len);
            printf("\n");
            seraph_ast_print(node->field.object, indent + 1);
            break;

        case AST_EXPR_BLOCK:
            printf("\n");
            for (Seraph_AST_Node* s = node->block.stmts; s != NULL; s = s->hdr.next) {
                seraph_ast_print(s, indent + 1);
            }
            if (node->block.expr) {
                print_indent(indent + 1);
                printf("result:\n");
                seraph_ast_print(node->block.expr, indent + 2);
            }
            break;

        case AST_EXPR_IF:
            printf("\n");
            print_indent(indent + 1);
            printf("cond:\n");
            seraph_ast_print(node->if_expr.cond, indent + 2);
            print_indent(indent + 1);
            printf("then:\n");
            seraph_ast_print(node->if_expr.then_branch, indent + 2);
            if (node->if_expr.else_branch) {
                print_indent(indent + 1);
                printf("else:\n");
                seraph_ast_print(node->if_expr.else_branch, indent + 2);
            }
            break;

        case AST_STMT_RETURN:
            printf("\n");
            if (node->return_stmt.expr) {
                seraph_ast_print(node->return_stmt.expr, indent + 1);
            }
            break;

        case AST_STMT_EXPR:
            printf("\n");
            seraph_ast_print(node->expr_stmt.expr, indent + 1);
            break;

        case AST_TYPE_PRIMITIVE:
            printf(" %s\n", seraph_token_type_name(node->prim_type.prim));
            break;

        case AST_TYPE_NAMED:
            printf(" ");
            print_name(node->named_type.name, node->named_type.name_len);
            printf("\n");
            break;

        case AST_TYPE_REF:
        case AST_TYPE_MUT_REF:
            printf("%s\n", node->ref_type.is_mut ? " &mut" : " &");
            seraph_ast_print(node->ref_type.inner, indent + 1);
            break;

        case AST_TYPE_VOID_ABLE:
            printf(" ??\n");
            seraph_ast_print(node->void_type.inner, indent + 1);
            break;

        case AST_PARAM:
            printf(" ");
            print_name(node->param.name, node->param.name_len);
            printf("\n");
            if (node->param.type) {
                seraph_ast_print(node->param.type, indent + 1);
            }
            break;

        case AST_FIELD_DEF:
            printf(" ");
            print_name(node->field_def.name, node->field_def.name_len);
            printf("\n");
            if (node->field_def.type) {
                seraph_ast_print(node->field_def.type, indent + 1);
            }
            break;

        default:
            printf("\n");
            break;
    }
}
