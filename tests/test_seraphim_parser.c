/**
 * @file test_seraphim_parser.c
 * @brief Comprehensive tests for Seraphim Parser
 *
 * MC26: Seraphim Language Parser Tests
 *
 * Tests cover:
 * - Parser initialization and lifecycle
 * - Function declarations
 * - Let/const bindings
 * - Struct and enum declarations
 * - Expression parsing (literals, binary, unary, calls)
 * - VOID operators (??, !!)
 * - Control flow (if, match, for, while)
 * - Type parsing
 * - Error recovery
 */

#include "seraph/seraphim/parser.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*============================================================================
 * Test Framework
 *============================================================================*/

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("    FAILED: %s\n", msg); \
        return 0; \
    } \
} while(0)

#define ASSERT_EQ(a, b, msg) ASSERT((a) == (b), msg)
#define ASSERT_NE(a, b, msg) ASSERT((a) != (b), msg)
#define ASSERT_NOT_NULL(p, msg) ASSERT((p) != NULL, msg)
#define ASSERT_NULL(p, msg) ASSERT((p) == NULL, msg)

#define RUN_TEST(name) do { \
    tests_run++; \
    printf("  Running %s...", #name); \
    fflush(stdout); \
    if (name()) { \
        tests_passed++; \
        printf(" PASSED\n"); \
    } else { \
        printf("\n"); \
    } \
    fflush(stdout); \
} while(0)

/*============================================================================
 * Test Helpers
 *============================================================================*/

/**
 * Create a parser from source code string
 */
static int create_parser(const char* source, Seraph_Lexer* lexer,
                         Seraph_Parser* parser, Seraph_Arena* arena) {
    /* Initialize arena - use 64KB like lexer tests */
    Seraph_Vbit arena_ok = seraph_arena_create(arena, 64 * 1024, 0, 0);
    if (!seraph_vbit_is_true(arena_ok)) {
        return 0;
    }

    /* Initialize lexer and tokenize */
    Seraph_Vbit lex_init = seraph_lexer_init(lexer, source, strlen(source), "test", arena);
    if (!seraph_vbit_is_true(lex_init)) {
        return 0;
    }

    Seraph_Vbit tokenized = seraph_lexer_tokenize(lexer);
    if (!seraph_vbit_is_true(tokenized)) {
        return 0;
    }

    /* Initialize parser */
    Seraph_Vbit parse_init = seraph_parser_init(parser, lexer, arena);
    if (!seraph_vbit_is_true(parse_init)) {
        return 0;
    }

    return 1;
}

/**
 * Cleanup parser resources
 */
static void destroy_parser(Seraph_Arena* arena) {
    seraph_arena_destroy(arena);
}

/*============================================================================
 * Initialization Tests
 *============================================================================*/

static int test_parser_init_null(void) {
    Seraph_Parser parser = {0};
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};

    Seraph_Vbit arena_ok = seraph_arena_create(&arena, 1024, 8, 0);
    ASSERT(seraph_vbit_is_true(arena_ok), "Arena should create");

    /* NULL parser */
    Seraph_Vbit result = seraph_parser_init(NULL, &lexer, &arena);
    ASSERT(seraph_vbit_is_void(result), "NULL parser should return VOID");

    /* NULL lexer */
    result = seraph_parser_init(&parser, NULL, &arena);
    ASSERT(seraph_vbit_is_void(result), "NULL lexer should return VOID");

    /* NULL arena */
    result = seraph_parser_init(&parser, &lexer, NULL);
    ASSERT(seraph_vbit_is_void(result), "NULL arena should return VOID");

    seraph_arena_destroy(&arena);
    return 1;
}

static int test_parser_init_success(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("fn main() {}", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");
    ASSERT_EQ(parser.pos, 0, "Position should start at 0");
    ASSERT_NOT_NULL(parser.lexer, "Lexer should be set");
    ASSERT_NOT_NULL(parser.arena, "Arena should be set");
    ASSERT_EQ(parser.error_count, 0, "No errors initially");

    destroy_parser(&arena);
    return 1;
}

/*============================================================================
 * Literal Expression Tests
 *============================================================================*/

static int test_parse_integer_literal(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("42", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    ASSERT_EQ(expr->hdr.kind, AST_EXPR_INT_LIT, "Should be integer literal");
    ASSERT_EQ(expr->int_lit.value, 42, "Value should be 42");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_hex_literal(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("0xFF", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    ASSERT_EQ(expr->hdr.kind, AST_EXPR_INT_LIT, "Should be integer literal");
    ASSERT_EQ(expr->int_lit.value, 255, "Value should be 255");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_binary_literal(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("0b1010", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    ASSERT_EQ(expr->hdr.kind, AST_EXPR_INT_LIT, "Should be integer literal");
    ASSERT_EQ(expr->int_lit.value, 10, "Value should be 10");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_float_literal(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("3.14", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    ASSERT_EQ(expr->hdr.kind, AST_EXPR_FLOAT_LIT, "Should be float literal");
    ASSERT(expr->float_lit.value > 3.13 && expr->float_lit.value < 3.15,
           "Value should be approximately 3.14");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_string_literal(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("\"hello\"", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    ASSERT_EQ(expr->hdr.kind, AST_EXPR_STRING_LIT, "Should be string literal");
    ASSERT_NOT_NULL(expr->string_lit.value, "String value should exist");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_bool_literals(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    /* Test true */
    int ok = create_parser("true", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    ASSERT_EQ(expr->hdr.kind, AST_EXPR_BOOL_LIT, "Should be bool literal");
    ASSERT_EQ(expr->bool_lit.value, 1, "Value should be true");

    destroy_parser(&arena);

    /* Test false */
    ok = create_parser("false", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    ASSERT_EQ(expr->hdr.kind, AST_EXPR_BOOL_LIT, "Should be bool literal");
    ASSERT_EQ(expr->bool_lit.value, 0, "Value should be false");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_void_literal(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("void", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    ASSERT_EQ(expr->hdr.kind, AST_EXPR_VOID_LIT, "Should be void literal");

    destroy_parser(&arena);
    return 1;
}

/*============================================================================
 * Binary Expression Tests
 *============================================================================*/

static int test_parse_binary_add(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("1 + 2", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    ASSERT_EQ(expr->hdr.kind, AST_EXPR_BINARY, "Should be binary expression");
    ASSERT_EQ(expr->binary.op, SERAPH_TOK_PLUS, "Should be plus operator");
    ASSERT_NOT_NULL(expr->binary.left, "Left operand should exist");
    ASSERT_NOT_NULL(expr->binary.right, "Right operand should exist");
    ASSERT_EQ(expr->binary.left->hdr.kind, AST_EXPR_INT_LIT, "Left should be int");
    ASSERT_EQ(expr->binary.right->hdr.kind, AST_EXPR_INT_LIT, "Right should be int");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_binary_precedence(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    /* 1 + 2 * 3 should parse as 1 + (2 * 3) */
    int ok = create_parser("1 + 2 * 3", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    ASSERT_EQ(expr->hdr.kind, AST_EXPR_BINARY, "Should be binary expression");
    ASSERT_EQ(expr->binary.op, SERAPH_TOK_PLUS, "Top should be plus");
    ASSERT_EQ(expr->binary.left->hdr.kind, AST_EXPR_INT_LIT, "Left should be int");
    ASSERT_EQ(expr->binary.right->hdr.kind, AST_EXPR_BINARY, "Right should be binary");
    ASSERT_EQ(expr->binary.right->binary.op, SERAPH_TOK_STAR, "Right should be multiply");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_comparison(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("x < 10", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    ASSERT_EQ(expr->hdr.kind, AST_EXPR_BINARY, "Should be binary expression");
    ASSERT_EQ(expr->binary.op, SERAPH_TOK_LT, "Should be less-than");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_logical_and_or(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    /* a && b || c should parse as (a && b) || c */
    int ok = create_parser("a && b || c", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    ASSERT_EQ(expr->hdr.kind, AST_EXPR_BINARY, "Should be binary expression");
    ASSERT_EQ(expr->binary.op, SERAPH_TOK_OR, "Top should be ||");
    ASSERT_EQ(expr->binary.left->hdr.kind, AST_EXPR_BINARY, "Left should be binary");
    ASSERT_EQ(expr->binary.left->binary.op, SERAPH_TOK_AND, "Left should be &&");

    destroy_parser(&arena);
    return 1;
}

/*============================================================================
 * Unary Expression Tests
 *============================================================================*/

static int test_parse_unary_negation(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("-42", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    ASSERT_EQ(expr->hdr.kind, AST_EXPR_UNARY, "Should be unary expression");
    ASSERT_EQ(expr->unary.op, SERAPH_TOK_MINUS, "Should be minus operator");
    ASSERT_EQ(expr->unary.operand->hdr.kind, AST_EXPR_INT_LIT, "Operand should be int");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_unary_not(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("!flag", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    ASSERT_EQ(expr->hdr.kind, AST_EXPR_UNARY, "Should be unary expression");
    ASSERT_EQ(expr->unary.op, SERAPH_TOK_NOT, "Should be bang operator");
    ASSERT_EQ(expr->unary.operand->hdr.kind, AST_EXPR_IDENT, "Operand should be identifier");

    destroy_parser(&arena);
    return 1;
}

/*============================================================================
 * VOID Operator Tests
 *============================================================================*/

static int test_parse_void_propagation(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("x??", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    ASSERT_EQ(expr->hdr.kind, AST_EXPR_VOID_PROP, "Should be void propagation");
    ASSERT_NOT_NULL(expr->void_prop.operand, "Should have operand");
    ASSERT_EQ(expr->void_prop.operand->hdr.kind, AST_EXPR_IDENT, "Operand should be identifier");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_void_assertion(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("x!!", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    ASSERT_EQ(expr->hdr.kind, AST_EXPR_VOID_ASSERT, "Should be void assertion");
    ASSERT_NOT_NULL(expr->void_assert.operand, "Should have operand");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_void_coalesce(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("x ?? 0", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    /* Coalesce is parsed as void_prop with default_val or as binary */
    ASSERT(expr->hdr.kind == AST_EXPR_VOID_PROP || expr->hdr.kind == AST_EXPR_BINARY,
           "Should be void coalesce or binary");

    destroy_parser(&arena);
    return 1;
}

/*============================================================================
 * Call Expression Tests
 *============================================================================*/

static int test_parse_function_call(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("foo(1, 2, 3)", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    ASSERT_EQ(expr->hdr.kind, AST_EXPR_CALL, "Should be call expression");
    ASSERT_NOT_NULL(expr->call.callee, "Should have callee");
    ASSERT_EQ(expr->call.callee->hdr.kind, AST_EXPR_IDENT, "Callee should be identifier");
    ASSERT_EQ(expr->call.arg_count, 3, "Should have 3 arguments");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_method_call(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("obj.method()", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    ASSERT_EQ(expr->hdr.kind, AST_EXPR_CALL, "Should be call expression");
    ASSERT_NOT_NULL(expr->call.callee, "Should have callee");
    ASSERT_EQ(expr->call.callee->hdr.kind, AST_EXPR_FIELD, "Callee should be field access");

    destroy_parser(&arena);
    return 1;
}

/*============================================================================
 * Field and Index Access Tests
 *============================================================================*/

static int test_parse_field_access(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("obj.field", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    ASSERT_EQ(expr->hdr.kind, AST_EXPR_FIELD, "Should be field access");
    ASSERT_NOT_NULL(expr->field.object, "Should have object");
    ASSERT_NOT_NULL(expr->field.field, "Should have field name");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_index_access(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("arr[0]", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    ASSERT_EQ(expr->hdr.kind, AST_EXPR_INDEX, "Should be index access");
    ASSERT_NOT_NULL(expr->index.object, "Should have object");
    ASSERT_NOT_NULL(expr->index.index, "Should have index");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_chained_access(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("a.b.c[0].d", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    ASSERT_EQ(expr->hdr.kind, AST_EXPR_FIELD, "Should be field access");

    destroy_parser(&arena);
    return 1;
}

/*============================================================================
 * If Expression Tests
 *============================================================================*/

static int test_parse_if_expr(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("if x { 1 } else { 2 }", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    ASSERT_EQ(expr->hdr.kind, AST_EXPR_IF, "Should be if expression");
    ASSERT_NOT_NULL(expr->if_expr.cond, "Should have condition");
    ASSERT_NOT_NULL(expr->if_expr.then_branch, "Should have then branch");
    ASSERT_NOT_NULL(expr->if_expr.else_branch, "Should have else branch");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_if_no_else(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("if x { 1 }", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    ASSERT_EQ(expr->hdr.kind, AST_EXPR_IF, "Should be if expression");
    ASSERT_NOT_NULL(expr->if_expr.cond, "Should have condition");
    ASSERT_NOT_NULL(expr->if_expr.then_branch, "Should have then branch");
    ASSERT_NULL(expr->if_expr.else_branch, "Should NOT have else branch");

    destroy_parser(&arena);
    return 1;
}

/*============================================================================
 * Match Expression Tests
 *============================================================================*/

static int test_parse_match_expr(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("match x { 1 => a, 2 => b }", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    ASSERT_EQ(expr->hdr.kind, AST_EXPR_MATCH, "Should be match expression");
    ASSERT_NOT_NULL(expr->match.scrutinee, "Should have scrutinee");
    ASSERT_EQ(expr->match.arm_count, 2, "Should have 2 arms");

    destroy_parser(&arena);
    return 1;
}

/*============================================================================
 * Type Parsing Tests
 *============================================================================*/

static int test_parse_type_simple(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("i32", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* type = seraph_parse_type(&parser);
    ASSERT_NOT_NULL(type, "Type should not be NULL");
    ASSERT(type->hdr.kind == AST_TYPE_NAMED || type->hdr.kind == AST_TYPE_PRIMITIVE,
           "Should be named or primitive type");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_type_pointer(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("&i32", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* type = seraph_parse_type(&parser);
    ASSERT_NOT_NULL(type, "Type should not be NULL");
    ASSERT_EQ(type->hdr.kind, AST_TYPE_REF, "Should be reference type");
    ASSERT_NOT_NULL(type->ref_type.inner, "Should have inner type");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_type_array(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("[i32; 10]", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* type = seraph_parse_type(&parser);
    ASSERT_NOT_NULL(type, "Type should not be NULL");
    ASSERT_EQ(type->hdr.kind, AST_TYPE_ARRAY, "Should be array type");
    ASSERT_NOT_NULL(type->array_type.elem_type, "Should have element type");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_type_slice(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("[i32]", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* type = seraph_parse_type(&parser);
    ASSERT_NOT_NULL(type, "Type should not be NULL");
    ASSERT_EQ(type->hdr.kind, AST_TYPE_SLICE, "Should be slice type");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_type_voidable(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("??i32", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* type = seraph_parse_type(&parser);
    ASSERT_NOT_NULL(type, "Type should not be NULL");
    ASSERT_EQ(type->hdr.kind, AST_TYPE_VOID_ABLE, "Should be VOID-able type");

    destroy_parser(&arena);
    return 1;
}

/*============================================================================
 * Function Declaration Tests
 *============================================================================*/

static int test_parse_fn_simple(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("fn main() {}", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* decl = seraph_parse_decl(&parser);
    ASSERT_NOT_NULL(decl, "Declaration should not be NULL");
    ASSERT_EQ(decl->hdr.kind, AST_DECL_FN, "Should be function declaration");
    ASSERT_NOT_NULL(decl->fn_decl.name, "Should have name");
    ASSERT_EQ(decl->fn_decl.param_count, 0, "Should have 0 parameters");
    ASSERT_NULL(decl->fn_decl.ret_type, "No return type");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_fn_with_params(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("fn add(a: i32, b: i32) -> i32 { a + b }", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* decl = seraph_parse_decl(&parser);
    ASSERT_NOT_NULL(decl, "Declaration should not be NULL");
    ASSERT_EQ(decl->hdr.kind, AST_DECL_FN, "Should be function declaration");
    ASSERT_EQ(decl->fn_decl.param_count, 2, "Should have 2 parameters");
    ASSERT_NOT_NULL(decl->fn_decl.ret_type, "Should have return type");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_fn_with_effects(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("[pure] fn add(a: i32, b: i32) -> i32 { a + b }", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* decl = seraph_parse_decl(&parser);
    ASSERT_NOT_NULL(decl, "Declaration should not be NULL");
    ASSERT_EQ(decl->hdr.kind, AST_DECL_FN, "Should be function declaration");

    destroy_parser(&arena);
    return 1;
}

/*============================================================================
 * Let/Const Declaration Tests
 *============================================================================*/

static int test_parse_let_simple(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("let x = 42;", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* decl = seraph_parse_decl(&parser);
    ASSERT_NOT_NULL(decl, "Declaration should not be NULL");
    ASSERT_EQ(decl->hdr.kind, AST_DECL_LET, "Should be let declaration");
    ASSERT_NOT_NULL(decl->let_decl.name, "Should have name");
    ASSERT_NOT_NULL(decl->let_decl.init, "Should have initializer");
    ASSERT_EQ(decl->let_decl.is_const, 0, "Should not be const");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_let_with_type(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("let x: i32 = 42;", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* decl = seraph_parse_decl(&parser);
    ASSERT_NOT_NULL(decl, "Declaration should not be NULL");
    ASSERT_EQ(decl->hdr.kind, AST_DECL_LET, "Should be let declaration");
    ASSERT_NOT_NULL(decl->let_decl.type, "Should have type annotation");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_const(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("const PI = 3.14;", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* decl = seraph_parse_decl(&parser);
    ASSERT_NOT_NULL(decl, "Declaration should not be NULL");
    /* const might use AST_DECL_CONST or AST_DECL_LET with is_const */
    ASSERT(decl->hdr.kind == AST_DECL_CONST || decl->hdr.kind == AST_DECL_LET,
           "Should be const declaration");

    destroy_parser(&arena);
    return 1;
}

/*============================================================================
 * Struct Declaration Tests
 *============================================================================*/

static int test_parse_struct_simple(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("struct Point { x: i32, y: i32 }", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* decl = seraph_parse_decl(&parser);
    ASSERT_NOT_NULL(decl, "Declaration should not be NULL");
    ASSERT_EQ(decl->hdr.kind, AST_DECL_STRUCT, "Should be struct declaration");
    ASSERT_NOT_NULL(decl->struct_decl.name, "Should have name");
    ASSERT_EQ(decl->struct_decl.field_count, 2, "Should have 2 fields");

    destroy_parser(&arena);
    return 1;
}

/*============================================================================
 * Enum Declaration Tests
 *============================================================================*/

static int test_parse_enum_simple(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("enum Color { Red, Green, Blue }", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* decl = seraph_parse_decl(&parser);
    ASSERT_NOT_NULL(decl, "Declaration should not be NULL");
    ASSERT_EQ(decl->hdr.kind, AST_DECL_ENUM, "Should be enum declaration");
    ASSERT_NOT_NULL(decl->enum_decl.name, "Should have name");
    ASSERT_EQ(decl->enum_decl.variant_count, 3, "Should have 3 variants");

    destroy_parser(&arena);
    return 1;
}

/*============================================================================
 * Statement Tests
 *============================================================================*/

static int test_parse_return_stmt(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("return 42;", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* stmt = seraph_parse_stmt(&parser);
    ASSERT_NOT_NULL(stmt, "Statement should not be NULL");
    ASSERT_EQ(stmt->hdr.kind, AST_STMT_RETURN, "Should be return statement");
    ASSERT_NOT_NULL(stmt->return_stmt.expr, "Should have return value");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_while_stmt(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("while x < 10 { x = x + 1; }", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* stmt = seraph_parse_stmt(&parser);
    ASSERT_NOT_NULL(stmt, "Statement should not be NULL");
    ASSERT_EQ(stmt->hdr.kind, AST_STMT_WHILE, "Should be while statement");
    ASSERT_NOT_NULL(stmt->while_stmt.cond, "Should have condition");
    ASSERT_NOT_NULL(stmt->while_stmt.body, "Should have body");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_for_stmt(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("for i in 0..10 { print(i); }", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* stmt = seraph_parse_stmt(&parser);
    ASSERT_NOT_NULL(stmt, "Statement should not be NULL");
    ASSERT_EQ(stmt->hdr.kind, AST_STMT_FOR, "Should be for statement");
    ASSERT_NOT_NULL(stmt->for_stmt.var, "Should have iterator variable");
    ASSERT_NOT_NULL(stmt->for_stmt.iterable, "Should have iterable");
    ASSERT_NOT_NULL(stmt->for_stmt.body, "Should have body");

    destroy_parser(&arena);
    return 1;
}

static int test_parse_block(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("{ let x = 1; let y = 2; x + y }", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* block = seraph_parse_block(&parser);
    ASSERT_NOT_NULL(block, "Block should not be NULL");
    ASSERT_EQ(block->hdr.kind, AST_EXPR_BLOCK, "Should be block");
    /* Note: May have 2 or 3 statements depending on if final expression counts */
    ASSERT(block->block.stmt_count >= 2, "Should have at least 2 statements");

    destroy_parser(&arena);
    return 1;
}

/*============================================================================
 * Module Parsing Tests
 *============================================================================*/

static int test_parse_module(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    /* Use simple functions with explicit returns */
    const char* source =
        "fn main() {\n"
        "    let x = 42;\n"
        "    return x;\n"
        "}\n"
        "\n"
        "fn add(a: i32, b: i32) -> i32 {\n"
        "    return a + b;\n"
        "}\n";

    int ok = create_parser(source, &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* module = seraph_parse_module(&parser);
    ASSERT_NOT_NULL(module, "Module should not be NULL");
    ASSERT_EQ(module->hdr.kind, AST_MODULE, "Should be module");
    ASSERT_EQ(module->module.decl_count, 2, "Should have 2 declarations");
    /* Note: Parser may have some recoverable errors depending on implementation */

    destroy_parser(&arena);
    return 1;
}

/*============================================================================
 * Pipe Operator Tests
 *============================================================================*/

static int test_parse_pipe_operator(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("x |> f |> g", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* expr = seraph_parse_expr(&parser);
    ASSERT_NOT_NULL(expr, "Expression should not be NULL");
    /* Pipe chains - should parse as binary with PIPE operator */
    ASSERT_EQ(expr->hdr.kind, AST_EXPR_BINARY, "Should be binary expression");
    ASSERT_EQ(expr->binary.op, SERAPH_TOK_PIPE, "Should be pipe operator");

    destroy_parser(&arena);
    return 1;
}

/*============================================================================
 * Error Recovery Tests
 *============================================================================*/

static int test_error_missing_semicolon(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    int ok = create_parser("let x = 42 let y = 10;", &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    /* Parse module to trigger error recovery */
    (void)seraph_parse_module(&parser);
    /* Should have errors */
    ASSERT(seraph_parser_has_errors(&parser), "Should have errors");

    destroy_parser(&arena);
    return 1;
}

static int test_error_recovery_continues(void) {
    Seraph_Arena arena = {0};
    Seraph_Lexer lexer = {0};
    Seraph_Parser parser = {0};

    const char* source =
        "fn broken( { }\n"  /* Missing param close */
        "fn good() { }\n";  /* Should still parse */

    int ok = create_parser(source, &lexer, &parser, &arena);
    ASSERT(ok, "Parser creation should succeed");

    Seraph_AST_Node* module = seraph_parse_module(&parser);
    ASSERT_NOT_NULL(module, "Module should not be NULL");
    ASSERT(seraph_parser_has_errors(&parser), "Should have errors");
    /* Error recovery should produce a module even with errors */

    destroy_parser(&arena);
    return 1;
}

/*============================================================================
 * Test Runner
 *============================================================================*/

void run_seraphim_parser_tests(void) {
    tests_run = 0;
    tests_passed = 0;

    printf("\n=== MC26: Seraphim Parser Tests ===\n\n");

    printf("Initialization:\n");
    RUN_TEST(test_parser_init_null);
    RUN_TEST(test_parser_init_success);

    printf("\nLiteral Expressions:\n");
    RUN_TEST(test_parse_integer_literal);
    RUN_TEST(test_parse_hex_literal);
    RUN_TEST(test_parse_binary_literal);
    RUN_TEST(test_parse_float_literal);
    RUN_TEST(test_parse_string_literal);
    RUN_TEST(test_parse_bool_literals);
    RUN_TEST(test_parse_void_literal);

    printf("\nBinary Expressions:\n");
    RUN_TEST(test_parse_binary_add);
    RUN_TEST(test_parse_binary_precedence);
    RUN_TEST(test_parse_comparison);
    RUN_TEST(test_parse_logical_and_or);

    printf("\nUnary Expressions:\n");
    RUN_TEST(test_parse_unary_negation);
    RUN_TEST(test_parse_unary_not);

    printf("\nVOID Operators:\n");
    RUN_TEST(test_parse_void_propagation);
    RUN_TEST(test_parse_void_assertion);
    RUN_TEST(test_parse_void_coalesce);

    printf("\nCall Expressions:\n");
    RUN_TEST(test_parse_function_call);
    RUN_TEST(test_parse_method_call);

    printf("\nField/Index Access:\n");
    RUN_TEST(test_parse_field_access);
    RUN_TEST(test_parse_index_access);
    RUN_TEST(test_parse_chained_access);

    printf("\nIf Expressions:\n");
    RUN_TEST(test_parse_if_expr);
    RUN_TEST(test_parse_if_no_else);

    printf("\nMatch Expressions:\n");
    RUN_TEST(test_parse_match_expr);

    printf("\nType Parsing:\n");
    RUN_TEST(test_parse_type_simple);
    RUN_TEST(test_parse_type_pointer);
    RUN_TEST(test_parse_type_array);
    RUN_TEST(test_parse_type_slice);
    RUN_TEST(test_parse_type_voidable);

    printf("\nFunction Declarations:\n");
    RUN_TEST(test_parse_fn_simple);
    RUN_TEST(test_parse_fn_with_params);
    RUN_TEST(test_parse_fn_with_effects);

    printf("\nLet/Const Declarations:\n");
    RUN_TEST(test_parse_let_simple);
    RUN_TEST(test_parse_let_with_type);
    RUN_TEST(test_parse_const);

    printf("\nStruct Declarations:\n");
    RUN_TEST(test_parse_struct_simple);

    printf("\nEnum Declarations:\n");
    RUN_TEST(test_parse_enum_simple);

    printf("\nStatements:\n");
    RUN_TEST(test_parse_return_stmt);
    RUN_TEST(test_parse_while_stmt);
    RUN_TEST(test_parse_for_stmt);
    RUN_TEST(test_parse_block);

    printf("\nModule Parsing:\n");
    RUN_TEST(test_parse_module);

    printf("\nPipe Operator:\n");
    RUN_TEST(test_parse_pipe_operator);

    printf("\nError Recovery:\n");
    RUN_TEST(test_error_missing_semicolon);
    RUN_TEST(test_error_recovery_continues);

    printf("\nSeraphim Parser: %d/%d tests passed\n", tests_passed, tests_run);
}
