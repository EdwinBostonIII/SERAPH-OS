/**
 * @file checker.c
 * @brief Seraphim Compiler - Type Checker Implementation
 *
 * MC26: Seraphim Language Type Checker
 *
 * Implements type checking for the Seraphim language, including:
 * - Expression type inference
 * - VOID propagation/assertion validation
 * - Substrate block validation
 * - Recover block validation
 */

#include "seraph/seraphim/checker.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/*============================================================================
 * Initialization
 *============================================================================*/

Seraph_Vbit seraph_checker_init(Seraph_Checker* checker,
                                 Seraph_Arena* arena,
                                 Seraph_Type_Context* types) {
    if (checker == NULL || arena == NULL) {
        return SERAPH_VBIT_VOID;
    }

    memset(checker, 0, sizeof(Seraph_Checker));
    checker->arena = arena;
    checker->types = types;

    /* Create internal type context if none provided */
    if (checker->types == NULL) {
        checker->types = (Seraph_Type_Context*)seraph_arena_alloc(
            arena, sizeof(Seraph_Type_Context), _Alignof(Seraph_Type_Context)
        );
        if (checker->types == NULL) {
            return SERAPH_VBIT_FALSE;
        }
        if (!seraph_vbit_is_true(seraph_type_context_init(checker->types, arena))) {
            return SERAPH_VBIT_FALSE;
        }
    }

    return SERAPH_VBIT_TRUE;
}

void seraph_checker_set_effects(Seraph_Checker* checker,
                                 Seraph_Effect_Context* effects) {
    if (checker != NULL) {
        checker->effects = effects;
    }
}

/*============================================================================
 * Module Checking
 *============================================================================*/

Seraph_Vbit seraph_checker_check_module(Seraph_Checker* checker,
                                         Seraph_AST_Node* module) {
    if (checker == NULL || module == NULL) {
        return SERAPH_VBIT_VOID;
    }
    if (module->hdr.kind != AST_MODULE) {
        return SERAPH_VBIT_VOID;
    }

    Seraph_Vbit result = SERAPH_VBIT_TRUE;

    /* First pass: register all declarations */
    for (Seraph_AST_Node* decl = module->module.decls; decl != NULL;
         decl = decl->hdr.next) {
        switch (decl->hdr.kind) {
            case AST_DECL_FN:
                /* Register function in symbol table */
                if (checker->types != NULL) {
                    Seraph_Type* fn_type = seraph_type_from_ast(checker->types,
                                                                 decl->fn_decl.ret_type);
                    seraph_type_define(checker->types,
                                       decl->fn_decl.name, decl->fn_decl.name_len,
                                       fn_type, decl, 0);
                }
                break;

            case AST_DECL_STRUCT:
                /* Register struct in symbol table */
                if (checker->types != NULL) {
                    Seraph_Type* struct_type = seraph_type_prim(checker->arena,
                                                                 SERAPH_TYPE_STRUCT);
                    if (struct_type != NULL) {
                        struct_type->named.name = decl->struct_decl.name;
                        struct_type->named.name_len = decl->struct_decl.name_len;
                        struct_type->named.decl = decl;
                    }
                    seraph_type_define(checker->types,
                                       decl->struct_decl.name, decl->struct_decl.name_len,
                                       struct_type, decl, 0);
                }
                break;

            case AST_DECL_ENUM:
                /* Register enum in symbol table */
                if (checker->types != NULL) {
                    Seraph_Type* enum_type = seraph_type_prim(checker->arena,
                                                               SERAPH_TYPE_ENUM);
                    if (enum_type != NULL) {
                        enum_type->named.name = decl->enum_decl.name;
                        enum_type->named.name_len = decl->enum_decl.name_len;
                        enum_type->named.decl = decl;
                    }
                    seraph_type_define(checker->types,
                                       decl->enum_decl.name, decl->enum_decl.name_len,
                                       enum_type, decl, 0);
                }
                break;

            case AST_DECL_CONST:
            case AST_DECL_LET:
                /* Register global constants and variables in symbol table
                 * so they're visible during function type-checking */
                if (checker->types != NULL && decl->let_decl.type != NULL) {
                    Seraph_Type* var_type = seraph_type_from_ast(checker->types,
                                                                  decl->let_decl.type);
                    seraph_type_define(checker->types,
                                       decl->let_decl.name, decl->let_decl.name_len,
                                       var_type, decl, decl->let_decl.is_mut);
                }
                break;

            default:
                break;
        }
    }

    /* Second pass: type check all declarations */
    for (Seraph_AST_Node* decl = module->module.decls; decl != NULL;
         decl = decl->hdr.next) {
        Seraph_Vbit decl_result = SERAPH_VBIT_TRUE;

        switch (decl->hdr.kind) {
            case AST_DECL_FN:
                decl_result = seraph_checker_check_fn(checker, decl);
                break;

            case AST_DECL_STRUCT:
                decl_result = seraph_checker_check_struct(checker, decl);
                break;

            case AST_DECL_ENUM:
                decl_result = seraph_checker_check_enum(checker, decl);
                break;

            case AST_DECL_LET:
            case AST_DECL_CONST:
                decl_result = seraph_checker_check_stmt(checker, decl);
                break;

            default:
                break;
        }

        if (!seraph_vbit_is_true(decl_result)) {
            result = SERAPH_VBIT_FALSE;
        }
    }

    return result;
}

/*============================================================================
 * Function Checking
 *============================================================================*/

Seraph_Vbit seraph_checker_check_fn(Seraph_Checker* checker,
                                     Seraph_AST_Node* fn_decl) {
    if (checker == NULL || fn_decl == NULL) {
        return SERAPH_VBIT_VOID;
    }
    if (fn_decl->hdr.kind != AST_DECL_FN) {
        return SERAPH_VBIT_VOID;
    }

    /* Save context */
    Seraph_AST_Node* old_fn = checker->current_fn;
    Seraph_Type* old_ret = checker->current_ret_type;
    checker->current_fn = fn_decl;

    /* Determine return type */
    if (fn_decl->fn_decl.ret_type != NULL) {
        checker->current_ret_type = seraph_type_from_ast(checker->types,
                                                          fn_decl->fn_decl.ret_type);
    } else {
        checker->current_ret_type = seraph_type_unit(checker->arena);
    }

    /* Push new scope for parameters */
    if (checker->types != NULL) {
        seraph_type_push_scope(checker->types);

        /* Register parameters */
        for (Seraph_AST_Node* param = fn_decl->fn_decl.params; param != NULL;
             param = param->hdr.next) {
            if (param->hdr.kind == AST_PARAM) {
                Seraph_Type* param_type = seraph_type_from_ast(checker->types,
                                                                param->param.type);
                seraph_type_define(checker->types,
                                   param->param.name, param->param.name_len,
                                   param_type, param, param->param.is_mut);
            }
        }
    }

    /* Enter effect context if available */
    Seraph_Effect_Flags declared_effects = SERAPH_EFFECT_NONE;
    if (fn_decl->fn_decl.is_pure) {
        declared_effects = SERAPH_EFFECT_NONE;
    } else if (fn_decl->fn_decl.effects != NULL &&
               fn_decl->fn_decl.effects->hdr.kind == AST_EFFECT_LIST) {
        declared_effects = fn_decl->fn_decl.effects->effect_list.effects;
    } else {
        declared_effects = SERAPH_EFFECT_ALL;  /* Default: allow all */
    }

    if (checker->effects != NULL) {
        seraph_effect_enter_fn(checker->effects, declared_effects);
    }

    /* Check function body */
    Seraph_Vbit result = SERAPH_VBIT_TRUE;
    if (fn_decl->fn_decl.body != NULL) {
        Seraph_Type* body_type = seraph_checker_check_block(checker,
                                                             fn_decl->fn_decl.body);

        /* Check return type compatibility */
        if (body_type != NULL && checker->current_ret_type != NULL) {
            if (!seraph_type_subtype(body_type, checker->current_ret_type)) {
                seraph_checker_type_mismatch(checker, fn_decl->fn_decl.body->hdr.loc,
                                              checker->current_ret_type, body_type);
                result = SERAPH_VBIT_FALSE;
            }
        }
    }

    /* Exit effect context */
    if (checker->effects != NULL) {
        Seraph_Vbit effect_result = seraph_effect_exit_fn(checker->effects);
        if (!seraph_vbit_is_true(effect_result)) {
            result = SERAPH_VBIT_FALSE;
        }
    }

    /* Pop parameter scope */
    if (checker->types != NULL) {
        seraph_type_pop_scope(checker->types);
    }

    /* Restore context */
    checker->current_fn = old_fn;
    checker->current_ret_type = old_ret;

    return result;
}

Seraph_Vbit seraph_checker_check_struct(Seraph_Checker* checker,
                                         Seraph_AST_Node* struct_decl) {
    if (checker == NULL || struct_decl == NULL) {
        return SERAPH_VBIT_VOID;
    }
    if (struct_decl->hdr.kind != AST_DECL_STRUCT) {
        return SERAPH_VBIT_VOID;
    }

    Seraph_Vbit result = SERAPH_VBIT_TRUE;

    /* Check all field types are valid */
    for (Seraph_AST_Node* field = struct_decl->struct_decl.fields; field != NULL;
         field = field->hdr.next) {
        if (field->hdr.kind == AST_FIELD_DEF) {
            Seraph_Type* field_type = seraph_type_from_ast(checker->types,
                                                            field->field_def.type);
            if (seraph_type_is_void(field_type)) {
                seraph_checker_error(checker, field->hdr.loc,
                                      "invalid field type for '%.*s'",
                                      (int)field->field_def.name_len,
                                      field->field_def.name);
                result = SERAPH_VBIT_FALSE;
            }
        }
    }

    return result;
}

Seraph_Vbit seraph_checker_check_enum(Seraph_Checker* checker,
                                       Seraph_AST_Node* enum_decl) {
    if (checker == NULL || enum_decl == NULL) {
        return SERAPH_VBIT_VOID;
    }
    if (enum_decl->hdr.kind != AST_DECL_ENUM) {
        return SERAPH_VBIT_VOID;
    }

    Seraph_Vbit result = SERAPH_VBIT_TRUE;

    /* Check all variant data types are valid */
    for (Seraph_AST_Node* variant = enum_decl->enum_decl.variants; variant != NULL;
         variant = variant->hdr.next) {
        if (variant->hdr.kind == AST_ENUM_VARIANT && variant->enum_variant.data != NULL) {
            Seraph_Type* data_type = seraph_type_from_ast(checker->types,
                                                           variant->enum_variant.data);
            if (seraph_type_is_void(data_type)) {
                seraph_checker_error(checker, variant->hdr.loc,
                                      "invalid data type for variant '%.*s'",
                                      (int)variant->enum_variant.name_len,
                                      variant->enum_variant.name);
                result = SERAPH_VBIT_FALSE;
            }
        }
    }

    return result;
}

/*============================================================================
 * Expression Checking
 *============================================================================*/

Seraph_Type* seraph_checker_check_expr(Seraph_Checker* checker,
                                        Seraph_AST_Node* expr) {
    if (checker == NULL || expr == NULL) {
        return seraph_type_void(checker ? checker->arena : NULL);
    }
    if (seraph_ast_is_void(expr)) {
        return seraph_type_void(checker->arena);
    }

    Seraph_Type* result = NULL;

    switch (expr->hdr.kind) {
        /* Literals */
        case AST_EXPR_INT_LIT:
            result = seraph_type_prim(checker->arena, SERAPH_TYPE_I64);
            break;

        case AST_EXPR_FLOAT_LIT:
            result = seraph_type_prim(checker->arena, SERAPH_TYPE_SCALAR);
            break;

        case AST_EXPR_BOOL_LIT:
            result = seraph_type_prim(checker->arena, SERAPH_TYPE_BOOL);
            break;

        case AST_EXPR_CHAR_LIT:
            result = seraph_type_prim(checker->arena, SERAPH_TYPE_CHAR);
            break;

        case AST_EXPR_STRING_LIT:
            result = seraph_type_slice(checker->arena,
                                        seraph_type_prim(checker->arena, SERAPH_TYPE_U8));
            break;

        case AST_EXPR_VOID_LIT:
            /* VOID literal has VOID-able type */
            result = seraph_type_voidable(checker->arena,
                                           seraph_type_never(checker->arena));
            break;

        /* Identifier */
        case AST_EXPR_IDENT:
            {
                Seraph_Symbol* sym = seraph_type_lookup(checker->types,
                                                         expr->ident.name,
                                                         expr->ident.name_len);
                if (sym == NULL) {
                    seraph_checker_error(checker, expr->hdr.loc,
                                          "undefined identifier '%.*s'",
                                          (int)expr->ident.name_len,
                                          expr->ident.name);
                    result = seraph_type_void(checker->arena);
                } else {
                    result = sym->type;
                }
            }
            break;

        /* Binary operators */
        case AST_EXPR_BINARY:
            {
                Seraph_Type* left = seraph_checker_check_expr(checker, expr->binary.left);
                Seraph_Type* right = seraph_checker_check_expr(checker, expr->binary.right);

                /* Check operand compatibility */
                switch (expr->binary.op) {
                    /* Arithmetic */
                    case SERAPH_TOK_PLUS:
                    case SERAPH_TOK_MINUS:
                    case SERAPH_TOK_STAR:
                        if (!seraph_type_is_numeric(left) || !seraph_type_is_numeric(right)) {
                            seraph_checker_error(checker, expr->hdr.loc,
                                                  "arithmetic requires numeric operands");
                        }
                        result = seraph_type_join(checker->types, left, right);
                        break;

                    /* Division - may produce VOID */
                    case SERAPH_TOK_SLASH:
                    case SERAPH_TOK_PERCENT:
                        if (!seraph_type_is_numeric(left) || !seraph_type_is_numeric(right)) {
                            seraph_checker_error(checker, expr->hdr.loc,
                                                  "division requires numeric operands");
                        }
                        result = seraph_type_voidable(checker->arena,
                                                       seraph_type_join(checker->types, left, right));
                        break;

                    /* Comparison */
                    case SERAPH_TOK_EQ:
                    case SERAPH_TOK_NE:
                    case SERAPH_TOK_LT:
                    case SERAPH_TOK_LE:
                    case SERAPH_TOK_GT:
                    case SERAPH_TOK_GE:
                        result = seraph_type_prim(checker->arena, SERAPH_TYPE_BOOL);
                        break;

                    /* Logical */
                    case SERAPH_TOK_AND:
                    case SERAPH_TOK_OR:
                        if (left->kind != SERAPH_TYPE_BOOL || right->kind != SERAPH_TYPE_BOOL) {
                            seraph_checker_error(checker, expr->hdr.loc,
                                                  "logical operators require bool operands");
                        }
                        result = seraph_type_prim(checker->arena, SERAPH_TYPE_BOOL);
                        break;

                    /* Bitwise */
                    case SERAPH_TOK_BIT_AND:
                    case SERAPH_TOK_BIT_OR:
                    case SERAPH_TOK_BIT_XOR:
                        if (!seraph_type_is_integer(left) || !seraph_type_is_integer(right)) {
                            seraph_checker_error(checker, expr->hdr.loc,
                                                  "bitwise operators require integer operands");
                        }
                        result = seraph_type_join(checker->types, left, right);
                        break;

                    /* Assignment */
                    case SERAPH_TOK_ASSIGN:
                        if (!seraph_type_subtype(right, left)) {
                            seraph_checker_type_mismatch(checker, expr->hdr.loc, left, right);
                        }
                        result = seraph_type_unit(checker->arena);
                        break;

                    default:
                        result = seraph_type_void(checker->arena);
                        break;
                }
            }
            break;

        /* Unary operators */
        case AST_EXPR_UNARY:
            {
                Seraph_Type* operand = seraph_checker_check_expr(checker, expr->unary.operand);

                switch (expr->unary.op) {
                    case SERAPH_TOK_MINUS:
                        if (!seraph_type_is_numeric(operand)) {
                            seraph_checker_error(checker, expr->hdr.loc,
                                                  "unary minus requires numeric operand");
                        }
                        result = operand;
                        break;

                    case SERAPH_TOK_NOT:
                        if (operand->kind != SERAPH_TYPE_BOOL) {
                            seraph_checker_error(checker, expr->hdr.loc,
                                                  "logical not requires bool operand");
                        }
                        result = seraph_type_prim(checker->arena, SERAPH_TYPE_BOOL);
                        break;

                    case SERAPH_TOK_BIT_NOT:
                        if (!seraph_type_is_integer(operand)) {
                            seraph_checker_error(checker, expr->hdr.loc,
                                                  "bitwise not requires integer operand");
                        }
                        result = operand;
                        break;

                    default:
                        result = operand;
                        break;
                }
            }
            break;

        /* VOID propagation */
        case AST_EXPR_VOID_PROP:
            seraph_checker_check_void_prop(checker, expr);
            {
                Seraph_Type* operand = seraph_checker_check_expr(checker,
                                                                  expr->void_prop.operand);
                /* Result type is the inner type (unwrapped from voidable) */
                if (operand->kind == SERAPH_TYPE_VOIDABLE) {
                    result = operand->voidable.inner;
                } else {
                    result = operand;
                }
            }
            break;

        /* VOID assertion */
        case AST_EXPR_VOID_ASSERT:
            seraph_checker_check_void_assert(checker, expr);
            {
                Seraph_Type* operand = seraph_checker_check_expr(checker,
                                                                  expr->void_assert.operand);
                /* Result type is the inner type (unwrapped from voidable) */
                if (operand->kind == SERAPH_TYPE_VOIDABLE) {
                    result = operand->voidable.inner;
                } else {
                    result = operand;
                }
            }
            break;

        /* Function call */
        case AST_EXPR_CALL:
            {
                Seraph_Type* callee_type = seraph_checker_check_expr(checker,
                                                                      expr->call.callee);

                if (callee_type == NULL || callee_type->kind != SERAPH_TYPE_FN) {
                    seraph_checker_error(checker, expr->hdr.loc,
                                          "cannot call non-function type");
                    result = seraph_type_void(checker->arena);
                } else {
                    /* Check argument count and types */
                    size_t arg_count = 0;
                    for (Seraph_AST_Node* arg = expr->call.args; arg != NULL;
                         arg = arg->hdr.next) {
                        Seraph_Type* arg_type = seraph_checker_check_expr(checker, arg);

                        if (arg_count < callee_type->fn.param_count) {
                            Seraph_Type* param_type = callee_type->fn.params[arg_count];
                            if (!seraph_type_subtype(arg_type, param_type)) {
                                seraph_checker_type_mismatch(checker, arg->hdr.loc,
                                                              param_type, arg_type);
                            }
                        }
                        arg_count++;
                    }

                    if (arg_count != callee_type->fn.param_count) {
                        seraph_checker_error(checker, expr->hdr.loc,
                                              "expected %zu arguments, got %zu",
                                              callee_type->fn.param_count, arg_count);
                    }

                    result = callee_type->fn.ret;
                }
            }
            break;

        /* Field access */
        case AST_EXPR_FIELD:
            {
                Seraph_Type* obj_type = seraph_checker_check_expr(checker,
                                                                   expr->field.object);
                /* Look up field in struct */
                if (obj_type != NULL && obj_type->kind == SERAPH_TYPE_STRUCT &&
                    obj_type->named.decl != NULL) {
                    Seraph_AST_Node* struct_decl = obj_type->named.decl;
                    for (Seraph_AST_Node* field = struct_decl->struct_decl.fields;
                         field != NULL; field = field->hdr.next) {
                        if (field->hdr.kind == AST_FIELD_DEF &&
                            field->field_def.name_len == expr->field.field_len &&
                            memcmp(field->field_def.name, expr->field.field,
                                   expr->field.field_len) == 0) {
                            result = seraph_type_from_ast(checker->types,
                                                           field->field_def.type);
                            break;
                        }
                    }
                }
                if (result == NULL) {
                    seraph_checker_error(checker, expr->hdr.loc,
                                          "unknown field '%.*s'",
                                          (int)expr->field.field_len,
                                          expr->field.field);
                    result = seraph_type_void(checker->arena);
                }
            }
            break;

        /* Index access */
        case AST_EXPR_INDEX:
            {
                Seraph_Type* obj_type = seraph_checker_check_expr(checker,
                                                                   expr->index.object);
                Seraph_Type* idx_type = seraph_checker_check_expr(checker,
                                                                   expr->index.index);

                if (!seraph_type_is_integer(idx_type)) {
                    seraph_checker_error(checker, expr->index.index->hdr.loc,
                                          "index must be integer type");
                }

                /* Get element type */
                if (obj_type != NULL) {
                    if (obj_type->kind == SERAPH_TYPE_ARRAY) {
                        result = seraph_type_voidable(checker->arena, obj_type->array.elem);
                    } else if (obj_type->kind == SERAPH_TYPE_SLICE) {
                        result = seraph_type_voidable(checker->arena, obj_type->slice.elem);
                    } else {
                        seraph_checker_error(checker, expr->hdr.loc,
                                              "cannot index non-array type");
                        result = seraph_type_void(checker->arena);
                    }
                } else {
                    result = seraph_type_void(checker->arena);
                }
            }
            break;

        /* Block */
        case AST_EXPR_BLOCK:
            result = seraph_checker_check_block(checker, expr);
            break;

        /* If expression */
        case AST_EXPR_IF:
            {
                Seraph_Type* cond_type = seraph_checker_check_expr(checker,
                                                                    expr->if_expr.cond);
                if (cond_type == NULL || cond_type->kind != SERAPH_TYPE_BOOL) {
                    seraph_checker_error(checker, expr->if_expr.cond->hdr.loc,
                                          "if condition must be bool");
                }

                Seraph_Type* then_type = seraph_checker_check_block(checker,
                                                                     expr->if_expr.then_branch);

                if (expr->if_expr.else_branch != NULL) {
                    Seraph_Type* else_type = seraph_checker_check_block(checker,
                                                                         expr->if_expr.else_branch);
                    result = seraph_type_join(checker->types, then_type, else_type);
                } else {
                    result = seraph_type_unit(checker->arena);
                }
            }
            break;

        /* Match expression */
        case AST_EXPR_MATCH:
            {
                seraph_checker_check_expr(checker, expr->match.scrutinee);
                Seraph_Type* arm_type = NULL;

                for (Seraph_AST_Node* arm = expr->match.arms; arm != NULL;
                     arm = arm->hdr.next) {
                    if (arm->hdr.kind == AST_MATCH_ARM) {
                        Seraph_Type* body_type = seraph_checker_check_expr(checker,
                                                                            arm->match_arm.body);
                        if (arm_type == NULL) {
                            arm_type = body_type;
                        } else {
                            arm_type = seraph_type_join(checker->types, arm_type, body_type);
                        }
                    }
                }

                result = arm_type ? arm_type : seraph_type_unit(checker->arena);
            }
            break;

        /* Array literal */
        case AST_EXPR_ARRAY:
            {
                Seraph_Type* elem_type = NULL;
                size_t count = 0;

                for (Seraph_AST_Node* elem = expr->array.elements; elem != NULL;
                     elem = elem->hdr.next) {
                    Seraph_Type* t = seraph_checker_check_expr(checker, elem);
                    if (elem_type == NULL) {
                        elem_type = t;
                    } else {
                        elem_type = seraph_type_join(checker->types, elem_type, t);
                    }
                    count++;
                }

                if (elem_type == NULL) {
                    elem_type = seraph_type_never(checker->arena);
                }
                result = seraph_type_array(checker->arena, elem_type, count);
            }
            break;

        default:
            result = seraph_type_void(checker->arena);
            break;
    }

    return result ? result : seraph_type_void(checker->arena);
}

Seraph_Vbit seraph_checker_expect(Seraph_Checker* checker,
                                   Seraph_AST_Node* expr,
                                   Seraph_Type* expected) {
    if (checker == NULL || expr == NULL || expected == NULL) {
        return SERAPH_VBIT_VOID;
    }

    Seraph_Type* actual = seraph_checker_check_expr(checker, expr);
    if (!seraph_type_subtype(actual, expected)) {
        seraph_checker_type_mismatch(checker, expr->hdr.loc, expected, actual);
        return SERAPH_VBIT_FALSE;
    }

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * VOID Operator Checking
 *============================================================================*/

Seraph_Vbit seraph_checker_check_void_prop(Seraph_Checker* checker,
                                            Seraph_AST_Node* node) {
    if (checker == NULL || node == NULL) {
        return SERAPH_VBIT_VOID;
    }
    if (node->hdr.kind != AST_EXPR_VOID_PROP) {
        return SERAPH_VBIT_VOID;
    }

    Seraph_Type* operand_type = seraph_checker_check_expr(checker,
                                                           node->void_prop.operand);

    /* Operand should be VOID-able */
    if (operand_type != NULL && operand_type->kind != SERAPH_TYPE_VOIDABLE) {
        seraph_checker_warning(checker, node->hdr.loc,
                                "VOID propagation on non-VOID-able type has no effect");
    }

    /* Add VOID effect */
    if (checker->effects != NULL) {
        seraph_effect_add(checker->effects, SERAPH_EFFECT_VOID);
    }

    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_checker_check_void_assert(Seraph_Checker* checker,
                                              Seraph_AST_Node* node) {
    if (checker == NULL || node == NULL) {
        return SERAPH_VBIT_VOID;
    }
    if (node->hdr.kind != AST_EXPR_VOID_ASSERT) {
        return SERAPH_VBIT_VOID;
    }

    Seraph_Type* operand_type = seraph_checker_check_expr(checker,
                                                           node->void_assert.operand);

    /* Operand should be VOID-able */
    if (operand_type != NULL && operand_type->kind != SERAPH_TYPE_VOIDABLE) {
        seraph_checker_warning(checker, node->hdr.loc,
                                "VOID assertion on non-VOID-able type has no effect");
    }

    /* Add VOID effect (assertion may panic) */
    if (checker->effects != NULL) {
        seraph_effect_add(checker->effects, SERAPH_EFFECT_VOID);
    }

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * Substrate Block Checking
 *============================================================================*/

Seraph_Vbit seraph_checker_check_substrate_block(Seraph_Checker* checker,
                                                  Seraph_AST_Node* node) {
    if (checker == NULL || node == NULL) {
        return SERAPH_VBIT_VOID;
    }

    switch (node->hdr.kind) {
        case AST_STMT_PERSIST:
            return seraph_checker_check_persist(checker, node);
        case AST_STMT_AETHER:
            return seraph_checker_check_aether(checker, node);
        default:
            return SERAPH_VBIT_VOID;
    }
}

Seraph_Vbit seraph_checker_check_persist(Seraph_Checker* checker,
                                          Seraph_AST_Node* node) {
    if (checker == NULL || node == NULL) {
        return SERAPH_VBIT_VOID;
    }
    if (node->hdr.kind != AST_STMT_PERSIST) {
        return SERAPH_VBIT_VOID;
    }

    /* Save context */
    int old_in_persist = checker->in_persist;
    checker->in_persist = 1;

    /* Add PERSIST effect */
    if (checker->effects != NULL) {
        seraph_effect_add(checker->effects, SERAPH_EFFECT_PERSIST | SERAPH_EFFECT_VOID);
    }

    /* Check body */
    Seraph_Vbit result = SERAPH_VBIT_TRUE;
    if (node->substrate_block.body != NULL) {
        seraph_checker_check_block(checker, node->substrate_block.body);
    }

    /* Restore context */
    checker->in_persist = old_in_persist;

    return result;
}

Seraph_Vbit seraph_checker_check_aether(Seraph_Checker* checker,
                                         Seraph_AST_Node* node) {
    if (checker == NULL || node == NULL) {
        return SERAPH_VBIT_VOID;
    }
    if (node->hdr.kind != AST_STMT_AETHER) {
        return SERAPH_VBIT_VOID;
    }

    /* Save context */
    int old_in_aether = checker->in_aether;
    checker->in_aether = 1;

    /* Add NETWORK effect */
    if (checker->effects != NULL) {
        seraph_effect_add(checker->effects, SERAPH_EFFECT_NETWORK | SERAPH_EFFECT_VOID);
    }

    /* Check body */
    Seraph_Vbit result = SERAPH_VBIT_TRUE;
    if (node->substrate_block.body != NULL) {
        seraph_checker_check_block(checker, node->substrate_block.body);
    }

    /* Restore context */
    checker->in_aether = old_in_aether;

    return result;
}

/*============================================================================
 * Recover Block Checking
 *============================================================================*/

Seraph_Vbit seraph_checker_check_recover(Seraph_Checker* checker,
                                          Seraph_AST_Node* node) {
    if (checker == NULL || node == NULL) {
        return SERAPH_VBIT_VOID;
    }
    if (node->hdr.kind != AST_STMT_RECOVER) {
        return SERAPH_VBIT_VOID;
    }

    /* Save context */
    int old_in_recover = checker->in_recover;
    checker->in_recover = 1;

    /* Check try body */
    Seraph_Type* try_type = NULL;
    if (node->recover_stmt.try_body != NULL) {
        try_type = seraph_checker_check_block(checker, node->recover_stmt.try_body);
    }

    /* Check else body */
    Seraph_Type* else_type = NULL;
    if (node->recover_stmt.else_body != NULL) {
        else_type = seraph_checker_check_block(checker, node->recover_stmt.else_body);
    }

    /* Restore context */
    checker->in_recover = old_in_recover;

    /* Types should be compatible */
    if (try_type != NULL && else_type != NULL) {
        if (!seraph_type_eq(try_type, else_type) &&
            seraph_type_join(checker->types, try_type, else_type) == NULL) {
            seraph_checker_error(checker, node->hdr.loc,
                                  "recover branches have incompatible types");
            return SERAPH_VBIT_FALSE;
        }
    }

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * Statement Checking
 *============================================================================*/

Seraph_Vbit seraph_checker_check_stmt(Seraph_Checker* checker,
                                       Seraph_AST_Node* stmt) {
    if (checker == NULL || stmt == NULL) {
        return SERAPH_VBIT_VOID;
    }

    switch (stmt->hdr.kind) {
        case AST_STMT_EXPR:
            seraph_checker_check_expr(checker, stmt->expr_stmt.expr);
            return SERAPH_VBIT_TRUE;

        case AST_STMT_RETURN:
            if (stmt->return_stmt.expr != NULL) {
                Seraph_Type* ret_type = seraph_checker_check_expr(checker,
                                                                   stmt->return_stmt.expr);
                if (checker->current_ret_type != NULL &&
                    !seraph_type_subtype(ret_type, checker->current_ret_type)) {
                    seraph_checker_type_mismatch(checker, stmt->hdr.loc,
                                                  checker->current_ret_type, ret_type);
                    return SERAPH_VBIT_FALSE;
                }
            }
            return SERAPH_VBIT_TRUE;

        case AST_STMT_BREAK:
        case AST_STMT_CONTINUE:
            if (!checker->in_loop) {
                seraph_checker_error(checker, stmt->hdr.loc,
                                      "%s outside of loop",
                                      stmt->hdr.kind == AST_STMT_BREAK ? "break" : "continue");
                return SERAPH_VBIT_FALSE;
            }
            return SERAPH_VBIT_TRUE;

        case AST_STMT_WHILE:
            {
                Seraph_Type* cond_type = seraph_checker_check_expr(checker,
                                                                    stmt->while_stmt.cond);
                if (cond_type == NULL || cond_type->kind != SERAPH_TYPE_BOOL) {
                    seraph_checker_error(checker, stmt->while_stmt.cond->hdr.loc,
                                          "while condition must be bool");
                }

                int old_in_loop = checker->in_loop;
                checker->in_loop = 1;
                seraph_checker_check_block(checker, stmt->while_stmt.body);
                checker->in_loop = old_in_loop;
            }
            return SERAPH_VBIT_TRUE;

        case AST_STMT_FOR:
            {
                /* Check iterable */
                seraph_checker_check_expr(checker, stmt->for_stmt.iterable);

                /* Push scope for loop variable */
                if (checker->types != NULL) {
                    seraph_type_push_scope(checker->types);
                    /* Register loop variable (type inferred from iterable) */
                    seraph_type_define(checker->types,
                                       stmt->for_stmt.var, stmt->for_stmt.var_len,
                                       seraph_type_prim(checker->arena, SERAPH_TYPE_I64),
                                       stmt, 0);
                }

                int old_in_loop = checker->in_loop;
                checker->in_loop = 1;
                seraph_checker_check_block(checker, stmt->for_stmt.body);
                checker->in_loop = old_in_loop;

                if (checker->types != NULL) {
                    seraph_type_pop_scope(checker->types);
                }
            }
            return SERAPH_VBIT_TRUE;

        case AST_DECL_LET:
        case AST_DECL_CONST:
            {
                Seraph_Type* init_type = NULL;
                if (stmt->let_decl.init != NULL) {
                    init_type = seraph_checker_check_expr(checker, stmt->let_decl.init);
                }

                Seraph_Type* decl_type = init_type;
                if (stmt->let_decl.type != NULL) {
                    decl_type = seraph_type_from_ast(checker->types, stmt->let_decl.type);
                    if (init_type != NULL && !seraph_type_subtype(init_type, decl_type)) {
                        seraph_checker_type_mismatch(checker, stmt->hdr.loc,
                                                      decl_type, init_type);
                        return SERAPH_VBIT_FALSE;
                    }
                }

                if (decl_type != NULL && checker->types != NULL) {
                    seraph_type_define(checker->types,
                                       stmt->let_decl.name, stmt->let_decl.name_len,
                                       decl_type, stmt, stmt->let_decl.is_mut);
                }
            }
            return SERAPH_VBIT_TRUE;

        case AST_STMT_PERSIST:
            return seraph_checker_check_persist(checker, stmt);

        case AST_STMT_AETHER:
            return seraph_checker_check_aether(checker, stmt);

        case AST_STMT_RECOVER:
            return seraph_checker_check_recover(checker, stmt);

        default:
            return SERAPH_VBIT_TRUE;
    }
}

Seraph_Type* seraph_checker_check_block(Seraph_Checker* checker,
                                         Seraph_AST_Node* block) {
    if (checker == NULL || block == NULL) {
        return seraph_type_unit(checker ? checker->arena : NULL);
    }
    if (block->hdr.kind != AST_EXPR_BLOCK) {
        return seraph_type_unit(checker->arena);
    }

    /* Push scope */
    if (checker->types != NULL) {
        seraph_type_push_scope(checker->types);
    }

    /* Check all statements */
    for (Seraph_AST_Node* stmt = block->block.stmts; stmt != NULL;
         stmt = stmt->hdr.next) {
        seraph_checker_check_stmt(checker, stmt);
    }

    /* Get result type from final expression */
    Seraph_Type* result = seraph_type_unit(checker->arena);
    if (block->block.expr != NULL) {
        result = seraph_checker_check_expr(checker, block->block.expr);
    }

    /* Pop scope */
    if (checker->types != NULL) {
        seraph_type_pop_scope(checker->types);
    }

    return result;
}

/*============================================================================
 * Diagnostics
 *============================================================================*/

void seraph_checker_error(Seraph_Checker* checker,
                           Seraph_Source_Loc loc,
                           const char* format, ...) {
    if (checker == NULL) return;

    checker->error_count++;

    Seraph_Checker_Diag* diag = (Seraph_Checker_Diag*)seraph_arena_alloc(
        checker->arena, sizeof(Seraph_Checker_Diag), _Alignof(Seraph_Checker_Diag)
    );
    if (diag == NULL) return;

    diag->loc = loc;
    diag->is_error = 1;
    diag->next = checker->diagnostics;
    checker->diagnostics = diag;

    va_list args;
    va_start(args, format);
    int len = vsnprintf(NULL, 0, format, args);
    va_end(args);

    char* msg = (char*)seraph_arena_alloc(checker->arena, len + 1, 1);
    if (msg != NULL) {
        va_start(args, format);
        vsnprintf(msg, len + 1, format, args);
        va_end(args);
        diag->message = msg;
    } else {
        diag->message = format;
    }
}

void seraph_checker_warning(Seraph_Checker* checker,
                             Seraph_Source_Loc loc,
                             const char* format, ...) {
    if (checker == NULL) return;

    checker->warning_count++;

    Seraph_Checker_Diag* diag = (Seraph_Checker_Diag*)seraph_arena_alloc(
        checker->arena, sizeof(Seraph_Checker_Diag), _Alignof(Seraph_Checker_Diag)
    );
    if (diag == NULL) return;

    diag->loc = loc;
    diag->is_error = 0;
    diag->next = checker->diagnostics;
    checker->diagnostics = diag;

    va_list args;
    va_start(args, format);
    int len = vsnprintf(NULL, 0, format, args);
    va_end(args);

    char* msg = (char*)seraph_arena_alloc(checker->arena, len + 1, 1);
    if (msg != NULL) {
        va_start(args, format);
        vsnprintf(msg, len + 1, format, args);
        va_end(args);
        diag->message = msg;
    } else {
        diag->message = format;
    }
}

void seraph_checker_type_mismatch(Seraph_Checker* checker,
                                   Seraph_Source_Loc loc,
                                   Seraph_Type* expected,
                                   Seraph_Type* actual) {
    char expected_str[128] = {0};
    char actual_str[128] = {0};

    if (expected != NULL) {
        seraph_type_print(expected, expected_str, sizeof(expected_str));
    } else {
        snprintf(expected_str, sizeof(expected_str), "<unknown>");
    }

    if (actual != NULL) {
        seraph_type_print(actual, actual_str, sizeof(actual_str));
    } else {
        snprintf(actual_str, sizeof(actual_str), "<unknown>");
    }

    seraph_checker_error(checker, loc, "type mismatch: expected %s, got %s",
                          expected_str, actual_str);
}

void seraph_checker_print_diagnostics(const Seraph_Checker* checker) {
    if (checker == NULL) return;

    for (Seraph_Checker_Diag* d = checker->diagnostics; d != NULL; d = d->next) {
        fprintf(stderr, "%s:%u:%u: %s: %s\n",
                d->loc.filename ? d->loc.filename : "<unknown>",
                d->loc.line,
                d->loc.column,
                d->is_error ? "error" : "warning",
                d->message);
    }
}
