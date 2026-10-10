/* SPDX-License-Identifier: MIT. Bounded scientific tables and trace records. */
#ifndef XX_SCIENTIFIC_TABLES_TRACES_H
#define XX_SCIENTIFIC_TABLES_TRACES_H
#include "xx_scientific_structure.h"
static XXFC_MAYBE_UNUSED __inline bool scientific_table_charge(memory_blob *b, uint64_t *budget, uint64_t n) {
    if (binary_stop(b->pd) || n > *budget) {
        return false;
    }
    *budget -= n;
    return true;
}
static XXFC_MAYBE_UNUSED __inline bool scientific_table_identifier(memory_blob *b, scientific_text_token t) {
    return t.n <= 255 &&
           scientific_text_chars(b, t, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.:|+-", true);
}
static XXFC_MAYBE_UNUSED __inline bool scientific_table_words(scientific_text_lines *c, scientific_text_token *line,
                                                              scientific_text_token *t, unsigned cap, unsigned *nt) {
    while (c->at < c->b->n) {
        if (!scientific_text_line(c, line))
            return false;
        *line = scientific_text_trim(c->b, *line);
        if (!line->n || c->b->p[(size_t)line->at] == '#')
            continue;
        return scientific_text_split(c->b, *line, t, cap, nt, false);
    }
    return false;
}
static XXFC_MAYBE_UNUSED __inline bool scientific_table_finish(scientific_text_lines *c) {
    scientific_text_token v;
    while (c->at < c->b->n) {
        if (!scientific_text_line(c, &v))
            return false;
        v = scientific_text_trim(c->b, v);
        if (v.n && c->b->p[(size_t)v.at] != '#')
            return false;
    }
    return c->at == c->b->n && !binary_stop(c->b->pd);
}

static XXFC_MAYBE_UNUSED __inline bool scientific_table_unique(memory_blob *b, scientific_text_token t,
                                                               scientific_text_token *known, unsigned count,
                                                               uint64_t *budget) {
    unsigned i;
    for (i = 0; i < count; ++i) {
        if (!scientific_table_charge(b, budget, 1))
            return false;
        if (t.n == known[i].n) {
            if (!scientific_table_charge(b, budget, t.n))
                return false;
            if (!xx_rt_memcmp(b->p + (size_t)t.at, b->p + (size_t)known[i].at, (size_t)t.n))
                return false;
        }
    }
    return true;
}
/* Track options are inert metadata; quoted values never cause external reads. */
static XXFC_MAYBE_UNUSED __inline bool scientific_table_track(memory_blob *b, scientific_text_token line, bool wig) {
    scientific_text_token keys[64];
    uint64_t at = 5, budget = 131072;
    unsigned count = 0;
    bool type = false;
    if (!scientific_text_prefix(b, line, "track") ||
        (line.n > 5 && b->p[(size_t)(line.at + 5)] != ' ' && b->p[(size_t)(line.at + 5)] != '\t'))
        return false;
    while (at < line.n) {
        uint64_t start;
        scientific_text_token key, value;
        while (at < line.n && (b->p[(size_t)(line.at + at)] == ' ' || b->p[(size_t)(line.at + at)] == '\t'))
            ++at;
        if (at == line.n) {
            break;
        }
        start = at;
        while (at < line.n && b->p[(size_t)(line.at + at)] != '=')
            ++at;
        key = scientific_text_slice(line, start, at - start);
        if (at == line.n ||
            !scientific_text_chars(b, key, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_", true) ||
            key.n > 64 || count == 64 || !scientific_table_unique(b, key, keys, count, &budget))
            return false;
        keys[count++] = key;
        ++at;
        start = at;
        if (at < line.n && b->p[(size_t)(line.at + at)] == '"') {
            start = ++at;
            while (at < line.n && b->p[(size_t)(line.at + at)] != '"')
                ++at;
            if (at == line.n) {
                return false;
            }
            value = scientific_text_slice(line, start, at - start);
            ++at;
        } else {
            while (at < line.n && b->p[(size_t)(line.at + at)] != ' ' && b->p[(size_t)(line.at + at)] != '\t')
                ++at;
            value = scientific_text_slice(line, start, at - start);
        }
        if (!value.n || value.n > 1024 ||
            (at < line.n && b->p[(size_t)(line.at + at)] != ' ' && b->p[(size_t)(line.at + at)] != '\t'))
            return false;
        if (scientific_text_eq(b, key, "type")) {
            if (wig && !scientific_text_eq(b, value, "wiggle_0"))
                return false;
            type = true;
        }
    }
    return count && (!wig || type);
}

#endif
