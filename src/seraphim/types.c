/**
 * @file types.c
 * @brief Seraphim Compiler - Type System Implementation
 *
 * MC26: Seraphim Language Type System
 *
 * Implements type construction, checking, inference, and unification.
 */

#include "seraph/seraphim/types.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

/*============================================================================
 * Type Construction Helpers
 *============================================================================*/

static Seraph_Type* alloc_type(Seraph_Arena* arena, Seraph_Type_Kind kind) {
    if (arena == NULL) return NULL;

    Seraph_Type* t = (Seraph_Type*)seraph_arena_alloc(
        arena, sizeof(Seraph_Type), _Alignof(Seraph_Type)
    );
    if (t == NULL) return NULL;

    memset(t, 0, sizeof(Seraph_Type));
    t->kind = kind;
    return t;
}

/*============================================================================
 * Type Construction
 *============================================================================*/

Seraph_Type* seraph_type_prim(Seraph_Arena* arena, Seraph_Type_Kind kind) {
    /* Validate it's actually a primitive kind */
    if (kind > SERAPH_TYPE_GALACTIC && kind != SERAPH_TYPE_BOOL &&
        kind != SERAPH_TYPE_CHAR && kind != SERAPH_TYPE_UNIT) {
        return seraph_type_void(arena);
    }
    return alloc_type(arena, kind);
}

Seraph_Type* seraph_type_array(Seraph_Arena* arena, Seraph_Type* elem, uint64_t size) {
    if (seraph_type_is_void(elem)) return seraph_type_void(arena);

    Seraph_Type* t = alloc_type(arena, SERAPH_TYPE_ARRAY);
    if (t == NULL) return NULL;
    t->array.elem = elem;
    t->array.size = size;
    return t;
}

Seraph_Type* seraph_type_slice(Seraph_Arena* arena, Seraph_Type* elem) {
    if (seraph_type_is_void(elem)) return seraph_type_void(arena);

    Seraph_Type* t = alloc_type(arena, SERAPH_TYPE_SLICE);
    if (t == NULL) return NULL;
    t->slice.elem = elem;
    return t;
}

Seraph_Type* seraph_type_tuple(Seraph_Arena* arena, Seraph_Type** elems, size_t count) {
    if (elems == NULL && count > 0) return seraph_type_void(arena);

    Seraph_Type* t = alloc_type(arena, SERAPH_TYPE_TUPLE);
    if (t == NULL) return NULL;

    /* Copy element types */
    if (count > 0) {
        t->tuple.elems = (Seraph_Type**)seraph_arena_alloc(
            arena, count * sizeof(Seraph_Type*), _Alignof(Seraph_Type*)
        );
        if (t->tuple.elems == NULL) return seraph_type_void(arena);
        memcpy(t->tuple.elems, elems, count * sizeof(Seraph_Type*));
    }
    t->tuple.count = count;
    return t;
}

Seraph_Type* seraph_type_ref(Seraph_Arena* arena, Seraph_Type* inner,
                              int is_mut, Seraph_Substrate substrate) {
    if (seraph_type_is_void(inner)) return seraph_type_void(arena);

    Seraph_Type* t = alloc_type(arena, is_mut ? SERAPH_TYPE_REF_MUT : SERAPH_TYPE_REF);
    if (t == NULL) return NULL;
    t->ref.inner = inner;
    t->ref.is_mut = is_mut;
    t->ref.substrate = substrate;
    return t;
}

Seraph_Type* seraph_type_voidable(Seraph_Arena* arena, Seraph_Type* inner) {
    if (seraph_type_is_void(inner)) return seraph_type_void(arena);

    /* ????T = ??T (voidable of voidable collapses) */
    if (inner->kind == SERAPH_TYPE_VOIDABLE) {
        return inner;
    }

    Seraph_Type* t = alloc_type(arena, SERAPH_TYPE_VOIDABLE);
    if (t == NULL) return NULL;
    t->voidable.inner = inner;
    return t;
}

Seraph_Type* seraph_type_fn(Seraph_Arena* arena,
                             Seraph_Type** params, size_t param_count,
                             Seraph_Type* ret, Seraph_Effect_Flags effects) {
    Seraph_Type* t = alloc_type(arena, SERAPH_TYPE_FN);
    if (t == NULL) return NULL;

    /* Copy parameter types */
    if (param_count > 0) {
        t->fn.params = (Seraph_Type**)seraph_arena_alloc(
            arena, param_count * sizeof(Seraph_Type*), _Alignof(Seraph_Type*)
        );
        if (t->fn.params == NULL) return seraph_type_void(arena);
        memcpy(t->fn.params, params, param_count * sizeof(Seraph_Type*));
    }
    t->fn.param_count = param_count;
    t->fn.ret = ret ? ret : seraph_type_unit(arena);
    t->fn.effects = effects;
    return t;
}

Seraph_Type* seraph_type_var(Seraph_Type_Context* ctx,
                              const char* name, size_t name_len) {
    if (ctx == NULL) return NULL;

    Seraph_Type* t = alloc_type(ctx->arena, SERAPH_TYPE_TYPEVAR);
    if (t == NULL) return NULL;
    t->typevar.id = ctx->next_typevar_id++;
    t->typevar.name = name;
    t->typevar.name_len = name_len;
    t->typevar.bound = NULL;
    return t;
}

Seraph_Type* seraph_type_void(Seraph_Arena* arena) {
    return alloc_type(arena, SERAPH_TYPE_VOID);
}

Seraph_Type* seraph_type_unit(Seraph_Arena* arena) {
    return alloc_type(arena, SERAPH_TYPE_UNIT);
}

Seraph_Type* seraph_type_never(Seraph_Arena* arena) {
    return alloc_type(arena, SERAPH_TYPE_NEVER);
}

/*============================================================================
 * Type Queries
 *============================================================================*/

int seraph_type_is_integer(const Seraph_Type* t) {
    if (t == NULL) return 0;
    return t->kind >= SERAPH_TYPE_U8 && t->kind <= SERAPH_TYPE_I64;
}

int seraph_type_is_numeric(const Seraph_Type* t) {
    if (t == NULL) return 0;
    return seraph_type_is_integer(t) ||
           t->kind == SERAPH_TYPE_SCALAR ||
           t->kind == SERAPH_TYPE_DUAL ||
           t->kind == SERAPH_TYPE_GALACTIC;
}

int seraph_type_is_ref(const Seraph_Type* t) {
    if (t == NULL) return 0;
    return t->kind == SERAPH_TYPE_REF || t->kind == SERAPH_TYPE_REF_MUT;
}

int seraph_type_is_voidable(const Seraph_Type* t) {
    if (t == NULL) return 0;
    return t->kind == SERAPH_TYPE_VOIDABLE;
}

int seraph_type_is_copy(const Seraph_Type* t) {
    if (t == NULL) return 0;

    switch (t->kind) {
        /* Primitives are Copy */
        case SERAPH_TYPE_U8:
        case SERAPH_TYPE_U16:
        case SERAPH_TYPE_U32:
        case SERAPH_TYPE_U64:
        case SERAPH_TYPE_I8:
        case SERAPH_TYPE_I16:
        case SERAPH_TYPE_I32:
        case SERAPH_TYPE_I64:
        case SERAPH_TYPE_BOOL:
        case SERAPH_TYPE_CHAR:
        case SERAPH_TYPE_UNIT:
        case SERAPH_TYPE_SCALAR:
        case SERAPH_TYPE_DUAL:
        case SERAPH_TYPE_GALACTIC:
            return 1;

        /* Immutable references are Copy */
        case SERAPH_TYPE_REF:
            return 1;

        /* VOID-able of Copy is Copy */
        case SERAPH_TYPE_VOIDABLE:
            return seraph_type_is_copy(t->voidable.inner);

        /* Arrays of Copy with known size are Copy */
        case SERAPH_TYPE_ARRAY:
            return seraph_type_is_copy(t->array.elem);

        /* Tuples of Copy are Copy */
        case SERAPH_TYPE_TUPLE:
            for (size_t i = 0; i < t->tuple.count; i++) {
                if (!seraph_type_is_copy(t->tuple.elems[i])) return 0;
            }
            return 1;

        default:
            return 0;
    }
}

size_t seraph_type_size(const Seraph_Type* t) {
    if (t == NULL) return 0;

    switch (t->kind) {
        case SERAPH_TYPE_U8:
        case SERAPH_TYPE_I8:
        case SERAPH_TYPE_BOOL:
            return 1;

        case SERAPH_TYPE_U16:
        case SERAPH_TYPE_I16:
            return 2;

        case SERAPH_TYPE_U32:
        case SERAPH_TYPE_I32:
        case SERAPH_TYPE_CHAR:
            return 4;

        case SERAPH_TYPE_U64:
        case SERAPH_TYPE_I64:
            return 8;

        case SERAPH_TYPE_SCALAR:
            return 8;  /* Q32.32 */

        case SERAPH_TYPE_DUAL:
            return 16;  /* 128-bit */

        case SERAPH_TYPE_GALACTIC:
            return 32;  /* 256-bit */

        case SERAPH_TYPE_UNIT:
            return 0;

        case SERAPH_TYPE_ARRAY:
            return seraph_type_size(t->array.elem) * t->array.size;

        case SERAPH_TYPE_REF:
        case SERAPH_TYPE_REF_MUT:
            return sizeof(void*);

        case SERAPH_TYPE_VOIDABLE:
            /* VOID-able adds a validity byte plus padding */
            return seraph_type_size(t->voidable.inner) + seraph_type_align(t->voidable.inner);

        default:
            return 0;  /* Unknown/unsized */
    }
}

size_t seraph_type_align(const Seraph_Type* t) {
    if (t == NULL) return 1;

    switch (t->kind) {
        case SERAPH_TYPE_U8:
        case SERAPH_TYPE_I8:
        case SERAPH_TYPE_BOOL:
            return 1;

        case SERAPH_TYPE_U16:
        case SERAPH_TYPE_I16:
            return 2;

        case SERAPH_TYPE_U32:
        case SERAPH_TYPE_I32:
        case SERAPH_TYPE_CHAR:
            return 4;

        case SERAPH_TYPE_U64:
        case SERAPH_TYPE_I64:
        case SERAPH_TYPE_SCALAR:
            return 8;

        case SERAPH_TYPE_DUAL:
        case SERAPH_TYPE_GALACTIC:
            return 16;

        case SERAPH_TYPE_UNIT:
            return 1;

        case SERAPH_TYPE_ARRAY:
            return seraph_type_align(t->array.elem);

        case SERAPH_TYPE_REF:
        case SERAPH_TYPE_REF_MUT:
            return sizeof(void*);

        case SERAPH_TYPE_VOIDABLE:
            return seraph_type_align(t->voidable.inner);

        default:
            return 8;  /* Default alignment */
    }
}

/*============================================================================
 * Type Comparison
 *============================================================================*/

int seraph_type_eq(const Seraph_Type* a, const Seraph_Type* b) {
    if (a == b) return 1;
    if (a == NULL || b == NULL) return 0;

    /* Resolve type variables */
    if (a->kind == SERAPH_TYPE_TYPEVAR && a->typevar.bound) {
        return seraph_type_eq(a->typevar.bound, b);
    }
    if (b->kind == SERAPH_TYPE_TYPEVAR && b->typevar.bound) {
        return seraph_type_eq(a, b->typevar.bound);
    }

    if (a->kind != b->kind) return 0;

    switch (a->kind) {
        /* Primitives - same kind means equal */
        case SERAPH_TYPE_U8:
        case SERAPH_TYPE_U16:
        case SERAPH_TYPE_U32:
        case SERAPH_TYPE_U64:
        case SERAPH_TYPE_I8:
        case SERAPH_TYPE_I16:
        case SERAPH_TYPE_I32:
        case SERAPH_TYPE_I64:
        case SERAPH_TYPE_BOOL:
        case SERAPH_TYPE_CHAR:
        case SERAPH_TYPE_UNIT:
        case SERAPH_TYPE_SCALAR:
        case SERAPH_TYPE_DUAL:
        case SERAPH_TYPE_GALACTIC:
        case SERAPH_TYPE_NEVER:
            return 1;

        case SERAPH_TYPE_ARRAY:
            return a->array.size == b->array.size &&
                   seraph_type_eq(a->array.elem, b->array.elem);

        case SERAPH_TYPE_SLICE:
            return seraph_type_eq(a->slice.elem, b->slice.elem);

        case SERAPH_TYPE_TUPLE:
            if (a->tuple.count != b->tuple.count) return 0;
            for (size_t i = 0; i < a->tuple.count; i++) {
                if (!seraph_type_eq(a->tuple.elems[i], b->tuple.elems[i])) return 0;
            }
            return 1;

        case SERAPH_TYPE_STRUCT:
        case SERAPH_TYPE_ENUM:
            /* Named types - compare by declaration identity */
            return a->named.decl == b->named.decl;

        case SERAPH_TYPE_REF:
        case SERAPH_TYPE_REF_MUT:
            return a->ref.substrate == b->ref.substrate &&
                   seraph_type_eq(a->ref.inner, b->ref.inner);

        case SERAPH_TYPE_VOIDABLE:
            return seraph_type_eq(a->voidable.inner, b->voidable.inner);

        case SERAPH_TYPE_FN:
            if (a->fn.param_count != b->fn.param_count) return 0;
            if (a->fn.effects != b->fn.effects) return 0;
            if (!seraph_type_eq(a->fn.ret, b->fn.ret)) return 0;
            for (size_t i = 0; i < a->fn.param_count; i++) {
                if (!seraph_type_eq(a->fn.params[i], b->fn.params[i])) return 0;
            }
            return 1;

        case SERAPH_TYPE_TYPEVAR:
            return a->typevar.id == b->typevar.id;

        default:
            return 0;
    }
}

int seraph_type_subtype(const Seraph_Type* sub, const Seraph_Type* super) {
    if (sub == super) return 1;
    if (sub == NULL || super == NULL) return 0;

    /* Resolve type variables */
    if (sub->kind == SERAPH_TYPE_TYPEVAR && sub->typevar.bound) {
        return seraph_type_subtype(sub->typevar.bound, super);
    }
    if (super->kind == SERAPH_TYPE_TYPEVAR && super->typevar.bound) {
        return seraph_type_subtype(sub, super->typevar.bound);
    }

    /* Never is subtype of everything */
    if (sub->kind == SERAPH_TYPE_NEVER) return 1;

    /* T <: ??T (non-voidable is subtype of voidable) */
    if (super->kind == SERAPH_TYPE_VOIDABLE) {
        return seraph_type_subtype(sub, super->voidable.inner) ||
               (sub->kind == SERAPH_TYPE_VOIDABLE &&
                seraph_type_subtype(sub->voidable.inner, super->voidable.inner));
    }

    /* Equal types are subtypes */
    return seraph_type_eq(sub, super);
}

/*============================================================================
 * Type Unification
 *============================================================================*/

/**
 * @brief Occurs check - detect if type variable occurs within a type
 *
 * Prevents infinite types like T = List<T> by checking if the type
 * variable would occur in its own binding.
 *
 * @param var The type variable to search for
 * @param type The type to search within
 * @return 1 if var occurs in type, 0 otherwise
 */
static int seraph_type_occurs(const Seraph_Type* var, const Seraph_Type* type) {
    if (var == NULL || type == NULL) return 0;
    if (var->kind != SERAPH_TYPE_TYPEVAR) return 0;

    /* Resolve type variables */
    while (type->kind == SERAPH_TYPE_TYPEVAR && type->typevar.bound) {
        type = type->typevar.bound;
    }

    /* Direct match */
    if (type == var) return 1;
    if (type->kind == SERAPH_TYPE_TYPEVAR && type->typevar.id == var->typevar.id) {
        return 1;
    }

    /* Recursive check in composite types */
    switch (type->kind) {
        case SERAPH_TYPE_ARRAY:
            return seraph_type_occurs(var, type->array.elem);

        case SERAPH_TYPE_SLICE:
            return seraph_type_occurs(var, type->slice.elem);

        case SERAPH_TYPE_TUPLE:
            for (size_t i = 0; i < type->tuple.count; i++) {
                if (seraph_type_occurs(var, type->tuple.elems[i])) return 1;
            }
            return 0;

        case SERAPH_TYPE_REF:
        case SERAPH_TYPE_REF_MUT:
            return seraph_type_occurs(var, type->ref.inner);

        case SERAPH_TYPE_VOIDABLE:
            return seraph_type_occurs(var, type->voidable.inner);

        case SERAPH_TYPE_FN:
            if (seraph_type_occurs(var, type->fn.ret)) return 1;
            for (size_t i = 0; i < type->fn.param_count; i++) {
                if (seraph_type_occurs(var, type->fn.params[i])) return 1;
            }
            return 0;

        default:
            return 0;
    }
}

Seraph_Type* seraph_type_unify(Seraph_Type_Context* ctx,
                                Seraph_Type* a, Seraph_Type* b) {
    if (ctx == NULL) return NULL;
    if (a == NULL || b == NULL) return seraph_type_void(ctx->arena);

    /* Resolve type variables */
    while (a->kind == SERAPH_TYPE_TYPEVAR && a->typevar.bound) {
        a = a->typevar.bound;
    }
    while (b->kind == SERAPH_TYPE_TYPEVAR && b->typevar.bound) {
        b = b->typevar.bound;
    }

    /* If both are the same, done */
    if (a == b) return a;

    /* Type variable binds to concrete type */
    if (a->kind == SERAPH_TYPE_TYPEVAR) {
        /* Occurs check: prevent infinite types like T = List<T> */
        if (seraph_type_occurs(a, b)) {
            seraph_type_error(ctx, (Seraph_Source_Loc){0},
                              "infinite type: type variable occurs in its own binding");
            return seraph_type_void(ctx->arena);
        }
        ((Seraph_Type*)a)->typevar.bound = b;
        return b;
    }
    if (b->kind == SERAPH_TYPE_TYPEVAR) {
        /* Occurs check for b as well */
        if (seraph_type_occurs(b, a)) {
            seraph_type_error(ctx, (Seraph_Source_Loc){0},
                              "infinite type: type variable occurs in its own binding");
            return seraph_type_void(ctx->arena);
        }
        ((Seraph_Type*)b)->typevar.bound = a;
        return a;
    }

    /* Never unifies with anything */
    if (a->kind == SERAPH_TYPE_NEVER) return b;
    if (b->kind == SERAPH_TYPE_NEVER) return a;

    /* Must have same kind to unify */
    if (a->kind != b->kind) {
        return seraph_type_void(ctx->arena);
    }

    switch (a->kind) {
        case SERAPH_TYPE_ARRAY:
            if (a->array.size != b->array.size) {
                return seraph_type_void(ctx->arena);
            }
            {
                Seraph_Type* elem = seraph_type_unify(ctx, a->array.elem, b->array.elem);
                if (seraph_type_is_void(elem)) return seraph_type_void(ctx->arena);
                return seraph_type_array(ctx->arena, elem, a->array.size);
            }

        case SERAPH_TYPE_VOIDABLE:
            {
                Seraph_Type* inner = seraph_type_unify(ctx, a->voidable.inner, b->voidable.inner);
                if (seraph_type_is_void(inner)) return seraph_type_void(ctx->arena);
                return seraph_type_voidable(ctx->arena, inner);
            }

        case SERAPH_TYPE_FN:
            if (a->fn.param_count != b->fn.param_count) {
                return seraph_type_void(ctx->arena);
            }
            {
                Seraph_Type** params = NULL;
                if (a->fn.param_count > 0) {
                    params = (Seraph_Type**)seraph_arena_alloc(
                        ctx->arena, a->fn.param_count * sizeof(Seraph_Type*),
                        _Alignof(Seraph_Type*)
                    );
                    if (params == NULL) return seraph_type_void(ctx->arena);
                    for (size_t i = 0; i < a->fn.param_count; i++) {
                        params[i] = seraph_type_unify(ctx, a->fn.params[i], b->fn.params[i]);
                        if (seraph_type_is_void(params[i])) return seraph_type_void(ctx->arena);
                    }
                }
                Seraph_Type* ret = seraph_type_unify(ctx, a->fn.ret, b->fn.ret);
                if (seraph_type_is_void(ret)) return seraph_type_void(ctx->arena);
                return seraph_type_fn(ctx->arena, params, a->fn.param_count, ret,
                                      a->fn.effects | b->fn.effects);
            }

        default:
            if (seraph_type_eq(a, b)) return a;
            return seraph_type_void(ctx->arena);
    }
}

Seraph_Type* seraph_type_join(Seraph_Type_Context* ctx,
                               Seraph_Type* a, Seraph_Type* b) {
    if (ctx == NULL) return NULL;

    /* If equal, trivially the join */
    if (seraph_type_eq(a, b)) return a;

    /* Never joins with anything to give the other */
    if (a->kind == SERAPH_TYPE_NEVER) return b;
    if (b->kind == SERAPH_TYPE_NEVER) return a;

    /* If one is VOID-able and other is not, join is VOID-able */
    if (a->kind == SERAPH_TYPE_VOIDABLE && b->kind != SERAPH_TYPE_VOIDABLE) {
        Seraph_Type* inner = seraph_type_join(ctx, a->voidable.inner, b);
        return seraph_type_voidable(ctx->arena, inner);
    }
    if (b->kind == SERAPH_TYPE_VOIDABLE && a->kind != SERAPH_TYPE_VOIDABLE) {
        Seraph_Type* inner = seraph_type_join(ctx, a, b->voidable.inner);
        return seraph_type_voidable(ctx->arena, inner);
    }

    /* Otherwise, must be same kind */
    if (a->kind != b->kind) {
        return seraph_type_void(ctx->arena);
    }

    /* Same logic as unify for structural types */
    return seraph_type_unify(ctx, a, b);
}

/*============================================================================
 * Type Context Management
 *============================================================================*/

Seraph_Vbit seraph_type_context_init(Seraph_Type_Context* ctx, Seraph_Arena* arena) {
    if (ctx == NULL || arena == NULL) return SERAPH_VBIT_VOID;

    memset(ctx, 0, sizeof(Seraph_Type_Context));
    ctx->arena = arena;
    ctx->next_typevar_id = 0;
    ctx->allowed_effects = SERAPH_EFFECT_ALL;

    /* Create global scope */
    ctx->global = (Seraph_Scope*)seraph_arena_alloc(
        arena, sizeof(Seraph_Scope), _Alignof(Seraph_Scope)
    );
    if (ctx->global == NULL) return SERAPH_VBIT_VOID;

    ctx->global->symbols = NULL;
    ctx->global->parent = NULL;
    ctx->scope = ctx->global;

    return SERAPH_VBIT_TRUE;
}

void seraph_type_push_scope(Seraph_Type_Context* ctx) {
    if (ctx == NULL) return;

    Seraph_Scope* scope = (Seraph_Scope*)seraph_arena_alloc(
        ctx->arena, sizeof(Seraph_Scope), _Alignof(Seraph_Scope)
    );
    if (scope == NULL) return;

    scope->symbols = NULL;
    scope->parent = ctx->scope;
    ctx->scope = scope;
}

void seraph_type_pop_scope(Seraph_Type_Context* ctx) {
    if (ctx == NULL || ctx->scope == NULL) return;
    if (ctx->scope == ctx->global) return;  /* Don't pop global */
    ctx->scope = ctx->scope->parent;
}

Seraph_Vbit seraph_type_define(Seraph_Type_Context* ctx,
                                const char* name, size_t name_len,
                                Seraph_Type* type, Seraph_AST_Node* decl,
                                int is_mut) {
    if (ctx == NULL || ctx->scope == NULL) return SERAPH_VBIT_VOID;
    if (name == NULL || type == NULL) return SERAPH_VBIT_VOID;

    Seraph_Symbol* sym = (Seraph_Symbol*)seraph_arena_alloc(
        ctx->arena, sizeof(Seraph_Symbol), _Alignof(Seraph_Symbol)
    );
    if (sym == NULL) return SERAPH_VBIT_VOID;

    sym->name = name;
    sym->name_len = name_len;
    sym->type = type;
    sym->decl = decl;
    sym->is_mut = is_mut;
    sym->next = ctx->scope->symbols;
    ctx->scope->symbols = sym;

    return SERAPH_VBIT_TRUE;
}

Seraph_Symbol* seraph_type_lookup(Seraph_Type_Context* ctx,
                                   const char* name, size_t name_len) {
    if (ctx == NULL || name == NULL) return NULL;

    for (Seraph_Scope* scope = ctx->scope; scope != NULL; scope = scope->parent) {
        for (Seraph_Symbol* sym = scope->symbols; sym != NULL; sym = sym->next) {
            if (sym->name_len == name_len && memcmp(sym->name, name, name_len) == 0) {
                return sym;
            }
        }
    }
    return NULL;
}

/*============================================================================
 * Type from AST
 *============================================================================*/

Seraph_Type* seraph_type_from_token(Seraph_Arena* arena, Seraph_Token_Type tok) {
    switch (tok) {
        case SERAPH_TOK_U8:       return seraph_type_prim(arena, SERAPH_TYPE_U8);
        case SERAPH_TOK_U16:      return seraph_type_prim(arena, SERAPH_TYPE_U16);
        case SERAPH_TOK_U32:      return seraph_type_prim(arena, SERAPH_TYPE_U32);
        case SERAPH_TOK_U64:      return seraph_type_prim(arena, SERAPH_TYPE_U64);
        case SERAPH_TOK_I8:       return seraph_type_prim(arena, SERAPH_TYPE_I8);
        case SERAPH_TOK_I16:      return seraph_type_prim(arena, SERAPH_TYPE_I16);
        case SERAPH_TOK_I32:      return seraph_type_prim(arena, SERAPH_TYPE_I32);
        case SERAPH_TOK_I64:      return seraph_type_prim(arena, SERAPH_TYPE_I64);
        case SERAPH_TOK_BOOL:     return seraph_type_prim(arena, SERAPH_TYPE_BOOL);
        case SERAPH_TOK_CHAR:     return seraph_type_prim(arena, SERAPH_TYPE_CHAR);
        case SERAPH_TOK_SCALAR:   return seraph_type_prim(arena, SERAPH_TYPE_SCALAR);
        case SERAPH_TOK_DUAL:     return seraph_type_prim(arena, SERAPH_TYPE_DUAL);
        case SERAPH_TOK_GALACTIC: return seraph_type_prim(arena, SERAPH_TYPE_GALACTIC);
        default:                  return seraph_type_void(arena);
    }
}

Seraph_Type* seraph_type_from_ast(Seraph_Type_Context* ctx, Seraph_AST_Node* ast_type) {
    if (ctx == NULL || ast_type == NULL) return NULL;
    if (seraph_ast_is_void(ast_type)) return seraph_type_void(ctx->arena);

    switch (ast_type->hdr.kind) {
        case AST_TYPE_PRIMITIVE:
            return seraph_type_from_token(ctx->arena, ast_type->prim_type.prim);

        case AST_TYPE_NAMED:
            {
                /* Look up the type */
                Seraph_Symbol* sym = seraph_type_lookup(ctx,
                    ast_type->named_type.name, ast_type->named_type.name_len);
                if (sym != NULL && sym->type != NULL) {
                    return sym->type;
                }
                /* Unknown type */
                seraph_type_error(ctx, ast_type->hdr.loc, "unknown type '%.*s'",
                                  (int)ast_type->named_type.name_len, ast_type->named_type.name);
                return seraph_type_void(ctx->arena);
            }

        case AST_TYPE_REF:
        case AST_TYPE_MUT_REF:
            {
                int is_mut = (ast_type->hdr.kind == AST_TYPE_MUT_REF);
                Seraph_Type* inner = seraph_type_from_ast(ctx, ast_type->ref_type.inner);
                Seraph_Substrate substrate = (Seraph_Substrate)ast_type->ref_type.substrate;
                return seraph_type_ref(ctx->arena, inner, is_mut, substrate);
            }

        case AST_TYPE_ARRAY:
            {
                Seraph_Type* elem = seraph_type_from_ast(ctx, ast_type->array_type.elem_type);
                /* Evaluate size expression - must be compile-time constant */
                uint64_t size = 0;
                if (ast_type->array_type.size != NULL) {
                    Seraph_AST_Node* size_expr = ast_type->array_type.size;
                    switch (size_expr->hdr.kind) {
                        case AST_EXPR_INT_LIT:
                            size = size_expr->int_lit.value;
                            break;
                        case AST_EXPR_IDENT:
                            {
                                /* Look up constant in scope */
                                Seraph_Symbol* sym = seraph_type_lookup(ctx,
                                    size_expr->ident.name, size_expr->ident.name_len);
                                if (sym != NULL && sym->decl != NULL &&
                                    sym->decl->hdr.kind == AST_DECL_CONST &&
                                    sym->decl->let_decl.init != NULL &&
                                    sym->decl->let_decl.init->hdr.kind == AST_EXPR_INT_LIT) {
                                    size = sym->decl->let_decl.init->int_lit.value;
                                } else {
                                    seraph_type_error(ctx, size_expr->hdr.loc,
                                                      "array size must be compile-time constant");
                                }
                            }
                            break;
                        case AST_EXPR_BINARY:
                            /* Evaluate simple binary expressions with constants */
                            if (size_expr->binary.left->hdr.kind == AST_EXPR_INT_LIT &&
                                size_expr->binary.right->hdr.kind == AST_EXPR_INT_LIT) {
                                uint64_t left = size_expr->binary.left->int_lit.value;
                                uint64_t right = size_expr->binary.right->int_lit.value;
                                switch (size_expr->binary.op) {
                                    case SERAPH_TOK_PLUS:  size = left + right; break;
                                    case SERAPH_TOK_MINUS: size = left - right; break;
                                    case SERAPH_TOK_STAR:  size = left * right; break;
                                    case SERAPH_TOK_SLASH: size = right ? left / right : 0; break;
                                    default:
                                        seraph_type_error(ctx, size_expr->hdr.loc,
                                                          "unsupported operator in array size");
                                        break;
                                }
                            } else {
                                seraph_type_error(ctx, size_expr->hdr.loc,
                                                  "array size must be compile-time constant");
                            }
                            break;
                        default:
                            seraph_type_error(ctx, size_expr->hdr.loc,
                                              "array size must be compile-time constant");
                            break;
                    }
                }
                return seraph_type_array(ctx->arena, elem, size);
            }

        case AST_TYPE_SLICE:
            {
                Seraph_Type* elem = seraph_type_from_ast(ctx, ast_type->array_type.elem_type);
                return seraph_type_slice(ctx->arena, elem);
            }

        case AST_TYPE_VOID_ABLE:
            {
                Seraph_Type* inner = seraph_type_from_ast(ctx, ast_type->void_type.inner);
                return seraph_type_voidable(ctx->arena, inner);
            }

        default:
            seraph_type_error(ctx, ast_type->hdr.loc, "unsupported type AST node");
            return seraph_type_void(ctx->arena);
    }
}

/*============================================================================
 * Type Checking (Stubs - to be implemented)
 *============================================================================*/

Seraph_Vbit seraph_type_check_module(Seraph_Type_Context* ctx,
                                      Seraph_AST_Node* module) {
    if (ctx == NULL || module == NULL) return SERAPH_VBIT_VOID;
    if (module->hdr.kind != AST_MODULE) return SERAPH_VBIT_VOID;

    /* First pass: collect all declarations */
    for (Seraph_AST_Node* decl = module->module.decls; decl != NULL; decl = decl->hdr.next) {
        if (decl->hdr.kind == AST_DECL_FN) {
            /* Build function type from declaration */
            size_t param_count = 0;
            for (Seraph_AST_Node* p = decl->fn_decl.params; p != NULL; p = p->hdr.next) {
                param_count++;
            }

            /* Allocate parameter type array */
            Seraph_Type** params = NULL;
            if (param_count > 0) {
                params = (Seraph_Type**)seraph_arena_alloc(
                    ctx->arena, param_count * sizeof(Seraph_Type*), _Alignof(Seraph_Type*)
                );
                if (params != NULL) {
                    size_t i = 0;
                    for (Seraph_AST_Node* p = decl->fn_decl.params; p != NULL; p = p->hdr.next, i++) {
                        params[i] = seraph_type_from_ast(ctx, p->param.type);
                    }
                }
            }

            /* Get return type */
            Seraph_Type* ret_type = decl->fn_decl.ret_type ?
                seraph_type_from_ast(ctx, decl->fn_decl.ret_type) :
                seraph_type_unit(ctx->arena);

            /* Get effect flags from declaration */
            Seraph_Effect_Flags effects = SERAPH_EFFECT_NONE;
            if (decl->fn_decl.effects != NULL) {
                effects = decl->fn_decl.effects->effect_list.effects;
            }

            /* Create function type */
            Seraph_Type* fn_type = seraph_type_fn(ctx->arena, params, param_count,
                                                   ret_type, effects);
            seraph_type_define(ctx, decl->fn_decl.name, decl->fn_decl.name_len,
                               fn_type, decl, 0);
        }
    }

    /* Second pass: type-check bodies */
    for (Seraph_AST_Node* decl = module->module.decls; decl != NULL; decl = decl->hdr.next) {
        seraph_type_check_decl(ctx, decl);
    }

    return ctx->error_count == 0 ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

Seraph_Vbit seraph_type_check_decl(Seraph_Type_Context* ctx, Seraph_AST_Node* decl) {
    if (ctx == NULL || decl == NULL) return SERAPH_VBIT_VOID;

    switch (decl->hdr.kind) {
        case AST_DECL_FN:
            {
                seraph_type_push_scope(ctx);

                /* Add parameters to scope */
                for (Seraph_AST_Node* p = decl->fn_decl.params; p != NULL; p = p->hdr.next) {
                    Seraph_Type* param_type = seraph_type_from_ast(ctx, p->param.type);
                    seraph_type_define(ctx, p->param.name, p->param.name_len,
                                       param_type, p, p->param.is_mut);
                }

                /* Set expected return type */
                ctx->current_fn_ret = decl->fn_decl.ret_type ?
                    seraph_type_from_ast(ctx, decl->fn_decl.ret_type) :
                    seraph_type_unit(ctx->arena);

                /* Check body */
                if (decl->fn_decl.body) {
                    seraph_type_check_block(ctx, decl->fn_decl.body);
                }

                ctx->current_fn_ret = NULL;
                seraph_type_pop_scope(ctx);
            }
            break;

        case AST_DECL_LET:
        case AST_DECL_CONST:
            {
                Seraph_Type* type = NULL;
                if (decl->let_decl.type) {
                    type = seraph_type_from_ast(ctx, decl->let_decl.type);
                }
                if (decl->let_decl.init) {
                    Seraph_Type* init_type = seraph_type_check_expr(ctx, decl->let_decl.init);
                    if (type == NULL) {
                        type = init_type;
                    } else {
                        /* Check types match */
                        if (!seraph_type_subtype(init_type, type)) {
                            seraph_type_mismatch(ctx, decl->let_decl.init->hdr.loc, type, init_type);
                        }
                    }
                }
                if (type == NULL) {
                    type = seraph_type_void(ctx->arena);
                }
                seraph_type_define(ctx, decl->let_decl.name, decl->let_decl.name_len,
                                   type, decl, decl->let_decl.is_mut);
            }
            break;

        default:
            break;
    }

    return ctx->error_count == 0 ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

Seraph_Type* seraph_type_check_expr(Seraph_Type_Context* ctx, Seraph_AST_Node* expr) {
    if (ctx == NULL || expr == NULL) return NULL;
    if (seraph_ast_is_void(expr)) return seraph_type_void(ctx->arena);

    switch (expr->hdr.kind) {
        case AST_EXPR_INT_LIT:
            /* Infer type from suffix or default to i64 */
            switch (expr->int_lit.suffix) {
                case SERAPH_NUM_SUFFIX_U8:  return seraph_type_prim(ctx->arena, SERAPH_TYPE_U8);
                case SERAPH_NUM_SUFFIX_U16: return seraph_type_prim(ctx->arena, SERAPH_TYPE_U16);
                case SERAPH_NUM_SUFFIX_U32: return seraph_type_prim(ctx->arena, SERAPH_TYPE_U32);
                case SERAPH_NUM_SUFFIX_U64:
                case SERAPH_NUM_SUFFIX_U:   return seraph_type_prim(ctx->arena, SERAPH_TYPE_U64);
                case SERAPH_NUM_SUFFIX_I8:  return seraph_type_prim(ctx->arena, SERAPH_TYPE_I8);
                case SERAPH_NUM_SUFFIX_I16: return seraph_type_prim(ctx->arena, SERAPH_TYPE_I16);
                case SERAPH_NUM_SUFFIX_I32: return seraph_type_prim(ctx->arena, SERAPH_TYPE_I32);
                default:                    return seraph_type_prim(ctx->arena, SERAPH_TYPE_I64);
            }

        case AST_EXPR_FLOAT_LIT:
            switch (expr->float_lit.suffix) {
                case SERAPH_NUM_SUFFIX_S: return seraph_type_prim(ctx->arena, SERAPH_TYPE_SCALAR);
                case SERAPH_NUM_SUFFIX_D: return seraph_type_prim(ctx->arena, SERAPH_TYPE_DUAL);
                case SERAPH_NUM_SUFFIX_G: return seraph_type_prim(ctx->arena, SERAPH_TYPE_GALACTIC);
                default:                  return seraph_type_prim(ctx->arena, SERAPH_TYPE_SCALAR);
            }

        case AST_EXPR_BOOL_LIT:
            return seraph_type_prim(ctx->arena, SERAPH_TYPE_BOOL);

        case AST_EXPR_CHAR_LIT:
            return seraph_type_prim(ctx->arena, SERAPH_TYPE_CHAR);

        case AST_EXPR_STRING_LIT:
            return seraph_type_slice(ctx->arena, seraph_type_prim(ctx->arena, SERAPH_TYPE_U8));

        case AST_EXPR_VOID_LIT:
            return seraph_type_voidable(ctx->arena, seraph_type_unit(ctx->arena));

        case AST_EXPR_IDENT:
            {
                Seraph_Symbol* sym = seraph_type_lookup(ctx,
                    expr->ident.name, expr->ident.name_len);
                if (sym == NULL) {
                    seraph_type_error(ctx, expr->hdr.loc, "undefined variable '%.*s'",
                                      (int)expr->ident.name_len, expr->ident.name);
                    return seraph_type_void(ctx->arena);
                }
                return sym->type;
            }

        case AST_EXPR_BINARY:
            {
                Seraph_Type* left = seraph_type_check_expr(ctx, expr->binary.left);
                Seraph_Type* right = seraph_type_check_expr(ctx, expr->binary.right);

                /* Check operand types match and infer result */
                switch (expr->binary.op) {
                    /* Arithmetic: require numeric, return same type */
                    case SERAPH_TOK_PLUS:
                    case SERAPH_TOK_MINUS:
                    case SERAPH_TOK_STAR:
                    case SERAPH_TOK_SLASH:
                    case SERAPH_TOK_PERCENT:
                        if (!seraph_type_is_numeric(left)) {
                            seraph_type_error(ctx, expr->binary.left->hdr.loc,
                                              "expected numeric type");
                            return seraph_type_void(ctx->arena);
                        }
                        {
                            Seraph_Type* unified = seraph_type_unify(ctx, left, right);
                            if (seraph_type_is_void(unified)) {
                                seraph_type_mismatch(ctx, expr->hdr.loc, left, right);
                            }
                            return unified;
                        }

                    /* Comparison: require same type, return bool */
                    case SERAPH_TOK_EQ:
                    case SERAPH_TOK_NE:
                    case SERAPH_TOK_LT:
                    case SERAPH_TOK_GT:
                    case SERAPH_TOK_LE:
                    case SERAPH_TOK_GE:
                        if (!seraph_type_eq(left, right)) {
                            seraph_type_mismatch(ctx, expr->hdr.loc, left, right);
                        }
                        return seraph_type_prim(ctx->arena, SERAPH_TYPE_BOOL);

                    /* Logical: require bool, return bool */
                    case SERAPH_TOK_AND:
                    case SERAPH_TOK_OR:
                        if (left->kind != SERAPH_TYPE_BOOL) {
                            seraph_type_error(ctx, expr->binary.left->hdr.loc,
                                              "expected bool");
                        }
                        if (right->kind != SERAPH_TYPE_BOOL) {
                            seraph_type_error(ctx, expr->binary.right->hdr.loc,
                                              "expected bool");
                        }
                        return seraph_type_prim(ctx->arena, SERAPH_TYPE_BOOL);

                    default:
                        return seraph_type_unify(ctx, left, right);
                }
            }

        case AST_EXPR_UNARY:
            {
                Seraph_Type* operand = seraph_type_check_expr(ctx, expr->unary.operand);

                switch (expr->unary.op) {
                    case SERAPH_TOK_MINUS:
                        if (!seraph_type_is_numeric(operand)) {
                            seraph_type_error(ctx, expr->hdr.loc, "expected numeric type");
                        }
                        return operand;

                    case SERAPH_TOK_NOT:
                        if (operand->kind != SERAPH_TYPE_BOOL) {
                            seraph_type_error(ctx, expr->hdr.loc, "expected bool");
                        }
                        return seraph_type_prim(ctx->arena, SERAPH_TYPE_BOOL);

                    default:
                        return operand;
                }
            }

        case AST_EXPR_BLOCK:
            return seraph_type_check_block(ctx, expr);

        case AST_EXPR_IF:
            {
                /* Condition must be bool */
                Seraph_Type* cond = seraph_type_check_expr(ctx, expr->if_expr.cond);
                if (cond->kind != SERAPH_TYPE_BOOL) {
                    seraph_type_error(ctx, expr->if_expr.cond->hdr.loc, "expected bool");
                }

                Seraph_Type* then_type = seraph_type_check_block(ctx, expr->if_expr.then_branch);

                if (expr->if_expr.else_branch) {
                    Seraph_Type* else_type = seraph_type_check_block(ctx, expr->if_expr.else_branch);
                    return seraph_type_join(ctx, then_type, else_type);
                } else {
                    return seraph_type_unit(ctx->arena);
                }
            }

        case AST_EXPR_CALL:
            {
                Seraph_Type* callee_type = seraph_type_check_expr(ctx, expr->call.callee);
                if (callee_type->kind != SERAPH_TYPE_FN) {
                    seraph_type_error(ctx, expr->call.callee->hdr.loc, "not a function");
                    return seraph_type_void(ctx->arena);
                }

                /* Check argument count */
                if (expr->call.arg_count != callee_type->fn.param_count) {
                    seraph_type_error(ctx, expr->hdr.loc,
                                      "expected %zu arguments, got %zu",
                                      callee_type->fn.param_count, expr->call.arg_count);
                }

                /* Check argument types */
                size_t i = 0;
                for (Seraph_AST_Node* arg = expr->call.args; arg != NULL; arg = arg->hdr.next, i++) {
                    if (i >= callee_type->fn.param_count) break;
                    Seraph_Type* arg_type = seraph_type_check_expr(ctx, arg);
                    if (!seraph_type_subtype(arg_type, callee_type->fn.params[i])) {
                        seraph_type_mismatch(ctx, arg->hdr.loc,
                                             callee_type->fn.params[i], arg_type);
                    }
                }

                return callee_type->fn.ret;
            }

        case AST_EXPR_VOID_PROP:
            {
                Seraph_Type* operand = seraph_type_check_expr(ctx, expr->void_prop.operand);
                /* Result is always VOID-able */
                return seraph_type_voidable(ctx->arena, operand);
            }

        case AST_EXPR_VOID_ASSERT:
            {
                Seraph_Type* operand = seraph_type_check_expr(ctx, expr->void_assert.operand);
                /* !! strips VOID-ability */
                if (operand->kind == SERAPH_TYPE_VOIDABLE) {
                    return operand->voidable.inner;
                }
                return operand;
            }

        default:
            return seraph_type_void(ctx->arena);
    }
}

Seraph_Vbit seraph_type_check_expect(Seraph_Type_Context* ctx,
                                      Seraph_AST_Node* expr,
                                      Seraph_Type* expected) {
    if (ctx == NULL || expr == NULL || expected == NULL) return SERAPH_VBIT_VOID;

    Seraph_Type* actual = seraph_type_check_expr(ctx, expr);
    if (!seraph_type_subtype(actual, expected)) {
        seraph_type_mismatch(ctx, expr->hdr.loc, expected, actual);
        return SERAPH_VBIT_FALSE;
    }
    return SERAPH_VBIT_TRUE;
}

Seraph_Vbit seraph_type_check_stmt(Seraph_Type_Context* ctx, Seraph_AST_Node* stmt) {
    if (ctx == NULL || stmt == NULL) return SERAPH_VBIT_VOID;

    switch (stmt->hdr.kind) {
        case AST_STMT_EXPR:
            seraph_type_check_expr(ctx, stmt->expr_stmt.expr);
            break;

        case AST_STMT_RETURN:
            if (stmt->return_stmt.expr) {
                seraph_type_check_expect(ctx, stmt->return_stmt.expr, ctx->current_fn_ret);
            }
            break;

        case AST_STMT_WHILE:
            {
                Seraph_Type* cond = seraph_type_check_expr(ctx, stmt->while_stmt.cond);
                if (cond->kind != SERAPH_TYPE_BOOL) {
                    seraph_type_error(ctx, stmt->while_stmt.cond->hdr.loc, "expected bool");
                }
                seraph_type_check_block(ctx, stmt->while_stmt.body);
            }
            break;

        case AST_STMT_FOR:
            {
                seraph_type_push_scope(ctx);
                /* Infer iterator variable type from iterable */
                Seraph_Type* iter_type = seraph_type_prim(ctx->arena, SERAPH_TYPE_I64);
                if (stmt->for_stmt.iterable != NULL) {
                    Seraph_Type* iterable_type = seraph_type_check_expr(ctx, stmt->for_stmt.iterable);
                    if (iterable_type != NULL) {
                        switch (iterable_type->kind) {
                            case SERAPH_TYPE_ARRAY:
                                /* Iterating over array yields element type */
                                iter_type = iterable_type->array.elem;
                                break;
                            case SERAPH_TYPE_SLICE:
                                /* Iterating over slice yields element type */
                                iter_type = iterable_type->slice.elem;
                                break;
                            case SERAPH_TYPE_TUPLE:
                                /* For range expressions like 0..n, yields i64 */
                                iter_type = seraph_type_prim(ctx->arena, SERAPH_TYPE_I64);
                                break;
                            default:
                                /* Default to i64 for range literals */
                                break;
                        }
                    }
                }
                seraph_type_define(ctx, stmt->for_stmt.var, stmt->for_stmt.var_len,
                                   iter_type, stmt, 0);
                seraph_type_check_block(ctx, stmt->for_stmt.body);
                seraph_type_pop_scope(ctx);
            }
            break;

        case AST_DECL_LET:
        case AST_DECL_CONST:
            seraph_type_check_decl(ctx, stmt);
            break;

        default:
            break;
    }

    return ctx->error_count == 0 ? SERAPH_VBIT_TRUE : SERAPH_VBIT_FALSE;
}

Seraph_Type* seraph_type_check_block(Seraph_Type_Context* ctx, Seraph_AST_Node* block) {
    if (ctx == NULL || block == NULL) return NULL;
    if (block->hdr.kind != AST_EXPR_BLOCK) return seraph_type_void(ctx->arena);

    seraph_type_push_scope(ctx);

    /* Check statements */
    for (Seraph_AST_Node* stmt = block->block.stmts; stmt != NULL; stmt = stmt->hdr.next) {
        seraph_type_check_stmt(ctx, stmt);
    }

    /* Block type is the type of the result expression (or unit) */
    Seraph_Type* result = seraph_type_unit(ctx->arena);
    if (block->block.expr) {
        result = seraph_type_check_expr(ctx, block->block.expr);
    }

    seraph_type_pop_scope(ctx);
    return result;
}

/*============================================================================
 * Diagnostics
 *============================================================================*/

void seraph_type_error(Seraph_Type_Context* ctx, Seraph_Source_Loc loc,
                        const char* format, ...) {
    if (ctx == NULL) return;

    ctx->error_count++;

    Seraph_Type_Diag* diag = (Seraph_Type_Diag*)seraph_arena_alloc(
        ctx->arena, sizeof(Seraph_Type_Diag), _Alignof(Seraph_Type_Diag)
    );
    if (diag == NULL) return;

    diag->loc = loc;
    diag->expected = NULL;
    diag->actual = NULL;
    diag->next = ctx->diagnostics;
    ctx->diagnostics = diag;

    va_list args;
    va_start(args, format);
    int len = vsnprintf(NULL, 0, format, args);
    va_end(args);

    char* msg = (char*)seraph_arena_alloc(ctx->arena, len + 1, 1);
    if (msg != NULL) {
        va_start(args, format);
        vsnprintf(msg, len + 1, format, args);
        va_end(args);
        diag->message = msg;
    } else {
        diag->message = format;
    }
}

void seraph_type_mismatch(Seraph_Type_Context* ctx, Seraph_Source_Loc loc,
                           Seraph_Type* expected, Seraph_Type* actual) {
    if (ctx == NULL) return;

    ctx->error_count++;

    Seraph_Type_Diag* diag = (Seraph_Type_Diag*)seraph_arena_alloc(
        ctx->arena, sizeof(Seraph_Type_Diag), _Alignof(Seraph_Type_Diag)
    );
    if (diag == NULL) return;

    diag->loc = loc;
    diag->expected = expected;
    diag->actual = actual;
    diag->next = ctx->diagnostics;
    ctx->diagnostics = diag;

    char expected_str[64], actual_str[64];
    seraph_type_print(expected, expected_str, sizeof(expected_str));
    seraph_type_print(actual, actual_str, sizeof(actual_str));

    int len = snprintf(NULL, 0, "type mismatch: expected '%s', got '%s'",
                       expected_str, actual_str);
    char* msg = (char*)seraph_arena_alloc(ctx->arena, len + 1, 1);
    if (msg != NULL) {
        snprintf(msg, len + 1, "type mismatch: expected '%s', got '%s'",
                 expected_str, actual_str);
        diag->message = msg;
    } else {
        diag->message = "type mismatch";
    }
}

void seraph_type_print_diagnostics(const Seraph_Type_Context* ctx) {
    if (ctx == NULL) return;

    for (Seraph_Type_Diag* d = ctx->diagnostics; d != NULL; d = d->next) {
        fprintf(stderr, "%s:%u:%u: error: %s\n",
                d->loc.filename ? d->loc.filename : "<unknown>",
                d->loc.line,
                d->loc.column,
                d->message);
    }
}

/*============================================================================
 * Type Printing
 *============================================================================*/

const char* seraph_type_kind_name(Seraph_Type_Kind kind) {
    switch (kind) {
        case SERAPH_TYPE_VOID:     return "VOID";
        case SERAPH_TYPE_U8:       return "u8";
        case SERAPH_TYPE_U16:      return "u16";
        case SERAPH_TYPE_U32:      return "u32";
        case SERAPH_TYPE_U64:      return "u64";
        case SERAPH_TYPE_I8:       return "i8";
        case SERAPH_TYPE_I16:      return "i16";
        case SERAPH_TYPE_I32:      return "i32";
        case SERAPH_TYPE_I64:      return "i64";
        case SERAPH_TYPE_BOOL:     return "bool";
        case SERAPH_TYPE_CHAR:     return "char";
        case SERAPH_TYPE_UNIT:     return "()";
        case SERAPH_TYPE_SCALAR:   return "scalar";
        case SERAPH_TYPE_DUAL:     return "dual";
        case SERAPH_TYPE_GALACTIC: return "galactic";
        case SERAPH_TYPE_ARRAY:    return "array";
        case SERAPH_TYPE_SLICE:    return "slice";
        case SERAPH_TYPE_TUPLE:    return "tuple";
        case SERAPH_TYPE_STRUCT:   return "struct";
        case SERAPH_TYPE_ENUM:     return "enum";
        case SERAPH_TYPE_REF:      return "&";
        case SERAPH_TYPE_REF_MUT:  return "&mut";
        case SERAPH_TYPE_VOIDABLE: return "??";
        case SERAPH_TYPE_FN:       return "fn";
        case SERAPH_TYPE_TYPEVAR:  return "typevar";
        case SERAPH_TYPE_NEVER:    return "!";
        default:                   return "<unknown>";
    }
}

size_t seraph_type_print(const Seraph_Type* t, char* buf, size_t buf_size) {
    if (buf == NULL || buf_size == 0) return 0;

    if (t == NULL) {
        return snprintf(buf, buf_size, "VOID");
    }

    switch (t->kind) {
        case SERAPH_TYPE_U8:
        case SERAPH_TYPE_U16:
        case SERAPH_TYPE_U32:
        case SERAPH_TYPE_U64:
        case SERAPH_TYPE_I8:
        case SERAPH_TYPE_I16:
        case SERAPH_TYPE_I32:
        case SERAPH_TYPE_I64:
        case SERAPH_TYPE_BOOL:
        case SERAPH_TYPE_CHAR:
        case SERAPH_TYPE_UNIT:
        case SERAPH_TYPE_SCALAR:
        case SERAPH_TYPE_DUAL:
        case SERAPH_TYPE_GALACTIC:
        case SERAPH_TYPE_VOID:
        case SERAPH_TYPE_NEVER:
            return snprintf(buf, buf_size, "%s", seraph_type_kind_name(t->kind));

        case SERAPH_TYPE_ARRAY:
            {
                char elem[32];
                seraph_type_print(t->array.elem, elem, sizeof(elem));
                return snprintf(buf, buf_size, "[%s; %llu]", elem, (unsigned long long)t->array.size);
            }

        case SERAPH_TYPE_SLICE:
            {
                char elem[32];
                seraph_type_print(t->slice.elem, elem, sizeof(elem));
                return snprintf(buf, buf_size, "[%s]", elem);
            }

        case SERAPH_TYPE_REF:
        case SERAPH_TYPE_REF_MUT:
            {
                char inner[32];
                seraph_type_print(t->ref.inner, inner, sizeof(inner));
                const char* sub = "";
                if (t->ref.substrate == SERAPH_SUBSTRATE_ATLAS) sub = "atlas ";
                else if (t->ref.substrate == SERAPH_SUBSTRATE_AETHER) sub = "aether ";
                return snprintf(buf, buf_size, "&%s%s%s",
                                t->ref.is_mut ? "mut " : "", sub, inner);
            }

        case SERAPH_TYPE_VOIDABLE:
            {
                char inner[32];
                seraph_type_print(t->voidable.inner, inner, sizeof(inner));
                return snprintf(buf, buf_size, "??%s", inner);
            }

        case SERAPH_TYPE_STRUCT:
        case SERAPH_TYPE_ENUM:
            return snprintf(buf, buf_size, "%.*s", (int)t->named.name_len, t->named.name);

        case SERAPH_TYPE_TYPEVAR:
            if (t->typevar.bound) {
                return seraph_type_print(t->typevar.bound, buf, buf_size);
            }
            return snprintf(buf, buf_size, "'T%u", t->typevar.id);

        case SERAPH_TYPE_FN:
            {
                size_t written = snprintf(buf, buf_size, "fn(");
                for (size_t i = 0; i < t->fn.param_count && written < buf_size; i++) {
                    if (i > 0) written += snprintf(buf + written, buf_size - written, ", ");
                    char param[32];
                    seraph_type_print(t->fn.params[i], param, sizeof(param));
                    written += snprintf(buf + written, buf_size - written, "%s", param);
                }
                char ret[32];
                seraph_type_print(t->fn.ret, ret, sizeof(ret));
                written += snprintf(buf + written, buf_size - written, ") -> %s", ret);
                return written;
            }

        default:
            return snprintf(buf, buf_size, "<unknown>");
    }
}
