/* SPDX-License-Identifier: MIT
 * Wire specification: https://numpy.org/doc/stable/reference/generated/numpy.lib.format.html */
#include "xxfclib/formats/numpy_npy/xx_numpy_npy.h"
#include "../common/xx_binary_cursor.h"

typedef struct np_text {
    const uint8_t *p;
    size_t at, n;
} np_text;
static void np_space(np_text *t)
{
    while (t->at < t->n && (t->p[t->at] == ' ' || t->p[t->at] == '\t' || t->p[t->at] == '\n' || t->p[t->at] == '\r')) ++t->at;
}
static bool np_char(np_text *t, uint8_t b)
{
    np_space(t);
    if (t->at >= t->n || t->p[t->at] != b) return false;
    ++t->at;
    return true;
}
static bool np_word(np_text *t, const char *p)
{
    size_t n = xx_rt_strlen(p);
    np_space(t);
    if (n > t->n - t->at || xx_rt_memcmp(t->p + t->at, p, n)) return false;
    t->at += n;
    return true;
}
static bool np_string(np_text *t, char *p, size_t cap)
{
    uint8_t q;
    size_t n = 0;
    np_space(t);
    if (t->at >= t->n || ((q = t->p[t->at++]) != '\'' && q != '"')) return false;
    while (t->at < t->n && t->p[t->at] != q) {
        uint8_t c = t->p[t->at++];
        if (c < 32 || c > 126 || c == '\\' || n + 1 >= cap) return false;
        p[n++] = (char)c;
    }
    if (t->at >= t->n) {
        return false;
    }
    ++t->at;
    p[n] = 0;
    return true;
}
static bool np_number(np_text *t, uint64_t *v)
{
    unsigned count = 0;
    *v = 0;
    np_space(t);
    while (t->at < t->n && t->p[t->at] >= '0' && t->p[t->at] <= '9') {
        unsigned c = t->p[t->at++] - '0';
        if (*v > ((uint64_t)INT64_MAX - c) / 10) return false;
        *v = *v * 10 + c;
        ++count;
    }
    return count != 0;
}
static bool np_dtype(const char *p, uint64_t *size)
{
    size_t at = 0;
    char kind;
    uint64_t n = 0;
    if (p[0] != '<' && p[0] != '>' && p[0] != '|' && p[0] != '=') {
        return false;
    }
    at = 1;
    kind = p[at++];
    if (!kind || !p[at]) return false;
    while (p[at] >= '0' && p[at] <= '9') {
        unsigned d = p[at++] - '0';
        if (n > (1048576U - d) / 10) return false;
        n = n * 10 + d;
    }
    if (p[at] || !n) return false;
    if (kind == 'b' || kind == '?') {
        if (n != 1) return false;
    } else if (kind == 'i' || kind == 'u') {
        if (n != 1 && n != 2 && n != 4 && n != 8) return false;
    } else if (kind == 'f') {
        if (n != 2 && n != 4 && n != 8 && n != 16) return false;
    } else if (kind == 'c') {
        if (n != 8 && n != 16 && n != 32) return false;
    } else if (kind == 'U') {
        if (!binary_mul(n, 4, &n)) return false;
    } else if (kind != 'S' && kind != 'V') return false;
    if (p[0] == '|' && kind != 'b' && kind != '?' && kind != 'S' && kind != 'V' && n != 1) return false;
    *size = n;
    return true;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[12], *header = NULL;
    uint32_t len, prefix;
    np_text t;
    unsigned keys = 0, dim = 0;
    uint64_t elements = 1, item = 0, bytes;
    bool result = false;
    if (!pm_read(f, 0, h, 10) || xx_rt_memcmp(h, "\x93NUMPY", 6) || h[7] || h[6] < 1 || h[6] > 3) return false;
    prefix = h[6] == 1 ? 10 : 12;
    if (prefix == 12 && !pm_read(f, 0, h, 12)) return false;
    len = prefix == 10 ? xx_data_get_u16(h + 8, 2, 0, false) : xx_data_get_u32(h + 8, 4, 0, false);
    if (!len || len > 65536 || !binary_range(prefix, len, (uint64_t)pm_available(f)) || (prefix + len) % 16) return false;
    header = (uint8_t *)xx_mem_alloc(len);
    if (!header || !pm_read(f, prefix, header, len) || header[len - 1] != '\n') goto done;
    t.p = header;
    t.at = 0;
    t.n = len;
    if (!np_char(&t, '{')) goto done;
    for (;;) {
        char key[32], dtype[64];
        unsigned bit;
        if (binary_stop(pd) || !np_string(&t, key, sizeof(key)) || !np_char(&t, ':')) goto done;
        if (!xx_rt_strcmp(key, "descr")) {
            bit = 1;
            if (!np_string(&t, dtype, sizeof(dtype)) || !np_dtype(dtype, &item)) goto done;
        } else if (!xx_rt_strcmp(key, "fortran_order")) {
            bit = 2;
            if (!np_word(&t, "False") && !np_word(&t, "True")) goto done;
        } else if (!xx_rt_strcmp(key, "shape")) {
            bit = 4;
            if (!np_char(&t, '(')) goto done;
            np_space(&t);
            if (t.at < t.n && t.p[t.at] == ')') ++t.at;
            else
                for (;;) {
                    uint64_t n;
                    if (++dim > 32 || !np_number(&t, &n) || !binary_mul(elements, n, &elements)) goto done;
                    if (!np_char(&t, ',')) {
                        if (dim == 1 || !np_char(&t, ')')) goto done;
                        break;
                    }
                    np_space(&t);
                    if (t.at < t.n && t.p[t.at] == ')') {
                        ++t.at;
                        break;
                    }
                }
        } else goto done;
        if (keys & bit) {
            goto done;
        }
        keys |= bit;
        np_space(&t);
        if (t.at < t.n && t.p[t.at] == '}') {
            ++t.at;
            break;
        }
        if (!np_char(&t, ',')) {
            goto done;
        }
        np_space(&t);
        if (t.at < t.n && t.p[t.at] == '}') {
            ++t.at;
            break;
        }
    }
    np_space(&t);
    if (t.at != t.n || keys != 7 || !binary_mul(elements, item, &bytes) || !binary_range(prefix + len, bytes, (uint64_t)pm_available(f))) goto done;
    if (!pm_add(f, s, "npy-header.txt", prefix, len) || !pm_add(f, s, "array-data.bin", prefix + len, (int64_t)bytes)) goto done;
    s->size = prefix + len + (int64_t)bytes;
    result = true;
done:
    if (header) xx_mem_free(header);
    return result;
}

void xx_numpy_npy_init(xx_numpy_npy *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_NUMPY_NPY, "numpy_npy");
    }
}
xx_numpy_npy *xx_numpy_npy_create(xx_io_device *d, int64_t b)
{
    xx_numpy_npy *r = (xx_numpy_npy *)xx_mem_alloc(sizeof(*r));
    if (r) xx_numpy_npy_init(r, d, b);
    return r;
}
void xx_numpy_npy_destroy(xx_numpy_npy *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_numpy_npy_free(xx_numpy_npy *r)
{
    if (r) {
        xx_numpy_npy_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_numpy_npy_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_numpy_npy_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
