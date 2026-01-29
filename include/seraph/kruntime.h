/**
 * @file kruntime.h
 * @brief SERAPH Kernel Runtime Library Declarations
 *
 * Provides declarations for freestanding implementations of standard C
 * functions. All implementations are pure NIH - no external dependencies.
 *
 * Include this header in kernel mode instead of <string.h> and <stdlib.h>.
 */

#ifndef SERAPH_KRUNTIME_H
#define SERAPH_KRUNTIME_H

#include <stddef.h>  /* size_t */
#include <stdint.h>  /* Fixed-width types */

/*============================================================================
 * Memory Operations
 *============================================================================*/

/**
 * @brief Fill memory with a constant byte
 * @param dest Pointer to memory to fill
 * @param val Value to fill with (converted to unsigned char)
 * @param count Number of bytes to fill
 * @return dest
 */
void* memset(void* dest, int val, size_t count);

/**
 * @brief Copy memory (non-overlapping regions)
 * @param dest Destination pointer
 * @param src Source pointer
 * @param count Number of bytes to copy
 * @return dest
 */
void* memcpy(void* dest, const void* src, size_t count);

/**
 * @brief Copy memory (overlapping safe)
 * @param dest Destination pointer
 * @param src Source pointer
 * @param count Number of bytes to copy
 * @return dest
 */
void* memmove(void* dest, const void* src, size_t count);

/**
 * @brief Compare memory
 * @param ptr1 First memory region
 * @param ptr2 Second memory region
 * @param count Number of bytes to compare
 * @return <0 if ptr1 < ptr2, 0 if equal, >0 if ptr1 > ptr2
 */
int memcmp(const void* ptr1, const void* ptr2, size_t count);

/*============================================================================
 * Memory Allocation
 *
 * These are wrappers to SERAPH's kmalloc system. In kernel mode, they map
 * to seraph_kmalloc, seraph_kcalloc, seraph_krealloc, seraph_kfree.
 *============================================================================*/

/**
 * @brief Allocate memory
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL on failure
 */
void* malloc(size_t size);

/**
 * @brief Allocate zeroed memory
 * @param nmemb Number of elements
 * @param size Size of each element
 * @return Pointer to zeroed memory, or NULL on failure
 */
void* calloc(size_t nmemb, size_t size);

/**
 * @brief Reallocate memory
 * @param ptr Pointer to existing allocation (or NULL)
 * @param size New size
 * @return Pointer to reallocated memory, or NULL on failure
 */
void* realloc(void* ptr, size_t size);

/**
 * @brief Free allocated memory
 * @param ptr Pointer to memory to free (or NULL)
 */
void free(void* ptr);

/**
 * @brief Allocate aligned memory
 * @param alignment Required alignment (must be power of 2)
 * @param size Number of bytes to allocate
 * @return Pointer to aligned memory, or NULL on failure
 */
void* aligned_alloc(size_t alignment, size_t size);

/**
 * @brief Free aligned memory
 * @param ptr Pointer returned from aligned_alloc (or NULL)
 */
void aligned_free(void* ptr);

/*============================================================================
 * String Operations
 *============================================================================*/

/**
 * @brief Get string length
 * @param str Null-terminated string
 * @return Length not including null terminator
 */
size_t strlen(const char* str);

/**
 * @brief Compare strings
 * @param s1 First string
 * @param s2 Second string
 * @return <0 if s1 < s2, 0 if equal, >0 if s1 > s2
 */
int strcmp(const char* s1, const char* s2);

/**
 * @brief Compare strings with limit
 * @param s1 First string
 * @param s2 Second string
 * @param n Maximum characters to compare
 * @return <0 if s1 < s2, 0 if equal, >0 if s1 > s2
 */
int strncmp(const char* s1, const char* s2, size_t n);

/**
 * @brief Copy string
 * @param dest Destination buffer
 * @param src Source string
 * @return dest
 */
char* strcpy(char* dest, const char* src);

/**
 * @brief Copy string with limit
 * @param dest Destination buffer
 * @param src Source string
 * @param n Maximum characters to copy
 * @return dest
 */
char* strncpy(char* dest, const char* src, size_t n);

#endif /* SERAPH_KRUNTIME_H */
