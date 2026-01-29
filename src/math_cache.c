/**
 * @file math_cache.c
 * @brief SERAPH Branchless Math Cache Implementation
 *
 * MC26: SERAPH Performance Revolution
 *
 * Thread-local cache management and utility functions.
 */

#include "seraph/math_cache.h"
#include "seraph/q16_trig.h"
#include <string.h>

/*============================================================================
 * Thread-Local Storage
 *
 * Each strand gets its own cache to avoid contention.
 *============================================================================*/

#if defined(__GNUC__) || defined(__clang__)
#define SERAPH_THREAD_LOCAL __thread
#elif defined(_MSC_VER)
#define SERAPH_THREAD_LOCAL __declspec(thread)
#else
#define SERAPH_THREAD_LOCAL  /* No thread-local support */
#endif

/* Thread-local Q16 trig cache */
static SERAPH_THREAD_LOCAL Seraph_Q16_Trig_Cache tls_q16_cache;
static SERAPH_THREAD_LOCAL int tls_q16_cache_initialized = 0;

/* Thread-local Q64 trig cache */
static SERAPH_THREAD_LOCAL Seraph_Q64_Trig_Cache tls_q64_cache;
static SERAPH_THREAD_LOCAL int tls_q64_cache_initialized = 0;

Seraph_Q16_Trig_Cache* seraph_q16_trig_cache_get(void) {
    if (!tls_q16_cache_initialized) {
        seraph_q16_trig_cache_init(&tls_q16_cache);
        tls_q16_cache_initialized = 1;
    }
    return &tls_q16_cache;
}

Seraph_Q64_Trig_Cache* seraph_q64_trig_cache_get(void) {
    if (!tls_q64_cache_initialized) {
        seraph_q64_trig_cache_init(&tls_q64_cache);
        tls_q64_cache_initialized = 1;
    }
    return &tls_q64_cache;
}

/*============================================================================
 * Cached Trig Functions
 *============================================================================*/

/**
 * @brief Q16 sincos callback for cache
 */
static void q16_sincos_callback(int32_t angle, int32_t* sin_out, int32_t* cos_out) {
    q16_sincos((Q16)angle, (Q16*)sin_out, (Q16*)cos_out);
}

/**
 * @brief Cached Q16 sincos
 */
void seraph_q16_sincos_cached(Q16 angle, Q16* sin_out, Q16* cos_out) {
    Seraph_Q16_Trig_Cache* cache = seraph_q16_trig_cache_get();
    seraph_q16_trig_cache_lookup(cache, angle, sin_out, cos_out, q16_sincos_callback);
}

/**
 * @brief Cached Q16 sin
 */
Q16 seraph_q16_sin_cached(Q16 angle) {
    Q16 s, c;
    seraph_q16_sincos_cached(angle, &s, &c);
    return s;
}

/**
 * @brief Cached Q16 cos
 */
Q16 seraph_q16_cos_cached(Q16 angle) {
    Q16 s, c;
    seraph_q16_sincos_cached(angle, &s, &c);
    return c;
}

/*============================================================================
 * Cache Warming
 *============================================================================*/

/**
 * @brief Pre-populate cache with common angles
 */
void seraph_q16_cache_warm(void) {
    Seraph_Q16_Trig_Cache* cache = seraph_q16_trig_cache_get();

    /* Warm cache with angles at regular intervals */
    Q16 step = Q16_2PI / 256;

    for (int i = 0; i < 256; i++) {
        Q16 angle = step * i;
        Q16 s, c;
        seraph_q16_trig_cache_lookup(cache, angle, &s, &c, q16_sincos_callback);
    }
}

/**
 * @brief Pre-populate cache with specific angles
 */
void seraph_q16_cache_warm_angles(const Q16* angles, size_t count) {
    Seraph_Q16_Trig_Cache* cache = seraph_q16_trig_cache_get();

    for (size_t i = 0; i < count; i++) {
        Q16 s, c;
        seraph_q16_trig_cache_lookup(cache, angles[i], &s, &c, q16_sincos_callback);
    }
}

/*============================================================================
 * Cache Diagnostics
 *============================================================================*/

#ifdef SERAPH_CACHE_STATS

/**
 * @brief Print cache statistics
 */
void seraph_q16_cache_print_stats(void) {
    Seraph_Q16_Trig_Cache* cache = seraph_q16_trig_cache_get();

    uint32_t total = cache->hits + cache->misses;
    double hit_rate = (total > 0) ? (double)cache->hits / total : 0.0;

    /* Would use seraph_print or similar */
    (void)hit_rate;  /* Suppress unused warning in release */
}

/**
 * @brief Get detailed cache info
 */
void seraph_q16_cache_info(uint32_t* hits, uint32_t* misses,
                            uint32_t* occupied, uint32_t* capacity) {
    Seraph_Q16_Trig_Cache* cache = seraph_q16_trig_cache_get();

    if (hits) *hits = cache->hits;
    if (misses) *misses = cache->misses;

    if (occupied) {
        uint32_t count = 0;
        for (int i = 0; i < SERAPH_MATH_CACHE_SIZE; i++) {
            if (cache->entries[i].valid) count++;
        }
        *occupied = count;
    }

    if (capacity) *capacity = SERAPH_MATH_CACHE_SIZE;
}

#endif /* SERAPH_CACHE_STATS */

/*============================================================================
 * Generic Memo Cache Implementation
 *============================================================================*/

/**
 * @brief Create a memo cache
 */
Seraph_Memo_Cache* seraph_memo_cache_create(void) {
    /* In a full implementation, this would allocate from arena */
    static Seraph_Memo_Cache static_cache;
    static int initialized = 0;

    if (!initialized) {
        seraph_memo_cache_init(&static_cache);
        initialized = 1;
    }

    return &static_cache;
}

/**
 * @brief Clear all entries in memo cache
 */
void seraph_memo_cache_clear(Seraph_Memo_Cache* cache) {
    if (cache == NULL) return;
    memset(cache, 0, sizeof(*cache));
}

/*============================================================================
 * Specialized Caches
 *============================================================================*/

/**
 * @brief Square root cache
 */
typedef struct {
    uint64_t keys[SERAPH_MATH_CACHE_SIZE];
    uint32_t values[SERAPH_MATH_CACHE_SIZE];  /* Q16 results */
    uint8_t valid[SERAPH_MATH_CACHE_SIZE / 8 + 1];
} Seraph_Sqrt_Cache;

static SERAPH_THREAD_LOCAL Seraph_Sqrt_Cache tls_sqrt_cache;
static SERAPH_THREAD_LOCAL int tls_sqrt_initialized = 0;

/**
 * @brief Cached Q16 sqrt
 */
Q16 seraph_q16_sqrt_cached(Q16 x) {
    if (!tls_sqrt_initialized) {
        memset(&tls_sqrt_cache, 0, sizeof(tls_sqrt_cache));
        tls_sqrt_initialized = 1;
    }

    if (x <= 0) return 0;

    uint32_t idx = seraph_cache_hash32((uint32_t)x) & SERAPH_MATH_CACHE_MASK;

    int valid = (tls_sqrt_cache.valid[idx / 8] >> (idx % 8)) & 1;
    int hit = valid && (tls_sqrt_cache.keys[idx] == (uint64_t)x);

    /* Always compute (branchless) */
    Q16 computed = q16_sqrt(x);

    /* Select result */
    Q16 result = seraph_select32(hit, tls_sqrt_cache.values[idx], (uint32_t)computed);

    /* Update cache */
    tls_sqrt_cache.keys[idx] = (uint64_t)x;
    tls_sqrt_cache.values[idx] = (uint32_t)result;
    tls_sqrt_cache.valid[idx / 8] |= (1 << (idx % 8));

    return result;
}

/*============================================================================
 * Angle Normalization Cache
 *
 * Caches the result of angle reduction for frequently used angles.
 *============================================================================*/

typedef struct {
    Q16 input[SERAPH_MATH_CACHE_SIZE];
    Q16 reduced[SERAPH_MATH_CACHE_SIZE];
    int8_t quadrant[SERAPH_MATH_CACHE_SIZE];
    uint8_t valid[SERAPH_MATH_CACHE_SIZE / 8 + 1];
} Seraph_AngleReduce_Cache;

static SERAPH_THREAD_LOCAL Seraph_AngleReduce_Cache tls_angle_cache;
static SERAPH_THREAD_LOCAL int tls_angle_initialized = 0;

/**
 * @brief Cached angle reduction
 */
Q16 seraph_q16_reduce_cached(Q16 angle, int* quadrant) {
    if (!tls_angle_initialized) {
        memset(&tls_angle_cache, 0, sizeof(tls_angle_cache));
        tls_angle_initialized = 1;
    }

    uint32_t idx = seraph_cache_hash32((uint32_t)angle) & SERAPH_MATH_CACHE_MASK;

    int valid = (tls_angle_cache.valid[idx / 8] >> (idx % 8)) & 1;
    int hit = valid && (tls_angle_cache.input[idx] == angle);

    /* Always compute */
    int computed_quad;
    Q16 computed = q16_reduce_angle(angle, &computed_quad);

    /* Select */
    Q16 result = seraph_select32(hit, tls_angle_cache.reduced[idx], computed);
    *quadrant = hit ? tls_angle_cache.quadrant[idx] : computed_quad;

    /* Update cache */
    tls_angle_cache.input[idx] = angle;
    tls_angle_cache.reduced[idx] = result;
    tls_angle_cache.quadrant[idx] = (int8_t)*quadrant;
    tls_angle_cache.valid[idx / 8] |= (1 << (idx % 8));

    return result;
}
