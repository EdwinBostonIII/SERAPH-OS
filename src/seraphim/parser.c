/**
 * @file parser.c
 * @brief Seraphim Compiler - Parser Implementation
 *
 * MC26: Seraphim Language Parser
 *
 * Recursive descent parser with Pratt parsing for expressions.
 */

#include "seraph/seraphim/parser.h"
#include "seraph/seraphim/types.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/*============================================================================
 * Internal Forward Declarations
 *============================================================================*/

static Seraph_AST_Node* parse_fn_decl(Seraph_Parser* p);
static Seraph_AST_Node* parse_let_decl(Seraph_Parser* p);
static Seraph_AST_Node* parse_struct_decl(Seraph_Parser* p);
static Seraph_AST_Node* parse_enum_decl(Seraph_Parser* p);
static Seraph_AST_Node* parse_impl_decl(Seraph_Parser* p);
static Seraph_AST_Node* parse_expr_bp(Seraph_Parser* p, int min_bp);
static Seraph_AST_Node* parse_primary(Seraph_Parser* p);
static Seraph_AST_Node* parse_if_expr(Seraph_Parser* p);
static Seraph_AST_Node* parse_match_expr(Seraph_Parser* p);
static Seraph_AST_Node* parse_for_stmt(Seraph_Parser* p);
static Seraph_AST_Node* parse_while_stmt(Seraph_Parser* p);
static Seraph_AST_Node* parse_persist_stmt(Seraph_Parser* p);
static Seraph_AST_Node* parse_aether_stmt(Seraph_Parser* p);
static Seraph_AST_Node* parse_recover_stmt(Seraph_Parser* p);
static Seraph_AST_Node* parse_effect_annotation(Seraph_Parser* p);

/*============================================================================
 * Token Stream Helpers
 *============================================================================*/

static Seraph_Token* current(Seraph_Parser* p) {
    return seraph_parser_current(p);
}

/* Peek ahead n tokens from current position */
static Seraph_Token* peek(Seraph_Parser* p, int n) {
    size_t pos = p->pos + (size_t)n;
    if (pos >= p->lexer->token_count) return NULL;
    return &p->lexer->tokens[pos];
}

/* Get current source location, safe if current token is NULL */
static Seraph_Source_Loc current_loc(Seraph_Parser* p) {
    Seraph_Token* tok = current(p);
    return tok ? tok->loc : (Seraph_Source_Loc){0};
}

static int at_end(Seraph_Parser* p) {
    return seraph_parser_at_end(p);
}

static int check(Seraph_Parser* p, Seraph_Token_Type type) {
    return seraph_parser_check(p, type);
}

Seraph_Token* seraph_parser_advance(Seraph_Parser* p) {
    if (p == NULL || at_end(p)) return NULL;
    Seraph_Token* tok = current(p);
    p->pos++;
    return tok;
}

static Seraph_Token* advance(Seraph_Parser* p) {
    return seraph_parser_advance(p);
}

int seraph_parser_match(Seraph_Parser* p, Seraph_Token_Type type) {
    if (check(p, type)) {
        advance(p);
        return 1;
    }
    return 0;
}

static int match(Seraph_Parser* p, Seraph_Token_Type type) {
    return seraph_parser_match(p, type);
}

Seraph_Token* seraph_parser_consume(Seraph_Parser* p, Seraph_Token_Type type,
                                     const char* msg) {
    if (check(p, type)) {
        return advance(p);
    }

    Seraph_Token* tok = current(p);
    Seraph_Source_Loc loc = tok ? tok->loc : (Seraph_Source_Loc){0};
    seraph_parser_error(p, loc, "%s, got %s",
                        msg, tok ? seraph_token_type_name(tok->type) : "EOF");
    return NULL;
}

static Seraph_Token* consume(Seraph_Parser* p, Seraph_Token_Type type,
                             const char* msg) {
    return seraph_parser_consume(p, type, msg);
}

/*============================================================================
 * Error Handling
 *============================================================================*/

void seraph_parser_error(Seraph_Parser* p, Seraph_Source_Loc loc,
                          const char* format, ...) {
    if (p == NULL) return;

    /* Don't cascade errors in panic mode */
    if (p->in_panic) return;

    p->in_panic = 1;
    p->error_count++;

    /* Allocate diagnostic */
    Seraph_Parser_Diag* diag = (Seraph_Parser_Diag*)seraph_arena_alloc(
        p->arena, sizeof(Seraph_Parser_Diag), _Alignof(Seraph_Parser_Diag)
    );
    if (diag == NULL) return;

    diag->loc = loc;
    diag->is_error = 1;
    diag->next = p->diagnostics;
    p->diagnostics = diag;

    /* Format message */
    va_list args;
    va_start(args, format);
    int len = vsnprintf(NULL, 0, format, args);
    va_end(args);

    char* msg = (char*)seraph_arena_alloc(p->arena, len + 1, 1);
    if (msg != NULL) {
        va_start(args, format);
        vsnprintf(msg, len + 1, format, args);
        va_end(args);
        diag->message = msg;
    } else {
        diag->message = format;
    }
}

void seraph_parser_warning(Seraph_Parser* p, Seraph_Source_Loc loc,
                            const char* format, ...) {
    if (p == NULL) return;

    p->warning_count++;

    Seraph_Parser_Diag* diag = (Seraph_Parser_Diag*)seraph_arena_alloc(
        p->arena, sizeof(Seraph_Parser_Diag), _Alignof(Seraph_Parser_Diag)
    );
    if (diag == NULL) return;

    diag->loc = loc;
    diag->is_error = 0;
    diag->next = p->diagnostics;
    p->diagnostics = diag;

    va_list args;
    va_start(args, format);
    int len = vsnprintf(NULL, 0, format, args);
    va_end(args);

    char* msg = (char*)seraph_arena_alloc(p->arena, len + 1, 1);
    if (msg != NULL) {
        va_start(args, format);
        vsnprintf(msg, len + 1, format, args);
        va_end(args);
        diag->message = msg;
    } else {
        diag->message = format;
    }
}

void seraph_parser_print_diagnostics(const Seraph_Parser* p) {
    if (p == NULL) return;

    for (Seraph_Parser_Diag* d = p->diagnostics; d != NULL; d = d->next) {
        fprintf(stderr, "%s:%u:%u: %s: %s\n",
                d->loc.filename ? d->loc.filename : "<unknown>",
                d->loc.line,
                d->loc.column,
                d->is_error ? "error" : "warning",
                d->message);
    }
}

void seraph_parser_synchronize(Seraph_Parser* p) {
    if (p == NULL) return;

    p->in_panic = 0;

    while (!at_end(p)) {
        /* Semicolon ends a statement */
        Seraph_Token* prev = p->pos > 0 ? &p->lexer->tokens[p->pos - 1] : NULL;
        if (prev && prev->type == SERAPH_TOK_SEMICOLON) return;

        /* Declaration keywords are sync points */
        Seraph_Token* tok = current(p);
        if (tok == NULL) return;

        switch (tok->type) {
            /* Declaration keywords are valid sync points - return here */
            case SERAPH_TOK_FN:
            case SERAPH_TOK_LET:
            case SERAPH_TOK_CONST:
            case SERAPH_TOK_STRUCT:
            case SERAPH_TOK_ENUM:
            case SERAPH_TOK_USE:
            case SERAPH_TOK_IMPL:
            case SERAPH_TOK_FOREIGN:
            case SERAPH_TOK_TYPE:
                return;
            /* RBRACE: skip past it and continue searching for sync point */
            /* This prevents infinite loops when sync lands on stray braces */
            case SERAPH_TOK_RBRACE:
            default:
                advance(p);
        }
    }
}

/*============================================================================
 * Parser Initialization
 *============================================================================*/

Seraph_Vbit seraph_parser_init(Seraph_Parser* p, Seraph_Lexer* lexer,
                                Seraph_Arena* arena) {
    if (p == NULL || lexer == NULL || arena == NULL) {
        return SERAPH_VBIT_VOID;
    }

    p->lexer = lexer;
    p->pos = 0;
    p->arena = arena;
    p->diagnostics = NULL;
    p->error_count = 0;
    p->warning_count = 0;
    p->in_panic = 0;

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * Effect Annotation Parsing
 *============================================================================*/

/**
 * @brief Parse an effect annotation [pure] or [effects(void, persist, ...)]
 *
 * Returns an AST_EFFECT_LIST node with the parsed effect flags,
 * or NULL if no annotation is present.
 *
 * Syntax:
 *   [pure]                          -> SERAPH_EFFECT_NONE
 *   [effects(void)]                 -> SERAPH_EFFECT_VOID
 *   [effects(void, persist)]        -> SERAPH_EFFECT_VOID | SERAPH_EFFECT_PERSIST
 *   [effects(void, persist, network)] -> SERAPH_EFFECT_VOID | SERAPH_EFFECT_PERSIST | SERAPH_EFFECT_NETWORK
 */
static Seraph_AST_Node* parse_effect_annotation(Seraph_Parser* p) {
    if (!check(p, SERAPH_TOK_LBRACKET)) {
        return NULL;
    }

    Seraph_Token* bracket_tok = advance(p);  /* [ */
    Seraph_Source_Loc loc = bracket_tok->loc;

    /* Allocate effect list node */
    Seraph_AST_Node* effect_node = (Seraph_AST_Node*)seraph_arena_alloc(
        p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
    );
    if (effect_node == NULL) {
        /* Skip to ] and return NULL */
        while (!check(p, SERAPH_TOK_RBRACKET) && !at_end(p)) {
            advance(p);
        }
        match(p, SERAPH_TOK_RBRACKET);
        return NULL;
    }
    memset(effect_node, 0, sizeof(Seraph_AST_Node));
    effect_node->hdr.kind = AST_EFFECT_LIST;
    effect_node->hdr.loc = loc;
    effect_node->effect_list.effects = 0;  /* Start with NONE */

    Seraph_Token* tok = current(p);
    if (tok == NULL) {
        match(p, SERAPH_TOK_RBRACKET);
        return effect_node;
    }

    /* Check for [pure] */
    if (tok->type == SERAPH_TOK_PURE) {
        advance(p);  /* consume 'pure' */
        effect_node->effect_list.effects = SERAPH_EFFECT_NONE;
        consume(p, SERAPH_TOK_RBRACKET, "expected ']' after 'pure'");
        return effect_node;
    }

    /* Check for [effects(...)] */
    if (tok->type == SERAPH_TOK_EFFECTS) {
        advance(p);  /* consume 'effects' */

        if (!consume(p, SERAPH_TOK_LPAREN, "expected '(' after 'effects'")) {
            while (!check(p, SERAPH_TOK_RBRACKET) && !at_end(p)) {
                advance(p);
            }
            match(p, SERAPH_TOK_RBRACKET);
            return effect_node;
        }

        /* Parse effect list: void, persist, network, timer, io */
        uint32_t effects = 0;
        while (!check(p, SERAPH_TOK_RPAREN) && !at_end(p)) {
            tok = current(p);
            if (tok == NULL) break;

            switch (tok->type) {
                case SERAPH_TOK_EFFECT_VOID:
                    effects |= SERAPH_EFFECT_VOID;
                    advance(p);
                    break;
                case SERAPH_TOK_EFFECT_PERSIST:
                    effects |= SERAPH_EFFECT_PERSIST;
                    advance(p);
                    break;
                case SERAPH_TOK_EFFECT_NETWORK:
                    effects |= SERAPH_EFFECT_NETWORK;
                    advance(p);
                    break;
                case SERAPH_TOK_EFFECT_TIMER:
                    effects |= SERAPH_EFFECT_TIMER;
                    advance(p);
                    break;
                case SERAPH_TOK_EFFECT_IO:
                    effects |= SERAPH_EFFECT_IO;
                    advance(p);
                    break;
                case SERAPH_TOK_IDENT:
                    /* Handle effect names as identifiers (lowercase) */
                    if (tok->lexeme_len == 4 && strncmp(tok->lexeme, "void", 4) == 0) {
                        effects |= SERAPH_EFFECT_VOID;
                    } else if (tok->lexeme_len == 7 && strncmp(tok->lexeme, "persist", 7) == 0) {
                        effects |= SERAPH_EFFECT_PERSIST;
                    } else if (tok->lexeme_len == 7 && strncmp(tok->lexeme, "network", 7) == 0) {
                        effects |= SERAPH_EFFECT_NETWORK;
                    } else if (tok->lexeme_len == 5 && strncmp(tok->lexeme, "timer", 5) == 0) {
                        effects |= SERAPH_EFFECT_TIMER;
                    } else if (tok->lexeme_len == 2 && strncmp(tok->lexeme, "io", 2) == 0) {
                        effects |= SERAPH_EFFECT_IO;
                    } else {
                        seraph_parser_warning(p, tok->loc,
                            "unknown effect '%.*s'", (int)tok->lexeme_len, tok->lexeme);
                    }
                    advance(p);
                    break;
                default:
                    seraph_parser_error(p, tok->loc, "expected effect name");
                    advance(p);
                    break;
            }

            /* Optional comma between effects */
            if (!check(p, SERAPH_TOK_RPAREN)) {
                match(p, SERAPH_TOK_COMMA);
            }
        }

        effect_node->effect_list.effects = effects;

        consume(p, SERAPH_TOK_RPAREN, "expected ')' after effects list");
        consume(p, SERAPH_TOK_RBRACKET, "expected ']' after effects annotation");
        return effect_node;
    }

    /* Unknown annotation - skip it with a warning */
    seraph_parser_warning(p, tok->loc,
        "unknown annotation, expected 'pure' or 'effects(...)'");
    while (!check(p, SERAPH_TOK_RBRACKET) && !at_end(p)) {
        advance(p);
    }
    match(p, SERAPH_TOK_RBRACKET);

    return effect_node;
}

/*============================================================================
 * Module Parsing
 *============================================================================*/

Seraph_AST_Node* seraph_parse_module(Seraph_Parser* p) {
    if (p == NULL) return NULL;

    Seraph_Token* first = current(p);
    Seraph_Source_Loc loc = first ? first->loc : (Seraph_Source_Loc){0};

    Seraph_AST_Node* module = seraph_ast_module(p->arena, loc);
    if (module == NULL) return NULL;

    while (!at_end(p)) {
        Seraph_AST_Node* decl = seraph_parse_decl(p);
        if (decl != NULL && !seraph_ast_is_void(decl)) {
            seraph_ast_append(&module->module.decls, decl);
            module->module.decl_count++;
        }
    }

    return module;
}

/*============================================================================
 * Declaration Parsing
 *============================================================================*/

Seraph_AST_Node* seraph_parse_decl(Seraph_Parser* p) {
    Seraph_Token* tok = current(p);
    if (tok == NULL) return NULL;

    Seraph_AST_Node* decl = NULL;
    Seraph_AST_Node* effects = NULL;

    /* Handle effect annotations [pure], [effects(...)] before declarations */
    if (tok->type == SERAPH_TOK_LBRACKET) {
        effects = parse_effect_annotation(p);
        tok = current(p);
        if (tok == NULL) return NULL;
    }

    switch (tok->type) {
        case SERAPH_TOK_FN:
            decl = parse_fn_decl(p);
            /* Attach effect annotation to function declaration */
            if (decl != NULL && !seraph_ast_is_void(decl) && effects != NULL) {
                decl->fn_decl.effects = effects;
                /* Set is_pure flag if effects are NONE */
                if (effects->effect_list.effects == SERAPH_EFFECT_NONE) {
                    decl->fn_decl.is_pure = 1;
                }
            }
            break;

        case SERAPH_TOK_LET:
            decl = parse_let_decl(p);
            break;

        case SERAPH_TOK_CONST:
            advance(p);  /* consume 'const' */
            decl = parse_let_decl(p);
            if (decl && !seraph_ast_is_void(decl)) {
                decl->let_decl.is_const = 1;
            }
            break;

        case SERAPH_TOK_STRUCT:
            decl = parse_struct_decl(p);
            break;

        case SERAPH_TOK_ENUM:
            decl = parse_enum_decl(p);
            break;

        case SERAPH_TOK_IMPL:
            decl = parse_impl_decl(p);
            break;

        default:
            seraph_parser_error(p, tok->loc, "expected declaration");
            seraph_parser_synchronize(p);
            return seraph_ast_void(p->arena, tok->loc);
    }

    if (p->in_panic) {
        seraph_parser_synchronize(p);
    }

    return decl;
}

static Seraph_AST_Node* parse_fn_decl(Seraph_Parser* p) {
    Seraph_Token* fn_tok = consume(p, SERAPH_TOK_FN, "expected 'fn'");
    if (fn_tok == NULL) return seraph_ast_void(p->arena, current_loc(p));

    /* Function name */
    Seraph_Token* name_tok = consume(p, SERAPH_TOK_IDENT, "expected function name");
    if (name_tok == NULL) return seraph_ast_void(p->arena, fn_tok->loc);

    Seraph_AST_Node* fn = seraph_ast_fn_decl(p->arena, fn_tok->loc,
                                              name_tok->lexeme, name_tok->lexeme_len);
    if (fn == NULL) return NULL;

    /* Parameters */
    if (!consume(p, SERAPH_TOK_LPAREN, "expected '(' after function name")) {
        return seraph_ast_void(p->arena, fn_tok->loc);
    }

    while (!check(p, SERAPH_TOK_RPAREN) && !at_end(p)) {
        /* Parameter name */
        Seraph_Token* param_name = consume(p, SERAPH_TOK_IDENT, "expected parameter name");
        if (param_name == NULL) break;

        /* : type */
        if (!consume(p, SERAPH_TOK_COLON, "expected ':' after parameter name")) break;

        Seraph_AST_Node* param_type = seraph_parse_type(p);

        Seraph_AST_Node* param = seraph_ast_param(p->arena, param_name->loc,
                                                   param_name->lexeme, param_name->lexeme_len,
                                                   param_type);
        seraph_ast_append(&fn->fn_decl.params, param);
        fn->fn_decl.param_count++;

        if (!check(p, SERAPH_TOK_RPAREN)) {
            if (!consume(p, SERAPH_TOK_COMMA, "expected ',' or ')' after parameter")) break;
        }
    }

    if (!consume(p, SERAPH_TOK_RPAREN, "expected ')' after parameters")) {
        return seraph_ast_void(p->arena, fn_tok->loc);
    }

    /* Return type (optional: -> Type) */
    if (match(p, SERAPH_TOK_ARROW)) {
        fn->fn_decl.ret_type = seraph_parse_type(p);
    }

    /* Function body or forward declaration */
    if (match(p, SERAPH_TOK_SEMICOLON)) {
        /* Forward declaration - no body */
        fn->fn_decl.body = NULL;
        fn->fn_decl.is_forward = 1;
    } else {
        /* Full function definition */
        fn->fn_decl.body = seraph_parse_block(p);
    }

    return fn;
}

static Seraph_AST_Node* parse_let_decl(Seraph_Parser* p) {
    Seraph_Token* let_tok = current(p);
    int is_let = check(p, SERAPH_TOK_LET);

    if (is_let) {
        advance(p);  /* consume 'let' */
    }

    /* Check for 'mut' */
    int is_mut = match(p, SERAPH_TOK_MUT);

    /* Variable name */
    Seraph_Token* name_tok = consume(p, SERAPH_TOK_IDENT, "expected variable name");
    if (name_tok == NULL) return seraph_ast_void(p->arena, let_tok->loc);

    Seraph_AST_Node* let = seraph_ast_let_decl(p->arena, let_tok->loc,
                                                name_tok->lexeme, name_tok->lexeme_len,
                                                is_mut, 0);
    if (let == NULL) return NULL;

    /* Optional type annotation */
    if (match(p, SERAPH_TOK_COLON)) {
        let->let_decl.type = seraph_parse_type(p);
    }

    /* Optional initializer - required only if no type annotation */
    if (match(p, SERAPH_TOK_ASSIGN)) {
        let->let_decl.init = seraph_parse_expr(p);
    } else if (let->let_decl.type == NULL) {
        /* No type and no initializer - error */
        seraph_parser_error(p, let_tok->loc, "expected '=' in let declaration (type inference requires initializer)");
        return seraph_ast_void(p->arena, let_tok->loc);
    }
    /* else: type provided, no initializer - uninitialized declaration */

    /* Semicolon */
    consume(p, SERAPH_TOK_SEMICOLON, "expected ';' after let declaration");

    return let;
}

static Seraph_AST_Node* parse_struct_decl(Seraph_Parser* p) {
    Seraph_Token* struct_tok = consume(p, SERAPH_TOK_STRUCT, "expected 'struct'");
    if (struct_tok == NULL) return seraph_ast_void(p->arena, current_loc(p));

    /* Struct name */
    Seraph_Token* name_tok = consume(p, SERAPH_TOK_IDENT, "expected struct name");
    if (name_tok == NULL) return seraph_ast_void(p->arena, struct_tok->loc);

    Seraph_AST_Node* st = seraph_ast_struct_decl(p->arena, struct_tok->loc,
                                                  name_tok->lexeme, name_tok->lexeme_len);
    if (st == NULL) return NULL;

    /* { fields } */
    if (!consume(p, SERAPH_TOK_LBRACE, "expected '{' after struct name")) {
        return seraph_ast_void(p->arena, struct_tok->loc);
    }

    while (!check(p, SERAPH_TOK_RBRACE) && !at_end(p)) {
        /* Field name */
        Seraph_Token* field_name = consume(p, SERAPH_TOK_IDENT, "expected field name");
        if (field_name == NULL) break;

        /* : type */
        if (!consume(p, SERAPH_TOK_COLON, "expected ':' after field name")) break;

        Seraph_AST_Node* field_type = seraph_parse_type(p);

        /* Create field definition node */
        Seraph_AST_Node* field = (Seraph_AST_Node*)seraph_arena_alloc(
            p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
        );
        if (field == NULL) break;
        memset(field, 0, sizeof(Seraph_AST_Node));
        field->hdr.kind = AST_FIELD_DEF;
        field->hdr.loc = field_name->loc;
        field->field_def.name = field_name->lexeme;
        field->field_def.name_len = field_name->lexeme_len;
        field->field_def.type = field_type;

        seraph_ast_append(&st->struct_decl.fields, field);
        st->struct_decl.field_count++;

        /* Optional comma */
        if (!check(p, SERAPH_TOK_RBRACE)) {
            match(p, SERAPH_TOK_COMMA);
        }
    }

    consume(p, SERAPH_TOK_RBRACE, "expected '}' after struct fields");

    return st;
}

static Seraph_AST_Node* parse_enum_decl(Seraph_Parser* p) {
    Seraph_Token* enum_tok = consume(p, SERAPH_TOK_ENUM, "expected 'enum'");
    if (enum_tok == NULL) return seraph_ast_void(p->arena, current_loc(p));

    /* Enum name */
    Seraph_Token* name_tok = consume(p, SERAPH_TOK_IDENT, "expected enum name");
    if (name_tok == NULL) return seraph_ast_void(p->arena, enum_tok->loc);

    Seraph_AST_Node* en = (Seraph_AST_Node*)seraph_arena_alloc(
        p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
    );
    if (en == NULL) return NULL;
    memset(en, 0, sizeof(Seraph_AST_Node));
    en->hdr.kind = AST_DECL_ENUM;
    en->hdr.loc = enum_tok->loc;
    en->enum_decl.name = name_tok->lexeme;
    en->enum_decl.name_len = name_tok->lexeme_len;

    /* { variants } */
    if (!consume(p, SERAPH_TOK_LBRACE, "expected '{' after enum name")) {
        return seraph_ast_void(p->arena, enum_tok->loc);
    }

    while (!check(p, SERAPH_TOK_RBRACE) && !at_end(p)) {
        Seraph_Token* var_name = consume(p, SERAPH_TOK_IDENT, "expected variant name");
        if (var_name == NULL) break;

        Seraph_AST_Node* variant = (Seraph_AST_Node*)seraph_arena_alloc(
            p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
        );
        if (variant == NULL) break;
        memset(variant, 0, sizeof(Seraph_AST_Node));
        variant->hdr.kind = AST_ENUM_VARIANT;
        variant->hdr.loc = var_name->loc;
        variant->enum_variant.name = var_name->lexeme;
        variant->enum_variant.name_len = var_name->lexeme_len;

        /* Optional data type in parentheses */
        if (match(p, SERAPH_TOK_LPAREN)) {
            variant->enum_variant.data = seraph_parse_type(p);
            consume(p, SERAPH_TOK_RPAREN, "expected ')' after variant data");
        }

        seraph_ast_append(&en->enum_decl.variants, variant);
        en->enum_decl.variant_count++;

        if (!check(p, SERAPH_TOK_RBRACE)) {
            match(p, SERAPH_TOK_COMMA);
        }
    }

    consume(p, SERAPH_TOK_RBRACE, "expected '}' after enum variants");

    return en;
}

static Seraph_AST_Node* parse_impl_decl(Seraph_Parser* p) {
    Seraph_Token* impl_tok = consume(p, SERAPH_TOK_IMPL, "expected 'impl'");
    if (impl_tok == NULL) return seraph_ast_void(p->arena, current_loc(p));

    /* Type being implemented - just an identifier for now */
    Seraph_Token* type_tok = consume(p, SERAPH_TOK_IDENT, "expected type name after 'impl'");
    if (type_tok == NULL) return seraph_ast_void(p->arena, impl_tok->loc);

    /* Create type node for the impl target */
    Seraph_AST_Node* type_node = seraph_ast_ident(p->arena, type_tok->loc,
                                                   type_tok->lexeme, type_tok->lexeme_len);

    /* Create impl node */
    Seraph_AST_Node* impl = (Seraph_AST_Node*)seraph_arena_alloc(
        p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
    );
    if (impl == NULL) return NULL;
    memset(impl, 0, sizeof(Seraph_AST_Node));
    impl->hdr.kind = AST_DECL_IMPL;
    impl->hdr.loc = impl_tok->loc;
    impl->impl_decl.type = type_node;

    /* { methods } */
    if (!consume(p, SERAPH_TOK_LBRACE, "expected '{' after impl type")) {
        return seraph_ast_void(p->arena, impl_tok->loc);
    }

    while (!check(p, SERAPH_TOK_RBRACE) && !at_end(p)) {
        /* Methods are function declarations */
        if (!check(p, SERAPH_TOK_FN)) {
            seraph_parser_error(p, current_loc(p), "expected 'fn' in impl block");
            break;
        }

        Seraph_AST_Node* method = parse_fn_decl(p);
        if (method == NULL || seraph_ast_is_void(method)) break;

        /* Mark as method and link type */
        method->fn_decl.is_method = 1;

        seraph_ast_append(&impl->impl_decl.methods, method);
        impl->impl_decl.method_count++;
    }

    consume(p, SERAPH_TOK_RBRACE, "expected '}' after impl methods");

    return impl;
}

/*============================================================================
 * Type Parsing
 *============================================================================*/

Seraph_AST_Node* seraph_parse_type(Seraph_Parser* p) {
    Seraph_Token* tok = current(p);
    if (tok == NULL) return seraph_ast_void(p->arena, (Seraph_Source_Loc){0});

    /* VOID-able type: ??Type */
    if (check(p, SERAPH_TOK_VOID_PROP)) {
        advance(p);
        Seraph_AST_Node* inner = seraph_parse_type(p);
        return seraph_ast_void_type(p->arena, tok->loc, inner);
    }

    /* Function type: fn(A, B, C) -> R */
    if (check(p, SERAPH_TOK_FN)) {
        advance(p);  /* consume 'fn' */

        Seraph_AST_Node* fn_type = (Seraph_AST_Node*)seraph_arena_alloc(
            p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
        );
        if (fn_type == NULL) return seraph_ast_void(p->arena, tok->loc);
        memset(fn_type, 0, sizeof(Seraph_AST_Node));
        fn_type->hdr.kind = AST_TYPE_FN;
        fn_type->hdr.loc = tok->loc;

        /* Parse parameter types */
        if (!consume(p, SERAPH_TOK_LPAREN, "expected '(' after 'fn' in type")) {
            return seraph_ast_void(p->arena, tok->loc);
        }

        while (!check(p, SERAPH_TOK_RPAREN) && !at_end(p)) {
            Seraph_AST_Node* param_type = seraph_parse_type(p);
            seraph_ast_append(&fn_type->fn_type.params, param_type);
            fn_type->fn_type.param_count++;

            if (!check(p, SERAPH_TOK_RPAREN)) {
                if (!match(p, SERAPH_TOK_COMMA)) break;
            }
        }

        if (!consume(p, SERAPH_TOK_RPAREN, "expected ')' after function parameters")) {
            return seraph_ast_void(p->arena, tok->loc);
        }

        /* Optional return type */
        if (match(p, SERAPH_TOK_ARROW)) {
            fn_type->fn_type.ret = seraph_parse_type(p);
        } else {
            /* No return type = void */
            fn_type->fn_type.ret = NULL;
        }

        return fn_type;
    }

    /* Pointer types: *T */
    if (check(p, SERAPH_TOK_STAR)) {
        advance(p);
        Seraph_AST_Node* inner = seraph_parse_type(p);
        return seraph_ast_ptr_type(p->arena, tok->loc, inner);
    }

    /* Reference types: &T, &mut T */
    if (check(p, SERAPH_TOK_BIT_AND)) {
        advance(p);
        int is_mut = match(p, SERAPH_TOK_MUT);

        /* Check for substrate: &volatile, &atlas, &aether */
        int substrate = 0;
        if (match(p, SERAPH_TOK_VOLATILE)) substrate = 1;
        else if (match(p, SERAPH_TOK_ATLAS)) substrate = 2;
        else if (match(p, SERAPH_TOK_AETHER)) substrate = 3;

        Seraph_AST_Node* inner = seraph_parse_type(p);
        Seraph_AST_Node* ref = seraph_ast_ref_type(p->arena, tok->loc, inner, is_mut);
        if (ref != NULL) {
            ref->ref_type.substrate = substrate;
        }
        return ref;
    }

    /* Array type: [T; N] */
    if (check(p, SERAPH_TOK_LBRACKET)) {
        advance(p);
        Seraph_AST_Node* elem_type = seraph_parse_type(p);
        Seraph_AST_Node* size = NULL;

        if (match(p, SERAPH_TOK_SEMICOLON)) {
            size = seraph_parse_expr(p);
        }

        consume(p, SERAPH_TOK_RBRACKET, "expected ']' after array type");

        Seraph_AST_Node* arr = (Seraph_AST_Node*)seraph_arena_alloc(
            p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
        );
        if (arr == NULL) return NULL;
        memset(arr, 0, sizeof(Seraph_AST_Node));
        arr->hdr.kind = size ? AST_TYPE_ARRAY : AST_TYPE_SLICE;
        arr->hdr.loc = tok->loc;
        arr->array_type.elem_type = elem_type;
        arr->array_type.size = size;
        return arr;
    }

    /* Primitive types */
    switch (tok->type) {
        case SERAPH_TOK_U8:
        case SERAPH_TOK_U16:
        case SERAPH_TOK_U32:
        case SERAPH_TOK_U64:
        case SERAPH_TOK_I8:
        case SERAPH_TOK_I16:
        case SERAPH_TOK_I32:
        case SERAPH_TOK_I64:
        case SERAPH_TOK_BOOL:
        case SERAPH_TOK_CHAR:
        case SERAPH_TOK_SCALAR:
        case SERAPH_TOK_DUAL:
        case SERAPH_TOK_GALACTIC:
        case SERAPH_TOK_F32:
        case SERAPH_TOK_F64:
            advance(p);
            return seraph_ast_prim_type(p->arena, tok->loc, tok->type);

        case SERAPH_TOK_IDENT:
            advance(p);
            return seraph_ast_named_type(p->arena, tok->loc, tok->lexeme, tok->lexeme_len);

        default:
            seraph_parser_error(p, tok->loc, "expected type");
            return seraph_ast_void(p->arena, tok->loc);
    }
}

/*============================================================================
 * Statement Parsing
 *============================================================================*/

Seraph_AST_Node* seraph_parse_stmt(Seraph_Parser* p) {
    Seraph_Token* tok = current(p);
    if (tok == NULL) return NULL;

    switch (tok->type) {
        case SERAPH_TOK_LET:
        case SERAPH_TOK_CONST:
            return parse_let_decl(p);

        case SERAPH_TOK_RETURN: {
            advance(p);
            Seraph_AST_Node* ret = (Seraph_AST_Node*)seraph_arena_alloc(
                p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
            );
            if (ret == NULL) return NULL;
            memset(ret, 0, sizeof(Seraph_AST_Node));
            ret->hdr.kind = AST_STMT_RETURN;
            ret->hdr.loc = tok->loc;

            if (!check(p, SERAPH_TOK_SEMICOLON)) {
                ret->return_stmt.expr = seraph_parse_expr(p);
            }

            consume(p, SERAPH_TOK_SEMICOLON, "expected ';' after return");
            return ret;
        }

        case SERAPH_TOK_BREAK: {
            advance(p);
            Seraph_AST_Node* brk = (Seraph_AST_Node*)seraph_arena_alloc(
                p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
            );
            if (brk == NULL) return NULL;
            memset(brk, 0, sizeof(Seraph_AST_Node));
            brk->hdr.kind = AST_STMT_BREAK;
            brk->hdr.loc = tok->loc;
            consume(p, SERAPH_TOK_SEMICOLON, "expected ';' after break");
            return brk;
        }

        case SERAPH_TOK_CONTINUE: {
            advance(p);
            Seraph_AST_Node* cont = (Seraph_AST_Node*)seraph_arena_alloc(
                p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
            );
            if (cont == NULL) return NULL;
            memset(cont, 0, sizeof(Seraph_AST_Node));
            cont->hdr.kind = AST_STMT_CONTINUE;
            cont->hdr.loc = tok->loc;
            consume(p, SERAPH_TOK_SEMICOLON, "expected ';' after continue");
            return cont;
        }

        case SERAPH_TOK_FOR:
            return parse_for_stmt(p);

        case SERAPH_TOK_WHILE:
            return parse_while_stmt(p);

        case SERAPH_TOK_IF:
            /* If as statement (no semicolon needed) */
            return parse_if_expr(p);

        case SERAPH_TOK_PERSIST:
            return parse_persist_stmt(p);

        case SERAPH_TOK_AETHER_BLOCK:
            return parse_aether_stmt(p);

        case SERAPH_TOK_RECOVER:
            return parse_recover_stmt(p);

        default: {
            /* Expression statement */
            Seraph_AST_Node* expr = seraph_parse_expr(p);
            if (seraph_ast_is_void(expr)) return expr;

            Seraph_AST_Node* stmt = (Seraph_AST_Node*)seraph_arena_alloc(
                p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
            );
            if (stmt == NULL) return NULL;
            memset(stmt, 0, sizeof(Seraph_AST_Node));
            stmt->hdr.kind = AST_STMT_EXPR;
            stmt->hdr.loc = expr->hdr.loc;
            stmt->expr_stmt.expr = expr;

            consume(p, SERAPH_TOK_SEMICOLON, "expected ';' after expression");
            return stmt;
        }
    }
}

static Seraph_AST_Node* parse_for_stmt(Seraph_Parser* p) {
    Seraph_Token* for_tok = consume(p, SERAPH_TOK_FOR, "expected 'for'");
    if (for_tok == NULL) return seraph_ast_void(p->arena, current_loc(p));

    /* Loop variable */
    Seraph_Token* var_tok = consume(p, SERAPH_TOK_IDENT, "expected loop variable");
    if (var_tok == NULL) return seraph_ast_void(p->arena, for_tok->loc);

    consume(p, SERAPH_TOK_IN, "expected 'in' after loop variable");

    /* Iterable expression */
    Seraph_AST_Node* iter = seraph_parse_expr(p);

    /* Body */
    Seraph_AST_Node* body = seraph_parse_block(p);

    Seraph_AST_Node* for_stmt = (Seraph_AST_Node*)seraph_arena_alloc(
        p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
    );
    if (for_stmt == NULL) return NULL;
    memset(for_stmt, 0, sizeof(Seraph_AST_Node));
    for_stmt->hdr.kind = AST_STMT_FOR;
    for_stmt->hdr.loc = for_tok->loc;
    for_stmt->for_stmt.var = var_tok->lexeme;
    for_stmt->for_stmt.var_len = var_tok->lexeme_len;
    for_stmt->for_stmt.iterable = iter;
    for_stmt->for_stmt.body = body;

    return for_stmt;
}

static Seraph_AST_Node* parse_while_stmt(Seraph_Parser* p) {
    Seraph_Token* while_tok = consume(p, SERAPH_TOK_WHILE, "expected 'while'");
    if (while_tok == NULL) return seraph_ast_void(p->arena, current_loc(p));

    /* Condition */
    Seraph_AST_Node* cond = seraph_parse_expr(p);

    /* Body */
    Seraph_AST_Node* body = seraph_parse_block(p);

    Seraph_AST_Node* while_stmt = (Seraph_AST_Node*)seraph_arena_alloc(
        p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
    );
    if (while_stmt == NULL) return NULL;
    memset(while_stmt, 0, sizeof(Seraph_AST_Node));
    while_stmt->hdr.kind = AST_STMT_WHILE;
    while_stmt->hdr.loc = while_tok->loc;
    while_stmt->while_stmt.cond = cond;
    while_stmt->while_stmt.body = body;

    return while_stmt;
}

/*============================================================================
 * Substrate Block Parsing
 *============================================================================*/

/**
 * @brief Parse a persist { } block
 *
 * Syntax: persist { statements }
 *
 * Creates an Atlas transaction context. Operations within the block
 * are transactional - they all succeed or all fail.
 */
static Seraph_AST_Node* parse_persist_stmt(Seraph_Parser* p) {
    Seraph_Token* persist_tok = consume(p, SERAPH_TOK_PERSIST, "expected 'persist'");
    if (persist_tok == NULL) return seraph_ast_void(p->arena, current_loc(p));

    /* Parse the block body */
    Seraph_AST_Node* body = seraph_parse_block(p);

    /* Create substrate block node */
    Seraph_AST_Node* persist_stmt = (Seraph_AST_Node*)seraph_arena_alloc(
        p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
    );
    if (persist_stmt == NULL) return NULL;
    memset(persist_stmt, 0, sizeof(Seraph_AST_Node));
    persist_stmt->hdr.kind = AST_STMT_PERSIST;
    persist_stmt->hdr.loc = persist_tok->loc;
    persist_stmt->substrate_block.body = body;

    return persist_stmt;
}

/**
 * @brief Parse an aether { } block
 *
 * Syntax: aether { statements }
 *
 * Creates an Aether network context. Operations within the block
 * access the distributed network memory.
 */
static Seraph_AST_Node* parse_aether_stmt(Seraph_Parser* p) {
    Seraph_Token* aether_tok = consume(p, SERAPH_TOK_AETHER_BLOCK, "expected 'aether'");
    if (aether_tok == NULL) return seraph_ast_void(p->arena, current_loc(p));

    /* Parse the block body */
    Seraph_AST_Node* body = seraph_parse_block(p);

    /* Create substrate block node */
    Seraph_AST_Node* aether_stmt = (Seraph_AST_Node*)seraph_arena_alloc(
        p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
    );
    if (aether_stmt == NULL) return NULL;
    memset(aether_stmt, 0, sizeof(Seraph_AST_Node));
    aether_stmt->hdr.kind = AST_STMT_AETHER;
    aether_stmt->hdr.loc = aether_tok->loc;
    aether_stmt->substrate_block.body = body;

    return aether_stmt;
}

/**
 * @brief Parse a recover { } else { } block
 *
 * Syntax: recover { try_body } else { else_body }
 *
 * Similar to try/catch but for VOID handling. If any operation in
 * the try_body produces VOID, control transfers to else_body.
 *
 * Example:
 *   let result = recover {
 *       let x = risky_operation()!!;  // Would panic on VOID
 *       x * 2
 *   } else {
 *       0  // Default value if VOID occurred
 *   };
 */
static Seraph_AST_Node* parse_recover_stmt(Seraph_Parser* p) {
    Seraph_Token* recover_tok = consume(p, SERAPH_TOK_RECOVER, "expected 'recover'");
    if (recover_tok == NULL) return seraph_ast_void(p->arena, current_loc(p));

    /* Parse the try body */
    Seraph_AST_Node* try_body = seraph_parse_block(p);

    /* Require 'else' keyword */
    if (!consume(p, SERAPH_TOK_ELSE, "expected 'else' after recover block")) {
        return seraph_ast_void(p->arena, recover_tok->loc);
    }

    /* Parse the else body */
    Seraph_AST_Node* else_body = seraph_parse_block(p);

    /* Create recover statement node */
    Seraph_AST_Node* recover_stmt = (Seraph_AST_Node*)seraph_arena_alloc(
        p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
    );
    if (recover_stmt == NULL) return NULL;
    memset(recover_stmt, 0, sizeof(Seraph_AST_Node));
    recover_stmt->hdr.kind = AST_STMT_RECOVER;
    recover_stmt->hdr.loc = recover_tok->loc;
    recover_stmt->recover_stmt.try_body = try_body;
    recover_stmt->recover_stmt.else_body = else_body;

    return recover_stmt;
}

/*============================================================================
 * Block Parsing
 *============================================================================*/

Seraph_AST_Node* seraph_parse_block(Seraph_Parser* p) {
    Seraph_Token* lbrace = consume(p, SERAPH_TOK_LBRACE, "expected '{'");
    if (lbrace == NULL) {
        return seraph_ast_void(p->arena, current_loc(p));
    }

    Seraph_AST_Node* block = seraph_ast_block(p->arena, lbrace->loc);
    if (block == NULL) return NULL;

    while (!check(p, SERAPH_TOK_RBRACE) && !at_end(p)) {
        Seraph_Token* tok = current(p);
        if (tok == NULL) break;

        /* Check if this looks like a statement vs trailing expression */
        int is_stmt_start = (tok->type == SERAPH_TOK_LET ||
                             tok->type == SERAPH_TOK_CONST ||
                             tok->type == SERAPH_TOK_RETURN ||
                             tok->type == SERAPH_TOK_BREAK ||
                             tok->type == SERAPH_TOK_CONTINUE ||
                             tok->type == SERAPH_TOK_FOR ||
                             tok->type == SERAPH_TOK_WHILE ||
                             tok->type == SERAPH_TOK_PERSIST ||
                             tok->type == SERAPH_TOK_AETHER_BLOCK ||
                             tok->type == SERAPH_TOK_RECOVER);

        if (is_stmt_start) {
            /* Definitely a statement */
            Seraph_AST_Node* stmt = seraph_parse_stmt(p);
            if (stmt != NULL && !seraph_ast_is_void(stmt)) {
                seraph_ast_append(&block->block.stmts, stmt);
                block->block.stmt_count++;
            }
        } else {
            /* Could be expression or if/match statement */
            Seraph_AST_Node* expr = seraph_parse_expr(p);
            if (expr == NULL || seraph_ast_is_void(expr)) {
                if (p->in_panic) seraph_parser_synchronize(p);
                continue;
            }

            /* If followed by semicolon, it's an expression statement */
            if (match(p, SERAPH_TOK_SEMICOLON)) {
                Seraph_AST_Node* stmt = (Seraph_AST_Node*)seraph_arena_alloc(
                    p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
                );
                if (stmt == NULL) break;
                memset(stmt, 0, sizeof(Seraph_AST_Node));
                stmt->hdr.kind = AST_STMT_EXPR;
                stmt->hdr.loc = expr->hdr.loc;
                stmt->expr_stmt.expr = expr;
                seraph_ast_append(&block->block.stmts, stmt);
                block->block.stmt_count++;
            }
            /* If followed by RBRACE, it's the block's result expression */
            else if (check(p, SERAPH_TOK_RBRACE)) {
                block->block.expr = expr;
            }
            /* Otherwise, might be an if/match/substrate block that doesn't need semicolon */
            else if (expr->hdr.kind == AST_EXPR_IF ||
                     expr->hdr.kind == AST_EXPR_MATCH ||
                     expr->hdr.kind == AST_EXPR_BLOCK ||
                     expr->hdr.kind == AST_STMT_PERSIST ||
                     expr->hdr.kind == AST_STMT_AETHER ||
                     expr->hdr.kind == AST_STMT_RECOVER) {
                Seraph_AST_Node* stmt = (Seraph_AST_Node*)seraph_arena_alloc(
                    p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
                );
                if (stmt == NULL) break;
                memset(stmt, 0, sizeof(Seraph_AST_Node));
                stmt->hdr.kind = AST_STMT_EXPR;
                stmt->hdr.loc = expr->hdr.loc;
                stmt->expr_stmt.expr = expr;
                seraph_ast_append(&block->block.stmts, stmt);
                block->block.stmt_count++;
            }
            /* Otherwise error - expected semicolon */
            else {
                seraph_parser_error(p, current_loc(p), "expected ';' after expression");
                if (p->in_panic) seraph_parser_synchronize(p);
            }
        }

        if (p->in_panic) {
            seraph_parser_synchronize(p);
        }
    }

    consume(p, SERAPH_TOK_RBRACE, "expected '}' after block");

    return block;
}

/*============================================================================
 * Expression Parsing (Pratt Parser)
 *============================================================================*/

Seraph_AST_Node* seraph_parse_expr(Seraph_Parser* p) {
    return parse_expr_bp(p, 0);
}

static Seraph_AST_Node* parse_expr_bp(Seraph_Parser* p, int min_bp) {
    /* Parse prefix/primary */
    Seraph_AST_Node* lhs = parse_primary(p);
    if (seraph_ast_is_void(lhs)) return lhs;

    while (!at_end(p)) {
        Seraph_Token* op = current(p);
        if (op == NULL) break;

        /* Check for postfix operators (??, !!) */
        if (op->type == SERAPH_TOK_VOID_PROP) {
            advance(p);
            Seraph_AST_Node* prop = (Seraph_AST_Node*)seraph_arena_alloc(
                p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
            );
            if (prop == NULL) return lhs;
            memset(prop, 0, sizeof(Seraph_AST_Node));
            prop->hdr.kind = AST_EXPR_VOID_PROP;
            prop->hdr.loc = op->loc;
            prop->void_prop.operand = lhs;
            prop->void_prop.default_val = NULL;
            lhs = prop;
            continue;
        }

        if (op->type == SERAPH_TOK_VOID_ASSERT) {
            advance(p);
            Seraph_AST_Node* assert_node = (Seraph_AST_Node*)seraph_arena_alloc(
                p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
            );
            if (assert_node == NULL) return lhs;
            memset(assert_node, 0, sizeof(Seraph_AST_Node));
            assert_node->hdr.kind = AST_EXPR_VOID_ASSERT;
            assert_node->hdr.loc = op->loc;
            assert_node->void_assert.operand = lhs;
            lhs = assert_node;
            continue;
        }

        /* Check for function call */
        if (op->type == SERAPH_TOK_LPAREN) {
            advance(p);
            Seraph_AST_Node* call = seraph_ast_call(p->arena, op->loc, lhs);
            if (call == NULL) return lhs;

            while (!check(p, SERAPH_TOK_RPAREN) && !at_end(p)) {
                Seraph_AST_Node* arg = seraph_parse_expr(p);
                seraph_ast_append(&call->call.args, arg);
                call->call.arg_count++;

                if (!check(p, SERAPH_TOK_RPAREN)) {
                    if (!match(p, SERAPH_TOK_COMMA)) break;
                }
            }
            consume(p, SERAPH_TOK_RPAREN, "expected ')' after arguments");
            lhs = call;
            continue;
        }

        /* Check for field access or method call */
        if (op->type == SERAPH_TOK_DOT) {
            advance(p);
            Seraph_Token* field_tok = consume(p, SERAPH_TOK_IDENT, "expected field name");
            if (field_tok == NULL) return lhs;

            /* Check if followed by '(' - method call */
            if (check(p, SERAPH_TOK_LPAREN)) {
                advance(p);  /* consume '(' */

                /* Parse method call arguments */
                Seraph_AST_Node* method_call = (Seraph_AST_Node*)seraph_arena_alloc(
                    p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
                );
                if (method_call == NULL) return lhs;
                memset(method_call, 0, sizeof(Seraph_AST_Node));
                method_call->hdr.kind = AST_EXPR_METHOD_CALL;
                method_call->hdr.loc = op->loc;
                method_call->method_call.receiver = lhs;
                method_call->method_call.method = field_tok->lexeme;
                method_call->method_call.method_len = field_tok->lexeme_len;

                /* Parse arguments */
                while (!check(p, SERAPH_TOK_RPAREN) && !at_end(p)) {
                    Seraph_AST_Node* arg = seraph_parse_expr(p);
                    seraph_ast_append(&method_call->method_call.args, arg);
                    method_call->method_call.arg_count++;
                    if (!match(p, SERAPH_TOK_COMMA)) break;
                }
                consume(p, SERAPH_TOK_RPAREN, "expected ')' after method arguments");
                lhs = method_call;
            } else {
                /* Regular field access */
                lhs = seraph_ast_field(p->arena, op->loc, lhs,
                                        field_tok->lexeme, field_tok->lexeme_len);
            }
            continue;
        }

        /* Check for index access */
        if (op->type == SERAPH_TOK_LBRACKET) {
            advance(p);
            Seraph_AST_Node* idx = seraph_parse_expr(p);
            consume(p, SERAPH_TOK_RBRACKET, "expected ']' after index");

            Seraph_AST_Node* index_expr = (Seraph_AST_Node*)seraph_arena_alloc(
                p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
            );
            if (index_expr == NULL) return lhs;
            memset(index_expr, 0, sizeof(Seraph_AST_Node));
            index_expr->hdr.kind = AST_EXPR_INDEX;
            index_expr->hdr.loc = op->loc;
            index_expr->index.object = lhs;
            index_expr->index.index = idx;
            lhs = index_expr;
            continue;
        }

        /* Get operator precedence */
        int bp = seraph_token_precedence(op->type);
        if (bp == 0 || bp < min_bp) break;

        /* Binary operator */
        advance(p);

        /* Type cast (as) - parse type, not expression */
        if (op->type == SERAPH_TOK_AS) {
            Seraph_AST_Node* target_type = seraph_parse_type(p);
            lhs = seraph_ast_cast(p->arena, op->loc, lhs, target_type);
            continue;
        }

        int right_bp = seraph_token_is_right_assoc(op->type) ? bp : bp + 1;
        Seraph_AST_Node* rhs = parse_expr_bp(p, right_bp);

        /* Range operators produce Range nodes, not Binary nodes */
        if (op->type == SERAPH_TOK_RANGE || op->type == SERAPH_TOK_RANGE_INCL) {
            Seraph_AST_Node* range = (Seraph_AST_Node*)seraph_arena_alloc(
                p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
            );
            if (range == NULL) return lhs;
            memset(range, 0, sizeof(Seraph_AST_Node));
            range->hdr.kind = AST_EXPR_RANGE;
            range->hdr.loc = op->loc;
            range->range.start = lhs;
            range->range.end = rhs;
            range->range.inclusive = (op->type == SERAPH_TOK_RANGE_INCL) ? 1 : 0;
            lhs = range;
        } else {
            lhs = seraph_ast_binary(p->arena, op->loc, op->type, lhs, rhs);
        }
    }

    return lhs;
}

static Seraph_AST_Node* parse_primary(Seraph_Parser* p) {
    Seraph_Token* tok = current(p);
    if (tok == NULL) return seraph_ast_void(p->arena, (Seraph_Source_Loc){0});

    switch (tok->type) {
        /* Literals */
        case SERAPH_TOK_INT_LITERAL:
            advance(p);
            return seraph_ast_int_lit(p->arena, tok->loc, tok->value.int_value, tok->num_suffix);

        case SERAPH_TOK_FLOAT_LITERAL:
            advance(p);
            return seraph_ast_float_lit(p->arena, tok->loc, tok->value.float_value, tok->num_suffix);

        case SERAPH_TOK_STRING_LITERAL:
            advance(p);
            return seraph_ast_string_lit(p->arena, tok->loc,
                                          tok->value.string_value.str,
                                          tok->value.string_value.len);

        case SERAPH_TOK_CHAR_LITERAL:
            advance(p);
            {
                Seraph_AST_Node* ch = (Seraph_AST_Node*)seraph_arena_alloc(
                    p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
                );
                if (ch == NULL) return NULL;
                memset(ch, 0, sizeof(Seraph_AST_Node));
                ch->hdr.kind = AST_EXPR_CHAR_LIT;
                ch->hdr.loc = tok->loc;
                ch->char_lit.value = tok->value.char_value;
                return ch;
            }

        case SERAPH_TOK_TRUE:
            advance(p);
            return seraph_ast_bool_lit(p->arena, tok->loc, 1);

        case SERAPH_TOK_FALSE:
            advance(p);
            return seraph_ast_bool_lit(p->arena, tok->loc, 0);

        case SERAPH_TOK_VOID_LIT:
            advance(p);
            {
                Seraph_AST_Node* v = (Seraph_AST_Node*)seraph_arena_alloc(
                    p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
                );
                if (v == NULL) return NULL;
                memset(v, 0, sizeof(Seraph_AST_Node));
                v->hdr.kind = AST_EXPR_VOID_LIT;
                v->hdr.loc = tok->loc;
                return v;
            }

        /* Identifier - may be struct init if followed by '{' */
        case SERAPH_TOK_IDENT:
            advance(p);
            /* Check if this is a struct initializer: Name { field: value, ... }
             * We need to distinguish from match arms: { pattern => expr, ... }
             * Look for IDENT followed by COLON (struct init) vs other (not struct init) */
            if (check(p, SERAPH_TOK_LBRACE)) {
                /* Peek ahead to check if this looks like struct init */
                Seraph_Token* next1 = peek(p, 1);  /* Should be IDENT for struct init */
                Seraph_Token* next2 = peek(p, 2);  /* Should be COLON for struct init */

                /* It's a struct init if: { IDENT : ... or { } */
                int is_struct_init = 0;
                if (next1 && next1->type == SERAPH_TOK_RBRACE) {
                    /* Empty struct init: Name { } */
                    is_struct_init = 1;
                } else if (next1 && next2 &&
                           next1->type == SERAPH_TOK_IDENT &&
                           next2->type == SERAPH_TOK_COLON) {
                    /* Field init: Name { field: value } */
                    is_struct_init = 1;
                }

                if (is_struct_init) {
                    /* Parse as struct initializer */
                    Seraph_AST_Node* type_path = seraph_ast_ident(p->arena, tok->loc,
                                                                   tok->lexeme, tok->lexeme_len);
                    advance(p);  /* consume '{' */

                    Seraph_AST_Node* struct_init = seraph_ast_struct_init(p->arena, tok->loc, type_path);
                    if (struct_init == NULL) return NULL;

                    /* Parse field initializers: name: expr, ... */
                    while (!check(p, SERAPH_TOK_RBRACE) && !at_end(p)) {
                        Seraph_Token* field_tok = consume(p, SERAPH_TOK_IDENT, "expected field name");
                        if (field_tok == NULL) break;

                    consume(p, SERAPH_TOK_COLON, "expected ':' after field name");

                    Seraph_AST_Node* value = seraph_parse_expr(p);

                    Seraph_AST_Node* field_init = seraph_ast_field_init(p->arena, field_tok->loc,
                                                                         field_tok->lexeme,
                                                                         field_tok->lexeme_len,
                                                                         value);
                    seraph_ast_append(&struct_init->struct_init.fields, field_init);
                    struct_init->struct_init.field_count++;

                    /* Optional comma between fields */
                    if (!check(p, SERAPH_TOK_RBRACE)) {
                        if (!match(p, SERAPH_TOK_COMMA)) break;
                    }
                }

                    consume(p, SERAPH_TOK_RBRACE, "expected '}' after struct initializer");
                    return struct_init;
                }
                /* Not a struct init, fall through to return identifier */
            }
            return seraph_ast_ident(p->arena, tok->loc, tok->lexeme, tok->lexeme_len);

        /* Grouped expression */
        case SERAPH_TOK_LPAREN:
            advance(p);
            {
                Seraph_AST_Node* expr = seraph_parse_expr(p);
                consume(p, SERAPH_TOK_RPAREN, "expected ')' after expression");
                return expr;
            }

        /* Block expression */
        case SERAPH_TOK_LBRACE:
            return seraph_parse_block(p);

        /* If expression */
        case SERAPH_TOK_IF:
            return parse_if_expr(p);

        /* Match expression */
        case SERAPH_TOK_MATCH:
            return parse_match_expr(p);

        /* Unary operators */
        case SERAPH_TOK_MINUS:
        case SERAPH_TOK_NOT:
        case SERAPH_TOK_BIT_NOT:
        case SERAPH_TOK_BIT_AND:   /* & (address-of) */
        case SERAPH_TOK_STAR:      /* * (dereference) */
            advance(p);
            {
                Seraph_AST_Node* operand = parse_expr_bp(p, 12);  /* High precedence */
                return seraph_ast_unary(p->arena, tok->loc, tok->type, operand);
            }

        /* Array literal */
        case SERAPH_TOK_LBRACKET:
            advance(p);
            {
                Seraph_AST_Node* arr = (Seraph_AST_Node*)seraph_arena_alloc(
                    p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
                );
                if (arr == NULL) return NULL;
                memset(arr, 0, sizeof(Seraph_AST_Node));
                arr->hdr.kind = AST_EXPR_ARRAY;
                arr->hdr.loc = tok->loc;

                while (!check(p, SERAPH_TOK_RBRACKET) && !at_end(p)) {
                    Seraph_AST_Node* elem = seraph_parse_expr(p);
                    seraph_ast_append(&arr->array.elements, elem);
                    arr->array.elem_count++;

                    if (!check(p, SERAPH_TOK_RBRACKET)) {
                        if (!match(p, SERAPH_TOK_COMMA)) break;
                    }
                }
                consume(p, SERAPH_TOK_RBRACKET, "expected ']' after array elements");
                return arr;
            }

        /* Closure expression: |params| -> RetType { body } or |params| expr */
        case SERAPH_TOK_BIT_OR:
            advance(p);  /* consume opening | */
            {
                Seraph_AST_Node* closure = seraph_ast_closure(p->arena, tok->loc);
                if (closure == NULL) return NULL;

                /* Parse parameters: |x, y: i32, z| */
                while (!check(p, SERAPH_TOK_BIT_OR) && !at_end(p)) {
                    Seraph_Token* param_tok = consume(p, SERAPH_TOK_IDENT, "expected parameter name");
                    if (param_tok == NULL) break;

                    Seraph_AST_Node* param_type = NULL;
                    if (match(p, SERAPH_TOK_COLON)) {
                        param_type = seraph_parse_type(p);
                    }

                    Seraph_AST_Node* param = seraph_ast_param(p->arena, param_tok->loc,
                                                               param_tok->lexeme,
                                                               param_tok->lexeme_len,
                                                               param_type);
                    seraph_ast_append(&closure->closure.params, param);
                    closure->closure.param_count++;

                    if (!check(p, SERAPH_TOK_BIT_OR)) {
                        if (!match(p, SERAPH_TOK_COMMA)) break;
                    }
                }

                if (!consume(p, SERAPH_TOK_BIT_OR, "expected '|' after closure parameters")) {
                    return seraph_ast_void(p->arena, tok->loc);
                }

                /* Optional return type: -> Type */
                if (match(p, SERAPH_TOK_ARROW)) {
                    closure->closure.ret_type = seraph_parse_type(p);
                }

                /* Body: block { ... } or expression */
                if (check(p, SERAPH_TOK_LBRACE)) {
                    closure->closure.body = seraph_parse_block(p);
                } else {
                    closure->closure.body = seraph_parse_expr(p);
                }

                return closure;
            }

        default:
            seraph_parser_error(p, tok->loc, "expected expression");
            return seraph_ast_void(p->arena, tok->loc);
    }
}

static Seraph_AST_Node* parse_if_expr(Seraph_Parser* p) {
    Seraph_Token* if_tok = consume(p, SERAPH_TOK_IF, "expected 'if'");
    if (if_tok == NULL) return seraph_ast_void(p->arena, current_loc(p));

    Seraph_AST_Node* cond = seraph_parse_expr(p);
    Seraph_AST_Node* then_branch = seraph_parse_block(p);
    Seraph_AST_Node* else_branch = NULL;

    if (match(p, SERAPH_TOK_ELSE)) {
        if (check(p, SERAPH_TOK_IF)) {
            else_branch = parse_if_expr(p);
        } else {
            else_branch = seraph_parse_block(p);
        }
    }

    return seraph_ast_if(p->arena, if_tok->loc, cond, then_branch, else_branch);
}

static Seraph_AST_Node* parse_match_expr(Seraph_Parser* p) {
    Seraph_Token* match_tok = consume(p, SERAPH_TOK_MATCH, "expected 'match'");
    if (match_tok == NULL) return seraph_ast_void(p->arena, current_loc(p));

    Seraph_AST_Node* scrutinee = seraph_parse_expr(p);

    Seraph_AST_Node* match_node = (Seraph_AST_Node*)seraph_arena_alloc(
        p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
    );
    if (match_node == NULL) return NULL;
    memset(match_node, 0, sizeof(Seraph_AST_Node));
    match_node->hdr.kind = AST_EXPR_MATCH;
    match_node->hdr.loc = match_tok->loc;
    match_node->match.scrutinee = scrutinee;

    consume(p, SERAPH_TOK_LBRACE, "expected '{' after match expression");

    while (!check(p, SERAPH_TOK_RBRACE) && !at_end(p)) {
        /* Parse pattern (simplified - just identifier or literal for now) */
        Seraph_AST_Node* pattern = (Seraph_AST_Node*)seraph_arena_alloc(
            p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
        );
        if (pattern == NULL) break;
        memset(pattern, 0, sizeof(Seraph_AST_Node));
        pattern->hdr.kind = AST_PATTERN;
        pattern->hdr.loc = current_loc(p);

        Seraph_Token* pat_tok = advance(p);
        if (pat_tok == NULL) break;  /* NULL safety */
        if (pat_tok->type == SERAPH_TOK_IDENT) {
            pattern->pattern.pattern_kind = 1;  /* Ident pattern */
            pattern->pattern.data.ident.name = pat_tok->lexeme;
            pattern->pattern.data.ident.name_len = pat_tok->lexeme_len;
        } else if (pat_tok->type == SERAPH_TOK_INT_LITERAL) {
            pattern->pattern.pattern_kind = 2;  /* Int pattern */
            pattern->pattern.data.int_val = pat_tok->value.int_value;
        }

        consume(p, SERAPH_TOK_FAT_ARROW, "expected '=>' in match arm");

        Seraph_AST_Node* body = seraph_parse_expr(p);

        Seraph_AST_Node* arm = (Seraph_AST_Node*)seraph_arena_alloc(
            p->arena, sizeof(Seraph_AST_Node), _Alignof(Seraph_AST_Node)
        );
        if (arm == NULL) break;
        memset(arm, 0, sizeof(Seraph_AST_Node));
        arm->hdr.kind = AST_MATCH_ARM;
        arm->hdr.loc = pattern->hdr.loc;
        arm->match_arm.pattern = pattern;
        arm->match_arm.body = body;

        seraph_ast_append(&match_node->match.arms, arm);
        match_node->match.arm_count++;

        match(p, SERAPH_TOK_COMMA);
    }

    consume(p, SERAPH_TOK_RBRACE, "expected '}' after match arms");

    return match_node;
}
