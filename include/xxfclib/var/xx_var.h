/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file xx_var.h
 * @brief Variant data type with automatic memory management for dynamically allocated types.
 */

#ifndef XX_VAR_H
#define XX_VAR_H

#include "xxfclib/xxfc_defs.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enumeration of supported variant value types.
 */
typedef enum xx_var_type_e {
    XX_VAR_TYPE_NONE = 0,

    /* Primitive scalar types (no dynamic memory allocated) */
    XX_VAR_TYPE_INT8,
    XX_VAR_TYPE_INT16,
    XX_VAR_TYPE_INT32,
    XX_VAR_TYPE_INT64,
    XX_VAR_TYPE_UINT8,
    XX_VAR_TYPE_UINT16,
    XX_VAR_TYPE_UINT32,
    XX_VAR_TYPE_UINT64,
    XX_VAR_TYPE_FLOAT,
    XX_VAR_TYPE_DOUBLE,
    XX_VAR_TYPE_BOOL,

    /* Dynamically allocated types (memory allocated via xx_mem_alloc, freed automatically) */
    XX_VAR_TYPE_STRING,         /**< Owned dynamic ANSI/UTF-8 string (freed with xx_mem_free) */
    XX_VAR_TYPE_WSTRING,        /**< Owned dynamic Unicode string (wchar_t*, freed with xx_mem_free) */
    XX_VAR_TYPE_BYTES,          /**< Owned dynamic byte buffer (freed with xx_mem_free) */

    /* View types (non-owning pointer references, never freed) */
    XX_VAR_TYPE_STRING_VIEW,    /**< Non-owning ANSI/UTF-8 string pointer */
    XX_VAR_TYPE_WSTRING_VIEW,   /**< Non-owning Unicode wchar_t string pointer */
    XX_VAR_TYPE_BYTES_VIEW,     /**< Non-owning byte slice pointer */

    /* Generic pointer type */
    XX_VAR_TYPE_PTR             /**< Generic pointer (freed if is_allocated is true) */
} xx_var_type_t;

/**
 * @brief Custom destructor function pointer for variant pointer types.
 */
typedef void (*xx_var_free_fn)(void *ptr);

/**
 * @brief Variant structure holding scalar, view, or dynamically allocated values.
 */
typedef struct xx_var {
    uint32_t type;             /**< Variant value type (xx_var_type_t) */
    bool is_allocated;         /**< True if value holds allocated memory that must be freed */
    xx_var_free_fn free_fn;    /**< Optional custom destructor (if NULL, xx_mem_free is used) */
    union {
        int8_t i8;
        int16_t i16;
        int32_t i32;
        int64_t i64;
        uint8_t u8;
        uint16_t u16;
        uint32_t u32;
        uint64_t u64;
        float f;
        double d;
        bool b;
        struct {
            char *ptr;
            size_t len;
        } str;
        struct {
            wchar_t *ptr;
            size_t len;
        } wstr;
        struct {
            uint8_t *data;
            size_t size;
        } bytes;
        void *ptr;
    } val;
} xx_var;

typedef struct xx_var xx_var_t;
typedef struct xx_var XX_VAR;

/* ========================================================================= */
/* --- Lifecycle & Memory Management                                     --- */
/* ========================================================================= */

/**
 * @brief Initialize an xx_var to an empty state (XX_VAR_TYPE_NONE).
 * @param var Pointer to xx_var.
 */
XXFC_API void xx_var_init(xx_var *var);

/**
 * @brief Safely free any dynamically allocated memory held by this variant.
 * If the variant holds a scalar or view (is_allocated == false), no freeing occurs.
 * After cleanup, the variant is reset to XX_VAR_TYPE_NONE.
 * @param var Pointer to xx_var.
 */
XXFC_API void xx_var_cleanup(xx_var *var);

/**
 * @brief Deep-copy the contents of src into dst.
 * If src holds dynamically allocated memory, dst allocates its own copy.
 * Any previous allocated memory in dst is freed first.
 * @param dst Destination variant pointer.
 * @param src Source variant pointer.
 * @return True on success, false on allocation failure.
 */
XXFC_API bool xx_var_copy(xx_var *dst, const xx_var *src);

/**
 * @brief Check if the variant holds dynamically allocated memory.
 * @param var Pointer to xx_var.
 * @return True if memory was allocated and will be freed on cleanup.
 */
static inline bool xx_var_is_allocated(const xx_var *var) {
    return var ? var->is_allocated : false;
}

/**
 * @brief Get the type of the variant.
 * @param var Pointer to xx_var.
 * @return xx_var_type_t enum value.
 */
static inline xx_var_type_t xx_var_get_type(const xx_var *var) {
    return var ? (xx_var_type_t)var->type : XX_VAR_TYPE_NONE;
}

/* ========================================================================= */
/* --- Setters (Primitive Scalars - No Allocation)                       --- */
/* ========================================================================= */

XXFC_API void xx_var_set_i8(xx_var *var, int8_t val);
XXFC_API void xx_var_set_i16(xx_var *var, int16_t val);
XXFC_API void xx_var_set_i32(xx_var *var, int32_t val);
XXFC_API void xx_var_set_i64(xx_var *var, int64_t val);
XXFC_API void xx_var_set_u8(xx_var *var, uint8_t val);
XXFC_API void xx_var_set_u16(xx_var *var, uint16_t val);
XXFC_API void xx_var_set_u32(xx_var *var, uint32_t val);
XXFC_API void xx_var_set_u64(xx_var *var, uint64_t val);
XXFC_API void xx_var_set_float(xx_var *var, float val);
XXFC_API void xx_var_set_double(xx_var *var, double val);
XXFC_API void xx_var_set_bool(xx_var *var, bool val);

/* ========================================================================= */
/* --- Setters (Dynamic Memory Allocated - Freed Automatically)           --- */
/* ========================================================================= */

/**
 * @brief Set string by duplicating input with xx_mem_alloc (is_allocated = true).
 */
XXFC_API bool xx_var_set_str(xx_var *var, const char *str);

/**
 * @brief Set string by taking ownership of an existing heap-allocated buffer.
 */
XXFC_API bool xx_var_set_str_take(xx_var *var, char *str, size_t len);

/**
 * @brief Set Unicode string by duplicating input with xx_str_wdup/xx_mem_alloc (is_allocated = true).
 */
XXFC_API bool xx_var_set_wstr(xx_var *var, const wchar_t *wstr);

/**
 * @brief Set Unicode string by taking ownership of an existing heap-allocated buffer.
 */
XXFC_API bool xx_var_set_wstr_take(xx_var *var, wchar_t *wstr, size_t len);

/**
 * @brief Set binary bytes by allocating a copy with xx_mem_alloc (is_allocated = true).
 */
XXFC_API bool xx_var_set_bytes(xx_var *var, const void *data, size_t size);

/**
 * @brief Set binary bytes by taking ownership of an existing heap-allocated buffer.
 */
XXFC_API bool xx_var_set_bytes_take(xx_var *var, void *data, size_t size);

/* ========================================================================= */
/* --- Setters (Non-Owning Views - Not Freed)                             --- */
/* ========================================================================= */

/**
 * @brief Set string view (non-owning reference, is_allocated = false).
 */
XXFC_API void xx_var_set_str_view(xx_var *var, const char *str, size_t len);

/**
 * @brief Set Unicode string view (non-owning reference, is_allocated = false).
 */
XXFC_API void xx_var_set_wstr_view(xx_var *var, const wchar_t *wstr, size_t len);

/**
 * @brief Set bytes view (non-owning reference, is_allocated = false).
 */
XXFC_API void xx_var_set_bytes_view(xx_var *var, const void *data, size_t size);

/**
 * @brief Set generic pointer, optionally marking it for automatic deallocation.
 */
XXFC_API void xx_var_set_ptr(xx_var *var, void *ptr, bool is_allocated, xx_var_free_fn free_fn);

/* ========================================================================= */
/* --- Getters                                                           --- */
/* ========================================================================= */

XXFC_API int64_t xx_var_get_i64(const xx_var *var);
XXFC_API uint64_t xx_var_get_u64(const xx_var *var);
XXFC_API double xx_var_get_double(const xx_var *var);
XXFC_API bool xx_var_get_bool(const xx_var *var);
XXFC_API const char* xx_var_get_str(const xx_var *var);
XXFC_API const wchar_t* xx_var_get_wstr(const xx_var *var);
XXFC_API const void* xx_var_get_bytes(const xx_var *var, size_t *out_size);
XXFC_API void* xx_var_get_ptr(const xx_var *var);

/* Unicode convenience aliases */
#define XX_VAR_TYPE_UNICODE       XX_VAR_TYPE_WSTRING
#define XX_VAR_TYPE_UNICODE_VIEW  XX_VAR_TYPE_WSTRING_VIEW
static inline bool xx_var_set_unicode(xx_var *var, const wchar_t *wstr) { return xx_var_set_wstr(var, wstr); }
static inline bool xx_var_set_unicode_take(xx_var *var, wchar_t *wstr, size_t len) { return xx_var_set_wstr_take(var, wstr, len); }
static inline void xx_var_set_unicode_view(xx_var *var, const wchar_t *wstr, size_t len) { xx_var_set_wstr_view(var, wstr, len); }
static inline const wchar_t* xx_var_get_unicode(const xx_var *var) { return xx_var_get_wstr(var); }

#ifdef __cplusplus
}
#endif

#endif /* XX_VAR_H */
