/**
 * @file parser.h
 * @brief Seraphim Compiler - Parser Interface
 *
 * MC26: Seraphim Language Parser
 *
 * The parser converts a stream of tokens (from the lexer) into an
 * Abstract Syntax Tree (AST). It uses recursive descent for most
 * constructs and Pratt parsing for expressions (operator precedence).
 *
 * Error recovery uses panic mode - on error, skip to a synchronization
 * point (semicolon, closing brace, or declaration keyword) and continue.
 */

#ifndef SERAPH_SERAPHIM_PARSER_H
#define SERAPH_SERAPHIM_PARSER_H

#include "lexer.h"
#include "ast.h"
#include "types.h"
#include "../arena.h"
#include "../vbit.h"

/*============================================================================
 * Parser State
 *============================================================================*/

/**
 * @brief Parser diagnostic (error or warning)
 */
typedef struct Seraph_Parser_Diag {
    Seraph_Source_Loc loc;              /**< Location of issue */
    const char* message;                /**< Error message */
    int is_error;                       /**< 1 = error, 0 = warning */
    struct Seraph_Parser_Diag* next;    /**< Next diagnostic */
} Seraph_Parser_Diag;

/**
 * @brief Parser state
 *
 * Consumes tokens from lexer and produces AST.
 */
typedef struct {
    /* Input */
    Seraph_Lexer* lexer;            /**< Token source (must be tokenized) */
    size_t pos;                     /**< Current token index */

    /* Memory */
    Seraph_Arena* arena;            /**< Arena for AST allocations */

    /* Diagnostics */
    Seraph_Parser_Diag* diagnostics;    /**< Linked list of diagnostics */
    int error_count;                /**< Number of errors */
    int warning_count;              /**< Number of warnings */

    /* Panic mode state */
    int in_panic;                   /**< Currently in panic mode */

} Seraph_Parser;

/*============================================================================
 * Parser Lifecycle
 *============================================================================*/

/**
 * @brief Initialize a parser
 *
 * @param parser Parser to initialize
 * @param lexer  Lexer with tokens (must be tokenized already)
 * @param arena  Arena for AST allocations
 * @return SERAPH_VBIT_TRUE on success, SERAPH_VBIT_VOID on failure
 */
Seraph_Vbit seraph_parser_init(
    Seraph_Parser* parser,
    Seraph_Lexer* lexer,
    Seraph_Arena* arena
);

/**
 * @brief Parse an entire module
 *
 * Parses all declarations in the token stream and returns the
 * module AST node.
 *
 * @param parser The parser
 * @return Module AST node, or AST_VOID on fatal error
 */
Seraph_AST_Node* seraph_parse_module(Seraph_Parser* parser);

/**
 * @brief Parse a single declaration
 *
 * @param parser The parser
 * @return Declaration AST node, or AST_VOID on error
 */
Seraph_AST_Node* seraph_parse_decl(Seraph_Parser* parser);

/**
 * @brief Parse an expression
 *
 * @param parser The parser
 * @return Expression AST node, or AST_VOID on error
 */
Seraph_AST_Node* seraph_parse_expr(Seraph_Parser* parser);

/**
 * @brief Parse a type
 *
 * @param parser The parser
 * @return Type AST node, or AST_VOID on error
 */
Seraph_AST_Node* seraph_parse_type(Seraph_Parser* parser);

/**
 * @brief Parse a statement
 *
 * @param parser The parser
 * @return Statement AST node, or AST_VOID on error
 */
Seraph_AST_Node* seraph_parse_stmt(Seraph_Parser* parser);

/**
 * @brief Parse a block
 *
 * @param parser The parser
 * @return Block AST node, or AST_VOID on error
 */
Seraph_AST_Node* seraph_parse_block(Seraph_Parser* parser);

/*============================================================================
 * Diagnostics
 *============================================================================*/

/**
 * @brief Report a parser error
 *
 * @param parser The parser
 * @param loc    Source location
 * @param format Printf-style format string
 */
void seraph_parser_error(
    Seraph_Parser* parser,
    Seraph_Source_Loc loc,
    const char* format,
    ...
);

/**
 * @brief Report a parser warning
 */
void seraph_parser_warning(
    Seraph_Parser* parser,
    Seraph_Source_Loc loc,
    const char* format,
    ...
);

/**
 * @brief Check if parser has errors
 */
SERAPH_INLINE int seraph_parser_has_errors(const Seraph_Parser* parser) {
    return parser != NULL && parser->error_count > 0;
}

/**
 * @brief Print all diagnostics to stderr
 */
void seraph_parser_print_diagnostics(const Seraph_Parser* parser);

/*============================================================================
 * Token Access Utilities
 *============================================================================*/

/**
 * @brief Get current token
 */
SERAPH_INLINE Seraph_Token* seraph_parser_current(const Seraph_Parser* parser) {
    if (parser == NULL || parser->lexer == NULL) return NULL;
    if (parser->pos >= parser->lexer->token_count) return NULL;
    return &parser->lexer->tokens[parser->pos];
}

/**
 * @brief Peek at token at offset from current
 */
SERAPH_INLINE Seraph_Token* seraph_parser_peek(const Seraph_Parser* parser, size_t offset) {
    if (parser == NULL || parser->lexer == NULL) return NULL;
    size_t idx = parser->pos + offset;
    if (idx >= parser->lexer->token_count) return NULL;
    return &parser->lexer->tokens[idx];
}

/**
 * @brief Check if at end of tokens
 */
SERAPH_INLINE int seraph_parser_at_end(const Seraph_Parser* parser) {
    if (parser == NULL || parser->lexer == NULL) return 1;
    Seraph_Token* tok = seraph_parser_current(parser);
    return tok == NULL || tok->type == SERAPH_TOK_EOF;
}

/**
 * @brief Check if current token matches a type
 */
SERAPH_INLINE int seraph_parser_check(const Seraph_Parser* parser, Seraph_Token_Type type) {
    Seraph_Token* tok = seraph_parser_current(parser);
    return tok != NULL && tok->type == type;
}

/**
 * @brief Advance to next token and return previous
 */
Seraph_Token* seraph_parser_advance(Seraph_Parser* parser);

/**
 * @brief Consume a token of expected type or report error
 *
 * @param parser The parser
 * @param type   Expected token type
 * @param msg    Error message if mismatch
 * @return The consumed token, or NULL on mismatch
 */
Seraph_Token* seraph_parser_consume(
    Seraph_Parser* parser,
    Seraph_Token_Type type,
    const char* msg
);

/**
 * @brief Try to consume a token, returning 1 if consumed
 */
int seraph_parser_match(Seraph_Parser* parser, Seraph_Token_Type type);

/**
 * @brief Synchronize after error (skip to recovery point)
 */
void seraph_parser_synchronize(Seraph_Parser* parser);

#endif /* SERAPH_SERAPHIM_PARSER_H */
