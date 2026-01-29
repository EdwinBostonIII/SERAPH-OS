/**
 * @file seraphic.c
 * @brief Seraphim Compiler Driver - The SERAPH Native Compiler
 *
 * This is the main entry point for the Seraphim compiler. It orchestrates
 * the complete compilation pipeline:
 *
 *   Source File → Lexer → Parser → Checker → IR Generator → Backend → ELF
 *
 * Usage:
 *   seraphic [options] <source-file>
 *
 * Options:
 *   -o <file>     Output file (default: a.out)
 *   --emit-ir     Output Celestial IR (for debugging)
 *   --emit-asm    Output assembly-like listing
 *   --emit-c      Output C code (transpilation mode)
 *   -O<n>         Optimization level (0-3)
 *   --target=<t>  Target architecture (x64, arm64, riscv64)
 *   --help        Show help
 *   --version     Show version
 *
 * NIH Compliance: This compiler has ZERO external dependencies.
 * It generates native executables without LLVM, GCC, or any external toolchain.
 *
 * Self-Hosting Plan:
 *   Stage 0: seraphic (this file, written in C) compiles Seraphim
 *   Stage 1: seraphic.srph compiles itself using Stage 0
 *   Stage 2: seraphic.srph compiles itself using Stage 1 (verification)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "seraph/arena.h"
#include "seraph/vbit.h"
#include "seraph/seraphim/lexer.h"
#include "seraph/seraphim/parser.h"
#include "seraph/seraphim/checker.h"
#include "seraph/seraphim/celestial_ir.h"
#include "seraph/seraphim/celestial_to_x64.h"
#include "seraph/seraphim/celestial_to_arm64.h"
#include "seraph/seraphim/celestial_to_riscv.h"
#include "seraph/seraphim/x64_encode.h"
#include "seraph/seraphim/arm64_encode.h"
#include "seraph/seraphim/riscv_encode.h"
#include "seraph/seraphim/elf64_writer.h"
#include "seraph/seraphim/codegen.h"
#include "seraph/seraphim/proofs.h"
#include "seraph/seraphim/ast_to_ir.h"

/*============================================================================
 * Version Information
 *============================================================================*/

#define SERAPHIC_VERSION_MAJOR 0
#define SERAPHIC_VERSION_MINOR 1
#define SERAPHIC_VERSION_PATCH 0
#define SERAPHIC_VERSION_STRING "0.1.0"

/*============================================================================
 * Compilation Options
 *============================================================================*/

typedef enum {
    TARGET_X64,
    TARGET_ARM64,
    TARGET_RISCV64,
} Seraphic_Target;

typedef enum {
    OUTPUT_EXECUTABLE,
    OUTPUT_OBJECT,
    OUTPUT_IR,
    OUTPUT_ASM,
    OUTPUT_C,
} Seraphic_Output_Type;

typedef struct {
    const char*         input_file;
    const char*         output_file;
    Seraphic_Target     target;
    Seraphic_Output_Type output_type;
    int                 opt_level;
    int                 debug_info;
    int                 verbose;
    int                 show_help;
    int                 show_version;
} Seraphic_Options;

/*============================================================================
 * Forward Declarations
 *============================================================================*/

static void print_usage(const char* program);
static void print_version(void);
static int parse_args(int argc, char** argv, Seraphic_Options* opts);
static char* read_file(const char* filename, size_t* out_size);
static Seraph_Vbit compile_to_c(Seraphic_Options* opts, const char* source, size_t source_len);
static Seraph_Vbit compile_to_native(Seraphic_Options* opts, const char* source, size_t source_len);
static Celestial_Module* ast_to_celestial_ir(Seraph_AST_Node* module_ast,
                                              Seraph_Type_Context* types,
                                              Seraph_Arena* arena);

/*============================================================================
 * Main Entry Point
 *============================================================================*/

int main(int argc, char** argv) {
    Seraphic_Options opts = {0};
    opts.target = TARGET_X64;
    opts.output_type = OUTPUT_EXECUTABLE;
    opts.output_file = "a.out";
    opts.opt_level = 0;

    /* Parse command-line arguments */
    if (parse_args(argc, argv, &opts) != 0) {
        return 1;
    }

    if (opts.show_help) {
        print_usage(argv[0]);
        return 0;
    }

    if (opts.show_version) {
        print_version();
        return 0;
    }

    if (!opts.input_file) {
        fprintf(stderr, "Error: No input file specified\n");
        print_usage(argv[0]);
        return 1;
    }

    /* Read source file */
    size_t source_len;
    char* source = read_file(opts.input_file, &source_len);
    if (!source) {
        fprintf(stderr, "Error: Could not read file '%s'\n", opts.input_file);
        return 1;
    }

    if (opts.verbose) {
        printf("Compiling '%s' (%zu bytes)\n", opts.input_file, source_len);
    }

    /* Select compilation path */
    Seraph_Vbit result;
    if (opts.output_type == OUTPUT_C) {
        result = compile_to_c(&opts, source, source_len);
    } else {
        result = compile_to_native(&opts, source, source_len);
    }

    free(source);

    if (!seraph_vbit_is_true(result)) {
        fprintf(stderr, "Compilation failed.\n");
        return 1;
    }

    if (opts.verbose) {
        printf("Successfully compiled to '%s'\n", opts.output_file);
    }

    return 0;
}

/*============================================================================
 * Argument Parsing
 *============================================================================*/

static int parse_args(int argc, char** argv, Seraphic_Options* opts) {
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            opts->show_help = 1;
        } else if (strcmp(arg, "--version") == 0 || strcmp(arg, "-V") == 0) {
            opts->show_version = 1;
        } else if (strcmp(arg, "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -o requires an argument\n");
                return 1;
            }
            opts->output_file = argv[++i];
        } else if (strcmp(arg, "--emit-ir") == 0) {
            opts->output_type = OUTPUT_IR;
        } else if (strcmp(arg, "--emit-asm") == 0) {
            opts->output_type = OUTPUT_ASM;
        } else if (strcmp(arg, "--emit-c") == 0) {
            opts->output_type = OUTPUT_C;
        } else if (strncmp(arg, "-O", 2) == 0) {
            opts->opt_level = arg[2] - '0';
            if (opts->opt_level < 0 || opts->opt_level > 3) {
                opts->opt_level = 0;
            }
        } else if (strcmp(arg, "-g") == 0) {
            opts->debug_info = 1;
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            opts->verbose = 1;
        } else if (strncmp(arg, "--target=", 9) == 0) {
            const char* target = arg + 9;
            if (strcmp(target, "x64") == 0 || strcmp(target, "x86_64") == 0) {
                opts->target = TARGET_X64;
            } else if (strcmp(target, "arm64") == 0 || strcmp(target, "aarch64") == 0) {
                opts->target = TARGET_ARM64;
            } else if (strcmp(target, "riscv64") == 0) {
                opts->target = TARGET_RISCV64;
            } else {
                fprintf(stderr, "Error: Unknown target '%s'\n", target);
                return 1;
            }
        } else if (arg[0] == '-') {
            fprintf(stderr, "Error: Unknown option '%s'\n", arg);
            return 1;
        } else {
            /* Input file */
            opts->input_file = arg;
        }
    }

    return 0;
}

static void print_usage(const char* program) {
    printf("Seraphim Compiler v%s\n", SERAPHIC_VERSION_STRING);
    printf("Usage: %s [options] <source-file>\n", program);
    printf("\nOptions:\n");
    printf("  -o <file>       Output file (default: a.out)\n");
    printf("  --emit-ir       Output Celestial IR\n");
    printf("  --emit-asm      Output assembly listing\n");
    printf("  --emit-c        Output C code (transpilation)\n");
    printf("  -O<n>           Optimization level (0-3)\n");
    printf("  -g              Include debug info\n");
    printf("  -v, --verbose   Verbose output\n");
    printf("  --target=<t>    Target: x64, arm64, riscv64\n");
    printf("  --help, -h      Show this help\n");
    printf("  --version, -V   Show version\n");
}

static void print_version(void) {
    printf("Seraphim Compiler (seraphic) v%s\n", SERAPHIC_VERSION_STRING);
    printf("Copyright (c) SERAPH Project\n");
    printf("Built with NIH-compliant toolchain (no external dependencies)\n");
}

/*============================================================================
 * File I/O
 *============================================================================*/

static char* read_file(const char* filename, size_t* out_size) {
    FILE* f = fopen(filename, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0) {
        fclose(f);
        return NULL;
    }

    char* buffer = (char*)malloc((size_t)size + 1);
    if (!buffer) {
        fclose(f);
        return NULL;
    }

    size_t read = fread(buffer, 1, (size_t)size, f);
    fclose(f);

    buffer[read] = '\0';
    *out_size = read;

    return buffer;
}

/*============================================================================
 * C Code Generation Path
 *============================================================================*/

static Seraph_Vbit compile_to_c(Seraphic_Options* opts, const char* source, size_t source_len) {
    Seraph_Arena arena;
    if (!seraph_vbit_is_true(seraph_arena_create(&arena, 1024 * 1024, 0, 0))) {
        fprintf(stderr, "Error: Failed to create arena\n");
        return SERAPH_VBIT_FALSE;
    }

    /* Lex */
    Seraph_Lexer lexer;
    if (!seraph_vbit_is_true(seraph_lexer_init(&lexer, source, source_len,
                                                opts->input_file, &arena))) {
        seraph_arena_destroy(&arena);
        return SERAPH_VBIT_FALSE;
    }

    if (!seraph_vbit_is_true(seraph_lexer_tokenize(&lexer))) {
        seraph_lexer_print_diagnostics(&lexer);
        seraph_arena_destroy(&arena);
        return SERAPH_VBIT_FALSE;
    }

    if (opts->verbose) {
        printf("Lexed %zu tokens\n", lexer.token_count);
    }

    /* Parse */
    Seraph_Parser parser;
    if (!seraph_vbit_is_true(seraph_parser_init(&parser, &lexer, &arena))) {
        seraph_arena_destroy(&arena);
        return SERAPH_VBIT_FALSE;
    }

    Seraph_AST_Node* module_ast = seraph_parse_module(&parser);
    if (seraph_parser_has_errors(&parser)) {
        seraph_parser_print_diagnostics(&parser);
        seraph_arena_destroy(&arena);
        return SERAPH_VBIT_FALSE;
    }

    if (opts->verbose) {
        printf("Parsed AST successfully\n");
    }

    /* Generate C code */
    FILE* output = fopen(opts->output_file, "w");
    if (!output) {
        fprintf(stderr, "Error: Could not open output file '%s'\n", opts->output_file);
        seraph_arena_destroy(&arena);
        return SERAPH_VBIT_FALSE;
    }

    Seraph_Codegen codegen;
    if (!seraph_vbit_is_true(seraph_codegen_init(&codegen, output, &arena))) {
        fclose(output);
        seraph_arena_destroy(&arena);
        return SERAPH_VBIT_FALSE;
    }

    seraph_codegen_set_options(&codegen, SERAPH_CODEGEN_OPT_DEBUG);
    seraph_codegen_module(&codegen, module_ast);

    fclose(output);
    seraph_arena_destroy(&arena);

    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * Native Code Generation Path
 *============================================================================*/

static Seraph_Vbit compile_to_native(Seraphic_Options* opts, const char* source, size_t source_len) {
    Seraph_Arena arena;
    if (!seraph_vbit_is_true(seraph_arena_create(&arena, 64 * 1024 * 1024, 0, 0))) {
        fprintf(stderr, "Error: Failed to create arena\n");
        return SERAPH_VBIT_FALSE;
    }

    /* Lex */
    Seraph_Lexer lexer;
    if (!seraph_vbit_is_true(seraph_lexer_init(&lexer, source, source_len,
                                                opts->input_file, &arena))) {
        seraph_arena_destroy(&arena);
        return SERAPH_VBIT_FALSE;
    }

    if (!seraph_vbit_is_true(seraph_lexer_tokenize(&lexer))) {
        seraph_lexer_print_diagnostics(&lexer);
        seraph_arena_destroy(&arena);
        return SERAPH_VBIT_FALSE;
    }

    if (opts->verbose) {
        printf("Lexed %zu tokens\n", lexer.token_count);
    }

    /* Parse */
    Seraph_Parser parser;
    if (!seraph_vbit_is_true(seraph_parser_init(&parser, &lexer, &arena))) {
        seraph_arena_destroy(&arena);
        return SERAPH_VBIT_FALSE;
    }

    Seraph_AST_Node* module_ast = seraph_parse_module(&parser);
    if (seraph_parser_has_errors(&parser)) {
        seraph_parser_print_diagnostics(&parser);
        seraph_arena_destroy(&arena);
        return SERAPH_VBIT_FALSE;
    }

    if (opts->verbose) {
        printf("Parsed AST successfully\n");
    }

    /* Type check */
    Seraph_Type_Context type_ctx;
    /* TODO: Initialize type context and run semantic analysis */

    /* Generate Celestial IR */
    Celestial_Module* ir_module = ast_to_celestial_ir(module_ast, &type_ctx, &arena);
    if (!ir_module) {
        fprintf(stderr, "Error: Failed to generate IR\n");
        seraph_arena_destroy(&arena);
        return SERAPH_VBIT_FALSE;
    }

    if (opts->verbose) {
        printf("Generated Celestial IR with %zu functions\n", ir_module->function_count);
    }

    /* Verify IR */
    if (!seraph_vbit_is_true(celestial_verify_module(ir_module))) {
        fprintf(stderr, "Error: IR verification failed\n");
        seraph_arena_destroy(&arena);
        return SERAPH_VBIT_FALSE;
    }

    /* Run optimization passes */
    int folded = celestial_fold_constants(ir_module);
    if (opts->verbose && folded > 0) {
        printf("Constant folding: %d instructions folded\n", folded);
    }

    int eliminated = celestial_eliminate_dead_code(ir_module);
    if (opts->verbose && eliminated > 0) {
        printf("Dead code elimination: %d instructions removed\n", eliminated);
    }

    /* Output IR if requested */
    if (opts->output_type == OUTPUT_IR) {
        FILE* ir_out = fopen(opts->output_file, "w");
        if (ir_out) {
            celestial_print_module(ir_module, ir_out);
            fclose(ir_out);
        }
        seraph_arena_destroy(&arena);
        return SERAPH_VBIT_TRUE;
    }

    /* Generate ELF executable for target architecture */
    Seraph_Proof_Table proofs = {0};  /* TODO: Generate proofs from checker */

    /* Map compiler target to ELF target */
    Seraph_Elf_Target elf_target;
    switch (opts->target) {
        case TARGET_X64:
            elf_target = SERAPH_ELF_TARGET_X64;
            break;
        case TARGET_ARM64:
            elf_target = SERAPH_ELF_TARGET_ARM64;
            break;
        case TARGET_RISCV64:
            elf_target = SERAPH_ELF_TARGET_RISCV64;
            break;
        default:
            fprintf(stderr, "Error: Unknown target architecture\n");
            seraph_arena_destroy(&arena);
            return SERAPH_VBIT_FALSE;
    }

    if (!seraph_vbit_is_true(seraph_elf_from_celestial_target(ir_module, &proofs,
                                                               elf_target,
                                                               opts->output_file))) {
        fprintf(stderr, "Error: Failed to generate executable\n");
        seraph_arena_destroy(&arena);
        return SERAPH_VBIT_FALSE;
    }

    seraph_arena_destroy(&arena);
    return SERAPH_VBIT_TRUE;
}

/*============================================================================
 * AST to Celestial IR Conversion
 *============================================================================*/

/**
 * @brief Convert AST module to Celestial IR
 *
 * This is the bridge between the Seraphim frontend (AST) and the
 * backend (Celestial IR → machine code).
 *
 * This function calls the full ir_convert_module from ast_to_ir.c
 */
static Celestial_Module* ast_to_celestial_ir(Seraph_AST_Node* module_ast,
                                              Seraph_Type_Context* types,
                                              Seraph_Arena* arena) {
    return ir_convert_module(module_ast, types, arena);
}
