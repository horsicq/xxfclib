/* SPDX-License-Identifier: MIT
 * Independently implemented from https://genome.ucsc.edu/goldenPath/help/wiggle.html */
#include "xxfclib/formats/genomics_wiggle/xx_genomics_wiggle.h"
#include "../common/xx_scientific_tables_traces.h"

static bool genomics_wiggle_parse_components(Abstractformat *f, pm_stream *s, memory_blob *b) {
    scientific_text_lines c = {0};
    scientific_text_token line, t[8];
    unsigned nt, count = 0, blocks = 0;
    uint64_t block = 0, position = 0, step = 0, span = 1, rows = 0;
    bool fixed = false, active = false, track_pending = false;
    c.b = b;
    if (!structure_ascii(b))
        return false;
    while (c.at < b->n) {
        uint64_t start = c.at;
        if (!scientific_text_line(&c, &line)) {
            return false;
        }
        line = scientific_text_trim(b, line);
        if (!line.n || b->p[(size_t)line.at] == '#')
            continue;
        if (scientific_text_prefix(b, line, "track")) {
            if (track_pending || (active && !rows) || !scientific_table_track(b, line, true))
                return false;
            track_pending = true;
            continue;
        }
        if (scientific_text_prefix(b, line, "fixedStep") || scientific_text_prefix(b, line, "variableStep")) {
            unsigned seen = 0, i;
            scientific_text_token chrom = {0};
            if (active && (!rows || !blob_add(f, s, b, "wiggle-block", block, start - block)))
                return false;
            if (++blocks > 4094 || !scientific_text_split(b, line, t, 8, &nt, false) || nt < 2)
                return false;
            fixed = scientific_text_eq(b, t[0], "fixedStep");
            if (!fixed && !scientific_text_eq(b, t[0], "variableStep"))
                return false;
            span = 1;
            position = 0;
            step = 0;
            for (i = 1; i < nt; ++i) {
                uint64_t a = 0;
                scientific_text_token key, value;
                unsigned bit;
                while (a < t[i].n && b->p[(size_t)(t[i].at + a)] != '=')
                    ++a;
                if (!a || a == t[i].n) {
                    return false;
                }
                key = scientific_text_slice(t[i], 0, a);
                value = scientific_text_slice(t[i], a + 1, t[i].n - a - 1);
                if (scientific_text_eq(b, key, "chrom")) {
                    bit = 1;
                    chrom = value;
                    if (!scientific_table_identifier(b, value))
                        return false;
                } else if (scientific_text_eq(b, key, "start")) {
                    bit = 2;
                    if (!fixed || !scientific_text_uint(b, value, &position) || !position)
                        return false;
                } else if (scientific_text_eq(b, key, "step")) {
                    bit = 4;
                    if (!fixed || !scientific_text_uint(b, value, &step) || !step)
                        return false;
                } else if (scientific_text_eq(b, key, "span")) {
                    bit = 8;
                    if (!scientific_text_uint(b, value, &span) || !span)
                        return false;
                } else
                    return false;
                if (seen & bit) {
                    return false;
                }
                seen |= bit;
            }
            if (!chrom.n || (fixed && (seen & 7) != 7) || position > UINT32_MAX || step > UINT32_MAX ||
                span > UINT32_MAX)
                return false;
            if (!active && start && !blob_add(f, s, b, "metadata", 0, start))
                return false;
            active = true;
            track_pending = false;
            block = start;
            rows = 0;
            continue;
        }
        if (!active || track_pending || !scientific_text_split(b, line, t, 8, &nt, false) || nt != (fixed ? 1U : 2U) ||
            ++count > 262144 || !scientific_text_float(b, t[nt - 1]))
            return false;
        if (!fixed) {
            uint64_t next;
            if (!scientific_text_uint(b, t[0], &next) || !next || (rows && next <= position))
                return false;
            position = next;
        }
        if (position > UINT32_MAX || span - 1 > UINT32_MAX - position)
            return false;
        if (fixed)
            position += step;
        ++rows;
    }
    return active && !track_pending && rows && blob_add(f, s, b, "wiggle-block", block, b->n - block);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd) {
    memory_blob b = {0};
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    ok = (f->file_type == XX_FILE_TYPE_GENOMICS_WIGGLE && genomics_wiggle_parse_components(f, s, &b));
    if (ok)
        s->size = (int64_t)b.n;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_genomics_wiggle_init(xx_genomics_wiggle *r, xx_io_device *d, int64_t b) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_GENOMICS_WIGGLE, "genomics_wiggle");
    }
}
xx_genomics_wiggle *xx_genomics_wiggle_create(xx_io_device *d, int64_t b) {
    xx_genomics_wiggle *r = (xx_genomics_wiggle *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_genomics_wiggle_init(r, d, b);
    return r;
}
void xx_genomics_wiggle_destroy(xx_genomics_wiggle *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_genomics_wiggle_free(xx_genomics_wiggle *r) {
    if (r) {
        xx_genomics_wiggle_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_genomics_wiggle_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_genomics_wiggle_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
