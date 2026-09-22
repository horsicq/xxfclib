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

#include "xx_js_deps.h"

/* The reserve size is measured rather than guessed, and the measurement is why
 * it has to stay small: it is taken out of the same headroom it exists to
 * cover, so every byte of it is a byte the evaluation no longer has. An
 * oversized reserve turns evaluations that would have finished into ones that
 * report a failure. 64 KiB covers the allocations between the first failure
 * and the end of a wind-down while costing nothing that was needed. */
#define XX_JS_OOM_RESERVE 0x10000

static void *g_oom_reserve = NULL;
static bool g_oom_soft = false;
static bool g_oom_raised = false;

static void xx_js_default_oom_handler(void) {
    xx_rt_fprintf(xx_rt_stderr(), "xxfclib: out of memory\n");
    xx_rt_exit(3);
}

static xx_js_oom_handler_fn g_oom_handler = xx_js_default_oom_handler;

xx_js_oom_handler_fn xx_js_set_oom_handler(xx_js_oom_handler_fn handler) {
    xx_js_oom_handler_fn previous = g_oom_handler;
    g_oom_handler = handler ? handler : xx_js_default_oom_handler;
    return previous;
}

/* Every path that runs out of memory ends here. The handler is not expected to
 * return; if it does, there is nothing left to do but stop, because the caller
 * is about to dereference the NULL it cannot check for. */
static void xx_js_out_of_memory(void) {
    g_oom_handler();
    /* The handler returned when it should not have. */
    xx_js_default_oom_handler();
}

bool xx_js_begin_soft_oom(void) {
    if (g_oom_soft) {
        return true;
    }
    /* Deliberately the raw allocator: failing to take the reserve must not be
     * the thing that invokes the handler. */
    g_oom_reserve = xx_rt_malloc(XX_JS_OOM_RESERVE);
    if (!g_oom_reserve) {
        return false;
    }
    g_oom_soft = true;
    g_oom_raised = false;
    return true;
}

void xx_js_end_soft_oom(void) {
    xx_rt_free(g_oom_reserve);
    g_oom_reserve = NULL;
    g_oom_soft = false;
    g_oom_raised = false;
}

bool xx_js_oom_raised(void) {
    return g_oom_raised;
}

/* Raises the sticky flag and releases the reserve so the caller's retry has
 * somewhere to come from. Returns true when a retry is worth making; with no
 * soft policy in force, or once the reserve is spent, the caller goes to the
 * handler instead. */
static bool xx_js_out_of_memory_retry(void) {
    if (!g_oom_soft) {
        return false;
    }
    g_oom_raised = true;
    if (!g_oom_reserve) {
        return false;
    }
    xx_rt_free(g_oom_reserve);
    g_oom_reserve = NULL;
    return true;
}

void *xx_js_malloc(size_t size) {
    size_t request = size ? size : 1U;
    void *result = xx_rt_malloc(request);

    if (!result && xx_js_out_of_memory_retry()) {
        result = xx_rt_malloc(request);
    }
    if (!result) {
        xx_js_out_of_memory();
    }
    return result;
}

void *xx_js_calloc(size_t count, size_t size) {
    size_t request_count = count ? count : 1U;
    size_t request_size = size ? size : 1U;
    void *result = xx_rt_calloc(request_count, request_size);

    if (!result && xx_js_out_of_memory_retry()) {
        result = xx_rt_calloc(request_count, request_size);
    }
    if (!result) {
        xx_js_out_of_memory();
    }
    return result;
}

void *xx_js_realloc(void *ptr, size_t size) {
    size_t request = size ? size : 1U;
    void *result = xx_rt_realloc(ptr, request);

    if (!result && xx_js_out_of_memory_retry()) {
        result = xx_rt_realloc(ptr, request);
    }
    if (!result) {
        xx_js_out_of_memory();
    }
    return result;
}

void xx_js_free(void *ptr) {
    xx_rt_free(ptr);
}

char *xx_js_strndup(const char *text, size_t size) {
    char *result = (char *)xx_js_malloc(size + 1U);

    if (size) {
        xx_rt_memcpy(result, text, size);
    }
    result[size] = '\0';
    return result;
}

char *xx_js_strdup(const char *text) {
    if (!text) {
        return NULL;
    }
    return xx_js_strndup(text, xx_rt_strlen(text));
}

uint32_t xx_js_hash_str(const char *text, size_t size) {
    /* FNV-1a */
    uint32_t hash = 2166136261u;
    size_t index;

    for (index = 0U; index < size; ++index) {
        hash ^= (uint8_t)text[index];
        hash *= 16777619u;
    }
    return hash;
}
