/* SPDX-License-Identifier: MIT
 * Independently implemented from https://biopython.org/docs/latest/api/Bio.SeqIO.InsdcIO.html */
#include "xxfclib/formats/genomics_embl/xx_genomics_embl.h"
#include "../common/xx_sequence_alignment.h"
static bool embl_header(memory_blob *b, scientific_text_token line, bool *accession)
{
    static const char *const keys[] = {"XX", "AC", "DE", "DT", "KW", "OS", "OC", "OG", "OX", "RN", "RP", "RC",
                                       "RX", "RA", "RT", "RL", "DR", "CC", "AH", "AS", "CO", "FH", "FT"};
    unsigned i;
    scientific_text_token key;
    if (line.n < 2) return false;
    key = scientific_text_slice(line, 0, 2);
    if (line.n > 2 && b->p[(size_t)(line.at + 2)] != ' ') return false;
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        if (scientific_text_eq(b, key, keys[i])) {
            if (i == 1) {
                if (line.n <= 5) return false;
                *accession = true;
            }
            return true;
        }
    }
    return false;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    scientific_text_lines c = {0};
    scientific_text_token line, t[64], *names = NULL;
    sequence_sequence q = {0};
    unsigned nt, count = 0;
    uint64_t total = 0, budget = 10000000;
    bool ok = false;
    q.decoded = &total;
    BLOB_NEED(blob_load(f, &b, pd) && structure_ascii(&b));
    c.b = &b;
    names = (scientific_text_token *)xx_mem_alloc(1024 * sizeof(*names));
    BLOB_NEED(names);
    while (c.at < b.n) {
        uint64_t start = c.at, length, declared, counts[5], observed[5] = {0};
        bool accession = false, sq = false;
        unsigned i;
        scientific_text_token id;
        BLOB_NEED(scientific_text_line(&c, &line));
        if (!line.n) {
            BLOB_NEED(count && sequence_terminated(&c));
            break;
        }
        BLOB_NEED(scientific_text_prefix(&b, line, "ID   ") && scientific_text_split(&b, line, t, 64, &nt, false) && nt >= 4 &&
                  scientific_text_eq(&b, t[nt - 1], "BP.") && scientific_text_uint(&b, t[nt - 2], &length) && length && length <= SEQUENCE_MAX_SEQUENCE && t[1].n > 1 &&
                  b.p[(size_t)(t[1].at + t[1].n - 1)] == ';');
        id = scientific_text_slice(t[1], 0, t[1].n - 1);
        BLOB_NEED(scientific_text_ident(&b, id) && count < 1024 && scientific_text_unique(&b, id, names, count, &budget));
        names[count++] = id;
        while (c.at < b.n) {
            BLOB_NEED(scientific_text_line(&c, &line));
            if (scientific_text_prefix(&b, line, "SQ   ")) {
                sq = true;
                break;
            }
            BLOB_NEED(embl_header(&b, line, &accession));
        }
        BLOB_NEED(sq && accession && scientific_text_split(&b, line, t, 64, &nt, false) && nt == 14 && scientific_text_eq(&b, t[0], "SQ") &&
                  scientific_text_eq(&b, t[1], "Sequence") && scientific_text_uint(&b, t[2], &declared) && declared == length && scientific_text_eq(&b, t[3], "BP;") &&
                  scientific_text_eq(&b, t[5], "A;") && scientific_text_eq(&b, t[7], "C;") && scientific_text_eq(&b, t[9], "G;") && scientific_text_eq(&b, t[11], "T;") &&
                  scientific_text_eq(&b, t[13], "other;"));
        for (i = 0; i < 5; ++i) {
            BLOB_NEED(scientific_text_uint(&b, t[4 + 2 * i], &counts[i]) && counts[i] <= length);
        }
        BLOB_NEED(blob_add(f, s, &b, "embl-header", start, c.at - start));
        while (c.at < b.n) {
            uint64_t position;
            BLOB_NEED(scientific_text_line(&c, &line));
            if (scientific_text_eq(&b, line, "//")) break;
            BLOB_NEED(scientific_text_split(&b, line, t, 64, &nt, false) && nt >= 2 && nt <= 7 && scientific_text_uint(&b, t[nt - 1], &position));
            for (i = 0; i < nt - 1; ++i) {
                uint64_t j;
                BLOB_NEED(t[i].n <= 10 && (i == nt - 2 || t[i].n == 10) && sequence_append(&b, &q, t[i], false, true, true));
                for (j = 0; j < t[i].n; ++j) {
                    unsigned ch = sequence_upper(b.p[(size_t)(t[i].at + j)]), k = ch == 'A' ? 0 : ch == 'C' ? 1 : ch == 'G' ? 2 : ch == 'T' ? 3 : 4;
                    ++observed[k];
                }
            }
            BLOB_NEED(q.n == position && q.n <= length);
        }
        BLOB_NEED(scientific_text_eq(&b, line, "//") && q.n == length);
        for (i = 0; i < 5; ++i) BLOB_NEED(counts[i] == observed[i]);
        BLOB_NEED(sequence_export(f, s, &q, "nucleotides", &total));
    }
    BLOB_NEED(count && c.at == b.n);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(names);
    xx_mem_free(q.p);
    xx_mem_free(b.p);
    return ok;
}

void xx_genomics_embl_init(xx_genomics_embl *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_GENOMICS_EMBL, "genomics_embl");
    }
}
xx_genomics_embl *xx_genomics_embl_create(xx_io_device *d, int64_t b)
{
    xx_genomics_embl *r = (xx_genomics_embl *)xx_mem_alloc(sizeof(*r));
    if (r) xx_genomics_embl_init(r, d, b);
    return r;
}
void xx_genomics_embl_destroy(xx_genomics_embl *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_genomics_embl_free(xx_genomics_embl *r)
{
    if (r) {
        xx_genomics_embl_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_genomics_embl_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_genomics_embl_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
