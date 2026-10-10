/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.ensembl.org/info/website/upload/gff.html */
#include "xxfclib/formats/genomics_gtf/xx_genomics_gtf.h"
#include "../common/xx_scientific_tables_traces.h"

static bool genomics_gtf_attributes(memory_blob *b, scientific_text_token t, bool gene, uint64_t *budget)
{
    uint64_t at = 0;
    scientific_text_token keys[128];
    unsigned count = 0;
    bool gid = false, tid = false;
    while (at < t.n) {
        uint64_t start;
        scientific_text_token key, value;
        while (at < t.n && (b->p[(size_t)(t.at + at)] == ' ' || b->p[(size_t)(t.at + at)] == '\t')) ++at;
        if (at == t.n) {
            break;
        }
        start = at;
        while (at < t.n && b->p[(size_t)(t.at + at)] != ' ' && b->p[(size_t)(t.at + at)] != '\t') ++at;
        key = scientific_text_slice(t, start, at - start);
        if (count == 128 || key.n > 64 || !scientific_text_chars(b, key, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_", true) ||
            !scientific_table_unique(b, key, keys, count, budget))
            return false;
        keys[count++] = key;
        while (at < t.n && (b->p[(size_t)(t.at + at)] == ' ' || b->p[(size_t)(t.at + at)] == '\t')) ++at;
        if (at == t.n || b->p[(size_t)(t.at + at++)] != '"') {
            return false;
        }
        start = at;
        while (at < t.n && b->p[(size_t)(t.at + at)] != '"') {
            if (b->p[(size_t)(t.at + at)] == '\\') {
                ++at;
                if (at == t.n || (b->p[(size_t)(t.at + at)] != '"' && b->p[(size_t)(t.at + at)] != '\\')) return false;
            }
            if (!scientific_table_charge(b, budget, 1)) {
                return false;
            }
            ++at;
        }
        if (at == t.n || at - start > 4096) {
            return false;
        }
        value = scientific_text_slice(t, start, at - start);
        ++at;
        if (at == t.n || b->p[(size_t)(t.at + at++)] != ';') return false;
        if (at < t.n && b->p[(size_t)(t.at + at)] != ' ' && b->p[(size_t)(t.at + at)] != '\t') return false;
        if (scientific_text_eq(b, key, "gene_id")) {
            if (!value.n) return false;
            gid = true;
        }
        if (scientific_text_eq(b, key, "transcript_id")) {
            if (!gene && !value.n) return false;
            tid = true;
        }
    }
    return gid && tid;
}

static bool genomics_gtf_parse_components(Abstractformat *f, pm_stream *s, memory_blob *b)
{
    scientific_text_lines c = {0};
    scientific_text_token line, t[9];
    unsigned nt, count = 0;
    uint64_t budget = 16777216;
    c.b = b;
    if (!structure_ascii(b)) return false;
    while (c.at < b->n) {
        uint64_t start = c.at, a, z;
        if (!scientific_text_line(&c, &line)) return false;
        if (!line.n || b->p[(size_t)line.at] == '#') {
            if (scientific_text_prefix(b, line, "##gff-version 3")) return false;
            if (!blob_add(f, s, b, "metadata", start, c.at - start)) return false;
            continue;
        }
        if (!scientific_text_split(b, line, t, 9, &nt, true) || nt != 9 || ++count > 4094 || !scientific_table_identifier(b, t[0]) || !scientific_text_ident(b, t[1]) ||
            !scientific_text_ident(b, t[2]) || !scientific_text_uint(b, t[3], &a) || !scientific_text_uint(b, t[4], &z) || !a || a > z || z > UINT32_MAX ||
            (!scientific_text_eq(b, t[5], ".") && !scientific_text_float(b, t[5])) || t[6].n != 1 || !scientific_text_chars(b, t[6], "+-.", true) || t[7].n != 1 ||
            !scientific_text_chars(b, t[7], "012.", true))
            return false;
        if (scientific_text_eq(b, t[2], "CDS") && scientific_text_eq(b, t[7], ".")) return false;
        if (!genomics_gtf_attributes(b, t[8], scientific_text_eq(b, t[2], "gene"), &budget) || !blob_add(f, s, b, "annotation-feature", start, c.at - start))
            return false;
    }
    return count != 0;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    ok = (f->file_type == XX_FILE_TYPE_GENOMICS_GTF && genomics_gtf_parse_components(f, s, &b));
    if (ok) s->size = (int64_t)b.n;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_genomics_gtf_init(xx_genomics_gtf *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_GENOMICS_GTF, "genomics_gtf");
    }
}
xx_genomics_gtf *xx_genomics_gtf_create(xx_io_device *d, int64_t b)
{
    xx_genomics_gtf *r = (xx_genomics_gtf *)xx_mem_alloc(sizeof(*r));
    if (r) xx_genomics_gtf_init(r, d, b);
    return r;
}
void xx_genomics_gtf_destroy(xx_genomics_gtf *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_genomics_gtf_free(xx_genomics_gtf *r)
{
    if (r) {
        xx_genomics_gtf_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_genomics_gtf_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_genomics_gtf_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
