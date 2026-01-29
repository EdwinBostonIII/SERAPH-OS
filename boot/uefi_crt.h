/**
 * @file uefi_crt.h
 * @brief MC20: Minimal UEFI C Runtime
 *
 * Provides basic C library functions needed for the UEFI bootloader.
 * These are implemented from scratch to avoid dependencies.
 *
 * Functions provided:
 *   - Memory: memcpy, memset, memcmp, memmove
 *   - Strings: strlen, strcmp (for ASCII)
 *   - Wide strings: strlen16, strcmp16, strcpy16 (for UEFI CHAR16)
 *   - Conversion: str_to_uint64
 */

#ifndef SERAPH_UEFI_CRT_H
#define SERAPH_UEFI_CRT_H

#include "seraph/uefi_types.h"

/*============================================================================
 * Memory Operations
 *============================================================================*/

/**
 * @brief Copy memory
 *
 * @param dst Destination buffer
 * @param src Source buffer
 * @param size Number of bytes to copy
 * @return dst
 */
VOID* efi_memcpy(VOID* dst, const VOID* src, UINTN size);

/**
 * @brief Set memory to a value
 *
 * @param dst Destination buffer
 * @param value Byte value to set
 * @param size Number of bytes to set
 * @return dst
 */
VOID* efi_memset(VOID* dst, UINT8 value, UINTN size);

/**
 * @brief Compare memory
 *
 * @param s1 First buffer
 * @param s2 Second buffer
 * @param size Number of bytes to compare
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2
 */
INTN efi_memcmp(const VOID* s1, const VOID* s2, UINTN size);

/**
 * @brief Move memory (handles overlapping regions)
 *
 * @param dst Destination buffer
 * @param src Source buffer
 * @param size Number of bytes to move
 * @return dst
 */
VOID* efi_memmove(VOID* dst, const VOID* src, UINTN size);

/*============================================================================
 * ASCII String Operations
 *============================================================================*/

/**
 * @brief Get length of ASCII string
 *
 * @param str Null-terminated string
 * @return Length not including null terminator
 */
UINTN efi_strlen(const char* str);

/**
 * @brief Compare ASCII strings
 *
 * @param s1 First string
 * @param s2 Second string
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2
 */
INTN efi_strcmp(const char* s1, const char* s2);

/**
 * @brief Compare ASCII strings up to n characters
 *
 * @param s1 First string
 * @param s2 Second string
 * @param n Maximum characters to compare
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2
 */
INTN efi_strncmp(const char* s1, const char* s2, UINTN n);

/*============================================================================
 * Wide String Operations (CHAR16)
 *============================================================================*/

/**
 * @brief Get length of wide string
 *
 * @param str Null-terminated CHAR16 string
 * @return Length not including null terminator
 */
UINTN efi_strlen16(const CHAR16* str);

/**
 * @brief Compare wide strings
 *
 * @param s1 First string
 * @param s2 Second string
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2
 */
INTN efi_strcmp16(const CHAR16* s1, const CHAR16* s2);

/**
 * @brief Copy wide string
 *
 * @param dst Destination buffer
 * @param src Source string
 * @return dst
 */
CHAR16* efi_strcpy16(CHAR16* dst, const CHAR16* src);

/**
 * @brief Concatenate wide strings
 *
 * @param dst Destination buffer (must have space)
 * @param src Source string to append
 * @return dst
 */
CHAR16* efi_strcat16(CHAR16* dst, const CHAR16* src);

/*============================================================================
 * Conversion Functions
 *============================================================================*/

/**
 * @brief Convert ASCII string to unsigned 64-bit integer
 *
 * @param str String containing decimal number
 * @return Converted value, or 0 if invalid
 */
UINT64 efi_str_to_uint64(const char* str);

/**
 * @brief Convert wide string to unsigned 64-bit integer
 *
 * @param str CHAR16 string containing decimal number
 * @return Converted value, or 0 if invalid
 */
UINT64 efi_str16_to_uint64(const CHAR16* str);

/**
 * @brief Convert unsigned 64-bit integer to wide string
 *
 * @param value Value to convert
 * @param buffer Output buffer (must be at least 21 CHAR16s)
 * @param radix Base (10 for decimal, 16 for hex)
 * @return buffer
 */
CHAR16* efi_uint64_to_str16(UINT64 value, CHAR16* buffer, UINTN radix);

/**
 * @brief Convert ASCII string to wide string
 *
 * @param dst Destination CHAR16 buffer
 * @param src Source ASCII string
 * @param max_chars Maximum characters to convert (including null)
 * @return dst
 */
CHAR16* efi_ascii_to_wide(CHAR16* dst, const char* src, UINTN max_chars);

/*============================================================================
 * Output Helpers
 *============================================================================*/

/**
 * @brief Print wide string to console
 *
 * @param con Console output protocol
 * @param str String to print
 */
VOID efi_print(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* con, const CHAR16* str);

/**
 * @brief Print ASCII string to console (converts to wide)
 *
 * @param con Console output protocol
 * @param str ASCII string to print
 */
VOID efi_print_ascii(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* con, const char* str);

/**
 * @brief Print unsigned integer to console
 *
 * @param con Console output protocol
 * @param value Value to print
 * @param radix Base (10 for decimal, 16 for hex)
 */
VOID efi_print_uint64(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* con, UINT64 value, UINTN radix);

/**
 * @brief Print hexadecimal value with prefix
 *
 * @param con Console output protocol
 * @param value Value to print
 */
VOID efi_print_hex(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* con, UINT64 value);

/**
 * @brief Print newline
 *
 * @param con Console output protocol
 */
VOID efi_print_newline(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* con);

#endif /* SERAPH_UEFI_CRT_H */
