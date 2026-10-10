/* SPDX-License-Identifier: MIT
 * Independently implemented from https://raw.githubusercontent.com/The-Sequence-Ontology/Specifications/master/gff3.md */
#include "xxfclib/formats/genomics_gff3/xx_genomics_gff3.h"
#include "../common/xx_scientific_text.h"
static bool gff_escape(memory_blob *b, scientific_text_token v, bool commas)
{
    uint64_t i;
    for (i = 0; i < v.n; ++i) {
        uint8_t ch = b->p[(size_t)(v.at + i)];
        if (ch == '%') {
            if (i + 2 >= v.n || !scientific_text_chars(b, scientific_text_slice(v, i + 1, 2), "0123456789ABCDEFabcdef", true)) return false;
            i += 2;
        } else if (ch <= 32 || ch == '=' || ch == ';' || (!commas && ch == ',')) return false;
    }
    return v.n != 0;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    scientific_text_lines c = {0};
    scientific_text_token line, t[10], regions[256];
    uint64_t low[256], high[256], head = 0, count = 0, budget = 8388608;
    unsigned nregion = 0, nt;
    bool ok = false, body = false;
    BLOB_NEED(blob_load(f, &b, pd));
    c.b = &b;
    BLOB_NEED(scientific_text_line(&c, &line) && scientific_text_prefix(&b, line, "##gff-version 3"));
    {
        uint64_t i = 15;
        unsigned pieces = 0, digits = 0;
        while (i < line.n) {
            uint8_t ch = b.p[(size_t)(line.at + i++)];
            if (ch == '.') {
                BLOB_NEED((!pieces || digits) && ++pieces <= 2);
                digits = 0;
            } else {
                BLOB_NEED(pieces && ch >= '0' && ch <= '9' && ++digits <= 3);
            }
        }
        BLOB_NEED(!pieces || digits);
    }
    while (c.at < b.n) {
        uint64_t start = c.at;
        unsigned i;
        BLOB_NEED(scientific_text_line(&c, &line));
        if (!line.n) continue;
        if (b.p[(size_t)line.at] == '#') {
            BLOB_NEED(!scientific_text_prefix(&b, line, "##FASTA") && !scientific_text_prefix(&b, line, "##gff-version"));
            if (scientific_text_prefix(&b, line, "##sequence-region ")) {
                BLOB_NEED(!body && nregion < 256 && scientific_text_split(&b, line, t, 10, &nt, false) && nt == 4 && gff_escape(&b, t[1], false) &&
                          scientific_text_unique(&b, t[1], regions, nregion, &budget) && scientific_text_uint(&b, t[2], &low[nregion]) &&
                          scientific_text_uint(&b, t[3], &high[nregion]) && low[nregion] && low[nregion] <= high[nregion] && high[nregion] <= 2147483647);
                regions[nregion++] = t[1];
            }
            continue;
        }
        BLOB_NEED(scientific_text_split(&b, line, t, 10, &nt, true) && nt == 9 && ++count <= 4094);
        {
            uint64_t a, z;
            BLOB_NEED(gff_escape(&b, t[0], false) && gff_escape(&b, t[1], false) && gff_escape(&b, t[2], false) && scientific_text_uint(&b, t[3], &a) &&
                      scientific_text_uint(&b, t[4], &z) && a && a <= z && z <= 2147483647 && (scientific_text_eq(&b, t[5], ".") || scientific_text_float(&b, t[5])) &&
                      t[6].n == 1 && scientific_text_chars(&b, t[6], "+-.?", true) && t[7].n == 1 && scientific_text_chars(&b, t[7], "012.", true) &&
                      (scientific_text_eq(&b, t[2], "CDS") ? !scientific_text_eq(&b, t[7], ".") : scientific_text_eq(&b, t[7], ".")));
            for (i = 0; i < nregion; ++i)
                if (t[0].n == regions[i].n && !xx_rt_memcmp(b.p + (size_t)t[0].at, b.p + (size_t)regions[i].at, (size_t)t[0].n)) BLOB_NEED(a >= low[i] && z <= high[i]);
        }
        if (!scientific_text_eq(&b, t[8], ".")) {
            scientific_text_token attrs[128], keys[128];
            unsigned na, j, k;
            BLOB_NEED(scientific_text_sep(&b, t[8], ';', attrs, 128, &na));
            for (j = 0; j < na; ++j) {
                uint64_t at = 0;
                while (at < attrs[j].n && b.p[(size_t)(attrs[j].at + at)] != '=') ++at;
                BLOB_NEED(at && at < attrs[j].n && at <= 255);
                keys[j] = scientific_text_slice(attrs[j], 0, at);
                BLOB_NEED(scientific_text_chars(&b, keys[j], "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-", true) &&
                          gff_escape(&b, scientific_text_slice(attrs[j], at + 1, attrs[j].n - at - 1), true));
                for (k = 0; k < j; ++k) {
                    BLOB_NEED(budget);
                    --budget;
                    if (keys[j].n == keys[k].n) {
                        BLOB_NEED(budget >= keys[j].n);
                        budget -= keys[j].n;
                        BLOB_NEED(xx_rt_memcmp(b.p + (size_t)keys[j].at, b.p + (size_t)keys[k].at, (size_t)keys[j].n));
                    }
                }
            }
        }
        if (!body) {
            head = start;
            BLOB_NEED(blob_add(f, s, &b, "header", 0, head));
            body = true;
        }
        BLOB_NEED(blob_add(f, s, &b, "feature", start, c.at - start));
    }
    BLOB_NEED(count);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_genomics_gff3_init(xx_genomics_gff3 *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_GENOMICS_GFF3, "genomics_gff3");
    }
}
xx_genomics_gff3 *xx_genomics_gff3_create(xx_io_device *d, int64_t b)
{
    xx_genomics_gff3 *r = (xx_genomics_gff3 *)xx_mem_alloc(sizeof(*r));
    if (r) xx_genomics_gff3_init(r, d, b);
    return r;
}
void xx_genomics_gff3_destroy(xx_genomics_gff3 *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_genomics_gff3_free(xx_genomics_gff3 *r)
{
    if (r) {
        xx_genomics_gff3_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_genomics_gff3_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_genomics_gff3_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
