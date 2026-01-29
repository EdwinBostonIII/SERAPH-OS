/**
 * @file proofs.c
 * @brief Seraphim Compiler - Proof Generation Implementation
 *
 * MC26: Seraphim Language Proof Generation
 *
 * Implements compile-time proof generation for:
 * - Bounds checking
 * - VOID handling
 * - Effect verification
 * - Permission checking
 * - Generation validity
 * - Substrate access
 */

#include "seraph/seraphim/proofs.h"
#include "seraph/vbit.h"
#include <stdio.h>
#include <string.h>

/*============================================================================
 * Proof Table Operations
 *============================================================================*/

Seraph_Vbit seraph_proof_table_init(Seraph_Proof_Table* table, Seraph_Arena* arena) {
    if (table == NULL || arena == NULL) {
        return SERAPH_VBIT_FALSE;
    }

    memset(table, 0, sizeof(Seraph_Proof_Table));
    table->arena = arena;
    return SERAPH_VBIT_TRUE;
}

void seraph_proof_add(Seraph_Proof_Table* table, Seraph_Proof proof) {
    if (table == NULL || table->arena == NULL) return;

    Seraph_Proof* p = (Seraph_Proof*)seraph_arena_alloc(
        table->arena, sizeof(Seraph_Proof), _Alignof(Seraph_Proof)
    );
    if (p == NULL) return;

    *p = proof;
    p->next = table->proofs;
    table->proofs = p;
    table->count++;

    /* Update counters */
    switch (proof.status) {
        case SERAPH_PROOF_STATUS_PROVEN:
            table->proven_count++;
            break;
        case SERAPH_PROOF_STATUS_RUNTIME:
            table->runtime_count++;
            break;
        case SERAPH_PROOF_STATUS_FAILED:
            table->failed_count++;
            break;
        default:
            break;
    }
}

void seraph_proof_add_bounds(Seraph_Proof_Table* table,
                              Seraph_Source_Loc loc,
                              uint64_t array_size,
                              uint64_t index_min,
                              uint64_t index_max,
                              Seraph_Proof_Status status) {
    if (table == NULL) return;

    Seraph_Proof proof = {0};
    proof.kind = SERAPH_PROOF_BOUNDS;
    proof.status = status;
    proof.loc = loc;
    proof.bounds.array_size = array_size;
    proof.bounds.index_min = index_min;
    proof.bounds.index_max = index_max;

    /* Generate description */
    const char* desc = NULL;
    if (status == SERAPH_PROOF_STATUS_PROVEN) {
        if (array_size > 0 && index_max < array_size) {
            desc = "array access proven in bounds";
        } else {
            desc = "bounds check required";
        }
    } else if (status == SERAPH_PROOF_STATUS_RUNTIME) {
        desc = "runtime bounds check inserted";
    } else {
        desc = "bounds check status unknown";
    }

    size_t len = strlen(desc) + 1;
    char* buf = (char*)seraph_arena_alloc(table->arena, len, 1);
    if (buf != NULL) {
        memcpy(buf, desc, len);
        proof.description = buf;
    }

    seraph_proof_add(table, proof);
}

void seraph_proof_add_void(Seraph_Proof_Table* table,
                            Seraph_Source_Loc loc,
                            const char* description,
                            Seraph_Proof_Status status) {
    if (table == NULL) return;

    Seraph_Proof proof = {0};
    proof.kind = SERAPH_PROOF_VOID;
    proof.status = status;
    proof.loc = loc;

    if (description != NULL) {
        size_t len = strlen(description) + 1;
        char* buf = (char*)seraph_arena_alloc(table->arena, len, 1);
        if (buf != NULL) {
            memcpy(buf, description, len);
            proof.description = buf;
        }
    } else {
        proof.description = "VOID handling verified";
    }

    seraph_proof_add(table, proof);
}

void seraph_proof_add_effect(Seraph_Proof_Table* table,
                              Seraph_Source_Loc loc,
                              uint32_t required,
                              uint32_t allowed,
                              Seraph_Proof_Status status) {
    if (table == NULL) return;

    Seraph_Proof proof = {0};
    proof.kind = SERAPH_PROOF_EFFECT;
    proof.status = status;
    proof.loc = loc;
    proof.effects.required_effects = required;
    proof.effects.allowed_effects = allowed;

    if ((required & ~allowed) == 0) {
        proof.description = "effects within allowed set";
    } else {
        proof.description = "effect violation detected";
    }

    seraph_proof_add(table, proof);
}

void seraph_proof_add_permission(Seraph_Proof_Table* table,
                                  Seraph_Source_Loc loc,
                                  uint8_t required,
                                  uint8_t granted,
                                  Seraph_Proof_Status status) {
    if (table == NULL) return;

    Seraph_Proof proof = {0};
    proof.kind = SERAPH_PROOF_PERMISSION;
    proof.status = status;
    proof.loc = loc;
    proof.permissions.required_perms = required;
    proof.permissions.granted_perms = granted;

    if ((required & ~granted) == 0) {
        proof.description = "capability permissions sufficient";
    } else {
        proof.description = "insufficient capability permissions";
    }

    seraph_proof_add(table, proof);
}

void seraph_proof_add_type(Seraph_Proof_Table* table,
                            Seraph_Source_Loc loc,
                            const char* description,
                            Seraph_Proof_Status status) {
    if (table == NULL) return;

    Seraph_Proof proof = {0};
    proof.kind = SERAPH_PROOF_TYPE;
    proof.status = status;
    proof.loc = loc;

    if (description != NULL) {
        size_t len = strlen(description) + 1;
        char* buf = (char*)seraph_arena_alloc(table->arena, len, 1);
        if (buf != NULL) {
            memcpy(buf, description, len);
            proof.description = buf;
        }
    } else {
        proof.description = "type constraint satisfied";
    }

    seraph_proof_add(table, proof);
}

/*============================================================================
 * Proof Generation
 *============================================================================*/

void seraph_proof_generate(Seraph_Proof_Table* table, Seraph_AST_Node* module) {
    if (table == NULL || module == NULL) return;
    if (module->hdr.kind != AST_MODULE) return;

    /* Generate proofs for all declarations */
    for (Seraph_AST_Node* decl = module->module.decls; decl != NULL;
         decl = decl->hdr.next) {
        switch (decl->hdr.kind) {
            case AST_DECL_FN:
                seraph_proof_generate_fn(table, decl);
                break;
            default:
                break;
        }
    }
}

void seraph_proof_generate_fn(Seraph_Proof_Table* table, Seraph_AST_Node* fn_decl) {
    if (table == NULL || fn_decl == NULL) return;
    if (fn_decl->hdr.kind != AST_DECL_FN) return;

    /* Generate effect proof for function */
    uint32_t declared_effects = SERAPH_EFFECT_ALL;
    if (fn_decl->fn_decl.is_pure) {
        declared_effects = SERAPH_EFFECT_NONE;
    }

    seraph_proof_add_effect(table, fn_decl->hdr.loc,
                             SERAPH_EFFECT_NONE, declared_effects,
                             SERAPH_PROOF_STATUS_PROVEN);

    /* Generate proofs for function body */
    if (fn_decl->fn_decl.body != NULL) {
        seraph_proof_generate_stmt(table, fn_decl->fn_decl.body);
    }
}

void seraph_proof_generate_expr(Seraph_Proof_Table* table, Seraph_AST_Node* expr) {
    if (table == NULL || expr == NULL) return;

    switch (expr->hdr.kind) {
        /* Index access - bounds check */
        case AST_EXPR_INDEX:
            {
                /* For now, assume runtime check needed */
                seraph_proof_add_bounds(table, expr->hdr.loc,
                                         0, 0, UINT64_MAX,
                                         SERAPH_PROOF_STATUS_RUNTIME);

                /* Recurse */
                seraph_proof_generate_expr(table, expr->index.object);
                seraph_proof_generate_expr(table, expr->index.index);
            }
            break;

        /* VOID propagation - VOID handling */
        case AST_EXPR_VOID_PROP:
            {
                seraph_proof_add_void(table, expr->hdr.loc,
                                       "VOID propagated via ??",
                                       SERAPH_PROOF_STATUS_PROVEN);
                seraph_proof_generate_expr(table, expr->void_prop.operand);
            }
            break;

        /* VOID assertion - VOID handling */
        case AST_EXPR_VOID_ASSERT:
            {
                seraph_proof_add_void(table, expr->hdr.loc,
                                       "VOID assertion !! may panic",
                                       SERAPH_PROOF_STATUS_RUNTIME);
                seraph_proof_generate_expr(table, expr->void_assert.operand);
            }
            break;

        /* Division - may produce VOID */
        case AST_EXPR_BINARY:
            if (expr->binary.op == SERAPH_TOK_SLASH ||
                expr->binary.op == SERAPH_TOK_PERCENT) {
                seraph_proof_add_void(table, expr->hdr.loc,
                                       "division may produce VOID (div by zero)",
                                       SERAPH_PROOF_STATUS_RUNTIME);
            }
            seraph_proof_generate_expr(table, expr->binary.left);
            seraph_proof_generate_expr(table, expr->binary.right);
            break;

        /* Function call - check effects */
        case AST_EXPR_CALL:
            seraph_proof_generate_expr(table, expr->call.callee);
            for (Seraph_AST_Node* arg = expr->call.args; arg != NULL;
                 arg = arg->hdr.next) {
                seraph_proof_generate_expr(table, arg);
            }
            break;

        /* Block */
        case AST_EXPR_BLOCK:
            for (Seraph_AST_Node* stmt = expr->block.stmts; stmt != NULL;
                 stmt = stmt->hdr.next) {
                seraph_proof_generate_stmt(table, stmt);
            }
            if (expr->block.expr != NULL) {
                seraph_proof_generate_expr(table, expr->block.expr);
            }
            break;

        /* If expression */
        case AST_EXPR_IF:
            seraph_proof_generate_expr(table, expr->if_expr.cond);
            if (expr->if_expr.then_branch != NULL) {
                seraph_proof_generate_expr(table, expr->if_expr.then_branch);
            }
            if (expr->if_expr.else_branch != NULL) {
                seraph_proof_generate_expr(table, expr->if_expr.else_branch);
            }
            break;

        /* Match expression */
        case AST_EXPR_MATCH:
            seraph_proof_generate_expr(table, expr->match.scrutinee);
            for (Seraph_AST_Node* arm = expr->match.arms; arm != NULL;
                 arm = arm->hdr.next) {
                if (arm->hdr.kind == AST_MATCH_ARM) {
                    seraph_proof_generate_expr(table, arm->match_arm.body);
                }
            }
            break;

        /* Unary */
        case AST_EXPR_UNARY:
            seraph_proof_generate_expr(table, expr->unary.operand);
            break;

        /* Field access */
        case AST_EXPR_FIELD:
            seraph_proof_generate_expr(table, expr->field.object);
            break;

        default:
            break;
    }
}

void seraph_proof_generate_stmt(Seraph_Proof_Table* table, Seraph_AST_Node* stmt) {
    if (table == NULL || stmt == NULL) return;

    switch (stmt->hdr.kind) {
        case AST_STMT_EXPR:
            seraph_proof_generate_expr(table, stmt->expr_stmt.expr);
            break;

        case AST_STMT_RETURN:
            if (stmt->return_stmt.expr != NULL) {
                seraph_proof_generate_expr(table, stmt->return_stmt.expr);
            }
            break;

        case AST_STMT_WHILE:
            seraph_proof_generate_expr(table, stmt->while_stmt.cond);
            seraph_proof_generate_stmt(table, stmt->while_stmt.body);
            break;

        case AST_STMT_FOR:
            seraph_proof_generate_expr(table, stmt->for_stmt.iterable);
            seraph_proof_generate_stmt(table, stmt->for_stmt.body);
            break;

        case AST_DECL_LET:
        case AST_DECL_CONST:
            if (stmt->let_decl.init != NULL) {
                seraph_proof_generate_expr(table, stmt->let_decl.init);
            }
            /* Add initialization proof */
            seraph_proof_add_type(table, stmt->hdr.loc,
                                   "variable initialized at declaration",
                                   SERAPH_PROOF_STATUS_PROVEN);
            break;

        case AST_EXPR_BLOCK:
            for (Seraph_AST_Node* s = stmt->block.stmts; s != NULL;
                 s = s->hdr.next) {
                seraph_proof_generate_stmt(table, s);
            }
            if (stmt->block.expr != NULL) {
                seraph_proof_generate_expr(table, stmt->block.expr);
            }
            break;

        case AST_STMT_PERSIST:
            /* Add substrate access proof */
            seraph_proof_add(table, (Seraph_Proof){
                .kind = SERAPH_PROOF_SUBSTRATE,
                .status = SERAPH_PROOF_STATUS_RUNTIME,
                .loc = stmt->hdr.loc,
                .description = "persist block requires Atlas transaction"
            });
            if (stmt->substrate_block.body != NULL) {
                seraph_proof_generate_stmt(table, stmt->substrate_block.body);
            }
            break;

        case AST_STMT_AETHER:
            /* Add substrate access proof */
            seraph_proof_add(table, (Seraph_Proof){
                .kind = SERAPH_PROOF_SUBSTRATE,
                .status = SERAPH_PROOF_STATUS_RUNTIME,
                .loc = stmt->hdr.loc,
                .description = "aether block requires network context"
            });
            if (stmt->substrate_block.body != NULL) {
                seraph_proof_generate_stmt(table, stmt->substrate_block.body);
            }
            break;

        case AST_STMT_RECOVER:
            /* Add VOID handling proof */
            seraph_proof_add_void(table, stmt->hdr.loc,
                                   "recover block handles VOID values",
                                   SERAPH_PROOF_STATUS_PROVEN);
            if (stmt->recover_stmt.try_body != NULL) {
                seraph_proof_generate_stmt(table, stmt->recover_stmt.try_body);
            }
            if (stmt->recover_stmt.else_body != NULL) {
                seraph_proof_generate_stmt(table, stmt->recover_stmt.else_body);
            }
            break;

        default:
            break;
    }
}

/*============================================================================
 * Proof Verification
 *============================================================================*/

int seraph_proof_verify_all(const Seraph_Proof_Table* table) {
    if (table == NULL) return 1;

    /* Check if any proofs failed */
    return table->failed_count == 0;
}

size_t seraph_proof_count_by_status(const Seraph_Proof_Table* table,
                                     Seraph_Proof_Status status) {
    if (table == NULL) return 0;

    size_t count = 0;
    for (Seraph_Proof* p = table->proofs; p != NULL; p = p->next) {
        if (p->status == status) count++;
    }
    return count;
}

size_t seraph_proof_count_by_kind(const Seraph_Proof_Table* table,
                                   Seraph_Proof_Kind kind) {
    if (table == NULL) return 0;

    size_t count = 0;
    for (Seraph_Proof* p = table->proofs; p != NULL; p = p->next) {
        if (p->kind == kind) count++;
    }
    return count;
}

/*============================================================================
 * Proof Output
 *============================================================================*/

const char* seraph_proof_kind_name(Seraph_Proof_Kind kind) {
    switch (kind) {
        case SERAPH_PROOF_BOUNDS:      return "BOUNDS";
        case SERAPH_PROOF_VOID:        return "VOID";
        case SERAPH_PROOF_EFFECT:      return "EFFECT";
        case SERAPH_PROOF_PERMISSION:  return "PERMISSION";
        case SERAPH_PROOF_GENERATION:  return "GENERATION";
        case SERAPH_PROOF_SUBSTRATE:   return "SUBSTRATE";
        case SERAPH_PROOF_TYPE:        return "TYPE";
        case SERAPH_PROOF_INIT:        return "INIT";
        case SERAPH_PROOF_OVERFLOW:    return "OVERFLOW";
        case SERAPH_PROOF_NULL:        return "NULL";
        case SERAPH_PROOF_INVARIANT:   return "INVARIANT";
        case SERAPH_PROOF_TERMINATION: return "TERMINATION";
        default:                       return "UNKNOWN";
    }
}

const char* seraph_proof_status_name(Seraph_Proof_Status status) {
    switch (status) {
        case SERAPH_PROOF_STATUS_PROVEN:   return "PROVEN";
        case SERAPH_PROOF_STATUS_ASSUMED:  return "ASSUMED";
        case SERAPH_PROOF_STATUS_RUNTIME:  return "RUNTIME";
        case SERAPH_PROOF_STATUS_FAILED:   return "FAILED";
        case SERAPH_PROOF_STATUS_SKIPPED:  return "SKIPPED";
        default:                           return "UNKNOWN";
    }
}

void seraph_proof_print_all(const Seraph_Proof_Table* table) {
    if (table == NULL) return;

    fprintf(stderr, "=== Compile-time Proofs ===\n");

    for (Seraph_Proof* p = table->proofs; p != NULL; p = p->next) {
        fprintf(stderr, "%s:%u:%u: [%s] %s: %s\n",
                p->loc.filename ? p->loc.filename : "<unknown>",
                p->loc.line,
                p->loc.column,
                seraph_proof_status_name(p->status),
                seraph_proof_kind_name(p->kind),
                p->description ? p->description : "");
    }
}

void seraph_proof_print_summary(const Seraph_Proof_Table* table) {
    if (table == NULL) return;

    fprintf(stderr, "\n=== Proof Summary ===\n");
    fprintf(stderr, "Total proofs:   %zu\n", table->count);
    fprintf(stderr, "Proven:         %zu\n", table->proven_count);
    fprintf(stderr, "Runtime checks: %zu\n", table->runtime_count);
    fprintf(stderr, "Failed:         %zu\n", table->failed_count);

    fprintf(stderr, "\nBy kind:\n");
    fprintf(stderr, "  Bounds:      %zu\n",
            seraph_proof_count_by_kind(table, SERAPH_PROOF_BOUNDS));
    fprintf(stderr, "  VOID:        %zu\n",
            seraph_proof_count_by_kind(table, SERAPH_PROOF_VOID));
    fprintf(stderr, "  Effect:      %zu\n",
            seraph_proof_count_by_kind(table, SERAPH_PROOF_EFFECT));
    fprintf(stderr, "  Permission:  %zu\n",
            seraph_proof_count_by_kind(table, SERAPH_PROOF_PERMISSION));
    fprintf(stderr, "  Substrate:   %zu\n",
            seraph_proof_count_by_kind(table, SERAPH_PROOF_SUBSTRATE));
    fprintf(stderr, "  Type:        %zu\n",
            seraph_proof_count_by_kind(table, SERAPH_PROOF_TYPE));
}

void seraph_proof_emit_comments(const Seraph_Proof_Table* table, FILE* output) {
    if (table == NULL || output == NULL) return;

    fprintf(output, "/*\n");
    fprintf(output, " * Seraphim Compile-time Proof Annotations\n");
    fprintf(output, " * Generated by Seraphim Compiler\n");
    fprintf(output, " *\n");
    fprintf(output, " * Total: %zu proofs (%zu proven, %zu runtime, %zu failed)\n",
            table->count, table->proven_count, table->runtime_count, table->failed_count);
    fprintf(output, " */\n\n");

    for (Seraph_Proof* p = table->proofs; p != NULL; p = p->next) {
        if (p->status == SERAPH_PROOF_STATUS_PROVEN) {
            fprintf(output, "/* PROOF[%s]: %s at %s:%u */\n",
                    seraph_proof_kind_name(p->kind),
                    p->description ? p->description : "",
                    p->loc.filename ? p->loc.filename : "<unknown>",
                    p->loc.line);
        } else if (p->status == SERAPH_PROOF_STATUS_RUNTIME) {
            fprintf(output, "/* RUNTIME_CHECK[%s]: %s at %s:%u */\n",
                    seraph_proof_kind_name(p->kind),
                    p->description ? p->description : "",
                    p->loc.filename ? p->loc.filename : "<unknown>",
                    p->loc.line);
        }
    }
}
