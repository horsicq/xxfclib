/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary grammar. Payloads are never executed.
 */
/* Primary: https://ftp.gnu.org/gnu/gdbm/gdbm-1.24.tar.gz */
#include "xxfclib/formats/gdbm_dump/xx_gdbm_dump.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_protocol_framing.h"

static int b64(uint8_t c)
{
    return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' ? 62 : c == '/' ? 63 : -1;
}
static bool datum(Abstractformat *f, pm_stream *s, memory_blob *b, uint64_t *at, const char *name)
{
    uint64_t start, n, len, pos = 0, chars;
    uint8_t *out = NULL;
    bool ok = false;
    if (!protocol_line(b, at, &start, &n, false) || n < 7 || !protocol_eq(b, start, 6, "#:len=") || !protocol_dec(b, start + 6, n - 6, &len) || len > 16777216)
        return false;
    out = (uint8_t *)xx_mem_alloc((size_t)(len ? len : 1));
    if (!out) return false;
    chars = ((len + 2) / 3) * 4;
    while (chars) {
        if (!protocol_line(b, at, &start, &n, false) || n != (chars > 76 ? 76 : chars) || n % 4) goto done;
        for (uint64_t i = 0; i < n; i += 4) {
            int a = b64(b->p[(size_t)(start + i)]), c = b64(b->p[(size_t)(start + i + 1)]), d = b64(b->p[(size_t)(start + i + 2)]),
                e = b64(b->p[(size_t)(start + i + 3)]);
            bool p2 = b->p[(size_t)(start + i + 2)] == '=', p3 = b->p[(size_t)(start + i + 3)] == '=';
            if (a < 0 || c < 0 || (!p2 && d < 0) || (!p3 && e < 0) || (p2 && !p3) || ((p2 || p3) && i + 4 != chars) || (p2 && (c & 15)) || (!p2 && p3 && (d & 3)) ||
                pos >= len)
                goto done;
            out[(size_t)pos++] = (uint8_t)((a << 2) | (c >> 4));
            if (!p2) {
                if (pos >= len) goto done;
                out[(size_t)pos++] = (uint8_t)((c << 4) | (d >> 2));
                if (!p3) {
                    if (pos >= len) goto done;
                    out[(size_t)pos++] = (uint8_t)((d << 6) | e);
                }
            }
        }
        chars -= n;
    }
    if (pos == len && protocol_mem(f, s, name, &out, len)) ok = true;
done:
    xx_mem_free(out);
    return ok;
}
static bool uid_header(memory_blob *b, uint64_t start, uint64_t n)
{
    uint64_t p = start, end = start + n, a, v;
    const char *keys[] = {"#:uid=", "gid=", "mode="};
    for (unsigned field = 0; field < 3; ++field) {
        size_t width = xx_rt_strlen(keys[field]);
        if (!record_span(p, width, end) || !protocol_eq(b, p, width, keys[field])) return false;
        p += width;
        a = p;
        while (p < end && b->p[(size_t)p] != ',') ++p;
        if (field == 2) {
            if (p != end || p - a != 3) return false;
            for (uint64_t i = a; i < p; ++i)
                if (b->p[(size_t)i] < '0' || b->p[(size_t)i] > '7') return false;
        } else {
            if (p == end || !protocol_dec(b, a, p - a, &v)) return false;
            ++p;
            const char *optional = field == 0 ? "user=" : "group=";
            width = xx_rt_strlen(optional);
            if (record_span(p, width, end) && protocol_eq(b, p, width, optional)) {
                p += width;
                a = p;
                while (p < end && b->p[(size_t)p] != ',') {
                    if (b->p[(size_t)p] < 33 || b->p[(size_t)p] > 126) return false;
                    ++p;
                }
                if (p == a || p == end) return false;
                ++p;
            }
        }
    }
    return p == end;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    uint64_t at = 0, start, n, header, count = 0, want;
    bool ok = false;
    if (!blob_load(f, &b, pd)) return false;
    BLOB_NEED(protocol_line(&b, &at, &start, &n, false) && n >= 28 && protocol_eq(&b, start, 28, "# GDBM dump file created by ") &&
              protocol_line(&b, &at, &start, &n, false) && protocol_eq(&b, start, n, "#:version=1.1") && protocol_line(&b, &at, &start, &n, false) && n > 7 &&
              protocol_eq(&b, start, 7, "#:file=") && protocol_line(&b, &at, &start, &n, false) && n > 6 && protocol_eq(&b, start, 6, "#:uid="));
    BLOB_NEED(uid_header(&b, start, n));
    BLOB_NEED(protocol_line(&b, &at, &start, &n, false) && protocol_eq(&b, start, n, "#:format=standard") && protocol_line(&b, &at, &start, &n, false) &&
              protocol_eq(&b, start, n, "# End of header"));
    header = at;
    BLOB_NEED(blob_add(f, s, &b, "dump-header", 0, header));
    while (at < b.n && record_span(at, 6, b.n) && protocol_eq(&b, at, 6, "#:len=")) {
        BLOB_NEED(++count <= 1024 && datum(f, s, &b, &at, "key") && datum(f, s, &b, &at, "value"));
    }
    start = at;
    BLOB_NEED(count && protocol_line(&b, &at, &header, &n, false) && n >= 9 && protocol_eq(&b, header, 8, "#:count=") && protocol_dec(&b, header + 8, n - 8, &want) &&
              want == count && protocol_line(&b, &at, &header, &n, false) && protocol_eq(&b, header, n, "# End of data") && at == b.n &&
              blob_add(f, s, &b, "dump-footer", start, at - start));
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_gdbm_dump_init(xx_gdbm_dump *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_GDBM_DUMP, "bin");
    }
}
xx_gdbm_dump *xx_gdbm_dump_create(xx_io_device *d, int64_t b)
{
    xx_gdbm_dump *r = (xx_gdbm_dump *)xx_mem_alloc(sizeof(*r));
    if (r) xx_gdbm_dump_init(r, d, b);
    return r;
}
void xx_gdbm_dump_destroy(xx_gdbm_dump *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_gdbm_dump_free(xx_gdbm_dump *r)
{
    if (r) {
        xx_gdbm_dump_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_gdbm_dump_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_gdbm_dump_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
