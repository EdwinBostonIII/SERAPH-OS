/**
 * @file lexer.c
 * @brief Seraphim Compiler - Lexer Implementation
 *
 * MC26: Seraphim Language Lexer
 *
 * Converts source text into tokens using SERAPH primitives.
 */

#include "seraph/seraphim/lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

/*============================================================================
 * Internal Helpers
 *============================================================================*/

/** Initial token array capacity */
#define INITIAL_TOKEN_CAPACITY 256

/** Check if character is identifier start */
static int is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

/** Check if character is identifier continuation */
static int is_ident_cont(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

/** Check if character is digit */
static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

/** Check if character is hex digit */
static int is_hex_digit(char c) {
    return is_digit(c) ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

/** Check if character is binary digit */
static int is_binary_digit(char c) {
    return c == '0' || c == '1';
}

/** Check if character is whitespace */
static int is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/*============================================================================
 * Lexer Helpers
 *============================================================================*/

/** Get current character */
static char current(const Seraph_Lexer* lexer) {
    if (lexer->pos >= lexer->source_len) return '\0';
    return lexer->source[lexer->pos];
}

/** Peek at character at offset */
static char peek_at(const Seraph_Lexer* lexer, size_t offset) {
    size_t pos = lexer->pos + offset;
    if (pos >= lexer->source_len) return '\0';
    return lexer->source[pos];
}

/** Advance position by n characters */
static void advance(Seraph_Lexer* lexer, size_t n) {
    for (size_t i = 0; i < n && lexer->pos < lexer->source_len; i++) {
        if (lexer->source[lexer->pos] == '\n') {
            lexer->line++;
            lexer->column = 1;
        } else {
            lexer->column++;
        }
        lexer->pos++;
    }
}

/** Get current source location */
static Seraph_Source_Loc current_loc(const Seraph_Lexer* lexer) {
    Seraph_Source_Loc loc;
    loc.filename = lexer->filename;
    loc.line = lexer->line;
    loc.column = lexer->column;
    loc.offset = (uint32_t)lexer->pos;
    return loc;
}

/** Skip whitespace and comments */
static void skip_whitespace_and_comments(Seraph_Lexer* lexer) {
    while (lexer->pos < lexer->source_len) {
        char c = current(lexer);

        /* Whitespace */
        if (is_whitespace(c)) {
            advance(lexer, 1);
            continue;
        }

        /* Line comment: // */
        if (c == '/' && peek_at(lexer, 1) == '/') {
            advance(lexer, 2);
            while (lexer->pos < lexer->source_len && current(lexer) != '\n') {
                advance(lexer, 1);
            }
            continue;
        }

        /* Block comment */
        if (c == '/' && peek_at(lexer, 1) == '*') {
            Seraph_Source_Loc start = current_loc(lexer);
            advance(lexer, 2);
            int depth = 1;  /* Nested comments */
            while (lexer->pos < lexer->source_len && depth > 0) {
                if (current(lexer) == '/' && peek_at(lexer, 1) == '*') {
                    depth++;
                    advance(lexer, 2);
                } else if (current(lexer) == '*' && peek_at(lexer, 1) == '/') {
                    depth--;
                    advance(lexer, 2);
                } else {
                    advance(lexer, 1);
                }
            }
            if (depth > 0) {
                seraph_lexer_error(lexer, start, "unterminated block comment");
            }
            continue;
        }

        break;  /* Not whitespace or comment */
    }
}

/*============================================================================
 * Keyword Table
 *============================================================================*/

typedef struct {
    const char* name;
    Seraph_Token_Type type;
} Keyword_Entry;

static const Keyword_Entry keywords[] = {
    /* Control flow */
    {"fn",       SERAPH_TOK_FN},
    {"let",      SERAPH_TOK_LET},
    {"mut",      SERAPH_TOK_MUT},
    {"if",       SERAPH_TOK_IF},
    {"else",     SERAPH_TOK_ELSE},
    {"for",      SERAPH_TOK_FOR},
    {"while",    SERAPH_TOK_WHILE},
    {"return",   SERAPH_TOK_RETURN},
    {"match",    SERAPH_TOK_MATCH},
    {"in",       SERAPH_TOK_IN},
    {"break",    SERAPH_TOK_BREAK},
    {"continue", SERAPH_TOK_CONTINUE},
    {"as",       SERAPH_TOK_AS},

    /* Declarations */
    {"struct",   SERAPH_TOK_STRUCT},
    {"enum",     SERAPH_TOK_ENUM},
    {"const",    SERAPH_TOK_CONST},
    {"use",      SERAPH_TOK_USE},
    {"foreign",  SERAPH_TOK_FOREIGN},
    {"type",     SERAPH_TOK_TYPE},
    {"impl",     SERAPH_TOK_IMPL},

    /* Substrate blocks */
    {"persist",  SERAPH_TOK_PERSIST},
    {"aether",   SERAPH_TOK_AETHER_BLOCK},
    {"recover",  SERAPH_TOK_RECOVER},
    {"effects",  SERAPH_TOK_EFFECTS},

    /* Effects */
    {"pure",     SERAPH_TOK_PURE},
    {"VOID",     SERAPH_TOK_EFFECT_VOID},
    {"PERSIST",  SERAPH_TOK_EFFECT_PERSIST},
    {"NETWORK",  SERAPH_TOK_EFFECT_NETWORK},
    {"TIMER",    SERAPH_TOK_EFFECT_TIMER},
    {"IO",       SERAPH_TOK_EFFECT_IO},

    /* Primitive types */
    {"u8",       SERAPH_TOK_U8},
    {"u16",      SERAPH_TOK_U16},
    {"u32",      SERAPH_TOK_U32},
    {"u64",      SERAPH_TOK_U64},
    {"i8",       SERAPH_TOK_I8},
    {"i16",      SERAPH_TOK_I16},
    {"i32",      SERAPH_TOK_I32},
    {"i64",      SERAPH_TOK_I64},
    {"bool",     SERAPH_TOK_BOOL},
    {"char",     SERAPH_TOK_CHAR},
    {"f32",      SERAPH_TOK_F32},
    {"f64",      SERAPH_TOK_F64},

    /* Numeric types */
    {"scalar",   SERAPH_TOK_SCALAR},
    {"dual",     SERAPH_TOK_DUAL},
    {"galactic", SERAPH_TOK_GALACTIC},

    /* Substrate types */
    {"volatile", SERAPH_TOK_VOLATILE},
    {"atlas",    SERAPH_TOK_ATLAS},

    /* Literals */
    {"true",     SERAPH_TOK_TRUE},
    {"false",    SERAPH_TOK_FALSE},
    {"void",     SERAPH_TOK_VOID_LIT},
    {"null",     SERAPH_TOK_VOID_LIT},  /* null == VOID for pointers */

    {NULL, SERAPH_TOK_VOID}
};

Seraph_Token_Type seraph_lexer_lookup_keyword(const char* name, size_t len) {
    for (const Keyword_Entry* entry = keywords; entry->name != NULL; entry++) {
        if (strlen(entry->name) == len && memcmp(entry->name, name, len) == 0) {
            return entry->type;
        }
    }
    return SERAPH_TOK_IDENT;
}

/*============================================================================
 * Token Scanning
 *============================================================================*/

/** Scan an identifier or keyword */
static Seraph_Token scan_identifier(Seraph_Lexer* lexer) {
    Seraph_Source_Loc loc = current_loc(lexer);
    const char* start = &lexer->source[lexer->pos];
    size_t len = 0;

    while (lexer->pos < lexer->source_len && is_ident_cont(current(lexer))) {
        advance(lexer, 1);
        len++;
    }

    Seraph_Token tok;
    tok.loc = loc;
    tok.lexeme = start;
    tok.lexeme_len = len;
    tok.type = seraph_lexer_lookup_keyword(start, len);
    tok.num_suffix = SERAPH_NUM_SUFFIX_NONE;

    return tok;
}

/** Parse numeric suffix */
static Seraph_Num_Suffix parse_num_suffix(Seraph_Lexer* lexer) {
    char c = current(lexer);
    char c2 = peek_at(lexer, 1);

    /* Check for type suffixes like u8, i32, etc. */
    if (c == 'u' || c == 'i') {
        if (c2 == '8') {
            advance(lexer, 2);
            return c == 'u' ? SERAPH_NUM_SUFFIX_U8 : SERAPH_NUM_SUFFIX_I8;
        }
        if (c2 == '1' && peek_at(lexer, 2) == '6') {
            advance(lexer, 3);
            return c == 'u' ? SERAPH_NUM_SUFFIX_U16 : SERAPH_NUM_SUFFIX_I16;
        }
        if (c2 == '3' && peek_at(lexer, 2) == '2') {
            advance(lexer, 3);
            return c == 'u' ? SERAPH_NUM_SUFFIX_U32 : SERAPH_NUM_SUFFIX_I32;
        }
        if (c2 == '6' && peek_at(lexer, 2) == '4') {
            advance(lexer, 3);
            return c == 'u' ? SERAPH_NUM_SUFFIX_U64 : SERAPH_NUM_SUFFIX_I64;
        }
        /* Just 'u' or 'i' alone */
        if (!is_ident_cont(c2)) {
            advance(lexer, 1);
            return c == 'u' ? SERAPH_NUM_SUFFIX_U : SERAPH_NUM_SUFFIX_I;
        }
    }

    /* Floating-point suffixes */
    if (c == 's' && !is_ident_cont(c2)) {
        advance(lexer, 1);
        return SERAPH_NUM_SUFFIX_S;
    }
    if (c == 'd' && !is_ident_cont(c2)) {
        advance(lexer, 1);
        return SERAPH_NUM_SUFFIX_D;
    }
    if (c == 'g' && !is_ident_cont(c2)) {
        advance(lexer, 1);
        return SERAPH_NUM_SUFFIX_G;
    }

    return SERAPH_NUM_SUFFIX_NONE;
}

/** Scan a number literal */
static Seraph_Token scan_number(Seraph_Lexer* lexer) {
    Seraph_Source_Loc loc = current_loc(lexer);
    const char* start = &lexer->source[lexer->pos];
    int is_float = 0;
    int base = 10;

    /* Check for base prefix */
    if (current(lexer) == '0') {
        char next = peek_at(lexer, 1);
        if (next == 'x' || next == 'X') {
            base = 16;
            advance(lexer, 2);
        } else if (next == 'b' || next == 'B') {
            base = 2;
            advance(lexer, 2);
        } else if (next == 'o' || next == 'O') {
            base = 8;
            advance(lexer, 2);
        }
    }

    /* Scan integer part */
    if (base == 16) {
        while (is_hex_digit(current(lexer)) || current(lexer) == '_') {
            advance(lexer, 1);
        }
    } else if (base == 2) {
        while (is_binary_digit(current(lexer)) || current(lexer) == '_') {
            advance(lexer, 1);
        }
    } else if (base == 8) {
        while ((current(lexer) >= '0' && current(lexer) <= '7') || current(lexer) == '_') {
            advance(lexer, 1);
        }
    } else {
        while (is_digit(current(lexer)) || current(lexer) == '_') {
            advance(lexer, 1);
        }
    }

    /* Check for decimal point (only for base 10) */
    if (base == 10 && current(lexer) == '.' && is_digit(peek_at(lexer, 1))) {
        is_float = 1;
        advance(lexer, 1);  /* Consume '.' */
        while (is_digit(current(lexer)) || current(lexer) == '_') {
            advance(lexer, 1);
        }
    }

    /* Check for exponent (only for base 10) */
    if (base == 10 && (current(lexer) == 'e' || current(lexer) == 'E')) {
        is_float = 1;
        advance(lexer, 1);
        if (current(lexer) == '+' || current(lexer) == '-') {
            advance(lexer, 1);
        }
        if (!is_digit(current(lexer))) {
            seraph_lexer_error(lexer, loc, "expected exponent digits");
        }
        while (is_digit(current(lexer)) || current(lexer) == '_') {
            advance(lexer, 1);
        }
    }

    size_t len = &lexer->source[lexer->pos] - start;

    /* Parse suffix */
    Seraph_Num_Suffix suffix = parse_num_suffix(lexer);

    /* Float suffixes make it a float */
    if (suffix == SERAPH_NUM_SUFFIX_S ||
        suffix == SERAPH_NUM_SUFFIX_D ||
        suffix == SERAPH_NUM_SUFFIX_G) {
        is_float = 1;
    }

    Seraph_Token tok;
    tok.loc = loc;
    tok.lexeme = start;
    tok.lexeme_len = len;
    tok.type = is_float ? SERAPH_TOK_FLOAT_LITERAL : SERAPH_TOK_INT_LITERAL;
    tok.num_suffix = suffix;

    /* Parse actual value (simplified - real impl would be more robust) */
    if (is_float) {
        tok.value.float_value = strtod(start, NULL);
    } else {
        const char* numstart = start;
        /* Skip prefix for strtoull - it doesn't understand 0b on all platforms */
        if (base == 2 && numstart[0] == '0' && (numstart[1] == 'b' || numstart[1] == 'B')) {
            numstart += 2;
        } else if (base == 8 && numstart[0] == '0' && (numstart[1] == 'o' || numstart[1] == 'O')) {
            numstart += 2;
        }
        /* Note: 0x is handled by strtoull with base 16 */
        tok.value.int_value = strtoull(numstart, NULL, base);
    }

    return tok;
}

/** Scan a string literal */
static Seraph_Token scan_string(Seraph_Lexer* lexer) {
    Seraph_Source_Loc loc = current_loc(lexer);
    advance(lexer, 1);  /* Consume opening quote */

    const char* start = &lexer->source[lexer->pos];
    size_t len = 0;

    while (lexer->pos < lexer->source_len && current(lexer) != '"') {
        if (current(lexer) == '\\') {
            advance(lexer, 1);  /* Skip escape char */
            if (lexer->pos < lexer->source_len) {
                advance(lexer, 1);  /* Skip escaped char */
                len += 2;
            }
        } else if (current(lexer) == '\n') {
            seraph_lexer_error(lexer, loc, "unterminated string literal");
            break;
        } else {
            advance(lexer, 1);
            len++;
        }
    }

    if (current(lexer) != '"') {
        seraph_lexer_error(lexer, loc, "unterminated string literal");
    } else {
        advance(lexer, 1);  /* Consume closing quote */
    }

    Seraph_Token tok;
    tok.loc = loc;
    tok.lexeme = start - 1;  /* Include opening quote */
    tok.lexeme_len = len + 2;  /* Include quotes */
    tok.type = SERAPH_TOK_STRING_LITERAL;
    tok.value.string_value.str = start;
    tok.value.string_value.len = len;
    tok.num_suffix = SERAPH_NUM_SUFFIX_NONE;

    return tok;
}

/** Scan a character literal */
static Seraph_Token scan_char(Seraph_Lexer* lexer) {
    Seraph_Source_Loc loc = current_loc(lexer);
    advance(lexer, 1);  /* Consume opening quote */

    char value = '\0';

    if (current(lexer) == '\\') {
        advance(lexer, 1);
        switch (current(lexer)) {
            case 'n':  value = '\n'; break;
            case 'r':  value = '\r'; break;
            case 't':  value = '\t'; break;
            case '\\': value = '\\'; break;
            case '\'': value = '\''; break;
            case '0':  value = '\0'; break;
            default:
                seraph_lexer_error(lexer, loc, "unknown escape sequence '\\%c'", current(lexer));
                value = current(lexer);
        }
        advance(lexer, 1);
    } else if (current(lexer) != '\'' && current(lexer) != '\n') {
        value = current(lexer);
        advance(lexer, 1);
    }

    if (current(lexer) != '\'') {
        seraph_lexer_error(lexer, loc, "unterminated character literal");
    } else {
        advance(lexer, 1);
    }

    Seraph_Token tok;
    tok.loc = loc;
    tok.lexeme = &lexer->source[loc.offset];
    tok.lexeme_len = lexer->pos - loc.offset;
    tok.type = SERAPH_TOK_CHAR_LITERAL;
    tok.value.char_value = value;
    tok.num_suffix = SERAPH_NUM_SUFFIX_NONE;

    return tok;
}

/*============================================================================
 * Main Lexer Functions
 *============================================================================*/

Seraph_Vbit seraph_lexer_init(
    Seraph_Lexer* lexer,
    const char* source,
    size_t source_len,
    const char* filename,
    Seraph_Arena* arena
) {
    if (lexer == NULL || source == NULL || arena == NULL) {
        return SERAPH_VBIT_VOID;
    }

    memset(lexer, 0, sizeof(Seraph_Lexer));

    lexer->source = source;
    lexer->source_len = source_len;
    lexer->filename = filename ? filename : "<input>";
    lexer->arena = arena;

    lexer->pos = 0;
    lexer->line = 1;
    lexer->column = 1;

    /* Allocate initial token array */
    lexer->token_capacity = INITIAL_TOKEN_CAPACITY;
    lexer->tokens = (Seraph_Token*)seraph_arena_alloc(
        arena,
        lexer->token_capacity * sizeof(Seraph_Token),
        _Alignof(Seraph_Token)
    );

    if (lexer->tokens == NULL) {
        return SERAPH_VBIT_VOID;
    }

    lexer->token_count = 0;
    lexer->diagnostics = NULL;
    lexer->error_count = 0;
    lexer->warning_count = 0;
    lexer->has_error = 0;

    return SERAPH_VBIT_TRUE;
}

/** Add a token to the array */
static void add_token(Seraph_Lexer* lexer, Seraph_Token tok) {
    /* Grow array if needed */
    if (lexer->token_count >= lexer->token_capacity) {
        size_t new_cap = lexer->token_capacity * 2;
        Seraph_Token* new_tokens = (Seraph_Token*)seraph_arena_alloc(
            lexer->arena,
            new_cap * sizeof(Seraph_Token),
            _Alignof(Seraph_Token)
        );
        if (new_tokens == NULL) {
            lexer->has_error = 1;
            return;
        }
        memcpy(new_tokens, lexer->tokens, lexer->token_count * sizeof(Seraph_Token));
        lexer->tokens = new_tokens;
        lexer->token_capacity = new_cap;
    }

    lexer->tokens[lexer->token_count++] = tok;
}

Seraph_Token seraph_lexer_next_token(Seraph_Lexer* lexer) {
    skip_whitespace_and_comments(lexer);

    if (lexer->pos >= lexer->source_len) {
        Seraph_Token tok;
        tok.type = SERAPH_TOK_EOF;
        tok.loc = current_loc(lexer);
        tok.lexeme = NULL;
        tok.lexeme_len = 0;
        tok.num_suffix = SERAPH_NUM_SUFFIX_NONE;
        return tok;
    }

    Seraph_Source_Loc loc = current_loc(lexer);
    char c = current(lexer);

    /* Identifier or keyword */
    if (is_ident_start(c)) {
        return scan_identifier(lexer);
    }

    /* Number */
    if (is_digit(c)) {
        return scan_number(lexer);
    }

    /* String */
    if (c == '"') {
        return scan_string(lexer);
    }

    /* Character */
    if (c == '\'') {
        return scan_char(lexer);
    }

    /* Operators and delimiters */
    Seraph_Token tok;
    tok.loc = loc;
    tok.num_suffix = SERAPH_NUM_SUFFIX_NONE;

    switch (c) {
        /* Multi-char operators */
        case '?':
            if (peek_at(lexer, 1) == '?') {
                advance(lexer, 2);
                tok.type = SERAPH_TOK_VOID_PROP;
                tok.lexeme = &lexer->source[loc.offset];
                tok.lexeme_len = 2;
                return tok;
            }
            break;  /* Single ? is an error */

        case '!':
            if (peek_at(lexer, 1) == '!') {
                advance(lexer, 2);
                tok.type = SERAPH_TOK_VOID_ASSERT;
            } else if (peek_at(lexer, 1) == '=') {
                advance(lexer, 2);
                tok.type = SERAPH_TOK_NE;
            } else {
                advance(lexer, 1);
                tok.type = SERAPH_TOK_NOT;
            }
            tok.lexeme = &lexer->source[loc.offset];
            tok.lexeme_len = lexer->pos - loc.offset;
            return tok;

        case '|':
            if (peek_at(lexer, 1) == '>') {
                advance(lexer, 2);
                tok.type = SERAPH_TOK_PIPE;
            } else if (peek_at(lexer, 1) == '|') {
                advance(lexer, 2);
                tok.type = SERAPH_TOK_OR;
            } else if (peek_at(lexer, 1) == '=') {
                advance(lexer, 2);
                tok.type = SERAPH_TOK_OR_ASSIGN;
            } else {
                advance(lexer, 1);
                tok.type = SERAPH_TOK_BIT_OR;
            }
            tok.lexeme = &lexer->source[loc.offset];
            tok.lexeme_len = lexer->pos - loc.offset;
            return tok;

        case '&':
            if (peek_at(lexer, 1) == '&') {
                advance(lexer, 2);
                tok.type = SERAPH_TOK_AND;
            } else if (peek_at(lexer, 1) == '=') {
                advance(lexer, 2);
                tok.type = SERAPH_TOK_AND_ASSIGN;
            } else {
                advance(lexer, 1);
                tok.type = SERAPH_TOK_BIT_AND;  /* Bitwise AND in expressions, ref operator in types */
            }
            tok.lexeme = &lexer->source[loc.offset];
            tok.lexeme_len = lexer->pos - loc.offset;
            return tok;

        case '=':
            if (peek_at(lexer, 1) == '=') {
                advance(lexer, 2);
                tok.type = SERAPH_TOK_EQ;
            } else if (peek_at(lexer, 1) == '>') {
                advance(lexer, 2);
                tok.type = SERAPH_TOK_FAT_ARROW;
            } else {
                advance(lexer, 1);
                tok.type = SERAPH_TOK_ASSIGN;
            }
            tok.lexeme = &lexer->source[loc.offset];
            tok.lexeme_len = lexer->pos - loc.offset;
            return tok;

        case '-':
            if (peek_at(lexer, 1) == '>') {
                advance(lexer, 2);
                tok.type = SERAPH_TOK_ARROW;
            } else if (peek_at(lexer, 1) == '=') {
                advance(lexer, 2);
                tok.type = SERAPH_TOK_MINUS_ASSIGN;
            } else {
                advance(lexer, 1);
                tok.type = SERAPH_TOK_MINUS;
            }
            tok.lexeme = &lexer->source[loc.offset];
            tok.lexeme_len = lexer->pos - loc.offset;
            return tok;

        case '+':
            if (peek_at(lexer, 1) == '=') {
                advance(lexer, 2);
                tok.type = SERAPH_TOK_PLUS_ASSIGN;
            } else {
                advance(lexer, 1);
                tok.type = SERAPH_TOK_PLUS;
            }
            tok.lexeme = &lexer->source[loc.offset];
            tok.lexeme_len = lexer->pos - loc.offset;
            return tok;

        case '*':
            if (peek_at(lexer, 1) == '=') {
                advance(lexer, 2);
                tok.type = SERAPH_TOK_STAR_ASSIGN;
            } else {
                advance(lexer, 1);
                tok.type = SERAPH_TOK_STAR;
            }
            tok.lexeme = &lexer->source[loc.offset];
            tok.lexeme_len = lexer->pos - loc.offset;
            return tok;

        case '/':
            if (peek_at(lexer, 1) == '=') {
                advance(lexer, 2);
                tok.type = SERAPH_TOK_SLASH_ASSIGN;
            } else {
                advance(lexer, 1);
                tok.type = SERAPH_TOK_SLASH;
            }
            tok.lexeme = &lexer->source[loc.offset];
            tok.lexeme_len = lexer->pos - loc.offset;
            return tok;

        case '%':
            if (peek_at(lexer, 1) == '=') {
                advance(lexer, 2);
                tok.type = SERAPH_TOK_PERCENT_ASSIGN;
            } else {
                advance(lexer, 1);
                tok.type = SERAPH_TOK_PERCENT;
            }
            tok.lexeme = &lexer->source[loc.offset];
            tok.lexeme_len = lexer->pos - loc.offset;
            return tok;

        case '<':
            if (peek_at(lexer, 1) == '<') {
                advance(lexer, 2);
                tok.type = SERAPH_TOK_SHL;
            } else if (peek_at(lexer, 1) == '=') {
                advance(lexer, 2);
                tok.type = SERAPH_TOK_LE;
            } else {
                advance(lexer, 1);
                tok.type = SERAPH_TOK_LT;
            }
            tok.lexeme = &lexer->source[loc.offset];
            tok.lexeme_len = lexer->pos - loc.offset;
            return tok;

        case '>':
            if (peek_at(lexer, 1) == '>') {
                advance(lexer, 2);
                tok.type = SERAPH_TOK_SHR;
            } else if (peek_at(lexer, 1) == '=') {
                advance(lexer, 2);
                tok.type = SERAPH_TOK_GE;
            } else {
                advance(lexer, 1);
                tok.type = SERAPH_TOK_GT;
            }
            tok.lexeme = &lexer->source[loc.offset];
            tok.lexeme_len = lexer->pos - loc.offset;
            return tok;

        case ':':
            if (peek_at(lexer, 1) == ':') {
                advance(lexer, 2);
                tok.type = SERAPH_TOK_DOUBLE_COLON;
            } else {
                advance(lexer, 1);
                tok.type = SERAPH_TOK_COLON;
            }
            tok.lexeme = &lexer->source[loc.offset];
            tok.lexeme_len = lexer->pos - loc.offset;
            return tok;

        case '.':
            if (peek_at(lexer, 1) == '.') {
                if (peek_at(lexer, 2) == '=') {
                    advance(lexer, 3);
                    tok.type = SERAPH_TOK_RANGE_INCL;
                } else {
                    advance(lexer, 2);
                    tok.type = SERAPH_TOK_RANGE;
                }
            } else {
                advance(lexer, 1);
                tok.type = SERAPH_TOK_DOT;
            }
            tok.lexeme = &lexer->source[loc.offset];
            tok.lexeme_len = lexer->pos - loc.offset;
            return tok;

        case '^':
            if (peek_at(lexer, 1) == '=') {
                advance(lexer, 2);
                tok.type = SERAPH_TOK_XOR_ASSIGN;
            } else {
                advance(lexer, 1);
                tok.type = SERAPH_TOK_BIT_XOR;
            }
            tok.lexeme = &lexer->source[loc.offset];
            tok.lexeme_len = lexer->pos - loc.offset;
            return tok;

        case '~':
            advance(lexer, 1);
            tok.type = SERAPH_TOK_BIT_NOT;
            tok.lexeme = &lexer->source[loc.offset];
            tok.lexeme_len = 1;
            return tok;

        /* Single-char delimiters */
        case '(':
            advance(lexer, 1);
            tok.type = SERAPH_TOK_LPAREN;
            tok.lexeme = &lexer->source[loc.offset];
            tok.lexeme_len = 1;
            return tok;

        case ')':
            advance(lexer, 1);
            tok.type = SERAPH_TOK_RPAREN;
            tok.lexeme = &lexer->source[loc.offset];
            tok.lexeme_len = 1;
            return tok;

        case '{':
            advance(lexer, 1);
            tok.type = SERAPH_TOK_LBRACE;
            tok.lexeme = &lexer->source[loc.offset];
            tok.lexeme_len = 1;
            return tok;

        case '}':
            advance(lexer, 1);
            tok.type = SERAPH_TOK_RBRACE;
            tok.lexeme = &lexer->source[loc.offset];
            tok.lexeme_len = 1;
            return tok;

        case '[':
            advance(lexer, 1);
            tok.type = SERAPH_TOK_LBRACKET;
            tok.lexeme = &lexer->source[loc.offset];
            tok.lexeme_len = 1;
            return tok;

        case ']':
            advance(lexer, 1);
            tok.type = SERAPH_TOK_RBRACKET;
            tok.lexeme = &lexer->source[loc.offset];
            tok.lexeme_len = 1;
            return tok;

        case ';':
            advance(lexer, 1);
            tok.type = SERAPH_TOK_SEMICOLON;
            tok.lexeme = &lexer->source[loc.offset];
            tok.lexeme_len = 1;
            return tok;

        case ',':
            advance(lexer, 1);
            tok.type = SERAPH_TOK_COMMA;
            tok.lexeme = &lexer->source[loc.offset];
            tok.lexeme_len = 1;
            return tok;

        case '@':
            advance(lexer, 1);
            tok.type = SERAPH_TOK_AT;
            tok.lexeme = &lexer->source[loc.offset];
            tok.lexeme_len = 1;
            return tok;

        case '#':
            advance(lexer, 1);
            tok.type = SERAPH_TOK_HASH;
            tok.lexeme = &lexer->source[loc.offset];
            tok.lexeme_len = 1;
            return tok;

        default:
            break;
    }

    /* Unknown character */
    seraph_lexer_error(lexer, loc, "unexpected character '%c' (0x%02X)", c, (unsigned char)c);
    advance(lexer, 1);
    return seraph_token_void(loc);
}

Seraph_Vbit seraph_lexer_tokenize(Seraph_Lexer* lexer) {
    if (lexer == NULL) {
        return SERAPH_VBIT_VOID;
    }

    while (1) {
        Seraph_Token tok = seraph_lexer_next_token(lexer);
        add_token(lexer, tok);

        if (tok.type == SERAPH_TOK_EOF || tok.type == SERAPH_TOK_VOID) {
            break;
        }
    }

    return lexer->error_count == 0 ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

/*============================================================================
 * Diagnostics
 *============================================================================*/

void seraph_lexer_error(
    Seraph_Lexer* lexer,
    Seraph_Source_Loc loc,
    const char* format,
    ...
) {
    if (lexer == NULL) return;

    /* Allocate diagnostic from arena */
    Seraph_Lexer_Diag* diag = (Seraph_Lexer_Diag*)seraph_arena_alloc(
        lexer->arena,
        sizeof(Seraph_Lexer_Diag),
        _Alignof(Seraph_Lexer_Diag)
    );
    if (diag == NULL) {
        lexer->has_error = 1;
        return;
    }

    diag->loc = loc;
    diag->is_error = 1;
    diag->next = lexer->diagnostics;
    lexer->diagnostics = diag;
    lexer->error_count++;
    lexer->has_error = 1;

    /* Format message */
    va_list args;
    va_start(args, format);
    int len = vsnprintf(NULL, 0, format, args);
    va_end(args);

    char* msg = (char*)seraph_arena_alloc(lexer->arena, len + 1, 1);
    if (msg != NULL) {
        va_start(args, format);
        vsnprintf(msg, len + 1, format, args);
        va_end(args);
        diag->message = msg;
    } else {
        diag->message = format;
    }
}

void seraph_lexer_warning(
    Seraph_Lexer* lexer,
    Seraph_Source_Loc loc,
    const char* format,
    ...
) {
    if (lexer == NULL) return;

    Seraph_Lexer_Diag* diag = (Seraph_Lexer_Diag*)seraph_arena_alloc(
        lexer->arena,
        sizeof(Seraph_Lexer_Diag),
        _Alignof(Seraph_Lexer_Diag)
    );
    if (diag == NULL) return;

    diag->loc = loc;
    diag->is_error = 0;
    diag->next = lexer->diagnostics;
    lexer->diagnostics = diag;
    lexer->warning_count++;

    va_list args;
    va_start(args, format);
    int len = vsnprintf(NULL, 0, format, args);
    va_end(args);

    char* msg = (char*)seraph_arena_alloc(lexer->arena, len + 1, 1);
    if (msg != NULL) {
        va_start(args, format);
        vsnprintf(msg, len + 1, format, args);
        va_end(args);
        diag->message = msg;
    } else {
        diag->message = format;
    }
}

void seraph_lexer_print_diagnostics(const Seraph_Lexer* lexer) {
    if (lexer == NULL) return;

    /* Print diagnostics (in reverse order - newest first) */
    for (Seraph_Lexer_Diag* d = lexer->diagnostics; d != NULL; d = d->next) {
        fprintf(stderr, "%s:%u:%u: %s: %s\n",
                d->loc.filename,
                d->loc.line,
                d->loc.column,
                d->is_error ? "error" : "warning",
                d->message);
    }

    if (lexer->error_count > 0) {
        fprintf(stderr, "%d error(s), %d warning(s)\n",
                lexer->error_count, lexer->warning_count);
    }
}

/*============================================================================
 * Token Access
 *============================================================================*/

Seraph_Token seraph_lexer_get_token(const Seraph_Lexer* lexer, size_t index) {
    if (lexer == NULL || index >= lexer->token_count) {
        Seraph_Source_Loc loc = {0};
        return seraph_token_void(loc);
    }
    return lexer->tokens[index];
}

char seraph_lexer_peek(const Seraph_Lexer* lexer) {
    return current(lexer);
}

char seraph_lexer_peek_at(const Seraph_Lexer* lexer, size_t offset) {
    return peek_at(lexer, offset);
}

int seraph_lexer_has_more(const Seraph_Lexer* lexer) {
    return lexer != NULL && lexer->pos < lexer->source_len;
}
