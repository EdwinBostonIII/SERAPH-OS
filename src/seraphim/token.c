/**
 * @file token.c
 * @brief Seraphim Compiler - Token Utilities Implementation
 *
 * MC26: Seraphim Language Token Utilities
 */

#include "seraph/seraphim/token.h"
#include <string.h>

/*============================================================================
 * Token Type Names
 *============================================================================*/

/**
 * @brief Get human-readable name for a token type
 */
const char* seraph_token_type_name(Seraph_Token_Type type) {
    switch (type) {
        /* Literals */
        case SERAPH_TOK_INT_LITERAL:    return "integer";
        case SERAPH_TOK_FLOAT_LITERAL:  return "float";
        case SERAPH_TOK_STRING_LITERAL: return "string";
        case SERAPH_TOK_CHAR_LITERAL:   return "char";
        case SERAPH_TOK_TRUE:           return "true";
        case SERAPH_TOK_FALSE:          return "false";
        case SERAPH_TOK_VOID_LIT:       return "VOID";

        /* Keywords - Control Flow */
        case SERAPH_TOK_FN:             return "fn";
        case SERAPH_TOK_LET:            return "let";
        case SERAPH_TOK_MUT:            return "mut";
        case SERAPH_TOK_IF:             return "if";
        case SERAPH_TOK_ELSE:           return "else";
        case SERAPH_TOK_FOR:            return "for";
        case SERAPH_TOK_WHILE:          return "while";
        case SERAPH_TOK_RETURN:         return "return";
        case SERAPH_TOK_MATCH:          return "match";
        case SERAPH_TOK_IN:             return "in";
        case SERAPH_TOK_BREAK:          return "break";
        case SERAPH_TOK_CONTINUE:       return "continue";

        /* Keywords - Declarations */
        case SERAPH_TOK_STRUCT:         return "struct";
        case SERAPH_TOK_ENUM:           return "enum";
        case SERAPH_TOK_CONST:          return "const";
        case SERAPH_TOK_USE:            return "use";
        case SERAPH_TOK_FOREIGN:        return "foreign";
        case SERAPH_TOK_TYPE:           return "type";
        case SERAPH_TOK_IMPL:           return "impl";

        /* Keywords - Substrate Blocks */
        case SERAPH_TOK_PERSIST:        return "persist";
        case SERAPH_TOK_AETHER_BLOCK:   return "aether";
        case SERAPH_TOK_RECOVER:        return "recover";
        case SERAPH_TOK_EFFECTS:        return "effects";

        /* Keywords - Effects */
        case SERAPH_TOK_PURE:           return "pure";
        case SERAPH_TOK_EFFECT_VOID:    return "VOID";
        case SERAPH_TOK_EFFECT_PERSIST: return "PERSIST";
        case SERAPH_TOK_EFFECT_NETWORK: return "NETWORK";
        case SERAPH_TOK_EFFECT_TIMER:   return "TIMER";
        case SERAPH_TOK_EFFECT_IO:      return "IO";

        /* Keywords - Primitive Types */
        case SERAPH_TOK_U8:             return "u8";
        case SERAPH_TOK_U16:            return "u16";
        case SERAPH_TOK_U32:            return "u32";
        case SERAPH_TOK_U64:            return "u64";
        case SERAPH_TOK_I8:             return "i8";
        case SERAPH_TOK_I16:            return "i16";
        case SERAPH_TOK_I32:            return "i32";
        case SERAPH_TOK_I64:            return "i64";
        case SERAPH_TOK_BOOL:           return "bool";
        case SERAPH_TOK_CHAR:           return "char";

        /* Keywords - Numeric Types */
        case SERAPH_TOK_SCALAR:         return "scalar";
        case SERAPH_TOK_DUAL:           return "dual";
        case SERAPH_TOK_GALACTIC:       return "galactic";

        /* Keywords - Substrate Types */
        case SERAPH_TOK_VOLATILE:       return "volatile";
        case SERAPH_TOK_ATLAS:          return "atlas";
        case SERAPH_TOK_AETHER:         return "aether";

        /* Operators - VOID */
        case SERAPH_TOK_VOID_PROP:      return "??";
        case SERAPH_TOK_VOID_ASSERT:    return "!!";

        /* Operators - Arrows & Pipes */
        case SERAPH_TOK_ARROW:          return "->";
        case SERAPH_TOK_FAT_ARROW:      return "=>";
        case SERAPH_TOK_PIPE:           return "|>";
        case SERAPH_TOK_DOUBLE_COLON:   return "::";
        case SERAPH_TOK_RANGE:          return "..";
        case SERAPH_TOK_RANGE_INCL:     return "..=";

        /* Operators - Arithmetic */
        case SERAPH_TOK_PLUS:           return "+";
        case SERAPH_TOK_MINUS:          return "-";
        case SERAPH_TOK_STAR:           return "*";
        case SERAPH_TOK_SLASH:          return "/";
        case SERAPH_TOK_PERCENT:        return "%";

        /* Operators - Comparison */
        case SERAPH_TOK_EQ:             return "==";
        case SERAPH_TOK_NE:             return "!=";
        case SERAPH_TOK_LT:             return "<";
        case SERAPH_TOK_GT:             return ">";
        case SERAPH_TOK_LE:             return "<=";
        case SERAPH_TOK_GE:             return ">=";

        /* Operators - Logical & Bitwise */
        case SERAPH_TOK_AND:            return "&&";
        case SERAPH_TOK_OR:             return "||";
        case SERAPH_TOK_NOT:            return "!";
        case SERAPH_TOK_BIT_AND:        return "&";
        case SERAPH_TOK_BIT_OR:         return "|";
        case SERAPH_TOK_BIT_XOR:        return "^";
        case SERAPH_TOK_BIT_NOT:        return "~";
        case SERAPH_TOK_SHL:            return "<<";
        case SERAPH_TOK_SHR:            return ">>";

        /* Operators - Assignment */
        case SERAPH_TOK_ASSIGN:         return "=";
        case SERAPH_TOK_PLUS_ASSIGN:    return "+=";
        case SERAPH_TOK_MINUS_ASSIGN:   return "-=";
        case SERAPH_TOK_STAR_ASSIGN:    return "*=";
        case SERAPH_TOK_SLASH_ASSIGN:   return "/=";
        case SERAPH_TOK_PERCENT_ASSIGN: return "%=";
        case SERAPH_TOK_AND_ASSIGN:     return "&=";
        case SERAPH_TOK_OR_ASSIGN:      return "|=";
        case SERAPH_TOK_XOR_ASSIGN:     return "^=";

        /* Delimiters */
        case SERAPH_TOK_LPAREN:         return "(";
        case SERAPH_TOK_RPAREN:         return ")";
        case SERAPH_TOK_LBRACE:         return "{";
        case SERAPH_TOK_RBRACE:         return "}";
        case SERAPH_TOK_LBRACKET:       return "[";
        case SERAPH_TOK_RBRACKET:       return "]";
        case SERAPH_TOK_SEMICOLON:      return ";";
        case SERAPH_TOK_COLON:          return ":";
        case SERAPH_TOK_COMMA:          return ",";
        case SERAPH_TOK_DOT:            return ".";
        case SERAPH_TOK_AMPERSAND:      return "&";
        case SERAPH_TOK_AT:             return "@";
        case SERAPH_TOK_HASH:           return "#";

        /* Special */
        case SERAPH_TOK_IDENT:          return "identifier";
        case SERAPH_TOK_EOF:            return "end of file";
        case SERAPH_TOK_VOID:           return "<invalid>";

        default:                        return "<unknown>";
    }
}

/*============================================================================
 * Token Classification
 *============================================================================*/

/**
 * @brief Check if token type is a keyword
 */
int seraph_token_is_keyword(Seraph_Token_Type type) {
    return (type >= SERAPH_TOK_FN && type <= SERAPH_TOK_CONTINUE) ||   /* Control flow */
           (type >= SERAPH_TOK_STRUCT && type <= SERAPH_TOK_IMPL) ||    /* Declarations */
           (type >= SERAPH_TOK_PERSIST && type <= SERAPH_TOK_EFFECTS) || /* Substrate */
           (type >= SERAPH_TOK_PURE && type <= SERAPH_TOK_EFFECT_IO) ||  /* Effects */
           (type >= SERAPH_TOK_U8 && type <= SERAPH_TOK_CHAR) ||        /* Primitives */
           (type >= SERAPH_TOK_SCALAR && type <= SERAPH_TOK_GALACTIC) || /* Numerics */
           (type >= SERAPH_TOK_VOLATILE && type <= SERAPH_TOK_AETHER) || /* Substrates */
           type == SERAPH_TOK_TRUE || type == SERAPH_TOK_FALSE ||
           type == SERAPH_TOK_VOID_LIT;
}

/**
 * @brief Check if token type is an operator
 */
int seraph_token_is_operator(Seraph_Token_Type type) {
    return (type >= SERAPH_TOK_VOID_PROP && type <= SERAPH_TOK_VOID_ASSERT) ||  /* VOID */
           (type >= SERAPH_TOK_ARROW && type <= SERAPH_TOK_RANGE_INCL) ||       /* Arrows */
           (type >= SERAPH_TOK_PLUS && type <= SERAPH_TOK_PERCENT) ||           /* Arith */
           (type >= SERAPH_TOK_EQ && type <= SERAPH_TOK_GE) ||                  /* Compare */
           (type >= SERAPH_TOK_AND && type <= SERAPH_TOK_SHR) ||                /* Logical */
           (type >= SERAPH_TOK_ASSIGN && type <= SERAPH_TOK_XOR_ASSIGN);        /* Assign */
}

/**
 * @brief Check if token type is a literal
 */
int seraph_token_is_literal(Seraph_Token_Type type) {
    return type >= SERAPH_TOK_INT_LITERAL && type <= SERAPH_TOK_VOID_LIT;
}

/*============================================================================
 * Operator Precedence
 *============================================================================*/

/**
 * @brief Get operator precedence
 *
 * Higher number = binds tighter (evaluated first)
 *
 * Precedence levels:
 *   1: = += -= etc. (assignment, right-assoc)
 *   2: ||
 *   3: &&
 *   4: |
 *   5: ^
 *   6: &
 *   7: == !=
 *   8: < > <= >=
 *   9: << >>
 *  10: + -
 *  11: * / %
 *  12: ! ~ - (unary)
 *  13: . () [] (postfix)
 *  14: ?? !! (VOID operators)
 *  15: |> (pipe)
 */
int seraph_token_precedence(Seraph_Token_Type type) {
    switch (type) {
        /* Assignment (lowest, right-associative) */
        case SERAPH_TOK_ASSIGN:
        case SERAPH_TOK_PLUS_ASSIGN:
        case SERAPH_TOK_MINUS_ASSIGN:
        case SERAPH_TOK_STAR_ASSIGN:
        case SERAPH_TOK_SLASH_ASSIGN:
        case SERAPH_TOK_PERCENT_ASSIGN:
        case SERAPH_TOK_AND_ASSIGN:
        case SERAPH_TOK_OR_ASSIGN:
        case SERAPH_TOK_XOR_ASSIGN:
            return 1;

        /* Logical OR */
        case SERAPH_TOK_OR:
            return 2;

        /* Logical AND */
        case SERAPH_TOK_AND:
            return 3;

        /* Bitwise OR */
        case SERAPH_TOK_BIT_OR:
            return 4;

        /* Bitwise XOR */
        case SERAPH_TOK_BIT_XOR:
            return 5;

        /* Bitwise AND */
        case SERAPH_TOK_BIT_AND:
            return 6;

        /* Equality */
        case SERAPH_TOK_EQ:
        case SERAPH_TOK_NE:
            return 7;

        /* Relational */
        case SERAPH_TOK_LT:
        case SERAPH_TOK_GT:
        case SERAPH_TOK_LE:
        case SERAPH_TOK_GE:
            return 8;

        /* Shift */
        case SERAPH_TOK_SHL:
        case SERAPH_TOK_SHR:
            return 9;

        /* Additive */
        case SERAPH_TOK_PLUS:
        case SERAPH_TOK_MINUS:
            return 10;

        /* Multiplicative */
        case SERAPH_TOK_STAR:
        case SERAPH_TOK_SLASH:
        case SERAPH_TOK_PERCENT:
            return 11;

        /* Unary (!, ~, -) handled specially */
        case SERAPH_TOK_NOT:
        case SERAPH_TOK_BIT_NOT:
            return 12;

        /* Type cast (as) - high precedence */
        case SERAPH_TOK_AS:
            return 12;

        /* Member access, call, index */
        case SERAPH_TOK_DOT:
            return 13;

        /* VOID operators (postfix) */
        case SERAPH_TOK_VOID_PROP:
        case SERAPH_TOK_VOID_ASSERT:
            return 14;

        /* Pipe (highest binary) */
        case SERAPH_TOK_PIPE:
            return 15;

        /* Range (low precedence, just above assignment) */
        case SERAPH_TOK_RANGE:
        case SERAPH_TOK_RANGE_INCL:
            return 2;

        default:
            return 0;  /* Not an operator or non-precedence token */
    }
}

/**
 * @brief Check if operator is right-associative
 */
int seraph_token_is_right_assoc(Seraph_Token_Type type) {
    switch (type) {
        /* Assignment operators are right-associative */
        case SERAPH_TOK_ASSIGN:
        case SERAPH_TOK_PLUS_ASSIGN:
        case SERAPH_TOK_MINUS_ASSIGN:
        case SERAPH_TOK_STAR_ASSIGN:
        case SERAPH_TOK_SLASH_ASSIGN:
        case SERAPH_TOK_PERCENT_ASSIGN:
        case SERAPH_TOK_AND_ASSIGN:
        case SERAPH_TOK_OR_ASSIGN:
        case SERAPH_TOK_XOR_ASSIGN:
            return 1;

        /* VOID coalesce is right-associative: a ?? b ?? c = a ?? (b ?? c) */
        case SERAPH_TOK_VOID_PROP:
            return 1;

        default:
            return 0;  /* Left-associative */
    }
}
