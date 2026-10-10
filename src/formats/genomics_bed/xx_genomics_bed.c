/* SPDX-License-Identifier: MIT
 * Independently implemented from https://samtools.github.io/hts-specs/BEDv1.pdf */
#include "xxfclib/formats/genomics_bed/xx_genomics_bed.h"
#include "../common/xx_scientific_tables_traces.h"

static bool genomics_bed_csv(memory_blob *b, scientific_text_token t, uint64_t *position, uint64_t *value)
{
    uint64_t start = *position;
    if (start >= t.n) return false;
    while (*position < t.n && b->p[(size_t)(t.at + *position)] != ',') ++*position;
    if (!scientific_text_uint(b, scientific_text_slice(t, start, *position - start), value)) return false;
    if (*position < t.n) {
        ++*position;
    }
    return true;
}

static bool genomics_bed_parse_components(Abstractformat *f, pm_stream *s, memory_blob *b)
{
    scientific_text_lines c = {0};
    scientific_text_token line, t[12];
    unsigned nt, count = 0, fields = 0;
    bool body = false;
    c.b = b;
    if (!structure_ascii(b)) return false;
    while (c.at < b->n) {
        uint64_t start = c.at, a, z;
        unsigned i;
        if (!scientific_text_line(&c, &line)) return false;
        if (!line.n || b->p[(size_t)line.at] == '#') {
            if (!blob_add(f, s, b, "metadata", start, c.at - start)) return false;
            continue;
        }
        if (scientific_text_prefix(b, line, "track")) {
            if (body || !scientific_table_track(b, line, false) || !blob_add(f, s, b, "track", start, c.at - start)) return false;
            continue;
        }
        if (scientific_text_prefix(b, line, "browser ")) {
            if (body || !scientific_text_split(b, line, t, 12, &nt, false) || nt != 3 || !scientific_text_eq(b, t[1], "position") || !scientific_text_ident(b, t[2]) ||
                !blob_add(f, s, b, "browser", start, c.at - start)) {
                return false;
            }
            continue;
        }
        if (!scientific_text_split(b, line, t, 12, &nt, true) || nt < 3 || ++count > 4094 || !scientific_table_identifier(b, t[0]) ||
            !scientific_text_uint(b, t[1], &a) || !scientific_text_uint(b, t[2], &z) || a > z || z > UINT32_MAX)
            return false;
        if (fields && nt != fields) {
            return false;
        }
        fields = nt;
        body = true;
        if (nt >= 4 && !scientific_text_ident(b, t[3])) return false;
        if (nt >= 5) {
            uint64_t score;
            if (!scientific_text_uint(b, t[4], &score) || score > 1000) return false;
        }
        if (nt >= 6 && (t[5].n != 1 || !scientific_text_chars(b, t[5], "+-.", true))) return false;
        if (nt >= 7) {
            uint64_t x;
            if (!scientific_text_uint(b, t[6], &x) || x < a || x > z) return false;
            if (nt >= 8) {
                uint64_t y;
                if (!scientific_text_uint(b, t[7], &y) || y < x || y > z) return false;
            }
        }
        if (nt >= 9 && !scientific_text_eq(b, t[8], "0")) {
            uint64_t position = 0, x;
            for (i = 0; i < 3; ++i)
                if (!genomics_bed_csv(b, t[8], &position, &x) || x > 255) return false;
            if (position != t[8].n || b->p[(size_t)(t[8].at + t[8].n - 1)] == ',') return false;
        }
        if (nt == 10 || nt == 11) return false;
        if (nt == 12) {
            uint64_t n, p = 0, q = 0, previous = 0;
            if (!scientific_text_uint(b, t[9], &n) || !n || n > 4096) return false;
            for (i = 0; i < n; ++i) {
                uint64_t length, offset;
                if (!genomics_bed_csv(b, t[10], &p, &length) || !genomics_bed_csv(b, t[11], &q, &offset) || !length || (!i && offset) || offset < previous ||
                    offset > z - a || length > z - a - offset)
                    return false;
                previous = offset + length;
            }
            if (previous != z - a || p != t[10].n || q != t[11].n) return false;
        }
        if (!blob_add(f, s, b, "interval", start, c.at - start)) return false;
    }
    return count != 0;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    ok = (f->file_type == XX_FILE_TYPE_GENOMICS_BED && genomics_bed_parse_components(f, s, &b));
    if (ok) s->size = (int64_t)b.n;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_genomics_bed_init(xx_genomics_bed *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_GENOMICS_BED, "genomics_bed");
    }
}
xx_genomics_bed *xx_genomics_bed_create(xx_io_device *d, int64_t b)
{
    xx_genomics_bed *r = (xx_genomics_bed *)xx_mem_alloc(sizeof(*r));
    if (r) xx_genomics_bed_init(r, d, b);
    return r;
}
void xx_genomics_bed_destroy(xx_genomics_bed *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_genomics_bed_free(xx_genomics_bed *r)
{
    if (r) {
        xx_genomics_bed_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_genomics_bed_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_genomics_bed_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
