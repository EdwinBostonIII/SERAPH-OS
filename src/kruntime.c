/**
 * @file kruntime.c
 * @brief SERAPH Kernel Runtime Library
 *
 * Provides freestanding implementations of standard C functions.
 * These are pure NIH implementations - no external dependencies.
 *
 * Functions provided:
 *   - memset, memcpy, memmove, memcmp
 *   - malloc, calloc, realloc, free (wrappers to kmalloc)
 *   - strlen, strcmp, strncmp
 */

#include "seraph/kmalloc.h"
#include <stddef.h>
#include <stdint.h>

/* Forward declarations from kmalloc.h */
extern void* seraph_kmalloc(size_t size);
extern void* seraph_kcalloc(size_t nmemb, size_t size);
extern void* seraph_krealloc(void* ptr, size_t size);
extern void seraph_kfree(void* ptr);

/* Map to SERAPH allocator functions */
#define kmalloc seraph_kmalloc
#define kcalloc seraph_kcalloc
#define krealloc seraph_krealloc
#define kfree seraph_kfree

/*============================================================================
 * Memory Operations (Kernel only - userspace uses standard library)
 *============================================================================*/

#ifdef SERAPH_KERNEL

/**
 * @brief Fill memory with a constant byte
 */
void* memset(void* dest, int val, size_t count) {
    unsigned char* d = (unsigned char*)dest;
    unsigned char v = (unsigned char)val;

    /* Optimize for common case of zero-fill */
    if (v == 0 && count >= 8) {
        /* Align to 8 bytes */
        while (((size_t)d & 7) && count) {
            *d++ = 0;
            count--;
        }
        /* Fill 8 bytes at a time */
        uint64_t* d64 = (uint64_t*)d;
        while (count >= 8) {
            *d64++ = 0;
            count -= 8;
        }
        d = (unsigned char*)d64;
    }

    /* Fill remaining bytes */
    while (count--) {
        *d++ = v;
    }

    return dest;
}

/**
 * @brief Copy memory (non-overlapping)
 */
void* memcpy(void* dest, const void* src, size_t count) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;

    /* Optimize for aligned 8-byte copies */
    if (count >= 8 && ((size_t)d & 7) == ((size_t)s & 7)) {
        /* Align to 8 bytes */
        while (((size_t)d & 7) && count) {
            *d++ = *s++;
            count--;
        }
        /* Copy 8 bytes at a time */
        uint64_t* d64 = (uint64_t*)d;
        const uint64_t* s64 = (const uint64_t*)s;
        while (count >= 8) {
            *d64++ = *s64++;
            count -= 8;
        }
        d = (unsigned char*)d64;
        s = (const unsigned char*)s64;
    }

    /* Copy remaining bytes */
    while (count--) {
        *d++ = *s++;
    }

    return dest;
}

/**
 * @brief Copy memory (overlapping safe)
 */
void* memmove(void* dest, const void* src, size_t count) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;

    if (d < s || d >= s + count) {
        /* No overlap, copy forward */
        return memcpy(dest, src, count);
    }

    /* Overlap - copy backward */
    d += count;
    s += count;
    while (count--) {
        *--d = *--s;
    }

    return dest;
}

/**
 * @brief Compare memory
 */
int memcmp(const void* ptr1, const void* ptr2, size_t count) {
    const unsigned char* p1 = (const unsigned char*)ptr1;
    const unsigned char* p2 = (const unsigned char*)ptr2;

    while (count--) {
        if (*p1 != *p2) {
            return (int)*p1 - (int)*p2;
        }
        p1++;
        p2++;
    }

    return 0;
}

/*============================================================================
 * Memory Allocation (Wrappers to SERAPH kmalloc)
 *============================================================================*/

/**
 * @brief Allocate memory
 */
void* malloc(size_t size) {
    return kmalloc(size);
}

/**
 * @brief Allocate zeroed memory
 */
void* calloc(size_t nmemb, size_t size) {
    return kcalloc(nmemb, size);
}

/**
 * @brief Reallocate memory
 */
void* realloc(void* ptr, size_t size) {
    return krealloc(ptr, size);
}

/**
 * @brief Free memory
 */
void free(void* ptr) {
    kfree(ptr);
}

/**
 * @brief Allocate aligned memory
 *
 * Uses the kernel page allocator for large alignments,
 * otherwise allocates extra and aligns manually.
 */
void* aligned_alloc(size_t alignment, size_t size) {
    if (alignment <= 16) {
        /* kmalloc is already 16-byte aligned */
        return kmalloc(size);
    }

    /* For larger alignments, allocate extra space */
    size_t total = size + alignment - 1 + sizeof(void*);
    void* raw = kmalloc(total);
    if (!raw) return NULL;

    /* Align the result */
    void* aligned = (void*)(((size_t)raw + sizeof(void*) + alignment - 1) & ~(alignment - 1));

    /* Store the original pointer just before the aligned address */
    ((void**)aligned)[-1] = raw;

    return aligned;
}

/**
 * @brief Free aligned memory
 */
void aligned_free(void* ptr) {
    if (!ptr) return;
    /* Retrieve the original pointer */
    void* raw = ((void**)ptr)[-1];
    kfree(raw);
}

/*============================================================================
 * String Operations
 *============================================================================*/

/**
 * @brief Get string length
 */
size_t strlen(const char* str) {
    const char* s = str;
    while (*s) s++;
    return (size_t)(s - str);
}

/**
 * @brief Compare strings
 */
int strcmp(const char* s1, const char* s2) {
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}

/**
 * @brief Compare strings with limit
 */
int strncmp(const char* s1, const char* s2, size_t n) {
    while (n && *s1 && *s1 == *s2) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}

/**
 * @brief Copy string
 */
char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

/**
 * @brief Copy string with limit
 */
char* strncpy(char* dest, const char* src, size_t n) {
    char* d = dest;
    while (n && (*d++ = *src++)) n--;
    while (n--) *d++ = '\0';
    return dest;
}

#endif /* SERAPH_KERNEL - end of memory/string operations */

/*============================================================================
 * GCC Runtime Support (Kernel only)
 *============================================================================*/

#ifdef SERAPH_KERNEL

/**
 * @brief Thread-local storage emulation for freestanding
 *
 * GCC generates calls to __emutls_get_address for thread-local variables.
 * In a single-threaded kernel boot context, we just return the address.
 */
void* __emutls_get_address(void* ptr) {
    /* For freestanding kernel, thread-local = regular storage */
    return ptr;
}

/**
 * @brief Stack protector failure handler
 */
void __stack_chk_fail(void) {
    /* Halt on stack corruption */
    while (1) {
        __asm__ volatile("hlt");
    }
}

/**
 * @brief Stack protector guard value
 */
uint64_t __stack_chk_guard = 0xDEADBEEFCAFEBABEULL;

#endif /* SERAPH_KERNEL - end of GCC runtime support */

/*============================================================================
 * Windows CRT Compatibility (for MinGW freestanding builds)
 * These are only needed for KERNEL builds
 *============================================================================*/

#if defined(_WIN32) && defined(SERAPH_KERNEL)
/**
 * @brief Windows-compatible aligned malloc
 *
 * We provide both the regular and __imp_ versions to satisfy different
 * linking modes (static vs import).
 */
void* _aligned_malloc(size_t size, size_t alignment) {
    return aligned_alloc(alignment, size);
}

/* Also provide the import version symbol */
void* (*__imp__aligned_malloc)(size_t, size_t) = _aligned_malloc;

/**
 * @brief Windows-compatible aligned free
 */
void _aligned_free(void* ptr) {
    aligned_free(ptr);
}

/* Also provide the import version symbol */
void (*__imp__aligned_free)(void*) = _aligned_free;
#endif /* _WIN32 && SERAPH_KERNEL */

/*============================================================================
 * Stub I/O Functions (for freestanding kernel builds)
 *
 * These are stubs that satisfy linker requirements for code that uses
 * printf/fprintf. In kernel mode, use seraph_kprintf or logging facilities.
 *============================================================================*/

#ifdef SERAPH_KERNEL

/* Minimal FILE structure for stubs */
typedef struct {
    int fd;
    int error;
} FILE;

/* Standard streams (kernel stubs) */
static FILE _stdin_stub = {0, 0};
static FILE _stdout_stub = {1, 0};
static FILE _stderr_stub = {2, 0};

FILE* stdin = &_stdin_stub;
FILE* stdout = &_stdout_stub;
FILE* stderr = &_stderr_stub;

/**
 * @brief Get standard stream (Windows CRT compatibility)
 */
FILE* __acrt_iob_func(unsigned index) {
    switch (index) {
        case 0: return &_stdin_stub;
        case 1: return &_stdout_stub;
        case 2: return &_stderr_stub;
        default: return NULL;
    }
}

/* Also provide the import version symbol */
FILE* (*__imp___acrt_iob_func)(unsigned) = __acrt_iob_func;

/**
 * @brief Stub printf - does nothing in kernel mode
 *
 * TODO: Replace with kernel console output
 */
int printf(const char* format, ...) {
    (void)format;
    return 0;
}

/**
 * @brief Stub fprintf - does nothing in kernel mode
 */
int fprintf(FILE* stream, const char* format, ...) {
    (void)stream;
    (void)format;
    return 0;
}

/**
 * @brief Stub snprintf
 */
int snprintf(char* str, size_t size, const char* format, ...) {
    (void)format;
    if (str && size > 0) {
        str[0] = '\0';
    }
    return 0;
}

/**
 * @brief Stub sprintf
 */
int sprintf(char* str, const char* format, ...) {
    (void)format;
    /* str is marked nonnull, so we can safely write to it */
    str[0] = '\0';
    return 0;
}

/**
 * @brief Stub sscanf
 */
int sscanf(const char* str, const char* format, ...) {
    (void)str;
    (void)format;
    return 0;
}

/**
 * @brief Stub fopen - always fails in kernel mode
 */
FILE* fopen(const char* filename, const char* mode) {
    (void)filename;
    (void)mode;
    return NULL;
}

/**
 * @brief Stub fclose
 */
int fclose(FILE* stream) {
    (void)stream;
    return 0;
}

/**
 * @brief Stub fread
 */
size_t fread(void* ptr, size_t size, size_t count, FILE* stream) {
    (void)ptr;
    (void)size;
    (void)count;
    (void)stream;
    return 0;
}

/**
 * @brief Stub fwrite
 */
size_t fwrite(const void* ptr, size_t size, size_t count, FILE* stream) {
    (void)ptr;
    (void)size;
    (void)count;
    (void)stream;
    return 0;
}

/**
 * @brief Stub fseek
 */
int fseek(FILE* stream, long offset, int whence) {
    (void)stream;
    (void)offset;
    (void)whence;
    return -1;
}

/**
 * @brief Stub ftell
 */
long ftell(FILE* stream) {
    (void)stream;
    return -1;
}

/**
 * @brief Stub fflush
 */
int fflush(FILE* stream) {
    (void)stream;
    return 0;
}

/**
 * @brief Stub fgets
 */
char* fgets(char* str, int n, FILE* stream) {
    (void)str;
    (void)n;
    (void)stream;
    return NULL;
}

/**
 * @brief Stub ferror
 */
int ferror(FILE* stream) {
    (void)stream;
    return 0;
}

/**
 * @brief Abort execution
 */
void abort(void) {
    /* Halt the CPU */
    while (1) {
        __asm__ volatile("hlt");
    }
}

/**
 * @brief Exit function (kernel stub)
 */
#ifdef SERAPH_KERNEL
void exit(int status) {
    (void)status;
    abort();
}

/**
 * @brief atexit stub
 */
int atexit(void (*func)(void)) {
    (void)func;
    return 0;
}
#endif /* SERAPH_KERNEL */

/**
 * @brief vsnprintf stub
 */
int vsnprintf(char* str, size_t size, const char* format, __builtin_va_list ap) {
    (void)format;
    (void)ap;
    if (str && size > 0) {
        str[0] = '\0';
    }
    return 0;
}

/**
 * @brief vfprintf stub
 */
int vfprintf(FILE* stream, const char* format, __builtin_va_list ap) {
    (void)stream;
    (void)format;
    (void)ap;
    return 0;
}

/**
 * @brief strtoull - Convert string to unsigned long long
 */
unsigned long long strtoull(const char* str, char** endptr, int base) {
    unsigned long long result = 0;
    const char* p = str;

    /* Skip whitespace */
    while (*p == ' ' || *p == '\t') p++;

    /* Handle 0x prefix for hex */
    if (base == 0 || base == 16) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            p += 2;
            base = 16;
        } else if (base == 0) {
            base = 10;
        }
    }

    /* Convert digits */
    while (*p) {
        int digit;
        if (*p >= '0' && *p <= '9') {
            digit = *p - '0';
        } else if (*p >= 'a' && *p <= 'f') {
            digit = *p - 'a' + 10;
        } else if (*p >= 'A' && *p <= 'F') {
            digit = *p - 'A' + 10;
        } else {
            break;
        }

        if (digit >= base) break;

        result = result * (unsigned)base + (unsigned)digit;
        p++;
    }

    if (endptr) *endptr = (char*)p;
    return result;
}

/**
 * @brief strtod stub - returns 0.0
 */
double strtod(const char* str, char** endptr) {
    (void)str;
    if (endptr) *endptr = (char*)str;
    return 0.0;
}

/**
 * @brief Simple random number generator state
 */
static unsigned int _rand_seed = 1;

/**
 * @brief Set random seed
 */
void srand(unsigned int seed) {
    _rand_seed = seed;
}

/**
 * @brief Simple LCG random number generator
 */
int rand(void) {
    _rand_seed = _rand_seed * 1103515245 + 12345;
    return (int)((_rand_seed / 65536) % 32768);
}

/**
 * @brief Get time (stub - returns 0)
 */
long long _time64(long long* timer) {
    if (timer) *timer = 0;
    return 0;
}

#endif /* SERAPH_KERNEL */

/*============================================================================
 * Math Functions (kernel stubs using compiler builtins where available)
 *============================================================================*/

#ifdef SERAPH_KERNEL

/**
 * @brief Square root (double)
 */
double sqrt(double x) {
    if (x < 0) return 0.0;
    if (x == 0) return 0.0;

    /* Newton-Raphson approximation */
    double guess = x / 2.0;
    for (int i = 0; i < 20; i++) {
        guess = (guess + x / guess) / 2.0;
    }
    return guess;
}

/**
 * @brief Square root (float)
 */
float sqrtf(float x) {
    return (float)sqrt((double)x);
}

/**
 * @brief Natural logarithm stub
 */
double log(double x) {
    (void)x;
    return 0.0;  /* Stub - proper implementation would need Taylor series */
}

/**
 * @brief Sine (float) - stub
 */
float sinf(float x) {
    (void)x;
    return 0.0f;  /* Stub */
}

/**
 * @brief Cosine (float) - stub
 */
float cosf(float x) {
    (void)x;
    return 1.0f;  /* Stub */
}

/**
 * @brief Arc sine stub
 */
double asin(double x) {
    (void)x;
    return 0.0;
}

/**
 * @brief Arc cosine stub
 */
double acos(double x) {
    (void)x;
    return 0.0;
}

/**
 * @brief Arc tangent stub
 */
double atan(double x) {
    (void)x;
    return 0.0;
}

/**
 * @brief Arc tangent of y/x stub
 */
double atan2(double y, double x) {
    (void)y;
    (void)x;
    return 0.0;
}

#endif /* SERAPH_KERNEL */

/*============================================================================
 * GCC/MinGW Runtime Intrinsics
 *============================================================================*/

/**
 * @brief Population count for 64-bit integer
 */
long long __popcountdi2(long long a) {
    unsigned long long x = (unsigned long long)a;
    x = x - ((x >> 1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    x = x * 0x0101010101010101ULL;
    return (long long)(x >> 56);
}

#ifdef _WIN32
/**
 * @brief Stack probing for large stack allocations (MinGW)
 *
 * Called by compiler for stack allocations > 4KB to ensure
 * each page is touched for proper stack growth.
 */
void ___chkstk_ms(void) {
    /* Stub - in freestanding kernel, stack is pre-allocated */
}
#endif
