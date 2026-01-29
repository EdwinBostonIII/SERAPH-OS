/**
 * @file fpu_check.c
 * @brief SERAPH Compiler FPU Enforcement
 *
 * MC26: SERAPH Performance Revolution - Pillar 6
 *
 * Static analysis pass to detect and reject FPU instruction generation.
 * Ensures the Zero-FPU guarantee at compile time.
 *
 * Detection Strategy:
 *   1. Scan generated assembly for FPU/SSE/AVX opcodes
 *   2. Since SERAPH IR has no floating-point types (only Scalar/Dual/Galactic),
 *      IR checking verifies no external float-using functions are called
 *
 * Enforcement Levels:
 *   - SERAPH_FPU_WARN: Emit warning for FPU usage
 *   - SERAPH_FPU_ERROR: Fail compilation on FPU usage
 *   - SERAPH_FPU_ALLOW: No enforcement (default for non-kernel code)
 */

#include "seraph/seraphim/celestial_ir.h"
#include "seraph/vbit.h"
#include <string.h>
#include <stdio.h>

/*============================================================================
 * FPU Instruction Patterns
 *============================================================================*/

/**
 * @brief x86-64 FPU instruction mnemonics to detect
 */
static const char* fpu_mnemonics[] = {
    /* x87 FPU */
    "fld", "fst", "fstp", "fild", "fist", "fistp",
    "fadd", "fsub", "fmul", "fdiv", "fabs", "fchs",
    "fsqrt", "fsin", "fcos", "fptan", "fpatan",
    "f2xm1", "fyl2x", "fyl2xp1", "fscale",
    "fxch", "fcom", "fcomp", "fcompp", "ftst",
    "fldz", "fld1", "fldpi", "fldl2e", "fldl2t",
    "finit", "fninit", "fclex", "fnclex",

    /* SSE floating-point */
    "addss", "addsd", "addps", "addpd",
    "subss", "subsd", "subps", "subpd",
    "mulss", "mulsd", "mulps", "mulpd",
    "divss", "divsd", "divps", "divpd",
    "sqrtss", "sqrtsd", "sqrtps", "sqrtpd",
    "maxss", "maxsd", "maxps", "maxpd",
    "minss", "minsd", "minps", "minpd",
    "movss", "movsd", "movaps", "movapd",
    "movups", "movupd", "movlps", "movlpd",
    "movhps", "movhpd", "movhlps", "movlhps",
    "cvtsi2ss", "cvtsi2sd", "cvtss2si", "cvtsd2si",
    "cvtss2sd", "cvtsd2ss", "cvtps2pd", "cvtpd2ps",
    "cvttss2si", "cvttsd2si", "cvtps2dq", "cvttpd2dq",
    "ucomiss", "ucomisd", "comiss", "comisd",
    "cmpss", "cmpsd", "cmpps", "cmppd",
    "andps", "andpd", "andnps", "andnpd",
    "orps", "orpd", "xorps", "xorpd",
    "unpcklps", "unpcklpd", "unpckhps", "unpckhpd",
    "shufps", "shufpd",
    "rcpss", "rcpps", "rsqrtss", "rsqrtps",

    /* AVX floating-point (v-prefixed) */
    "vaddss", "vaddsd", "vaddps", "vaddpd",
    "vsubss", "vsubsd", "vsubps", "vsubpd",
    "vmulss", "vmulsd", "vmulps", "vmulpd",
    "vdivss", "vdivsd", "vdivps", "vdivpd",
    "vsqrtss", "vsqrtsd", "vsqrtps", "vsqrtpd",
    "vfmadd", "vfmsub", "vfnmadd", "vfnmsub",

    NULL  /* Sentinel */
};

/*============================================================================
 * FPU Check Context
 *============================================================================*/

typedef struct {
    int warnings;           /**< Number of warnings emitted */
    int errors;             /**< Number of errors emitted */
    int enforcement_level;  /**< 0=allow, 1=warn, 2=error */
    const char* current_fn; /**< Current function being checked */

    /* Collected violations */
    struct {
        const char* location;
        const char* instruction;
    } violations[256];
    int violation_count;
} FPU_Check_Context;

static FPU_Check_Context fpu_ctx;

/*============================================================================
 * Assembly Scanning
 *============================================================================*/

/**
 * @brief Check if a string starts with an FPU mnemonic
 */
static int is_fpu_mnemonic(const char* line) {
    /* Skip whitespace */
    while (*line == ' ' || *line == '\t') line++;

    /* Check against known FPU mnemonics */
    for (int i = 0; fpu_mnemonics[i] != NULL; i++) {
        size_t len = strlen(fpu_mnemonics[i]);
        if (strncmp(line, fpu_mnemonics[i], len) == 0) {
            /* Must be followed by whitespace or end of line */
            char next = line[len];
            if (next == ' ' || next == '\t' || next == '\n' ||
                next == '\r' || next == '\0') {
                return 1;
            }
        }
    }

    return 0;
}

/**
 * @brief Scan assembly output for FPU instructions
 *
 * @param asm_text Assembly text to scan
 * @param filename Source filename for error reporting
 * @return Number of FPU instructions found
 */
int seraph_fpu_scan_asm(const char* asm_text, const char* filename) {
    if (asm_text == NULL) return 0;

    int count = 0;
    const char* line = asm_text;

    while (*line) {
        if (is_fpu_mnemonic(line)) {
            count++;

            if (fpu_ctx.violation_count < 256) {
                fpu_ctx.violations[fpu_ctx.violation_count].location = filename;

                /* Extract the mnemonic */
                const char* start = line;
                while (*start == ' ' || *start == '\t') start++;

                fpu_ctx.violations[fpu_ctx.violation_count].instruction = start;
                fpu_ctx.violation_count++;
            }

            if (fpu_ctx.enforcement_level >= 2) {
                fpu_ctx.errors++;
            } else if (fpu_ctx.enforcement_level >= 1) {
                fpu_ctx.warnings++;
            }
        }

        /* Advance to next line */
        while (*line && *line != '\n') line++;
        if (*line == '\n') line++;
    }

    return count;
}

/*============================================================================
 * IR Checking
 *
 * Note: SERAPH's Celestial IR has NO floating-point types. It uses:
 *   - CIR_TYPE_SCALAR (Q64.64 fixed-point)
 *   - CIR_TYPE_DUAL (dual numbers)
 *   - CIR_TYPE_GALACTIC (hyper-dual quaternions)
 *
 * All math is done with integer operations. This check verifies
 * that no external floating-point functions are called.
 *============================================================================*/

/**
 * @brief Known floating-point function names to detect
 */
static const char* float_function_names[] = {
    "sinf", "cosf", "tanf", "sqrtf", "expf", "logf", "powf",
    "sin", "cos", "tan", "sqrt", "exp", "log", "pow",
    "sinl", "cosl", "tanl", "sqrtl", "expl", "logl", "powl",
    "floorf", "ceilf", "fabsf", "fmodf",
    "floor", "ceil", "fabs", "fmod",
    "floorl", "ceill", "fabsl", "fmodl",
    NULL
};

/**
 * @brief Check if a function name is a known float function
 */
static int is_float_function(const char* name) {
    if (name == NULL) return 0;

    for (int i = 0; float_function_names[i] != NULL; i++) {
        if (strcmp(name, float_function_names[i]) == 0) {
            return 1;
        }
    }

    return 0;
}

/**
 * @brief Check a function for calls to floating-point functions
 *
 * @param func Function to check
 * @return Number of float function calls found
 */
int seraph_fpu_check_function(const Celestial_Function* func) {
    if (func == NULL) return 0;

    int count = 0;
    fpu_ctx.current_fn = func->name;

    /* Check all basic blocks */
    for (Celestial_Block* block = func->blocks; block; block = block->next) {
        /* Check all instructions */
        for (Celestial_Instr* instr = block->first; instr; instr = instr->next) {
            /* Check for calls to float functions */
            if (instr->opcode == CIR_CALL && instr->callee) {
                if (is_float_function(instr->callee->name)) {
                    count++;
                    if (fpu_ctx.violation_count < 256) {
                        fpu_ctx.violations[fpu_ctx.violation_count].location = func->name;
                        fpu_ctx.violations[fpu_ctx.violation_count].instruction = instr->callee->name;
                        fpu_ctx.violation_count++;
                    }
                }
            }
        }
    }

    if (count > 0) {
        if (fpu_ctx.enforcement_level >= 2) {
            fpu_ctx.errors += count;
        } else if (fpu_ctx.enforcement_level >= 1) {
            fpu_ctx.warnings += count;
        }
    }

    return count;
}

/**
 * @brief Check entire module for FPU usage
 *
 * @param module Module to check
 * @return Total FPU operations found
 */
int seraph_fpu_check_module(const Celestial_Module* module) {
    if (module == NULL) return 0;

    int total = 0;

    /* Reset context */
    memset(&fpu_ctx, 0, sizeof(fpu_ctx));
    fpu_ctx.enforcement_level = 2;  /* Default: error on FPU */

    /* Check all functions */
    for (Celestial_Function* func = module->functions; func; func = func->next) {
        total += seraph_fpu_check_function(func);
    }

    return total;
}

/*============================================================================
 * Configuration
 *============================================================================*/

/**
 * @brief Set FPU enforcement level
 *
 * @param level 0=allow, 1=warn, 2=error
 */
void seraph_fpu_set_enforcement(int level) {
    fpu_ctx.enforcement_level = level;
}

/**
 * @brief Get current enforcement level
 */
int seraph_fpu_get_enforcement(void) {
    return fpu_ctx.enforcement_level;
}

/**
 * @brief Check if any errors were found
 */
int seraph_fpu_has_errors(void) {
    return fpu_ctx.errors > 0;
}

/**
 * @brief Check if any warnings were found
 */
int seraph_fpu_has_warnings(void) {
    return fpu_ctx.warnings > 0;
}

/*============================================================================
 * Reporting
 *============================================================================*/

/**
 * @brief Print all violations
 */
void seraph_fpu_print_violations(void) {
    if (fpu_ctx.violation_count == 0) {
        fprintf(stderr, "[FPU-CHECK] No floating-point usage detected.\n");
        return;
    }

    fprintf(stderr, "[FPU-CHECK] Found %d floating-point violations:\n",
            fpu_ctx.violation_count);

    for (int i = 0; i < fpu_ctx.violation_count; i++) {
        const char* level = (fpu_ctx.enforcement_level >= 2) ? "error" : "warning";
        fprintf(stderr, "  %s: %s: %s\n",
                level,
                fpu_ctx.violations[i].location ? fpu_ctx.violations[i].location : "<unknown>",
                fpu_ctx.violations[i].instruction ? fpu_ctx.violations[i].instruction : "<unknown>");
    }

    fprintf(stderr, "[FPU-CHECK] Total: %d errors, %d warnings\n",
            fpu_ctx.errors, fpu_ctx.warnings);
}

/**
 * @brief Get violation count
 */
int seraph_fpu_violation_count(void) {
    return fpu_ctx.violation_count;
}

/*============================================================================
 * Attribute Checking
 *============================================================================*/

/**
 * @brief Check if function has integer-only attribute
 *
 * Functions marked with #[integer_only] must not use floats.
 * In SERAPH, all q16_/q32_/q64_ functions should be integer-only.
 */
int seraph_fpu_check_integer_only_attr(const Celestial_Function* func) {
    if (func == NULL) return 0;

    /* Check function name prefix for integer-only convention */
    if (func->name && strncmp(func->name, "q16_", 4) == 0) {
        return 1;  /* Q16 functions should be integer-only */
    }
    if (func->name && strncmp(func->name, "q32_", 4) == 0) {
        return 1;
    }
    if (func->name && strncmp(func->name, "q64_", 4) == 0) {
        return 1;
    }
    if (func->name && strncmp(func->name, "seraph_", 7) == 0) {
        return 1;  /* All SERAPH functions should be integer-only */
    }

    return 0;
}

/**
 * @brief Validate integer-only functions
 */
Seraph_Vbit seraph_fpu_validate_integer_only(const Celestial_Module* module) {
    if (module == NULL) return SERAPH_VBIT_VOID;

    int violations = 0;

    for (Celestial_Function* func = module->functions; func; func = func->next) {
        if (seraph_fpu_check_integer_only_attr(func)) {
            int count = seraph_fpu_check_function(func);
            if (count > 0) {
                fprintf(stderr, "[FPU-CHECK] Error: integer-only function '%s' uses FPU\n",
                        func->name);
                violations++;
            }
        }
    }

    return (violations == 0) ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}
