/**
 * @file lexer.h
 * @brief Seraphim Compiler - Lexer Interface
 *
 * MC26: Seraphim Language Lexer
 *
 * The lexer converts source text into a stream of tokens.
 * It uses SERAPH Arena allocation for all token storage.
 */

#ifndef SERAPH_SERAPHIM_LEXER_H
#define SERAPH_SERAPHIM_LEXER_H

#include "token.h"
#include "../arena.h"
#include "../vbit.h"

/*============================================================================
 * Lexer State
 *============================================================================*/

/**
 * @brief Lexer diagnostic (error or warning)
 */
typedef struct Seraph_Lexer_Diag {
    Seraph_Source_Loc loc;          /**< Location of issue */
    const char* message;            /**< Error message */
    int is_error;                   /**< 1 = error, 0 = warning */
    struct Seraph_Lexer_Diag* next; /**< Next diagnostic */
} Seraph_Lexer_Diag;

/**
 * @brief Lexer state
 *
 * Maintains position in source and allocates tokens from arena.
 */
typedef struct {
    /* Source */
    const char* source;         /**< Source text (must stay valid) */
    size_t source_len;          /**< Source length */
    const char* filename;       /**< Source filename */

    /* Position */
    size_t pos;                 /**< Current byte position */
    uint32_t line;              /**< Current line (1-based) */
    uint32_t column;            /**< Current column (1-based) */

    /* Memory */
    Seraph_Arena* arena;        /**< Arena for allocations */

    /* Output */
    Seraph_Token* tokens;       /**< Token array (arena-allocated) */
    size_t token_count;         /**< Number of tokens */
    size_t token_capacity;      /**< Capacity of token array */

    /* Diagnostics */
    Seraph_Lexer_Diag* diagnostics;  /**< Linked list of diagnostics */
    int error_count;                 /**< Number of errors */
    int warning_count;               /**< Number of warnings */

    /* State */
    int has_error;              /**< 1 if any error occurred */

} Seraph_Lexer;

/*============================================================================
 * Lexer Lifecycle
 *============================================================================*/

/**
 * @brief Initialize a lexer
 *
 * @param lexer      Lexer to initialize
 * @param source     Source text (must remain valid during lexing)
 * @param source_len Length of source
 * @param filename   Filename for error messages
 * @param arena      Arena for allocations
 * @return SERAPH_VBIT_TRUE on success, SERAPH_VBIT_VOID on failure
 */
Seraph_Vbit seraph_lexer_init(
    Seraph_Lexer* lexer,
    const char* source,
    size_t source_len,
    const char* filename,
    Seraph_Arena* arena
);

/**
 * @brief Tokenize the entire source
 *
 * After this call, lexer->tokens contains all tokens.
 * The final token is always EOF (or VOID on error).
 *
 * @param lexer The lexer
 * @return SERAPH_VBIT_TRUE on success, SERAPH_VBIT_FALSE if errors occurred
 */
Seraph_Vbit seraph_lexer_tokenize(Seraph_Lexer* lexer);

/**
 * @brief Get the next token without storing
 *
 * Used for look-ahead during parsing.
 *
 * @param lexer The lexer
 * @return Next token (or VOID token on error)
 */
Seraph_Token seraph_lexer_next_token(Seraph_Lexer* lexer);

/**
 * @brief Peek at current character without consuming
 */
char seraph_lexer_peek(const Seraph_Lexer* lexer);

/**
 * @brief Peek at character at offset from current position
 */
char seraph_lexer_peek_at(const Seraph_Lexer* lexer, size_t offset);

/**
 * @brief Check if lexer has more input
 */
int seraph_lexer_has_more(const Seraph_Lexer* lexer);

/*============================================================================
 * Diagnostics
 *============================================================================*/

/**
 * @brief Report an error
 *
 * @param lexer  The lexer
 * @param loc    Source location
 * @param format Printf-style format string
 */
void seraph_lexer_error(
    Seraph_Lexer* lexer,
    Seraph_Source_Loc loc,
    const char* format,
    ...
);

/**
 * @brief Report a warning
 */
void seraph_lexer_warning(
    Seraph_Lexer* lexer,
    Seraph_Source_Loc loc,
    const char* format,
    ...
);

/**
 * @brief Check if lexer has errors
 */
SERAPH_INLINE int seraph_lexer_has_errors(const Seraph_Lexer* lexer) {
    return lexer != NULL && lexer->error_count > 0;
}

/**
 * @brief Print all diagnostics to stderr
 */
void seraph_lexer_print_diagnostics(const Seraph_Lexer* lexer);

/*============================================================================
 * Token Access
 *============================================================================*/

/**
 * @brief Get token at index
 *
 * @param lexer The lexer (must have been tokenized)
 * @param index Token index
 * @return Token at index, or VOID token if out of bounds
 */
Seraph_Token seraph_lexer_get_token(const Seraph_Lexer* lexer, size_t index);

/**
 * @brief Get total token count
 */
SERAPH_INLINE size_t seraph_lexer_token_count(const Seraph_Lexer* lexer) {
    return lexer != NULL ? lexer->token_count : 0;
}

/*============================================================================
 * Keyword Lookup
 *============================================================================*/

/**
 * @brief Look up a keyword by name
 *
 * @param name    Identifier to look up
 * @param len     Length of identifier
 * @return Token type if keyword, SERAPH_TOK_IDENT if not
 */
Seraph_Token_Type seraph_lexer_lookup_keyword(const char* name, size_t len);

#endif /* SERAPH_SERAPHIM_LEXER_H */
