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

#include "xxfclib/var/xx_var.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* ========================================================================= */
/* --- Lifecycle & Memory Management                                     --- */
/* ========================================================================= */

void xx_var_init(xx_var *var) {
    if (!var) {
        return;
    }
    xx_mem_zero(var, sizeof(xx_var));
}

void xx_var_cleanup(xx_var *var) {
    if (!var) {
        return;
    }
    if (var->is_allocated) {
        if (var->free_fn) {
            if (var->type == XX_VAR_TYPE_STRING) {
                if (var->val.str.ptr) {
                    var->free_fn(var->val.str.ptr);
                }
            } else if (var->type == XX_VAR_TYPE_WSTRING) {
                if (var->val.wstr.ptr) {
                    var->free_fn(var->val.wstr.ptr);
                }
            } else if (var->type == XX_VAR_TYPE_BYTES) {
                if (var->val.bytes.data) {
                    var->free_fn(var->val.bytes.data);
                }
            } else {
                if (var->val.ptr) {
                    var->free_fn(var->val.ptr);
                }
            }
        } else {
            if (var->type == XX_VAR_TYPE_STRING && var->val.str.ptr) {
                xx_mem_free(var->val.str.ptr);
            } else if (var->type == XX_VAR_TYPE_WSTRING && var->val.wstr.ptr) {
                xx_str_wfree(var->val.wstr.ptr);
            } else if (var->type == XX_VAR_TYPE_BYTES && var->val.bytes.data) {
                xx_mem_free(var->val.bytes.data);
            } else if (var->type == XX_VAR_TYPE_PTR && var->val.ptr) {
                xx_mem_free(var->val.ptr);
            }
        }
    }
    xx_mem_zero(var, sizeof(xx_var));
}

bool xx_var_copy(xx_var *dst, const xx_var *src) {
    if (!dst) {
        return false;
    }
    xx_var_cleanup(dst);
    if (!src) {
        return true;
    }

    switch ((xx_var_type_t)src->type) {
        case XX_VAR_TYPE_STRING:
            return xx_var_set_str(dst, src->val.str.ptr);

        case XX_VAR_TYPE_WSTRING:
            return xx_var_set_wstr(dst, src->val.wstr.ptr);

        case XX_VAR_TYPE_BYTES:
            return xx_var_set_bytes(dst, src->val.bytes.data, src->val.bytes.size);

        case XX_VAR_TYPE_PTR:
            dst->type = src->type;
            dst->val.ptr = src->val.ptr;
            /* Cloned generic pointer does not own the memory to prevent double free */
            dst->is_allocated = false;
            dst->free_fn = NULL;
            return true;

        default:
            xx_mem_copy(dst, src, sizeof(xx_var));
            return true;
    }
}

/* ========================================================================= */
/* --- Setters (Primitive Scalars - No Allocation)                       --- */
/* ========================================================================= */

void xx_var_set_i8(xx_var *var, int8_t val) {
    if (!var) return;
    xx_var_cleanup(var);
    var->type = XX_VAR_TYPE_INT8;
    var->val.i8 = val;
}

void xx_var_set_i16(xx_var *var, int16_t val) {
    if (!var) return;
    xx_var_cleanup(var);
    var->type = XX_VAR_TYPE_INT16;
    var->val.i16 = val;
}

void xx_var_set_i32(xx_var *var, int32_t val) {
    if (!var) return;
    xx_var_cleanup(var);
    var->type = XX_VAR_TYPE_INT32;
    var->val.i32 = val;
}

void xx_var_set_i64(xx_var *var, int64_t val) {
    if (!var) return;
    xx_var_cleanup(var);
    var->type = XX_VAR_TYPE_INT64;
    var->val.i64 = val;
}

void xx_var_set_u8(xx_var *var, uint8_t val) {
    if (!var) return;
    xx_var_cleanup(var);
    var->type = XX_VAR_TYPE_UINT8;
    var->val.u8 = val;
}

void xx_var_set_u16(xx_var *var, uint16_t val) {
    if (!var) return;
    xx_var_cleanup(var);
    var->type = XX_VAR_TYPE_UINT16;
    var->val.u16 = val;
}

void xx_var_set_u32(xx_var *var, uint32_t val) {
    if (!var) return;
    xx_var_cleanup(var);
    var->type = XX_VAR_TYPE_UINT32;
    var->val.u32 = val;
}

void xx_var_set_u64(xx_var *var, uint64_t val) {
    if (!var) return;
    xx_var_cleanup(var);
    var->type = XX_VAR_TYPE_UINT64;
    var->val.u64 = val;
}

void xx_var_set_float(xx_var *var, float val) {
    if (!var) return;
    xx_var_cleanup(var);
    var->type = XX_VAR_TYPE_FLOAT;
    var->val.f = val;
}

void xx_var_set_double(xx_var *var, double val) {
    if (!var) return;
    xx_var_cleanup(var);
    var->type = XX_VAR_TYPE_DOUBLE;
    var->val.d = val;
}

void xx_var_set_bool(xx_var *var, bool val) {
    if (!var) return;
    xx_var_cleanup(var);
    var->type = XX_VAR_TYPE_BOOL;
    var->val.b = val;
}

/* ========================================================================= */
/* --- Setters (Dynamic Memory Allocated - Freed Automatically)           --- */
/* ========================================================================= */

bool xx_var_set_str(xx_var *var, const char *str) {
    if (!var) return false;
    xx_var_cleanup(var);
    if (!str) return false;

    char *dup = xx_str_dup(str);
    if (!dup) {
        return false;
    }

    var->type = XX_VAR_TYPE_STRING;
    var->is_allocated = true;
    var->free_fn = NULL;
    var->val.str.ptr = dup;
    var->val.str.len = xx_str_len(dup);
    return true;
}

bool xx_var_set_str_take(xx_var *var, char *str, size_t len) {
    if (!var) return false;
    xx_var_cleanup(var);

    var->type = XX_VAR_TYPE_STRING;
    var->is_allocated = (str != NULL);
    var->free_fn = NULL;
    var->val.str.ptr = str;
    var->val.str.len = len;
    return true;
}

bool xx_var_set_wstr(xx_var *var, const wchar_t *wstr) {
    if (!var) return false;
    xx_var_cleanup(var);
    if (!wstr) return false;

    wchar_t *dup = xx_str_wdup(wstr);
    if (!dup) {
        return false;
    }

    var->type = XX_VAR_TYPE_WSTRING;
    var->is_allocated = true;
    var->free_fn = NULL;
    var->val.wstr.ptr = dup;
    var->val.wstr.len = xx_str_wlen(dup);
    return true;
}

bool xx_var_set_wstr_take(xx_var *var, wchar_t *wstr, size_t len) {
    if (!var) return false;
    xx_var_cleanup(var);

    var->type = XX_VAR_TYPE_WSTRING;
    var->is_allocated = (wstr != NULL);
    var->free_fn = NULL;
    var->val.wstr.ptr = wstr;
    var->val.wstr.len = len;
    return true;
}

bool xx_var_set_bytes(xx_var *var, const void *data, size_t size) {
    if (!var) return false;
    xx_var_cleanup(var);
    if (size > 0 && !data) return false;

    uint8_t *buf = NULL;
    if (size > 0) {
        buf = (uint8_t*)xx_mem_alloc(size);
        if (!buf) {
            return false;
        }
        xx_mem_copy(buf, data, size);
    }

    var->type = XX_VAR_TYPE_BYTES;
    var->is_allocated = (buf != NULL);
    var->free_fn = NULL;
    var->val.bytes.data = buf;
    var->val.bytes.size = size;
    return true;
}

bool xx_var_set_bytes_take(xx_var *var, void *data, size_t size) {
    if (!var) return false;
    xx_var_cleanup(var);

    var->type = XX_VAR_TYPE_BYTES;
    var->is_allocated = (data != NULL);
    var->free_fn = NULL;
    var->val.bytes.data = (uint8_t*)data;
    var->val.bytes.size = size;
    return true;
}

/* ========================================================================= */
/* --- Setters (Non-Owning Views - Not Freed)                             --- */
/* ========================================================================= */

void xx_var_set_str_view(xx_var *var, const char *str, size_t len) {
    if (!var) return;
    xx_var_cleanup(var);

    var->type = XX_VAR_TYPE_STRING_VIEW;
    var->is_allocated = false;
    var->free_fn = NULL;
    var->val.str.ptr = (char*)str;
    var->val.str.len = len;
}

void xx_var_set_wstr_view(xx_var *var, const wchar_t *wstr, size_t len) {
    if (!var) return;
    xx_var_cleanup(var);

    var->type = XX_VAR_TYPE_WSTRING_VIEW;
    var->is_allocated = false;
    var->free_fn = NULL;
    var->val.wstr.ptr = (wchar_t*)wstr;
    var->val.wstr.len = len;
}

void xx_var_set_bytes_view(xx_var *var, const void *data, size_t size) {
    if (!var) return;
    xx_var_cleanup(var);

    var->type = XX_VAR_TYPE_BYTES_VIEW;
    var->is_allocated = false;
    var->free_fn = NULL;
    var->val.bytes.data = (uint8_t*)data;
    var->val.bytes.size = size;
}

void xx_var_set_ptr(xx_var *var, void *ptr, bool is_allocated, xx_var_free_fn free_fn) {
    if (!var) return;
    xx_var_cleanup(var);

    var->type = XX_VAR_TYPE_PTR;
    var->is_allocated = is_allocated;
    var->free_fn = free_fn;
    var->val.ptr = ptr;
}

/* ========================================================================= */
/* --- Getters                                                           --- */
/* ========================================================================= */

int64_t xx_var_get_i64(const xx_var *var) {
    if (!var) return 0;
    switch ((xx_var_type_t)var->type) {
        case XX_VAR_TYPE_INT8:   return (int64_t)var->val.i8;
        case XX_VAR_TYPE_INT16:  return (int64_t)var->val.i16;
        case XX_VAR_TYPE_INT32:  return (int64_t)var->val.i32;
        case XX_VAR_TYPE_INT64:  return var->val.i64;
        case XX_VAR_TYPE_UINT8:  return (int64_t)var->val.u8;
        case XX_VAR_TYPE_UINT16: return (int64_t)var->val.u16;
        case XX_VAR_TYPE_UINT32: return (int64_t)var->val.u32;
        case XX_VAR_TYPE_UINT64: return (int64_t)var->val.u64;
        case XX_VAR_TYPE_BOOL:   return var->val.b ? 1 : 0;
        case XX_VAR_TYPE_FLOAT:  return (int64_t)var->val.f;
        case XX_VAR_TYPE_DOUBLE: return (int64_t)var->val.d;
        default: return 0;
    }
}

uint64_t xx_var_get_u64(const xx_var *var) {
    if (!var) return 0;
    switch ((xx_var_type_t)var->type) {
        case XX_VAR_TYPE_UINT8:  return (uint64_t)var->val.u8;
        case XX_VAR_TYPE_UINT16: return (uint64_t)var->val.u16;
        case XX_VAR_TYPE_UINT32: return (uint64_t)var->val.u32;
        case XX_VAR_TYPE_UINT64: return var->val.u64;
        case XX_VAR_TYPE_INT8:   return (uint64_t)var->val.i8;
        case XX_VAR_TYPE_INT16:  return (uint64_t)var->val.i16;
        case XX_VAR_TYPE_INT32:  return (uint64_t)var->val.i32;
        case XX_VAR_TYPE_INT64:  return (uint64_t)var->val.i64;
        case XX_VAR_TYPE_BOOL:   return var->val.b ? 1 : 0;
        case XX_VAR_TYPE_FLOAT:  return (uint64_t)var->val.f;
        case XX_VAR_TYPE_DOUBLE: return (uint64_t)var->val.d;
        default: return 0;
    }
}

double xx_var_get_double(const xx_var *var) {
    if (!var) return 0.0;
    switch ((xx_var_type_t)var->type) {
        case XX_VAR_TYPE_DOUBLE: return var->val.d;
        case XX_VAR_TYPE_FLOAT:  return (double)var->val.f;
        case XX_VAR_TYPE_INT8:   return (double)var->val.i8;
        case XX_VAR_TYPE_INT16:  return (double)var->val.i16;
        case XX_VAR_TYPE_INT32:  return (double)var->val.i32;
        case XX_VAR_TYPE_INT64:  return (double)var->val.i64;
        case XX_VAR_TYPE_UINT8:  return (double)var->val.u8;
        case XX_VAR_TYPE_UINT16: return (double)var->val.u16;
        case XX_VAR_TYPE_UINT32: return (double)var->val.u32;
        case XX_VAR_TYPE_UINT64: return (double)var->val.u64;
        case XX_VAR_TYPE_BOOL:   return var->val.b ? 1.0 : 0.0;
        default: return 0.0;
    }
}

bool xx_var_get_bool(const xx_var *var) {
    if (!var) return false;
    switch ((xx_var_type_t)var->type) {
        case XX_VAR_TYPE_BOOL:   return var->val.b;
        case XX_VAR_TYPE_INT8:   return var->val.i8 != 0;
        case XX_VAR_TYPE_INT16:  return var->val.i16 != 0;
        case XX_VAR_TYPE_INT32:  return var->val.i32 != 0;
        case XX_VAR_TYPE_INT64:  return var->val.i64 != 0;
        case XX_VAR_TYPE_UINT8:  return var->val.u8 != 0;
        case XX_VAR_TYPE_UINT16: return var->val.u16 != 0;
        case XX_VAR_TYPE_UINT32: return var->val.u32 != 0;
        case XX_VAR_TYPE_UINT64: return var->val.u64 != 0;
        case XX_VAR_TYPE_FLOAT:  return var->val.f != 0.0f;
        case XX_VAR_TYPE_DOUBLE: return var->val.d != 0.0;
        case XX_VAR_TYPE_STRING:
        case XX_VAR_TYPE_STRING_VIEW: return var->val.str.ptr != NULL && var->val.str.ptr[0] != '\0';
        case XX_VAR_TYPE_WSTRING:
        case XX_VAR_TYPE_WSTRING_VIEW: return var->val.wstr.ptr != NULL && var->val.wstr.ptr[0] != L'\0';
        case XX_VAR_TYPE_PTR:    return var->val.ptr != NULL;
        default: return false;
    }
}

const char* xx_var_get_str(const xx_var *var) {
    if (!var) return NULL;
    if (var->type == XX_VAR_TYPE_STRING || var->type == XX_VAR_TYPE_STRING_VIEW) {
        return var->val.str.ptr;
    }
    return NULL;
}

const wchar_t* xx_var_get_wstr(const xx_var *var) {
    if (!var) return NULL;
    if (var->type == XX_VAR_TYPE_WSTRING || var->type == XX_VAR_TYPE_WSTRING_VIEW) {
        return var->val.wstr.ptr;
    }
    return NULL;
}

const void* xx_var_get_bytes(const xx_var *var, size_t *out_size) {
    if (!var) {
        if (out_size) *out_size = 0;
        return NULL;
    }
    if (var->type == XX_VAR_TYPE_BYTES || var->type == XX_VAR_TYPE_BYTES_VIEW) {
        if (out_size) *out_size = var->val.bytes.size;
        return var->val.bytes.data;
    }
    if (out_size) *out_size = 0;
    return NULL;
}

void* xx_var_get_ptr(const xx_var *var) {
    if (!var) return NULL;
    if (var->type == XX_VAR_TYPE_PTR) {
        return var->val.ptr;
    }
    return NULL;
}
