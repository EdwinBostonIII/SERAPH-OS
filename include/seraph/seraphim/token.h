/**
 * @file token.h
 * @brief Seraphim Compiler - Token Types and Structures
 *
 * MC26: Seraphim Language Tokens
 *
 * Defines all token types for the Seraphim language lexer.
 * Tokens are the atomic units produced by lexical analysis.
 */

#ifndef SERAPH_SERAPHIM_TOKEN_H
#define SERAPH_SERAPHIM_TOKEN_H

#include <stdint.h>
#include <stddef.h>
#include "../void.h"
#include "../q128.h"

/*============================================================================
 * Token Type Enumeration
 *============================================================================*/

/**
 * @brief All token types in the Seraphim language
 *
 * Organized by category for clarity. Uses 0xFF for VOID/invalid
 * following SERAPH conventions.
 */
typedef enum {
    /*--------------------------------------------------------------------
     * Literals (0x00-0x0F)
     *--------------------------------------------------------------------*/
    SERAPH_TOK_INT_LITERAL      = 0x00,  /**< Integer: 42, 0xFF, 0b1010 */
    SERAPH_TOK_FLOAT_LITERAL    = 0x01,  /**< Float: 3.14, 1e-5 */
    SERAPH_TOK_STRING_LITERAL   = 0x02,  /**< String: "hello" */
    SERAPH_TOK_CHAR_LITERAL     = 0x03,  /**< Char: 'c' */
    SERAPH_TOK_TRUE             = 0x04,  /**< Boolean true */
    SERAPH_TOK_FALSE            = 0x05,  /**< Boolean false */
    SERAPH_TOK_VOID_LIT         = 0x06,  /**< VOID literal */

    /*--------------------------------------------------------------------
     * Keywords - Control Flow (0x10-0x1F)
     *--------------------------------------------------------------------*/
    SERAPH_TOK_FN               = 0x10,  /**< fn */
    SERAPH_TOK_LET              = 0x11,  /**< let */
    SERAPH_TOK_MUT              = 0x12,  /**< mut */
    SERAPH_TOK_IF               = 0x13,  /**< if */
    SERAPH_TOK_ELSE             = 0x14,  /**< else */
    SERAPH_TOK_FOR              = 0x15,  /**< for */
    SERAPH_TOK_WHILE            = 0x16,  /**< while */
    SERAPH_TOK_RETURN           = 0x17,  /**< return */
    SERAPH_TOK_MATCH            = 0x18,  /**< match */
    SERAPH_TOK_IN               = 0x19,  /**< in */
    SERAPH_TOK_BREAK            = 0x1A,  /**< break */
    SERAPH_TOK_CONTINUE         = 0x1B,  /**< continue */
    SERAPH_TOK_AS               = 0x1C,  /**< as (type cast) */

    /*--------------------------------------------------------------------
     * Keywords - Declarations (0x20-0x2F)
     *--------------------------------------------------------------------*/
    SERAPH_TOK_STRUCT           = 0x20,  /**< struct */
    SERAPH_TOK_ENUM             = 0x21,  /**< enum */
    SERAPH_TOK_CONST            = 0x22,  /**< const */
    SERAPH_TOK_USE              = 0x23,  /**< use */
    SERAPH_TOK_FOREIGN          = 0x24,  /**< foreign */
    SERAPH_TOK_TYPE             = 0x25,  /**< type (alias) */
    SERAPH_TOK_IMPL             = 0x26,  /**< impl */

    /*--------------------------------------------------------------------
     * Keywords - Substrate Blocks (0x30-0x3F)
     *--------------------------------------------------------------------*/
    SERAPH_TOK_PERSIST          = 0x30,  /**< persist { } block */
    SERAPH_TOK_AETHER_BLOCK     = 0x31,  /**< aether { } block */
    SERAPH_TOK_RECOVER          = 0x32,  /**< recover { } else { } */
    SERAPH_TOK_EFFECTS          = 0x33,  /**< effects(...) annotation */

    /*--------------------------------------------------------------------
     * Keywords - Effects (0x40-0x4F)
     *--------------------------------------------------------------------*/
    SERAPH_TOK_PURE             = 0x40,  /**< pure (no effects) */
    SERAPH_TOK_EFFECT_VOID      = 0x41,  /**< VOID effect */
    SERAPH_TOK_EFFECT_PERSIST   = 0x42,  /**< PERSIST effect */
    SERAPH_TOK_EFFECT_NETWORK   = 0x43,  /**< NETWORK effect */
    SERAPH_TOK_EFFECT_TIMER     = 0x44,  /**< TIMER effect */
    SERAPH_TOK_EFFECT_IO        = 0x45,  /**< IO effect */

    /*--------------------------------------------------------------------
     * Keywords - Primitive Types (0x50-0x5F)
     *--------------------------------------------------------------------*/
    SERAPH_TOK_U8               = 0x50,  /**< u8 */
    SERAPH_TOK_U16              = 0x51,  /**< u16 */
    SERAPH_TOK_U32              = 0x52,  /**< u32 */
    SERAPH_TOK_U64              = 0x53,  /**< u64 */
    SERAPH_TOK_I8               = 0x54,  /**< i8 */
    SERAPH_TOK_I16              = 0x55,  /**< i16 */
    SERAPH_TOK_I32              = 0x56,  /**< i32 */
    SERAPH_TOK_I64              = 0x57,  /**< i64 */
    SERAPH_TOK_BOOL             = 0x58,  /**< bool */
    SERAPH_TOK_CHAR             = 0x59,  /**< char */
    SERAPH_TOK_F32              = 0x5A,  /**< f32 (32-bit float for bootstrap) */
    SERAPH_TOK_F64              = 0x5B,  /**< f64 (64-bit float for bootstrap) */

    /*--------------------------------------------------------------------
     * Keywords - Numeric Types (0x60-0x6F)
     *--------------------------------------------------------------------*/
    SERAPH_TOK_SCALAR           = 0x60,  /**< scalar (Q32.32) */
    SERAPH_TOK_DUAL             = 0x61,  /**< dual (128-bit AD) */
    SERAPH_TOK_GALACTIC         = 0x62,  /**< galactic (256-bit AD) */

    /*--------------------------------------------------------------------
     * Keywords - Substrate Types (0x70-0x7F)
     *--------------------------------------------------------------------*/
    SERAPH_TOK_VOLATILE         = 0x70,  /**< volatile (RAM) */
    SERAPH_TOK_ATLAS            = 0x71,  /**< atlas (persistent) */
    SERAPH_TOK_AETHER           = 0x72,  /**< aether (network) */

    /*--------------------------------------------------------------------
     * Operators - VOID (0x80-0x8F)
     *--------------------------------------------------------------------*/
    SERAPH_TOK_VOID_PROP        = 0x80,  /**< ?? (propagation) */
    SERAPH_TOK_VOID_ASSERT      = 0x81,  /**< !! (assertion) */

    /*--------------------------------------------------------------------
     * Operators - Arrows & Pipes (0x90-0x9F)
     *--------------------------------------------------------------------*/
    SERAPH_TOK_ARROW            = 0x90,  /**< -> (return type) */
    SERAPH_TOK_FAT_ARROW        = 0x91,  /**< => (match arm) */
    SERAPH_TOK_PIPE             = 0x92,  /**< |> (pipe) */
    SERAPH_TOK_DOUBLE_COLON     = 0x93,  /**< :: (path separator) */
    SERAPH_TOK_RANGE            = 0x94,  /**< .. (range) */
    SERAPH_TOK_RANGE_INCL       = 0x95,  /**< ..= (inclusive range) */

    /*--------------------------------------------------------------------
     * Operators - Arithmetic (0xA0-0xAF)
     *--------------------------------------------------------------------*/
    SERAPH_TOK_PLUS             = 0xA0,  /**< + */
    SERAPH_TOK_MINUS            = 0xA1,  /**< - */
    SERAPH_TOK_STAR             = 0xA2,  /**< * */
    SERAPH_TOK_SLASH            = 0xA3,  /**< / */
    SERAPH_TOK_PERCENT          = 0xA4,  /**< % */

    /*--------------------------------------------------------------------
     * Operators - Comparison (0xB0-0xBF)
     *--------------------------------------------------------------------*/
    SERAPH_TOK_EQ               = 0xB0,  /**< == */
    SERAPH_TOK_NE               = 0xB1,  /**< != */
    SERAPH_TOK_LT               = 0xB2,  /**< < */
    SERAPH_TOK_GT               = 0xB3,  /**< > */
    SERAPH_TOK_LE               = 0xB4,  /**< <= */
    SERAPH_TOK_GE               = 0xB5,  /**< >= */

    /*--------------------------------------------------------------------
     * Operators - Logical & Bitwise (0xC0-0xCF)
     *--------------------------------------------------------------------*/
    SERAPH_TOK_AND              = 0xC0,  /**< && */
    SERAPH_TOK_OR               = 0xC1,  /**< || */
    SERAPH_TOK_NOT              = 0xC2,  /**< ! */
    SERAPH_TOK_BIT_AND          = 0xC3,  /**< & */
    SERAPH_TOK_BIT_OR           = 0xC4,  /**< | */
    SERAPH_TOK_BIT_XOR          = 0xC5,  /**< ^ */
    SERAPH_TOK_BIT_NOT          = 0xC6,  /**< ~ */
    SERAPH_TOK_SHL              = 0xC7,  /**< << */
    SERAPH_TOK_SHR              = 0xC8,  /**< >> */

    /*--------------------------------------------------------------------
     * Operators - Assignment (0xD0-0xDF)
     *--------------------------------------------------------------------*/
    SERAPH_TOK_ASSIGN           = 0xD0,  /**< = */
    SERAPH_TOK_PLUS_ASSIGN      = 0xD1,  /**< += */
    SERAPH_TOK_MINUS_ASSIGN     = 0xD2,  /**< -= */
    SERAPH_TOK_STAR_ASSIGN      = 0xD3,  /**< *= */
    SERAPH_TOK_SLASH_ASSIGN     = 0xD4,  /**< /= */
    SERAPH_TOK_PERCENT_ASSIGN   = 0xD5,  /**< %= */
    SERAPH_TOK_AND_ASSIGN       = 0xD6,  /**< &= */
    SERAPH_TOK_OR_ASSIGN        = 0xD7,  /**< |= */
    SERAPH_TOK_XOR_ASSIGN       = 0xD8,  /**< ^= */

    /*--------------------------------------------------------------------
     * Delimiters (0xE0-0xEF)
     *--------------------------------------------------------------------*/
    SERAPH_TOK_LPAREN           = 0xE0,  /**< ( */
    SERAPH_TOK_RPAREN           = 0xE1,  /**< ) */
    SERAPH_TOK_LBRACE           = 0xE2,  /**< { */
    SERAPH_TOK_RBRACE           = 0xE3,  /**< } */
    SERAPH_TOK_LBRACKET         = 0xE4,  /**< [ */
    SERAPH_TOK_RBRACKET         = 0xE5,  /**< ] */
    SERAPH_TOK_SEMICOLON        = 0xE6,  /**< ; */
    SERAPH_TOK_COLON            = 0xE7,  /**< : */
    SERAPH_TOK_COMMA            = 0xE8,  /**< , */
    SERAPH_TOK_DOT              = 0xE9,  /**< . */
    SERAPH_TOK_AMPERSAND        = 0xEA,  /**< & (reference) */
    SERAPH_TOK_AT               = 0xEB,  /**< @ */
    SERAPH_TOK_HASH             = 0xEC,  /**< # */

    /*--------------------------------------------------------------------
     * Special (0xF0-0xFF)
     *--------------------------------------------------------------------*/
    SERAPH_TOK_IDENT            = 0xF0,  /**< Identifier */
    SERAPH_TOK_EOF              = 0xFE,  /**< End of file */
    SERAPH_TOK_VOID             = 0xFF   /**< Invalid/error token */

} Seraph_Token_Type;

/*============================================================================
 * Numeric Literal Suffix
 *============================================================================*/

/**
 * @brief Suffix for numeric literals
 *
 * Determines the type of a numeric literal.
 */
typedef enum {
    SERAPH_NUM_SUFFIX_NONE      = 0x00,  /**< No suffix (infer type) */
    SERAPH_NUM_SUFFIX_U         = 0x01,  /**< u - unsigned 64-bit */
    SERAPH_NUM_SUFFIX_I         = 0x02,  /**< i - signed 64-bit */
    SERAPH_NUM_SUFFIX_U8        = 0x03,  /**< u8 */
    SERAPH_NUM_SUFFIX_U16       = 0x04,  /**< u16 */
    SERAPH_NUM_SUFFIX_U32       = 0x05,  /**< u32 */
    SERAPH_NUM_SUFFIX_U64       = 0x06,  /**< u64 */
    SERAPH_NUM_SUFFIX_I8        = 0x07,  /**< i8 */
    SERAPH_NUM_SUFFIX_I16       = 0x08,  /**< i16 */
    SERAPH_NUM_SUFFIX_I32       = 0x09,  /**< i32 */
    SERAPH_NUM_SUFFIX_I64       = 0x0A,  /**< i64 */
    SERAPH_NUM_SUFFIX_S         = 0x0B,  /**< s - scalar (Q32.32) */
    SERAPH_NUM_SUFFIX_D         = 0x0C,  /**< d - dual (128-bit AD) */
    SERAPH_NUM_SUFFIX_G         = 0x0D,  /**< g - galactic (256-bit AD) */
    SERAPH_NUM_SUFFIX_VOID      = 0xFF   /**< Invalid suffix */
} Seraph_Num_Suffix;

/*============================================================================
 * Source Location
 *============================================================================*/

/**
 * @brief Location in source code
 *
 * Tracks where a token came from for error reporting.
 */
typedef struct {
    const char* filename;   /**< Source file name (interned) */
    uint32_t line;          /**< Line number (1-based) */
    uint32_t column;        /**< Column number (1-based) */
    uint32_t offset;        /**< Byte offset from start of file */
} Seraph_Source_Loc;

/*============================================================================
 * Token Structure
 *============================================================================*/

/**
 * @brief A single token from the lexer
 *
 * Contains the token type, source location, and value.
 * Strings and identifiers point into the source buffer
 * (zero-copy for performance).
 */
typedef struct {
    Seraph_Token_Type type;     /**< Token type */
    Seraph_Source_Loc loc;      /**< Source location */

    const char* lexeme;         /**< Pointer into source (NOT null-terminated) */
    size_t lexeme_len;          /**< Length of lexeme */

    /** Value for literals */
    union {
        uint64_t int_value;             /**< Integer literal value */
        double float_value;             /**< Float literal value (before conversion) */
        Seraph_Q128 q128_value;         /**< Q128 fixed-point value */
        struct {
            const char* str;            /**< String content (escaped) */
            size_t len;                 /**< String length */
        } string_value;
        char char_value;                /**< Character literal value */
    } value;

    Seraph_Num_Suffix num_suffix;  /**< Numeric suffix (for literals) */

} Seraph_Token;

/*============================================================================
 * Token Utilities
 *============================================================================*/

/**
 * @brief Create a VOID token (for errors)
 */
SERAPH_INLINE Seraph_Token seraph_token_void(Seraph_Source_Loc loc) {
    Seraph_Token tok = {0};
    tok.type = SERAPH_TOK_VOID;
    tok.loc = loc;
    tok.lexeme = NULL;
    tok.lexeme_len = 0;
    return tok;
}

/**
 * @brief Check if a token is VOID (error)
 */
SERAPH_INLINE int seraph_token_is_void(const Seraph_Token* tok) {
    return tok == NULL || tok->type == SERAPH_TOK_VOID;
}

/**
 * @brief Check if a token is EOF
 */
SERAPH_INLINE int seraph_token_is_eof(const Seraph_Token* tok) {
    return tok != NULL && tok->type == SERAPH_TOK_EOF;
}

/**
 * @brief Get the name of a token type (for error messages)
 */
const char* seraph_token_type_name(Seraph_Token_Type type);

/**
 * @brief Check if a token type is a keyword
 */
int seraph_token_is_keyword(Seraph_Token_Type type);

/**
 * @brief Check if a token type is an operator
 */
int seraph_token_is_operator(Seraph_Token_Type type);

/**
 * @brief Check if a token type is a literal
 */
int seraph_token_is_literal(Seraph_Token_Type type);

/**
 * @brief Get operator precedence (higher = binds tighter)
 *
 * Returns 0 for non-operators.
 */
int seraph_token_precedence(Seraph_Token_Type type);

/**
 * @brief Check if operator is right-associative
 */
int seraph_token_is_right_assoc(Seraph_Token_Type type);

#endif /* SERAPH_SERAPHIM_TOKEN_H */
