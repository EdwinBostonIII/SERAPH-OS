/**
 * @file math_cache.h
 * @brief SERAPH Branchless Math Memoization Cache
 *
 * MC26: SERAPH Performance Revolution
 *
 * Provides branchless, constant-time memoization for expensive
 * mathematical computations. Cache lookup never branches on
 * hit/miss - both paths execute with the same timing.
 *
 * Design Philosophy:
 *   - Zero conditional branches (timing-attack resistant)
 *   - Fixed-size direct-mapped cache (predictable memory)
 *   - XOR-based mixing for index computation
 *   - Valid bits packed for cache-line efficiency
 *
 * Cache Organization:
 *   - Power-of-2 entries (default: 256)
 *   - Each entry: key + value
 *   - Valid bits stored separately for cache efficiency
 *   - Direct-mapped: index = hash(key) & (size-1)
 */

#ifndef SERAPH_MATH_CACHE_H
#define SERAPH_MATH_CACHE_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * Cache Configuration
 *============================================================================*/

/** Default cache size (must be power of 2) */
#ifndef SERAPH_MATH_CACHE_SIZE
#define SERAPH_MATH_CACHE_SIZE 256
#endif

/** Cache size mask for index computation */
#define SERAPH_MATH_CACHE_MASK (SERAPH_MATH_CACHE_SIZE - 1)

/*============================================================================
 * Hash Functions (Branchless)
 *============================================================================*/

/**
 * @brief Mix bits for cache index (32-bit)
 *
 * Uses MurmurHash3 finalizer mixing.
 */
static inline uint32_t seraph_cache_hash32(uint32_t key) {
    key ^= key >> 16;
    key *= 0x85ebca6b;
    key ^= key >> 13;
    key *= 0xc2b2ae35;
    key ^= key >> 16;
    return key;
}

/**
 * @brief Mix bits for cache index (64-bit)
 *
 * SplitMix64 mixing function.
 */
static inline uint64_t seraph_cache_hash64(uint64_t key) {
    key ^= key >> 30;
    key *= 0xbf58476d1ce4e5b9ULL;
    key ^= key >> 27;
    key *= 0x94d049bb133111ebULL;
    key ^= key >> 31;
    return key;
}

/*============================================================================
 * Branchless Select
 *============================================================================*/

/**
 * @brief Branchless conditional select (32-bit)
 *
 * Returns a if cond, else b.
 * No branches - constant time.
 */
static inline uint32_t seraph_select32(int cond, uint32_t a, uint32_t b) {
    uint32_t mask = -(uint32_t)(cond != 0);
    return (a & mask) | (b & ~mask);
}

/**
 * @brief Branchless conditional select (64-bit)
 */
static inline uint64_t seraph_select64(int cond, uint64_t a, uint64_t b) {
    uint64_t mask = -(uint64_t)(cond != 0);
    return (a & mask) | (b & ~mask);
}

/*============================================================================
 * Q16 Trig Cache
 *============================================================================*/

/**
 * @brief Cache entry for Q16 sin/cos pair
 */
typedef struct {
    int32_t key;        /**< Input angle (or VOID if empty) */
    int32_t sin_val;    /**< Cached sin(key) */
    int32_t cos_val;    /**< Cached cos(key) */
    int32_t valid;      /**< Non-zero if entry valid */
} Seraph_Q16_Trig_Entry;

/**
 * @brief Q16 trig cache
 */
typedef struct {
    Seraph_Q16_Trig_Entry entries[SERAPH_MATH_CACHE_SIZE];
    uint32_t hits;      /**< Cache hit count */
    uint32_t misses;    /**< Cache miss count */
} Seraph_Q16_Trig_Cache;

/**
 * @brief Initialize Q16 trig cache
 */
static inline void seraph_q16_trig_cache_init(Seraph_Q16_Trig_Cache* cache) {
    memset(cache, 0, sizeof(*cache));
}

/**
 * @brief Look up or compute sin/cos (branchless)
 *
 * Returns cached value if present, else computes and caches.
 * The control flow is identical for hit and miss (branchless).
 *
 * @param cache The cache
 * @param angle Input angle in Q16
 * @param sin_out Output: sin(angle)
 * @param cos_out Output: cos(angle)
 * @param compute Function to compute sin/cos if not cached
 */
typedef void (*Seraph_Sincos_Fn)(int32_t angle, int32_t* sin_out, int32_t* cos_out);

static inline void seraph_q16_trig_cache_lookup(
    Seraph_Q16_Trig_Cache* cache,
    int32_t angle,
    int32_t* sin_out,
    int32_t* cos_out,
    Seraph_Sincos_Fn compute)
{
    /* Compute cache index */
    uint32_t idx = seraph_cache_hash32((uint32_t)angle) & SERAPH_MATH_CACHE_MASK;
    Seraph_Q16_Trig_Entry* entry = &cache->entries[idx];

    /* Check for hit (branchless) */
    int hit = (entry->valid != 0) && (entry->key == angle);

    /* Always compute (even on hit - discarded if not needed) */
    int32_t computed_sin, computed_cos;
    compute(angle, &computed_sin, &computed_cos);

    /* Branchless select: use cached if hit, else computed */
    *sin_out = seraph_select32(hit, entry->sin_val, computed_sin);
    *cos_out = seraph_select32(hit, entry->cos_val, computed_cos);

    /* Always update cache (overwrites on miss, no-op semantically on hit) */
    /* This is branchless but may hurt cache performance on hits */
    /* For truly branchless, we accept the redundant write */
    entry->key = angle;
    entry->sin_val = *sin_out;
    entry->cos_val = *cos_out;
    entry->valid = 1;

    /* Update stats (these can be removed in release builds) */
#ifdef SERAPH_CACHE_STATS
    cache->hits += hit;
    cache->misses += !hit;
#endif
}

/*============================================================================
 * Q64 Trig Cache
 *============================================================================*/

/**
 * @brief Cache entry for Q64 sin/cos pair
 */
typedef struct {
    int64_t key_hi;     /**< Input angle high bits */
    uint64_t key_lo;    /**< Input angle low bits */
    int64_t sin_hi;     /**< Cached sin high bits */
    uint64_t sin_lo;    /**< Cached sin low bits */
    int64_t cos_hi;     /**< Cached cos high bits */
    uint64_t cos_lo;    /**< Cached cos low bits */
    int64_t valid;      /**< Non-zero if valid */
} Seraph_Q64_Trig_Entry;

/**
 * @brief Q64 trig cache
 */
typedef struct {
    Seraph_Q64_Trig_Entry entries[SERAPH_MATH_CACHE_SIZE];
    uint64_t hits;
    uint64_t misses;
} Seraph_Q64_Trig_Cache;

/**
 * @brief Initialize Q64 trig cache
 */
static inline void seraph_q64_trig_cache_init(Seraph_Q64_Trig_Cache* cache) {
    memset(cache, 0, sizeof(*cache));
}

/*============================================================================
 * Generic Value Cache (LRU)
 *============================================================================*/

/**
 * @brief Generic memoization cache for single-argument functions
 *
 * Maps uint64_t -> uint64_t with direct-mapped caching.
 */
typedef struct {
    uint64_t keys[SERAPH_MATH_CACHE_SIZE];
    uint64_t values[SERAPH_MATH_CACHE_SIZE];
    uint8_t valid[SERAPH_MATH_CACHE_SIZE / 8 + 1];  /* Packed valid bits */
} Seraph_Memo_Cache;

/**
 * @brief Initialize memo cache
 */
static inline void seraph_memo_cache_init(Seraph_Memo_Cache* cache) {
    memset(cache, 0, sizeof(*cache));
}

/**
 * @brief Check if entry is valid (branchless bit extraction)
 */
static inline int seraph_memo_cache_valid(const Seraph_Memo_Cache* cache, uint32_t idx) {
    return (cache->valid[idx / 8] >> (idx % 8)) & 1;
}

/**
 * @brief Set entry valid bit
 */
static inline void seraph_memo_cache_set_valid(Seraph_Memo_Cache* cache, uint32_t idx) {
    cache->valid[idx / 8] |= (1 << (idx % 8));
}

/**
 * @brief Lookup or compute (branchless)
 *
 * @param cache The cache
 * @param key Input key
 * @param compute Function to compute value if not cached
 * @return Cached or computed value
 */
typedef uint64_t (*Seraph_Compute_Fn)(uint64_t key);

static inline uint64_t seraph_memo_cache_lookup(
    Seraph_Memo_Cache* cache,
    uint64_t key,
    Seraph_Compute_Fn compute)
{
    uint32_t idx = (uint32_t)(seraph_cache_hash64(key) & SERAPH_MATH_CACHE_MASK);

    int valid = seraph_memo_cache_valid(cache, idx);
    int hit = valid && (cache->keys[idx] == key);

    /* Always compute (branchless) */
    uint64_t computed = compute(key);

    /* Select cached or computed */
    uint64_t result = seraph_select64(hit, cache->values[idx], computed);

    /* Update cache */
    cache->keys[idx] = key;
    cache->values[idx] = result;
    seraph_memo_cache_set_valid(cache, idx);

    return result;
}

/*============================================================================
 * Cache Statistics
 *============================================================================*/

#ifdef SERAPH_CACHE_STATS

/**
 * @brief Get Q16 cache hit rate
 */
static inline double seraph_q16_cache_hit_rate(const Seraph_Q16_Trig_Cache* cache) {
    uint64_t total = cache->hits + cache->misses;
    if (total == 0) return 0.0;
    return (double)cache->hits / (double)total;
}

/**
 * @brief Reset Q16 cache statistics
 */
static inline void seraph_q16_cache_reset_stats(Seraph_Q16_Trig_Cache* cache) {
    cache->hits = 0;
    cache->misses = 0;
}

#endif /* SERAPH_CACHE_STATS */

/*============================================================================
 * Thread-Local Caches
 *============================================================================*/

/**
 * @brief Get thread-local Q16 trig cache
 *
 * Each strand has its own cache to avoid contention.
 */
Seraph_Q16_Trig_Cache* seraph_q16_trig_cache_get(void);

/**
 * @brief Get thread-local Q64 trig cache
 */
Seraph_Q64_Trig_Cache* seraph_q64_trig_cache_get(void);

#ifdef __cplusplus
}
#endif

#endif /* SERAPH_MATH_CACHE_H */
