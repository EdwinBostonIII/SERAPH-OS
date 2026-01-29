/**
 * @file effects.c
 * @brief Seraphim Compiler - Effect System Implementation
 *
 * MC26: Seraphim Language Effect System
 *
 * Implements compile-time effect tracking and verification.
 */

#include "seraph/seraphim/effects.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/*============================================================================
 * Effect Operations
 *============================================================================*/

const char* seraph_effect_name(Seraph_Effect_Flags effect) {
    switch (effect) {
        case SERAPH_EFFECT_NONE:    return "pure";
        case SERAPH_EFFECT_VOID:    return "VOID";
        case SERAPH_EFFECT_PERSIST: return "PERSIST";
        case SERAPH_EFFECT_NETWORK: return "NETWORK";
        case SERAPH_EFFECT_TIMER:   return "TIMER";
        case SERAPH_EFFECT_IO:      return "IO";
        case SERAPH_EFFECT_ALL:     return "ALL";
        default:                    return "effects";
    }
}

size_t seraph_effect_print(Seraph_Effect_Flags set, char* buf, size_t buf_size) {
    if (buf == NULL || buf_size == 0) return 0;

    if (set == SERAPH_EFFECT_NONE) {
        return snprintf(buf, buf_size, "[pure]");
    }

    if (set == SERAPH_EFFECT_ALL) {
        return snprintf(buf, buf_size, "[unsafe]");
    }

    size_t written = snprintf(buf, buf_size, "effects(");
    int first = 1;

    if (set & SERAPH_EFFECT_VOID) {
        written += snprintf(buf + written, buf_size - written, "%sVOID",
                           first ? "" : ", ");
        first = 0;
    }
    if (set & SERAPH_EFFECT_PERSIST) {
        written += snprintf(buf + written, buf_size - written, "%sPERSIST",
                           first ? "" : ", ");
        first = 0;
    }
    if (set & SERAPH_EFFECT_NETWORK) {
        written += snprintf(buf + written, buf_size - written, "%sNETWORK",
                           first ? "" : ", ");
        first = 0;
    }
    if (set & SERAPH_EFFECT_TIMER) {
        written += snprintf(buf + written, buf_size - written, "%sTIMER",
                           first ? "" : ", ");
        first = 0;
    }
    if (set & SERAPH_EFFECT_IO) {
        written += snprintf(buf + written, buf_size - written, "%sIO",
                           first ? "" : ", ");
        first = 0;
    }

    written += snprintf(buf + written, buf_size - written, ")");
    return written;
}

/*============================================================================
 * Context Management
 *============================================================================*/

Seraph_Vbit seraph_effect_context_init(Seraph_Effect_Context* ctx,
                                        Seraph_Arena* arena,
                                        Seraph_Type_Context* type_ctx) {
    if (ctx == NULL || arena == NULL) return SERAPH_VBIT_VOID;

    memset(ctx, 0, sizeof(Seraph_Effect_Context));
    ctx->arena = arena;
    ctx->type_ctx = type_ctx;
    ctx->allowed = SERAPH_EFFECT_ALL;  /* Global scope allows all effects */
    ctx->inferred = SERAPH_EFFECT_NONE;
    ctx->fn_depth = 0;

    return SERAPH_VBIT_TRUE;
}

void seraph_effect_enter_fn(Seraph_Effect_Context* ctx,
                             Seraph_Effect_Flags declared) {
    if (ctx == NULL) return;
    if (ctx->fn_depth >= 32) return;  /* Stack overflow protection */

    /* Push current state */
    ctx->fn_stack[ctx->fn_depth].allowed = ctx->allowed;
    ctx->fn_stack[ctx->fn_depth].inferred = ctx->inferred;
    ctx->fn_depth++;

    /* Enter new function context */
    ctx->allowed = declared;
    ctx->inferred = SERAPH_EFFECT_NONE;
}

Seraph_Vbit seraph_effect_exit_fn(Seraph_Effect_Context* ctx) {
    if (ctx == NULL || ctx->fn_depth == 0) return SERAPH_VBIT_VOID;

    /* Check for violations */
    Seraph_Vbit result = seraph_effect_subset(ctx->inferred, ctx->allowed)
                         ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;

    /* Pop state */
    ctx->fn_depth--;
    ctx->allowed = ctx->fn_stack[ctx->fn_depth].allowed;
    ctx->inferred = ctx->fn_stack[ctx->fn_depth].inferred;

    return result;
}

void seraph_effect_add(Seraph_Effect_Context* ctx, Seraph_Effect_Flags effect) {
    if (ctx == NULL) return;
    ctx->inferred = seraph_effect_union(ctx->inferred, effect);
}

Seraph_Vbit seraph_effect_check(Seraph_Effect_Context* ctx,
                                 Seraph_Effect_Flags effect) {
    if (ctx == NULL) return SERAPH_VBIT_VOID;
    return seraph_effect_subset(effect, ctx->allowed)
           ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

/*============================================================================
 * Intrinsic Effect Inference
 *============================================================================*/

Seraph_Effect_Flags seraph_effect_for_builtin(const char* name, size_t name_len) {
    if (name == NULL || name_len == 0) return SERAPH_EFFECT_NONE;

    /* Atlas operations → PERSIST */
    if (name_len >= 5 && memcmp(name, "atlas", 5) == 0) {
        return SERAPH_EFFECT_PERSIST | SERAPH_EFFECT_VOID;
    }

    /* Aether operations → NETWORK */
    if (name_len >= 6 && memcmp(name, "aether", 6) == 0) {
        return SERAPH_EFFECT_NETWORK | SERAPH_EFFECT_VOID;
    }

    /* Timer operations → TIMER */
    if (name_len >= 5 && memcmp(name, "timer", 5) == 0) {
        return SERAPH_EFFECT_TIMER;
    }
    if (name_len >= 6 && memcmp(name, "chronon", 7) == 0) {
        return SERAPH_EFFECT_TIMER;
    }

    /* I/O operations → IO */
    if (name_len >= 5 && memcmp(name, "print", 5) == 0) {
        return SERAPH_EFFECT_IO;
    }
    if (name_len >= 4 && memcmp(name, "read", 4) == 0) {
        return SERAPH_EFFECT_IO | SERAPH_EFFECT_VOID;
    }
    if (name_len >= 5 && memcmp(name, "write", 5) == 0) {
        return SERAPH_EFFECT_IO | SERAPH_EFFECT_VOID;
    }

    /* Default: no inherent effects */
    return SERAPH_EFFECT_NONE;
}

Seraph_Effect_Flags seraph_effect_for_operator(Seraph_Token_Type op) {
    switch (op) {
        /* Division and modulo may produce VOID (divide by zero) */
        case SERAPH_TOK_SLASH:
        case SERAPH_TOK_PERCENT:
            return SERAPH_EFFECT_VOID;

        /* Index access may produce VOID (out of bounds) */
        case SERAPH_TOK_LBRACKET:
            return SERAPH_EFFECT_VOID;

        /* VOID operators inherently have VOID effect */
        case SERAPH_TOK_VOID_PROP:      /* ?? */
        case SERAPH_TOK_VOID_ASSERT:    /* !! */
            return SERAPH_EFFECT_VOID;

        default:
            return SERAPH_EFFECT_NONE;
    }
}

/*============================================================================
 * Effect Inference
 *============================================================================*/

Seraph_Effect_Flags seraph_effect_infer_expr(Seraph_Effect_Context* ctx,
                                              Seraph_AST_Node* expr) {
    if (ctx == NULL || expr == NULL) return SERAPH_EFFECT_NONE;
    if (seraph_ast_is_void(expr)) return SERAPH_EFFECT_NONE;

    Seraph_Effect_Flags effects = SERAPH_EFFECT_NONE;

    switch (expr->hdr.kind) {
        /* Literals are pure */
        case AST_EXPR_INT_LIT:
        case AST_EXPR_FLOAT_LIT:
        case AST_EXPR_BOOL_LIT:
        case AST_EXPR_CHAR_LIT:
        case AST_EXPR_STRING_LIT:
            break;

        /* VOID literal has VOID effect */
        case AST_EXPR_VOID_LIT:
            effects = SERAPH_EFFECT_VOID;
            break;

        /* Identifiers are pure (reading a variable) */
        case AST_EXPR_IDENT:
            break;

        /* Binary operators - infer from operands and operator */
        case AST_EXPR_BINARY:
            effects = seraph_effect_infer_expr(ctx, expr->binary.left);
            effects = seraph_effect_union(effects,
                          seraph_effect_infer_expr(ctx, expr->binary.right));
            effects = seraph_effect_union(effects,
                          seraph_effect_for_operator(expr->binary.op));
            break;

        /* Unary operators */
        case AST_EXPR_UNARY:
            effects = seraph_effect_infer_expr(ctx, expr->unary.operand);
            effects = seraph_effect_union(effects,
                          seraph_effect_for_operator(expr->unary.op));
            break;

        /* VOID propagation has VOID effect */
        case AST_EXPR_VOID_PROP:
            effects = seraph_effect_infer_expr(ctx, expr->void_prop.operand);
            effects = seraph_effect_union(effects, SERAPH_EFFECT_VOID);
            break;

        /* VOID assertion has VOID effect (may panic) */
        case AST_EXPR_VOID_ASSERT:
            effects = seraph_effect_infer_expr(ctx, expr->void_assert.operand);
            effects = seraph_effect_union(effects, SERAPH_EFFECT_VOID);
            break;

        /* Function call - get effects from callee type */
        case AST_EXPR_CALL:
            {
                /* Infer callee effects */
                effects = seraph_effect_infer_expr(ctx, expr->call.callee);

                /* Get callee's declared effects from its type */
                if (ctx->type_ctx) {
                    Seraph_Type* callee_type = seraph_type_check_expr(
                        ctx->type_ctx, expr->call.callee);
                    effects = seraph_effect_union(effects,
                                  seraph_effect_from_fn_type(callee_type));
                }

                /* Infer argument effects */
                for (Seraph_AST_Node* arg = expr->call.args; arg != NULL;
                     arg = arg->hdr.next) {
                    effects = seraph_effect_union(effects,
                                  seraph_effect_infer_expr(ctx, arg));
                }
            }
            break;

        /* Method call - same structure as function call */
        case AST_EXPR_METHOD_CALL:
            {
                effects = seraph_effect_infer_expr(ctx, expr->call.callee);
                for (Seraph_AST_Node* arg = expr->call.args; arg != NULL;
                     arg = arg->hdr.next) {
                    effects = seraph_effect_union(effects,
                                  seraph_effect_infer_expr(ctx, arg));
                }
                /* TODO: look up method type for declared effects */
            }
            break;

        /* Field access is pure */
        case AST_EXPR_FIELD:
            effects = seraph_effect_infer_expr(ctx, expr->field.object);
            break;

        /* Index access may be VOID (out of bounds) */
        case AST_EXPR_INDEX:
            effects = seraph_effect_infer_expr(ctx, expr->index.object);
            effects = seraph_effect_union(effects,
                          seraph_effect_infer_expr(ctx, expr->index.index));
            effects = seraph_effect_union(effects, SERAPH_EFFECT_VOID);
            break;

        /* Block - infer from contents */
        case AST_EXPR_BLOCK:
            effects = seraph_effect_infer_block(ctx, expr);
            break;

        /* If - infer from all branches */
        case AST_EXPR_IF:
            effects = seraph_effect_infer_expr(ctx, expr->if_expr.cond);
            effects = seraph_effect_union(effects,
                          seraph_effect_infer_block(ctx, expr->if_expr.then_branch));
            if (expr->if_expr.else_branch) {
                effects = seraph_effect_union(effects,
                              seraph_effect_infer_block(ctx, expr->if_expr.else_branch));
            }
            break;

        /* Match - infer from all arms */
        case AST_EXPR_MATCH:
            effects = seraph_effect_infer_expr(ctx, expr->match.scrutinee);
            for (Seraph_AST_Node* arm = expr->match.arms; arm != NULL;
                 arm = arm->hdr.next) {
                effects = seraph_effect_union(effects,
                              seraph_effect_infer_block(ctx, arm->match_arm.body));
            }
            break;

        /* Range expression is pure */
        case AST_EXPR_RANGE:
            effects = seraph_effect_infer_expr(ctx, expr->range.start);
            if (expr->range.end) {
                effects = seraph_effect_union(effects,
                              seraph_effect_infer_expr(ctx, expr->range.end));
            }
            break;

        /* Pipe is handled as part of binary expressions */
        /* Assignment is handled as part of binary expressions */

        default:
            break;
    }

    /* Add to context and check for violations */
    seraph_effect_add(ctx, effects);

    if (!seraph_vbit_is_true(seraph_effect_check(ctx, effects))) {
        seraph_effect_violation(ctx, expr->hdr.loc, effects, ctx->allowed);
    }

    return effects;
}

Seraph_Effect_Flags seraph_effect_infer_stmt(Seraph_Effect_Context* ctx,
                                              Seraph_AST_Node* stmt) {
    if (ctx == NULL || stmt == NULL) return SERAPH_EFFECT_NONE;

    Seraph_Effect_Flags effects = SERAPH_EFFECT_NONE;

    switch (stmt->hdr.kind) {
        case AST_STMT_EXPR:
            effects = seraph_effect_infer_expr(ctx, stmt->expr_stmt.expr);
            break;

        case AST_STMT_RETURN:
            if (stmt->return_stmt.expr) {
                effects = seraph_effect_infer_expr(ctx, stmt->return_stmt.expr);
            }
            break;

        case AST_STMT_WHILE:
            effects = seraph_effect_infer_expr(ctx, stmt->while_stmt.cond);
            effects = seraph_effect_union(effects,
                          seraph_effect_infer_block(ctx, stmt->while_stmt.body));
            break;

        case AST_STMT_FOR:
            effects = seraph_effect_infer_expr(ctx, stmt->for_stmt.iterable);
            effects = seraph_effect_union(effects,
                          seraph_effect_infer_block(ctx, stmt->for_stmt.body));
            break;

        case AST_DECL_LET:
        case AST_DECL_CONST:
            if (stmt->let_decl.init) {
                effects = seraph_effect_infer_expr(ctx, stmt->let_decl.init);
            }
            break;

        default:
            break;
    }

    return effects;
}

Seraph_Effect_Flags seraph_effect_infer_block(Seraph_Effect_Context* ctx,
                                               Seraph_AST_Node* block) {
    if (ctx == NULL || block == NULL) return SERAPH_EFFECT_NONE;
    if (block->hdr.kind != AST_EXPR_BLOCK) return SERAPH_EFFECT_NONE;

    Seraph_Effect_Flags effects = SERAPH_EFFECT_NONE;

    /* Infer effects of all statements */
    for (Seraph_AST_Node* stmt = block->block.stmts; stmt != NULL;
         stmt = stmt->hdr.next) {
        effects = seraph_effect_union(effects,
                      seraph_effect_infer_stmt(ctx, stmt));
    }

    /* Infer effects of result expression if present */
    if (block->block.expr) {
        effects = seraph_effect_union(effects,
                      seraph_effect_infer_expr(ctx, block->block.expr));
    }

    return effects;
}

/*============================================================================
 * Effect Checking
 *============================================================================*/

/**
 * @brief Convert effect AST node to effect flags
 */
static Seraph_Effect_Flags effects_from_ast(Seraph_AST_Node* effects_node,
                                            int is_pure) {
    if (is_pure) {
        return SERAPH_EFFECT_NONE;
    }

    if (effects_node == NULL) {
        /* No effect annotation - allow all effects */
        return SERAPH_EFFECT_ALL;
    }

    /* The effect_list node contains flags directly in effect_list.effects */
    if (effects_node->hdr.kind == AST_EFFECT_LIST) {
        return (Seraph_Effect_Flags)effects_node->effect_list.effects;
    }

    return SERAPH_EFFECT_ALL;
}

Seraph_Vbit seraph_effect_check_fn(Seraph_Effect_Context* ctx,
                                    Seraph_AST_Node* fn_decl) {
    if (ctx == NULL || fn_decl == NULL) return SERAPH_VBIT_VOID;
    if (fn_decl->hdr.kind != AST_DECL_FN) return SERAPH_VBIT_VOID;

    /* Get declared effects from function */
    Seraph_Effect_Flags declared = effects_from_ast(
        fn_decl->fn_decl.effects, fn_decl->fn_decl.is_pure);

    /* Enter function scope */
    seraph_effect_enter_fn(ctx, declared);

    /* Infer effects of function body */
    if (fn_decl->fn_decl.body) {
        seraph_effect_infer_block(ctx, fn_decl->fn_decl.body);
    }

    /* Exit and check for violations */
    Seraph_Vbit result = seraph_effect_exit_fn(ctx);

    if (!seraph_vbit_is_true(result)) {
        /* Report violation at function declaration */
        Seraph_Effect_Flags inferred = ctx->fn_depth > 0
            ? ctx->fn_stack[ctx->fn_depth].inferred
            : ctx->inferred;

        char declared_str[64], inferred_str[64];
        seraph_effect_print(declared, declared_str, sizeof(declared_str));
        seraph_effect_print(inferred, inferred_str, sizeof(inferred_str));

        seraph_effect_error(ctx, fn_decl->hdr.loc,
                            "function '%.*s' declares %s but has %s",
                            (int)fn_decl->fn_decl.name_len, fn_decl->fn_decl.name,
                            declared_str, inferred_str);
    }

    return result;
}

Seraph_Vbit seraph_effect_check_module(Seraph_Effect_Context* ctx,
                                        Seraph_AST_Node* module) {
    if (ctx == NULL || module == NULL) return SERAPH_VBIT_VOID;
    if (module->hdr.kind != AST_MODULE) return SERAPH_VBIT_VOID;

    Seraph_Vbit result = SERAPH_VBIT_TRUE;

    /* Check all function declarations */
    for (Seraph_AST_Node* decl = module->module.decls; decl != NULL;
         decl = decl->hdr.next) {
        if (decl->hdr.kind == AST_DECL_FN) {
            Seraph_Vbit fn_result = seraph_effect_check_fn(ctx, decl);
            if (!seraph_vbit_is_true(fn_result)) {
                result = SERAPH_VBIT_FALSE;
            }
        }
    }

    return result;
}

/*============================================================================
 * Diagnostics
 *============================================================================*/

void seraph_effect_violation(Seraph_Effect_Context* ctx,
                              Seraph_Source_Loc loc,
                              Seraph_Effect_Flags required,
                              Seraph_Effect_Flags allowed) {
    if (ctx == NULL) return;

    ctx->error_count++;

    Seraph_Effect_Diag* diag = (Seraph_Effect_Diag*)seraph_arena_alloc(
        ctx->arena, sizeof(Seraph_Effect_Diag), _Alignof(Seraph_Effect_Diag)
    );
    if (diag == NULL) return;

    diag->loc = loc;
    diag->required = required;
    diag->allowed = allowed;
    diag->next = ctx->diagnostics;
    ctx->diagnostics = diag;

    /* Build violation message */
    char req_str[64], allow_str[64];
    seraph_effect_print(required, req_str, sizeof(req_str));
    seraph_effect_print(allowed, allow_str, sizeof(allow_str));

    int len = snprintf(NULL, 0, "effect violation: %s required but only %s allowed",
                       req_str, allow_str);
    char* msg = (char*)seraph_arena_alloc(ctx->arena, len + 1, 1);
    if (msg != NULL) {
        snprintf(msg, len + 1, "effect violation: %s required but only %s allowed",
                 req_str, allow_str);
        diag->message = msg;
    } else {
        diag->message = "effect violation";
    }
}

void seraph_effect_error(Seraph_Effect_Context* ctx,
                          Seraph_Source_Loc loc,
                          const char* format, ...) {
    if (ctx == NULL) return;

    ctx->error_count++;

    Seraph_Effect_Diag* diag = (Seraph_Effect_Diag*)seraph_arena_alloc(
        ctx->arena, sizeof(Seraph_Effect_Diag), _Alignof(Seraph_Effect_Diag)
    );
    if (diag == NULL) return;

    diag->loc = loc;
    diag->required = SERAPH_EFFECT_NONE;
    diag->allowed = SERAPH_EFFECT_NONE;
    diag->next = ctx->diagnostics;
    ctx->diagnostics = diag;

    va_list args;
    va_start(args, format);
    int len = vsnprintf(NULL, 0, format, args);
    va_end(args);

    char* msg = (char*)seraph_arena_alloc(ctx->arena, len + 1, 1);
    if (msg != NULL) {
        va_start(args, format);
        vsnprintf(msg, len + 1, format, args);
        va_end(args);
        diag->message = msg;
    } else {
        diag->message = format;
    }
}

void seraph_effect_print_diagnostics(const Seraph_Effect_Context* ctx) {
    if (ctx == NULL) return;

    for (Seraph_Effect_Diag* d = ctx->diagnostics; d != NULL; d = d->next) {
        fprintf(stderr, "%s:%u:%u: effect error: %s\n",
                d->loc.filename ? d->loc.filename : "<unknown>",
                d->loc.line,
                d->loc.column,
                d->message);
    }
}
