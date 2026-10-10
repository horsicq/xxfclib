/* SPDX-License-Identifier: MIT. Bounded sequence and alignment primitives. */
#ifndef XX_SEQUENCE_ALIGNMENT_H
#define XX_SEQUENCE_ALIGNMENT_H
#include "xx_scientific_structure.h"
#include "xxfclib/algo/crc/xx_crc.h"
#define SEQUENCE_MAX_SEQUENCE 1048576U
#define SEQUENCE_MAX_DECODED 33554432U
typedef struct sequence_sequence {
    uint8_t *p;
    size_t n, cap;
    scientific_text_token id;
    uint64_t real;
    uint64_t *decoded;
} sequence_sequence;
static XXFC_MAYBE_UNUSED sequence_sequence *sequence_sequences(uint64_t *decoded)
{
    unsigned i;
    sequence_sequence *q = (sequence_sequence *)xx_mem_alloc(1024 * sizeof(*q));
    if (q) {
        xx_mem_zero(q, 1024 * sizeof(*q));
        for (i = 0; i < 1024; ++i) q[i].decoded = decoded;
    }
    return q;
}
static unsigned sequence_upper(unsigned ch)
{
    return ch >= 'a' && ch <= 'z' ? ch - 32 : ch;
}
static bool sequence_residue(unsigned ch, bool aligned, bool dna)
{
    unsigned u = sequence_upper(ch);
    const char *set = dna ? "ACGTURYSWKMBDHVN" : "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    size_t i;
    if (aligned && (ch == '-' || ch == '.')) return true;
    for (i = 0; set[i]; ++i) {
        if (u == (unsigned char)set[i]) return true;
    }
    return false;
}
static bool sequence_push(sequence_sequence *q, uint8_t ch, xx_pd_struct *pd)
{
    if (q->n >= SEQUENCE_MAX_SEQUENCE || (q->decoded && *q->decoded >= SEQUENCE_MAX_DECODED) || (!(q->n & 4095U) && binary_stop(pd))) return false;
    if (q->n == q->cap) {
        size_t cap = q->cap ? q->cap * 2 : 256;
        uint8_t *next = (uint8_t *)xx_mem_realloc(q->p, cap);
        if (!next) return false;
        q->p = next;
        q->cap = cap;
    }
    q->p[q->n++] = ch;
    if (q->decoded) ++*q->decoded;
    if (ch != '-' && ch != '.') ++q->real;
    return true;
}
static XXFC_MAYBE_UNUSED bool sequence_append(memory_blob *b, sequence_sequence *q, scientific_text_token v, bool aligned, bool dna, bool upper)
{
    uint64_t i;
    if (!v.n) return false;
    for (i = 0; i < v.n; ++i) {
        unsigned ch = b->p[(size_t)(v.at + i)];
        if (!sequence_residue(ch, aligned, dna) || !sequence_push(q, (uint8_t)(upper ? sequence_upper(ch) : ch), b->pd)) return false;
    }
    return true;
}
static bool sequence_export(Abstractformat *f, pm_stream *s, sequence_sequence *q, const char *label, uint64_t *total)
{
    pm_member *m;
    if (!q->n || (q->decoded && q->decoded != total) || (!q->decoded && q->n > SEQUENCE_MAX_DECODED - *total) || s->count >= 4096 || !pm_add(f, s, label, 0, 0))
        return false;
    m = &s->items[s->count - 1];
    m->memory = q->p;
    m->size = m->packed_size = (int64_t)q->n;
    if (!q->decoded) *total += q->n;
    q->p = NULL;
    q->n = q->cap = 0;
    q->real = 0;
    return true;
}
static XXFC_MAYBE_UNUSED bool sequence_find(memory_blob *b, scientific_text_token id, sequence_sequence *q, unsigned n, unsigned *out, uint64_t *budget)
{
    unsigned i;
    if (!scientific_text_ident(b, id)) return false;
    for (i = 0; i < n; ++i) {
        uint64_t j;
        if (!*budget) return false;
        --*budget;
        if (q[i].id.n != id.n) continue;
        for (j = 0; j < id.n; ++j) {
            if (!*budget) return false;
            --*budget;
            if (b->p[(size_t)(id.at + j)] != b->p[(size_t)(q[i].id.at + j)]) break;
        }
        if (j == id.n) {
            *out = i;
            return true;
        }
    }
    *out = n;
    return true;
}
static XXFC_MAYBE_UNUSED bool sequence_nonblank(scientific_text_lines *c, scientific_text_token *line)
{
    while (c->at < c->b->n) {
        if (!scientific_text_line(c, line)) return false;
        *line = scientific_text_trim(c->b, *line);
        if (line->n) return true;
    }
    return false;
}
static XXFC_MAYBE_UNUSED bool sequence_alignment_export(Abstractformat *f, pm_stream *s, sequence_sequence *q, unsigned n, uint64_t *total)
{
    unsigned i;
    size_t width;
    if (n < 2) return false;
    width = q[0].n;
    if (!width) return false;
    for (i = 0; i < n; ++i)
        if (q[i].n != width || !q[i].real) return false;
    for (i = 0; i < n; ++i) {
        char label[64];
        xx_rt_snprintf(label, sizeof(label), "sequence-%04u", i);
        if (!sequence_export(f, s, &q[i], label, total)) return false;
    }
    return true;
}
static XXFC_MAYBE_UNUSED void sequence_free_sequences(sequence_sequence *q, unsigned n)
{
    unsigned i;
    if (q) {
        for (i = 0; i < n; ++i) xx_mem_free(q[i].p);
        xx_mem_free(q);
    }
}
static XXFC_MAYBE_UNUSED bool sequence_terminated(scientific_text_lines *c)
{
    scientific_text_token t;
    while (c->at < c->b->n)
        if (!scientific_text_line(c, &t) || scientific_text_trim(c->b, t).n) return false;
    return !binary_stop(c->b->pd);
}
static XXFC_MAYBE_UNUSED bool sequence_hex(memory_blob *b, scientific_text_token t, uint64_t *out)
{
    uint64_t i, n = 0;
    if (t.n != 16) return false;
    for (i = 0; i < t.n; ++i) {
        unsigned ch = sequence_upper(b->p[(size_t)(t.at + i)]), v;
        if (ch >= '0' && ch <= '9') v = ch - '0';
        else if (ch >= 'A' && ch <= 'F') v = ch - 'A' + 10;
        else return false;
        n = (n << 4) | v;
    }
    *out = n;
    return true;
}
static XXFC_MAYBE_UNUSED uint64_t sequence_crc64(const uint8_t *p, size_t n, xx_pd_struct *pd)
{
    static const xx_crc_model model = {64, UINT64_C(0x1b), 0, true, true, 0, "CRC-64/ISO raw"};
    xx_crc_context ctx;
    size_t at = 0;
    if (!xx_crc_context_init(&ctx, &model)) return UINT64_MAX;
    while (at < n) {
        size_t part = n - at > 4096U ? 4096U : n - at;
        if (binary_stop(pd)) return UINT64_MAX;
        xx_crc_context_update(&ctx, p + at, part);
        at += part;
    }
    return xx_crc_context_final(&ctx);
}
#endif
