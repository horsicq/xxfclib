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

#include "xxfclib/buf/xx_buf.h"

/* Everything here goes through the xx_rt_ layer rather than the C library, so
 * the buffer is usable from a build that links without a CRT. */

#define XX_BUF_INITIAL_CAPACITY 32U
#define XX_BUF_SIZE_MAX ((size_t)-1)

/* Marks the buffer failed and reports it, so callers can `return xx_buf_fail(b)`. */
static bool xx_buf_fail(xx_buf_t *buf) {
    buf->failed = true;
    return false;
}

void xx_buf_init(xx_buf_t *buf) {
    if (!buf) {
        return;
    }
    buf->data = NULL;
    buf->size = 0U;
    buf->capacity = 0U;
    buf->failed = false;
}

void xx_buf_free(xx_buf_t *buf) {
    if (!buf) {
        return;
    }
    if (buf->data) {
        xx_rt_free(buf->data);
    }
    xx_buf_init(buf);
}

bool xx_buf_reserve(xx_buf_t *buf, size_t capacity) {
    size_t needed;
    size_t new_capacity;
    char *grown;

    if (!buf) {
        return false;
    }
    if (buf->failed) {
        return false;
    }
    /* One byte is always held back for the terminator, so a request for the
     * whole address space cannot be satisfied. */
    if (capacity == XX_BUF_SIZE_MAX) {
        return xx_buf_fail(buf);
    }
    needed = capacity + 1U;
    if (needed <= buf->capacity) {
        return true;
    }

    new_capacity = buf->capacity ? buf->capacity : XX_BUF_INITIAL_CAPACITY;
    while (new_capacity < needed) {
        /* Doubling past half the address space wraps to zero and the loop
         * never ends, so take the exact size instead. */
        if (new_capacity > (XX_BUF_SIZE_MAX / 2U)) {
            new_capacity = needed;
            break;
        }
        new_capacity *= 2U;
    }

    grown = (char *)xx_rt_realloc(buf->data, new_capacity);
    if (!grown) {
        return xx_buf_fail(buf);
    }
    buf->data = grown;
    buf->capacity = new_capacity;
    return true;
}

void xx_buf_clear(xx_buf_t *buf) {
    if (!buf) {
        return;
    }
    buf->size = 0U;
    if (buf->data) {
        buf->data[0] = '\0';
    }
}

bool xx_buf_append(xx_buf_t *buf, const void *data, size_t size) {
    if (!buf) {
        return false;
    }
    if (buf->failed) {
        return false;
    }
    if (size == 0U) {
        return true;
    }
    if (!data) {
        return xx_buf_fail(buf);
    }
    if (size > XX_BUF_SIZE_MAX - buf->size) {
        return xx_buf_fail(buf);
    }
    if (!xx_buf_reserve(buf, buf->size + size)) {
        return false;
    }
    xx_rt_memcpy(buf->data + buf->size, data, size);
    buf->size += size;
    buf->data[buf->size] = '\0';
    return true;
}

bool xx_buf_append_str(xx_buf_t *buf, const char *str) {
    if (!buf) {
        return false;
    }
    if (!str) {
        /* Appending nothing is not a failure; it matches CDBuf and keeps
         * optional fields from needing a guard at every call site. */
        return !buf->failed;
    }
    return xx_buf_append(buf, str, xx_rt_strlen(str));
}

bool xx_buf_append_char(xx_buf_t *buf, char c) {
    if (!buf) {
        return false;
    }
    if (buf->failed) {
        return false;
    }
    if (!xx_buf_reserve(buf, buf->size + 1U)) {
        return false;
    }
    /* Stored unconditionally: a zero byte is content here, not a terminator. */
    buf->data[buf->size++] = c;
    buf->data[buf->size] = '\0';
    return true;
}

XX_RT_PRINTF_LIKE(2, 3) bool xx_buf_appendf(xx_buf_t *buf, const char *fmt, ...) {
    char stack[512];
    XX_RT_VA_LIST args;
    int count;

    if (!buf) {
        return false;
    }
    if (buf->failed) {
        return false;
    }
    if (!fmt) {
        return xx_buf_fail(buf);
    }

    XX_RT_VA_START(args, fmt);
    count = xx_rt_vsnprintf(stack, sizeof(stack), fmt, args);
    XX_RT_VA_END(args);

    if (count < 0) {
        return xx_buf_fail(buf);
    }
    if ((size_t)count < sizeof(stack)) {
        return xx_buf_append(buf, stack, (size_t)count);
    }

    /* Too long for the stack copy. xx_rt_vsnprintf returns the length the
     * output WOULD have had, so one heap pass of exactly that size finishes
     * the job. */
    {
        char *heap = (char *)xx_rt_malloc((size_t)count + 1U);
        bool result;

        if (!heap) {
            return xx_buf_fail(buf);
        }
        XX_RT_VA_START(args, fmt);
        (void)xx_rt_vsnprintf(heap, (size_t)count + 1U, fmt, args);
        XX_RT_VA_END(args);

        result = xx_buf_append(buf, heap, (size_t)count);
        xx_rt_free(heap);
        return result;
    }
}

bool xx_buf_ok(const xx_buf_t *buf) {
    return buf && !buf->failed;
}

char *xx_buf_detach(xx_buf_t *buf, size_t *size) {
    char *result;

    if (size) {
        *size = 0U;
    }
    if (!buf) {
        return NULL;
    }
    if (buf->failed) {
        /* The contents are incomplete. Handing them over would let a caller
         * treat a truncated result as a whole one. */
        xx_buf_free(buf);
        return NULL;
    }

    result = buf->data;
    if (!result) {
        /* An empty buffer never allocated. Callers own and free whatever they
         * get back, so synthesise the one-byte terminator rather than return
         * NULL and make every caller special-case it. */
        result = (char *)xx_rt_malloc(1U);
        if (!result) {
            return NULL;
        }
        result[0] = '\0';
    }
    if (size) {
        *size = buf->size;
    }
    xx_buf_init(buf);
    return result;
}
