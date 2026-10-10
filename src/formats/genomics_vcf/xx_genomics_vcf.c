/* SPDX-License-Identifier: MIT
 * Independently implemented from https://samtools.github.io/hts-specs/VCFv4.3.pdf */
#include "xxfclib/formats/genomics_vcf/xx_genomics_vcf.h"
#include "../common/xx_scientific_text.h"
static bool vcf_key(memory_blob *b, scientific_text_token v)
{
    return v.n && v.n <= 255 && scientific_text_chars(b, v, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-", true);
}
static bool vcf_meta(memory_blob *b, scientific_text_token line)
{
    uint64_t i = 2, eq;
    bool quoted = false, escaped = false;
    unsigned brackets = 0;
    if (!scientific_text_prefix(b, line, "##") || line.n < 4) return false;
    while (i < line.n && b->p[(size_t)(line.at + i)] != '=') {
        ++i;
    }
    eq = i;
    if (eq == line.n || !vcf_key(b, scientific_text_slice(line, 2, eq - 2)) || ++i == line.n) return false;
    for (; i < line.n; ++i) {
        uint8_t ch = b->p[(size_t)(line.at + i)];
        if (escaped) {
            if (ch != '\\' && ch != '"') return false;
            escaped = false;
            continue;
        }
        if (quoted && ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') quoted = !quoted;
        else if (!quoted && ch == '<') {
            if (++brackets > 1) return false;
        } else if (!quoted && ch == '>') {
            if (!brackets) return false;
            --brackets;
        }
    }
    return !quoted && !escaped && !brackets;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    scientific_text_lines c = {0};
    scientific_text_token line, t[10];
    uint64_t count = 0, budget = 8388608;
    unsigned nt;
    bool ok = false, columns = false;
    BLOB_NEED(blob_load(f, &b, pd));
    c.b = &b;
    BLOB_NEED(scientific_text_line(&c, &line) && line.n == 20 && scientific_text_prefix(&b, line, "##fileformat=VCFv4.") && b.p[(size_t)line.at + 19] >= '1' &&
              b.p[(size_t)line.at + 19] <= '3');
    while (c.at < b.n) {
        uint64_t start = c.at;
        BLOB_NEED(scientific_text_line(&c, &line) && line.n);
        if (!columns) {
            if (scientific_text_prefix(&b, line, "##")) {
                BLOB_NEED(!scientific_text_prefix(&b, line, "##fileformat") && vcf_meta(&b, line));
                continue;
            }
            BLOB_NEED(scientific_text_eq(&b, line, "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO"));
            columns = true;
            BLOB_NEED(blob_add(f, s, &b, "header", 0, c.at));
            continue;
        }
        BLOB_NEED(++count <= 4094 && scientific_text_split(&b, line, t, 10, &nt, true) && nt == 8);
        {
            uint64_t pos;
            scientific_text_token alts[512];
            unsigned na, i;
            BLOB_NEED(scientific_text_ident(&b, t[0]) && scientific_text_uint(&b, t[1], &pos) && pos && pos <= 2147483647 &&
                      scientific_text_chars(&b, t[3], "ACGTNacgtn", true) &&
                      (scientific_text_eq(&b, t[5], ".") || (scientific_text_float(&b, t[5]) && b.p[(size_t)t[5].at] != '-')));
            BLOB_NEED(scientific_text_sep(&b, t[4], ',', alts, 512, &na));
            for (i = 0; i < na; ++i) {
                if (scientific_text_eq(&b, alts[i], ".") || scientific_text_eq(&b, alts[i], "*")) continue;
                if (b.p[(size_t)alts[i].at] == '<') {
                    BLOB_NEED(alts[i].n > 2 && b.p[(size_t)(alts[i].at + alts[i].n - 1)] == '>' &&
                              scientific_text_chars(&b, scientific_text_slice(alts[i], 1, alts[i].n - 2),
                                                    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_:.-", true));
                } else BLOB_NEED(scientific_text_chars(&b, alts[i], "ACGTNacgtn", true));
            }
            BLOB_NEED(scientific_text_ident(&b, t[2]));
        }
        if (!scientific_text_eq(&b, t[6], ".") && !scientific_text_eq(&b, t[6], "PASS")) {
            scientific_text_token filter[128];
            unsigned n, i;
            BLOB_NEED(scientific_text_sep(&b, t[6], ';', filter, 128, &n));
            for (i = 0; i < n; ++i) BLOB_NEED(vcf_key(&b, filter[i]) && !scientific_text_eq(&b, filter[i], "PASS"));
        }
        if (!scientific_text_eq(&b, t[7], ".")) {
            scientific_text_token attrs[128], keys[128];
            unsigned n, i, j;
            BLOB_NEED(scientific_text_sep(&b, t[7], ';', attrs, 128, &n));
            for (i = 0; i < n; ++i) {
                uint64_t at = 0, k;
                while (at < attrs[i].n && b.p[(size_t)(attrs[i].at + at)] != '=') ++at;
                keys[i] = scientific_text_slice(attrs[i], 0, at);
                BLOB_NEED(vcf_key(&b, keys[i]));
                for (j = 0; j < i; ++j) {
                    BLOB_NEED(budget);
                    --budget;
                    BLOB_NEED(keys[i].n != keys[j].n || xx_rt_memcmp(b.p + (size_t)keys[i].at, b.p + (size_t)keys[j].at, (size_t)keys[i].n));
                }
                if (at < attrs[i].n) {
                    BLOB_NEED(at + 1 < attrs[i].n);
                    for (k = at + 1; k < attrs[i].n; ++k) {
                        uint8_t ch = b.p[(size_t)(attrs[i].at + k)];
                        BLOB_NEED(ch > 32 && ch != '=' && ch != ';');
                        if (ch == '%') {
                            BLOB_NEED(k + 2 < attrs[i].n && scientific_text_chars(&b, scientific_text_slice(attrs[i], k + 1, 2), "0123456789ABCDEFabcdef", true));
                            k += 2;
                        }
                    }
                }
            }
        }
        BLOB_NEED(blob_add(f, s, &b, "variant", start, c.at - start));
    }
    BLOB_NEED(columns && count);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_genomics_vcf_init(xx_genomics_vcf *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_GENOMICS_VCF, "genomics_vcf");
    }
}
xx_genomics_vcf *xx_genomics_vcf_create(xx_io_device *d, int64_t b)
{
    xx_genomics_vcf *r = (xx_genomics_vcf *)xx_mem_alloc(sizeof(*r));
    if (r) xx_genomics_vcf_init(r, d, b);
    return r;
}
void xx_genomics_vcf_destroy(xx_genomics_vcf *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_genomics_vcf_free(xx_genomics_vcf *r)
{
    if (r) {
        xx_genomics_vcf_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_genomics_vcf_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_genomics_vcf_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
