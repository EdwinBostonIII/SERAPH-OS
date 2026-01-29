/**
 * @file test_seraphim_lexer.c
 * @brief Unit tests for Seraphim Lexer
 *
 * MC26: Seraphim Compiler Tests - Lexer
 */

#include <stdio.h>
#include <string.h>
#include "seraph/seraphim/token.h"
#include "seraph/seraphim/lexer.h"
#include "seraph/arena.h"
#include "seraph/vbit.h"

/*============================================================================
 * Test Infrastructure
 *============================================================================*/

static int tests_run = 0;
static int tests_passed = 0;
static int current_test_failed = 0;

#define TEST(name) __attribute__((unused)) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("  Running %s... ", #name); fflush(stdout); \
    tests_run++; \
    current_test_failed = 0; \
    test_##name(); \
    if (!current_test_failed) { \
        tests_passed++; \
        printf("PASSED\n"); \
    } \
    fflush(stdout); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAILED at line %d: %s\n", __LINE__, #cond); \
        current_test_failed = 1; \
        return; \
    } \
} while(0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))
#define ASSERT_STR_EQ(a, b, len) ASSERT(memcmp(a, b, len) == 0)

/*============================================================================
 * Helper Functions
 *============================================================================*/

static Seraph_Arena test_arena;
static int arena_initialized = 0;

static void setup(void) {
    if (arena_initialized) {
        seraph_arena_destroy(&test_arena);
    }
    seraph_arena_create(&test_arena, 64 * 1024, 0, 0);  /* 64KB */
    arena_initialized = 1;
}

static void teardown(void) {
    if (arena_initialized) {
        seraph_arena_destroy(&test_arena);
        arena_initialized = 0;
    }
}

/** Tokenize a string and return success */
static int tokenize(const char* source, Seraph_Lexer* lexer) {
    setup();
    Seraph_Vbit result = seraph_lexer_init(
        lexer, source, strlen(source), "test", &test_arena
    );
    if (result != SERAPH_VBIT_TRUE) return 0;

    result = seraph_lexer_tokenize(lexer);
    return result == SERAPH_VBIT_TRUE;
}

/*============================================================================
 * Token Type Name Tests
 *============================================================================*/

TEST(token_type_names) {
    ASSERT_STR_EQ(seraph_token_type_name(SERAPH_TOK_FN), "fn", 2);
    ASSERT_STR_EQ(seraph_token_type_name(SERAPH_TOK_LET), "let", 3);
    ASSERT_STR_EQ(seraph_token_type_name(SERAPH_TOK_VOID_PROP), "??", 2);
    ASSERT_STR_EQ(seraph_token_type_name(SERAPH_TOK_PIPE), "|>", 2);
    ASSERT_STR_EQ(seraph_token_type_name(SERAPH_TOK_GALACTIC), "galactic", 8);
    ASSERT_STR_EQ(seraph_token_type_name(SERAPH_TOK_EOF), "end of file", 11);
}

/*============================================================================
 * Lexer Initialization Tests
 *============================================================================*/

TEST(lexer_init_null) {
    Seraph_Vbit result = seraph_lexer_init(NULL, "test", 4, "test", NULL);
    ASSERT_EQ(result, SERAPH_VBIT_VOID);
}

TEST(lexer_init_success) {
    setup();
    Seraph_Lexer lexer;
    Seraph_Vbit result = seraph_lexer_init(
        &lexer, "fn main() {}", 12, "test.seraph", &test_arena
    );
    ASSERT_EQ(result, SERAPH_VBIT_TRUE);
    ASSERT_EQ(lexer.line, 1);
    ASSERT_EQ(lexer.column, 1);
    ASSERT_EQ(lexer.pos, 0);
    teardown();
}

/*============================================================================
 * Keyword Tests
 *============================================================================*/

TEST(keyword_fn) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("fn", &lexer));
    ASSERT_EQ(lexer.token_count, 2);  /* fn + EOF */
    ASSERT_EQ(lexer.tokens[0].type, SERAPH_TOK_FN);
    teardown();
}

TEST(keyword_let) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("let mut", &lexer));
    ASSERT_EQ(lexer.token_count, 3);
    ASSERT_EQ(lexer.tokens[0].type, SERAPH_TOK_LET);
    ASSERT_EQ(lexer.tokens[1].type, SERAPH_TOK_MUT);
    teardown();
}

TEST(keywords_control_flow) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("if else for while return match", &lexer));
    ASSERT_EQ(lexer.tokens[0].type, SERAPH_TOK_IF);
    ASSERT_EQ(lexer.tokens[1].type, SERAPH_TOK_ELSE);
    ASSERT_EQ(lexer.tokens[2].type, SERAPH_TOK_FOR);
    ASSERT_EQ(lexer.tokens[3].type, SERAPH_TOK_WHILE);
    ASSERT_EQ(lexer.tokens[4].type, SERAPH_TOK_RETURN);
    ASSERT_EQ(lexer.tokens[5].type, SERAPH_TOK_MATCH);
    teardown();
}

TEST(keywords_types) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("u8 u16 u32 u64 i8 i16 i32 i64 bool char", &lexer));
    ASSERT_EQ(lexer.tokens[0].type, SERAPH_TOK_U8);
    ASSERT_EQ(lexer.tokens[1].type, SERAPH_TOK_U16);
    ASSERT_EQ(lexer.tokens[2].type, SERAPH_TOK_U32);
    ASSERT_EQ(lexer.tokens[3].type, SERAPH_TOK_U64);
    ASSERT_EQ(lexer.tokens[8].type, SERAPH_TOK_BOOL);
    ASSERT_EQ(lexer.tokens[9].type, SERAPH_TOK_CHAR);
    teardown();
}

TEST(keywords_numeric_types) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("scalar dual galactic", &lexer));
    ASSERT_EQ(lexer.tokens[0].type, SERAPH_TOK_SCALAR);
    ASSERT_EQ(lexer.tokens[1].type, SERAPH_TOK_DUAL);
    ASSERT_EQ(lexer.tokens[2].type, SERAPH_TOK_GALACTIC);
    teardown();
}

TEST(keywords_substrate) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("volatile atlas aether persist", &lexer));
    ASSERT_EQ(lexer.tokens[0].type, SERAPH_TOK_VOLATILE);
    ASSERT_EQ(lexer.tokens[1].type, SERAPH_TOK_ATLAS);
    /* aether can be both block and type - lexer returns appropriate token */
    ASSERT_EQ(lexer.tokens[3].type, SERAPH_TOK_PERSIST);
    teardown();
}

TEST(keywords_effects) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("pure VOID PERSIST NETWORK TIMER IO", &lexer));
    ASSERT_EQ(lexer.tokens[0].type, SERAPH_TOK_PURE);
    ASSERT_EQ(lexer.tokens[1].type, SERAPH_TOK_EFFECT_VOID);
    ASSERT_EQ(lexer.tokens[2].type, SERAPH_TOK_EFFECT_PERSIST);
    ASSERT_EQ(lexer.tokens[3].type, SERAPH_TOK_EFFECT_NETWORK);
    ASSERT_EQ(lexer.tokens[4].type, SERAPH_TOK_EFFECT_TIMER);
    ASSERT_EQ(lexer.tokens[5].type, SERAPH_TOK_EFFECT_IO);
    teardown();
}

/*============================================================================
 * Literal Tests
 *============================================================================*/

TEST(integer_literals) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("42 0xFF 0b1010 0o777", &lexer));
    ASSERT_EQ(lexer.tokens[0].type, SERAPH_TOK_INT_LITERAL);
    ASSERT_EQ(lexer.tokens[0].value.int_value, 42);
    ASSERT_EQ(lexer.tokens[1].value.int_value, 0xFF);
    ASSERT_EQ(lexer.tokens[2].value.int_value, 10);  /* 0b1010 */
    ASSERT_EQ(lexer.tokens[3].value.int_value, 0777);
    teardown();
}

TEST(integer_with_suffix) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("42u 42i 42u64 42i32", &lexer));
    ASSERT_EQ(lexer.tokens[0].num_suffix, SERAPH_NUM_SUFFIX_U);
    ASSERT_EQ(lexer.tokens[1].num_suffix, SERAPH_NUM_SUFFIX_I);
    ASSERT_EQ(lexer.tokens[2].num_suffix, SERAPH_NUM_SUFFIX_U64);
    ASSERT_EQ(lexer.tokens[3].num_suffix, SERAPH_NUM_SUFFIX_I32);
    teardown();
}

TEST(float_literals) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("3.14 1.0e-5 2.5E10", &lexer));
    ASSERT_EQ(lexer.tokens[0].type, SERAPH_TOK_FLOAT_LITERAL);
    ASSERT_EQ(lexer.tokens[1].type, SERAPH_TOK_FLOAT_LITERAL);
    ASSERT_EQ(lexer.tokens[2].type, SERAPH_TOK_FLOAT_LITERAL);
    teardown();
}

TEST(float_with_suffix) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("3.14s 2.0d 1.0g", &lexer));
    ASSERT_EQ(lexer.tokens[0].num_suffix, SERAPH_NUM_SUFFIX_S);
    ASSERT_EQ(lexer.tokens[1].num_suffix, SERAPH_NUM_SUFFIX_D);
    ASSERT_EQ(lexer.tokens[2].num_suffix, SERAPH_NUM_SUFFIX_G);
    teardown();
}

TEST(string_literal) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("\"hello world\"", &lexer));
    ASSERT_EQ(lexer.tokens[0].type, SERAPH_TOK_STRING_LITERAL);
    ASSERT_EQ(lexer.tokens[0].value.string_value.len, 11);
    teardown();
}

TEST(string_with_escapes) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("\"hello\\nworld\"", &lexer));
    ASSERT_EQ(lexer.tokens[0].type, SERAPH_TOK_STRING_LITERAL);
    teardown();
}

TEST(char_literal) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("'a' '\\n' '\\0'", &lexer));
    ASSERT_EQ(lexer.tokens[0].type, SERAPH_TOK_CHAR_LITERAL);
    ASSERT_EQ(lexer.tokens[0].value.char_value, 'a');
    ASSERT_EQ(lexer.tokens[1].value.char_value, '\n');
    ASSERT_EQ(lexer.tokens[2].value.char_value, '\0');
    teardown();
}

TEST(bool_literals) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("true false", &lexer));
    ASSERT_EQ(lexer.tokens[0].type, SERAPH_TOK_TRUE);
    ASSERT_EQ(lexer.tokens[1].type, SERAPH_TOK_FALSE);
    teardown();
}

/*============================================================================
 * Operator Tests
 *============================================================================*/

TEST(void_operators) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("x?? y!!", &lexer));
    ASSERT_EQ(lexer.tokens[1].type, SERAPH_TOK_VOID_PROP);
    ASSERT_EQ(lexer.tokens[3].type, SERAPH_TOK_VOID_ASSERT);
    teardown();
}

TEST(arrow_operators) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("-> => |>", &lexer));
    ASSERT_EQ(lexer.tokens[0].type, SERAPH_TOK_ARROW);
    ASSERT_EQ(lexer.tokens[1].type, SERAPH_TOK_FAT_ARROW);
    ASSERT_EQ(lexer.tokens[2].type, SERAPH_TOK_PIPE);
    teardown();
}

TEST(comparison_operators) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("== != < > <= >=", &lexer));
    ASSERT_EQ(lexer.tokens[0].type, SERAPH_TOK_EQ);
    ASSERT_EQ(lexer.tokens[1].type, SERAPH_TOK_NE);
    ASSERT_EQ(lexer.tokens[2].type, SERAPH_TOK_LT);
    ASSERT_EQ(lexer.tokens[3].type, SERAPH_TOK_GT);
    ASSERT_EQ(lexer.tokens[4].type, SERAPH_TOK_LE);
    ASSERT_EQ(lexer.tokens[5].type, SERAPH_TOK_GE);
    teardown();
}

TEST(arithmetic_operators) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("+ - * / %", &lexer));
    ASSERT_EQ(lexer.tokens[0].type, SERAPH_TOK_PLUS);
    ASSERT_EQ(lexer.tokens[1].type, SERAPH_TOK_MINUS);
    ASSERT_EQ(lexer.tokens[2].type, SERAPH_TOK_STAR);
    ASSERT_EQ(lexer.tokens[3].type, SERAPH_TOK_SLASH);
    ASSERT_EQ(lexer.tokens[4].type, SERAPH_TOK_PERCENT);
    teardown();
}

TEST(logical_operators) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("&& || !", &lexer));
    ASSERT_EQ(lexer.tokens[0].type, SERAPH_TOK_AND);
    ASSERT_EQ(lexer.tokens[1].type, SERAPH_TOK_OR);
    ASSERT_EQ(lexer.tokens[2].type, SERAPH_TOK_NOT);
    teardown();
}

TEST(bitwise_operators) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("& | ^ ~ << >>", &lexer));
    ASSERT_EQ(lexer.tokens[0].type, SERAPH_TOK_AMPERSAND);
    ASSERT_EQ(lexer.tokens[1].type, SERAPH_TOK_BIT_OR);
    ASSERT_EQ(lexer.tokens[2].type, SERAPH_TOK_BIT_XOR);
    ASSERT_EQ(lexer.tokens[3].type, SERAPH_TOK_BIT_NOT);
    ASSERT_EQ(lexer.tokens[4].type, SERAPH_TOK_SHL);
    ASSERT_EQ(lexer.tokens[5].type, SERAPH_TOK_SHR);
    teardown();
}

TEST(assignment_operators) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("= += -= *= /=", &lexer));
    ASSERT_EQ(lexer.tokens[0].type, SERAPH_TOK_ASSIGN);
    ASSERT_EQ(lexer.tokens[1].type, SERAPH_TOK_PLUS_ASSIGN);
    ASSERT_EQ(lexer.tokens[2].type, SERAPH_TOK_MINUS_ASSIGN);
    ASSERT_EQ(lexer.tokens[3].type, SERAPH_TOK_STAR_ASSIGN);
    ASSERT_EQ(lexer.tokens[4].type, SERAPH_TOK_SLASH_ASSIGN);
    teardown();
}

TEST(range_operators) {
    Seraph_Lexer lexer;
    ASSERT(tokenize(".. ..=", &lexer));
    ASSERT_EQ(lexer.tokens[0].type, SERAPH_TOK_RANGE);
    ASSERT_EQ(lexer.tokens[1].type, SERAPH_TOK_RANGE_INCL);
    teardown();
}

/*============================================================================
 * Delimiter Tests
 *============================================================================*/

TEST(delimiters) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("( ) { } [ ] ; : , . :: @", &lexer));
    ASSERT_EQ(lexer.tokens[0].type, SERAPH_TOK_LPAREN);
    ASSERT_EQ(lexer.tokens[1].type, SERAPH_TOK_RPAREN);
    ASSERT_EQ(lexer.tokens[2].type, SERAPH_TOK_LBRACE);
    ASSERT_EQ(lexer.tokens[3].type, SERAPH_TOK_RBRACE);
    ASSERT_EQ(lexer.tokens[4].type, SERAPH_TOK_LBRACKET);
    ASSERT_EQ(lexer.tokens[5].type, SERAPH_TOK_RBRACKET);
    ASSERT_EQ(lexer.tokens[6].type, SERAPH_TOK_SEMICOLON);
    ASSERT_EQ(lexer.tokens[7].type, SERAPH_TOK_COLON);
    ASSERT_EQ(lexer.tokens[8].type, SERAPH_TOK_COMMA);
    ASSERT_EQ(lexer.tokens[9].type, SERAPH_TOK_DOT);
    ASSERT_EQ(lexer.tokens[10].type, SERAPH_TOK_DOUBLE_COLON);
    ASSERT_EQ(lexer.tokens[11].type, SERAPH_TOK_AT);
    teardown();
}

/*============================================================================
 * Comment Tests
 *============================================================================*/

TEST(line_comment) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("fn // comment\nmain", &lexer));
    ASSERT_EQ(lexer.token_count, 3);  /* fn, main, EOF */
    ASSERT_EQ(lexer.tokens[0].type, SERAPH_TOK_FN);
    ASSERT_EQ(lexer.tokens[1].type, SERAPH_TOK_IDENT);
    teardown();
}

TEST(block_comment) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("fn /* comment */ main", &lexer));
    ASSERT_EQ(lexer.token_count, 3);
    ASSERT_EQ(lexer.tokens[0].type, SERAPH_TOK_FN);
    ASSERT_EQ(lexer.tokens[1].type, SERAPH_TOK_IDENT);
    teardown();
}

TEST(nested_block_comment) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("fn /* outer /* inner */ outer */ main", &lexer));
    ASSERT_EQ(lexer.token_count, 3);
    teardown();
}

/*============================================================================
 * Identifier Tests
 *============================================================================*/

TEST(identifiers) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("foo bar_baz _private __double", &lexer));
    ASSERT_EQ(lexer.tokens[0].type, SERAPH_TOK_IDENT);
    ASSERT_EQ(lexer.tokens[1].type, SERAPH_TOK_IDENT);
    ASSERT_EQ(lexer.tokens[2].type, SERAPH_TOK_IDENT);
    ASSERT_EQ(lexer.tokens[3].type, SERAPH_TOK_IDENT);
    teardown();
}

/*============================================================================
 * Source Location Tests
 *============================================================================*/

TEST(source_location) {
    Seraph_Lexer lexer;
    ASSERT(tokenize("fn\nmain", &lexer));
    ASSERT_EQ(lexer.tokens[0].loc.line, 1);
    ASSERT_EQ(lexer.tokens[0].loc.column, 1);
    ASSERT_EQ(lexer.tokens[1].loc.line, 2);
    ASSERT_EQ(lexer.tokens[1].loc.column, 1);
    teardown();
}

/*============================================================================
 * Complete Program Tests
 *============================================================================*/

TEST(simple_function) {
    Seraph_Lexer lexer;
    const char* source = "fn add(a: u64, b: u64) -> u64 { return a + b; }";
    ASSERT(tokenize(source, &lexer));
    ASSERT_EQ(lexer.error_count, 0);
    ASSERT_EQ(lexer.tokens[0].type, SERAPH_TOK_FN);
    ASSERT_EQ(lexer.tokens[1].type, SERAPH_TOK_IDENT);
    teardown();
}

TEST(void_propagation) {
    Seraph_Lexer lexer;
    const char* source = "fn safe_div(a: u64, b: u64) -> ??u64 { return a / b; }";
    ASSERT(tokenize(source, &lexer));
    ASSERT_EQ(lexer.error_count, 0);

    int found_void_prop = 0;
    for (size_t i = 0; i < lexer.token_count; i++) {
        if (lexer.tokens[i].type == SERAPH_TOK_VOID_PROP) {
            found_void_prop = 1;
            break;
        }
    }
    ASSERT(found_void_prop);
    teardown();
}

TEST(galactic_literal) {
    Seraph_Lexer lexer;
    const char* source = "let g: galactic = 3.14g;";
    ASSERT(tokenize(source, &lexer));

    int found_galactic = 0;
    int found_g_suffix = 0;
    for (size_t i = 0; i < lexer.token_count; i++) {
        if (lexer.tokens[i].type == SERAPH_TOK_GALACTIC) found_galactic = 1;
        if (lexer.tokens[i].num_suffix == SERAPH_NUM_SUFFIX_G) found_g_suffix = 1;
    }
    ASSERT(found_galactic);
    ASSERT(found_g_suffix);
    teardown();
}

TEST(pipe_operator) {
    Seraph_Lexer lexer;
    const char* source = "let x = input |> trim |> parse;";
    ASSERT(tokenize(source, &lexer));

    int pipe_count = 0;
    for (size_t i = 0; i < lexer.token_count; i++) {
        if (lexer.tokens[i].type == SERAPH_TOK_PIPE) pipe_count++;
    }
    ASSERT_EQ(pipe_count, 2);
    teardown();
}

/*============================================================================
 * Precedence Tests
 *============================================================================*/

TEST(operator_precedence) {
    ASSERT(seraph_token_precedence(SERAPH_TOK_STAR) >
           seraph_token_precedence(SERAPH_TOK_PLUS));
    ASSERT(seraph_token_precedence(SERAPH_TOK_PLUS) >
           seraph_token_precedence(SERAPH_TOK_EQ));
    ASSERT(seraph_token_precedence(SERAPH_TOK_AND) >
           seraph_token_precedence(SERAPH_TOK_OR));
    ASSERT(seraph_token_precedence(SERAPH_TOK_PIPE) >
           seraph_token_precedence(SERAPH_TOK_STAR));
}

TEST(right_associativity) {
    ASSERT(seraph_token_is_right_assoc(SERAPH_TOK_ASSIGN));
    ASSERT(seraph_token_is_right_assoc(SERAPH_TOK_VOID_PROP));
    ASSERT(!seraph_token_is_right_assoc(SERAPH_TOK_PLUS));
}

/*============================================================================
 * Main Test Runner
 *============================================================================*/

void run_seraphim_lexer_tests(void) {
    printf("\n=== MC26: Seraphim Lexer Tests ===\n\n");

    tests_run = 0;
    tests_passed = 0;

    printf("Token Utilities:\n");
    RUN_TEST(token_type_names);

    printf("\nInitialization:\n");
    RUN_TEST(lexer_init_null);
    RUN_TEST(lexer_init_success);

    printf("\nKeywords:\n");
    RUN_TEST(keyword_fn);
    RUN_TEST(keyword_let);
    RUN_TEST(keywords_control_flow);
    RUN_TEST(keywords_types);
    RUN_TEST(keywords_numeric_types);
    RUN_TEST(keywords_substrate);
    RUN_TEST(keywords_effects);

    printf("\nLiterals:\n");
    RUN_TEST(integer_literals);
    RUN_TEST(integer_with_suffix);
    RUN_TEST(float_literals);
    RUN_TEST(float_with_suffix);
    RUN_TEST(string_literal);
    RUN_TEST(string_with_escapes);
    RUN_TEST(char_literal);
    RUN_TEST(bool_literals);

    printf("\nOperators:\n");
    RUN_TEST(void_operators);
    RUN_TEST(arrow_operators);
    RUN_TEST(comparison_operators);
    RUN_TEST(arithmetic_operators);
    RUN_TEST(logical_operators);
    RUN_TEST(bitwise_operators);
    RUN_TEST(assignment_operators);
    RUN_TEST(range_operators);

    printf("\nDelimiters:\n");
    RUN_TEST(delimiters);

    printf("\nComments:\n");
    RUN_TEST(line_comment);
    RUN_TEST(block_comment);
    RUN_TEST(nested_block_comment);

    printf("\nIdentifiers:\n");
    RUN_TEST(identifiers);

    printf("\nSource Locations:\n");
    RUN_TEST(source_location);

    printf("\nComplete Programs:\n");
    RUN_TEST(simple_function);
    RUN_TEST(void_propagation);
    RUN_TEST(galactic_literal);
    RUN_TEST(pipe_operator);

    printf("\nPrecedence:\n");
    RUN_TEST(operator_precedence);
    RUN_TEST(right_associativity);

    printf("\nSeraphim Lexer: %d/%d tests passed\n", tests_passed, tests_run);
}
