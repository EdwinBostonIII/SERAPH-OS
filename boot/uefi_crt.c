/**
 * @file uefi_crt.c
 * @brief MC20: Minimal UEFI C Runtime Implementation
 *
 * Hand-written implementations of basic C library functions.
 * No dependencies on any external libraries.
 */

#include "uefi_crt.h"

/*============================================================================
 * Memory Operations
 *============================================================================*/

VOID* efi_memcpy(VOID* dst, const VOID* src, UINTN size) {
    UINT8* d = (UINT8*)dst;
    const UINT8* s = (const UINT8*)src;

    /* Copy 8 bytes at a time when possible */
    while (size >= 8) {
        *(UINT64*)d = *(const UINT64*)s;
        d += 8;
        s += 8;
        size -= 8;
    }

    /* Copy remaining bytes */
    while (size > 0) {
        *d++ = *s++;
        size--;
    }

    return dst;
}

VOID* efi_memset(VOID* dst, UINT8 value, UINTN size) {
    UINT8* d = (UINT8*)dst;

    /* Expand byte value to 64-bit pattern */
    UINT64 pattern = value;
    pattern |= pattern << 8;
    pattern |= pattern << 16;
    pattern |= pattern << 32;

    /* Set 8 bytes at a time when possible */
    while (size >= 8) {
        *(UINT64*)d = pattern;
        d += 8;
        size -= 8;
    }

    /* Set remaining bytes */
    while (size > 0) {
        *d++ = value;
        size--;
    }

    return dst;
}

INTN efi_memcmp(const VOID* s1, const VOID* s2, UINTN size) {
    const UINT8* p1 = (const UINT8*)s1;
    const UINT8* p2 = (const UINT8*)s2;

    while (size > 0) {
        if (*p1 != *p2) {
            return (INTN)*p1 - (INTN)*p2;
        }
        p1++;
        p2++;
        size--;
    }

    return 0;
}

VOID* efi_memmove(VOID* dst, const VOID* src, UINTN size) {
    UINT8* d = (UINT8*)dst;
    const UINT8* s = (const UINT8*)src;

    if (d == s || size == 0) {
        return dst;
    }

    /* Check for overlap and copy in appropriate direction */
    if (d < s || d >= s + size) {
        /* No overlap or dst before src: copy forward */
        while (size > 0) {
            *d++ = *s++;
            size--;
        }
    } else {
        /* Overlap with dst after src: copy backward */
        d += size;
        s += size;
        while (size > 0) {
            *--d = *--s;
            size--;
        }
    }

    return dst;
}

/*============================================================================
 * ASCII String Operations
 *============================================================================*/

UINTN efi_strlen(const char* str) {
    UINTN len = 0;
    if (str) {
        while (str[len] != '\0') {
            len++;
        }
    }
    return len;
}

INTN efi_strcmp(const char* s1, const char* s2) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;

    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }

    return (INTN)(UINT8)*s1 - (INTN)(UINT8)*s2;
}

INTN efi_strncmp(const char* s1, const char* s2, UINTN n) {
    if (n == 0) return 0;
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;

    while (n > 0 && *s1 && *s1 == *s2) {
        s1++;
        s2++;
        n--;
    }

    if (n == 0) return 0;
    return (INTN)(UINT8)*s1 - (INTN)(UINT8)*s2;
}

/*============================================================================
 * Wide String Operations (CHAR16)
 *============================================================================*/

UINTN efi_strlen16(const CHAR16* str) {
    UINTN len = 0;
    if (str) {
        while (str[len] != 0) {
            len++;
        }
    }
    return len;
}

INTN efi_strcmp16(const CHAR16* s1, const CHAR16* s2) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;

    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }

    return (INTN)*s1 - (INTN)*s2;
}

CHAR16* efi_strcpy16(CHAR16* dst, const CHAR16* src) {
    CHAR16* d = dst;
    if (dst && src) {
        while (*src) {
            *d++ = *src++;
        }
        *d = 0;
    }
    return dst;
}

CHAR16* efi_strcat16(CHAR16* dst, const CHAR16* src) {
    if (dst && src) {
        CHAR16* d = dst;
        /* Find end of dst */
        while (*d) d++;
        /* Copy src */
        while (*src) {
            *d++ = *src++;
        }
        *d = 0;
    }
    return dst;
}

/*============================================================================
 * Conversion Functions
 *============================================================================*/

UINT64 efi_str_to_uint64(const char* str) {
    UINT64 result = 0;

    if (!str) return 0;

    /* Skip leading whitespace */
    while (*str == ' ' || *str == '\t') str++;

    /* Handle hex prefix */
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        str += 2;
        while (*str) {
            UINT8 digit;
            if (*str >= '0' && *str <= '9') {
                digit = *str - '0';
            } else if (*str >= 'a' && *str <= 'f') {
                digit = *str - 'a' + 10;
            } else if (*str >= 'A' && *str <= 'F') {
                digit = *str - 'A' + 10;
            } else {
                break;
            }
            result = result * 16 + digit;
            str++;
        }
    } else {
        /* Decimal */
        while (*str >= '0' && *str <= '9') {
            result = result * 10 + (*str - '0');
            str++;
        }
    }

    return result;
}

UINT64 efi_str16_to_uint64(const CHAR16* str) {
    UINT64 result = 0;

    if (!str) return 0;

    /* Skip leading whitespace */
    while (*str == L' ' || *str == L'\t') str++;

    /* Handle hex prefix */
    if (str[0] == L'0' && (str[1] == L'x' || str[1] == L'X')) {
        str += 2;
        while (*str) {
            UINT8 digit;
            if (*str >= L'0' && *str <= L'9') {
                digit = (UINT8)(*str - L'0');
            } else if (*str >= L'a' && *str <= L'f') {
                digit = (UINT8)(*str - L'a' + 10);
            } else if (*str >= L'A' && *str <= L'F') {
                digit = (UINT8)(*str - L'A' + 10);
            } else {
                break;
            }
            result = result * 16 + digit;
            str++;
        }
    } else {
        /* Decimal */
        while (*str >= L'0' && *str <= L'9') {
            result = result * 10 + (*str - L'0');
            str++;
        }
    }

    return result;
}

CHAR16* efi_uint64_to_str16(UINT64 value, CHAR16* buffer, UINTN radix) {
    static const CHAR16 digits[] = L"0123456789ABCDEF";
    CHAR16 temp[24];
    UINTN i = 0;
    UINTN j = 0;

    if (!buffer) return NULL;
    if (radix < 2 || radix > 16) radix = 10;

    /* Handle zero */
    if (value == 0) {
        buffer[0] = L'0';
        buffer[1] = 0;
        return buffer;
    }

    /* Build string in reverse */
    while (value > 0) {
        temp[i++] = digits[value % radix];
        value /= radix;
    }

    /* Reverse into output buffer */
    while (i > 0) {
        buffer[j++] = temp[--i];
    }
    buffer[j] = 0;

    return buffer;
}

CHAR16* efi_ascii_to_wide(CHAR16* dst, const char* src, UINTN max_chars) {
    UINTN i = 0;

    if (!dst || !src || max_chars == 0) return dst;

    while (src[i] != '\0' && i < max_chars - 1) {
        dst[i] = (CHAR16)(UINT8)src[i];
        i++;
    }
    dst[i] = 0;

    return dst;
}

/*============================================================================
 * Output Helpers
 *============================================================================*/

VOID efi_print(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* con, const CHAR16* str) {
    if (con && str) {
        con->OutputString(con, (CHAR16*)str);
    }
}

VOID efi_print_ascii(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* con, const char* str) {
    if (con && str) {
        CHAR16 buffer[256];
        efi_ascii_to_wide(buffer, str, 256);
        con->OutputString(con, buffer);
    }
}

VOID efi_print_uint64(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* con, UINT64 value, UINTN radix) {
    if (con) {
        CHAR16 buffer[24];
        efi_uint64_to_str16(value, buffer, radix);
        con->OutputString(con, buffer);
    }
}

VOID efi_print_hex(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* con, UINT64 value) {
    if (con) {
        efi_print(con, L"0x");
        efi_print_uint64(con, value, 16);
    }
}

VOID efi_print_newline(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* con) {
    if (con) {
        con->OutputString(con, L"\r\n");
    }
}
