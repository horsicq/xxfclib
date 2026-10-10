/* SPDX-License-Identifier: MIT. Checked scientific-format primitives. */
#ifndef XX_MOLECULAR_TEXT_H
#define XX_MOLECULAR_TEXT_H
#include "xx_scientific_text.h"
static const char *const molecular_elements[] = {"X",  "H",  "He", "Li", "Be", "B",  "C",  "N",  "O",  "F",  "Ne", "Na", "Mg", "Al", "Si", "P",  "S",  "Cl", "Ar", "K",
                                                 "Ca", "Sc", "Ti", "V",  "Cr", "Mn", "Fe", "Co", "Ni", "Cu", "Zn", "Ga", "Ge", "As", "Se", "Br", "Kr", "Rb", "Sr", "Y",
                                                 "Zr", "Nb", "Mo", "Tc", "Ru", "Rh", "Pd", "Ag", "Cd", "In", "Sn", "Sb", "Te", "I",  "Xe", "Cs", "Ba", "La", "Ce", "Pr",
                                                 "Nd", "Pm", "Sm", "Eu", "Gd", "Tb", "Dy", "Ho", "Er", "Tm", "Yb", "Lu", "Hf", "Ta", "W",  "Re", "Os", "Ir", "Pt", "Au",
                                                 "Hg", "Tl", "Pb", "Bi", "Po", "At", "Rn", "Fr", "Ra", "Ac", "Th", "Pa", "U",  "Np", "Pu", "Am", "Cm", "Bk", "Cf", "Es",
                                                 "Fm", "Md", "No", "Lr", "Rf", "Db", "Sg", "Bh", "Hs", "Mt", "Ds", "Rg", "Cn", "Nh", "Fl", "Mc", "Lv", "Ts", "Og"};
static XXFC_MAYBE_UNUSED bool molecular_element(memory_blob *b, scientific_text_token v)
{
    unsigned i;
    for (i = 1; i < 119; ++i)
        if (scientific_text_eq(b, v, molecular_elements[i])) return true;
    return false;
}
static XXFC_MAYBE_UNUSED bool molecular_z(memory_blob *b, scientific_text_token v)
{
    uint64_t n;
    return scientific_text_uint(b, v, &n) && n >= 1 && n <= 118;
}
static XXFC_MAYBE_UNUSED bool molecular_words(scientific_text_lines *c, scientific_text_token *line, scientific_text_token *t, unsigned cap, unsigned *n)
{
    return scientific_text_line(c, line) && scientific_text_split(c->b, *line, t, cap, n, false);
}
static XXFC_MAYBE_UNUSED bool molecular_floats(memory_blob *b, scientific_text_token *t, unsigned begin, unsigned end)
{
    unsigned i;
    for (i = begin; i < end; ++i)
        if (!scientific_text_float(b, t[i])) return false;
    return true;
}
static XXFC_MAYBE_UNUSED bool molecular_numbers(scientific_text_lines *c, uint64_t count)
{
    scientific_text_token line, t[128];
    unsigned n, i;
    uint64_t seen = 0;
    while (seen < count) {
        uint64_t begin = c->at;
        if (!scientific_text_line(c, &line)) {
            uint64_t j;
            if (binary_stop(c->b->pd) || c->at != c->b->n || c->b->n - begin > 1048576) return false;
            for (j = begin; j < c->b->n; ++j) {
                uint8_t ch = c->b->p[(size_t)j];
                if (ch < 32 && ch != '\t') return false;
                if (ch > 126) return false;
            }
            line.at = begin;
            line.n = c->b->n - begin;
        }
        if (!scientific_text_split(c->b, line, t, 128, &n, false) || !n || n > count - seen) {
            return false;
        }
        for (i = 0; i < n; ++i)
            if (!scientific_text_float(c->b, t[i])) return false;
        seen += n;
    }
    return true;
}
static XXFC_MAYBE_UNUSED bool molecular_trailing(scientific_text_lines *c)
{
    scientific_text_token line;
    while (c->at < c->b->n)
        if (!scientific_text_line(c, &line) || scientific_text_trim(c->b, line).n) return false;
    return true;
}
static bool molecular_fixed(memory_blob *b, scientific_text_token line, unsigned width, unsigned *count, bool real)
{
    uint64_t at = 0;
    unsigned n = 0;
    if (!line.n || line.n % width) return false;
    while (at < line.n) {
        scientific_text_token v = scientific_text_trim(b, scientific_text_slice(line, at, width));
        if (!v.n || (real ? !scientific_text_float(b, v) : !scientific_text_integer(b, v))) return false;
        at += width;
        ++n;
    }
    *count = n;
    return true;
}
static XXFC_MAYBE_UNUSED bool molecular_fixed_array(scientific_text_lines *c, uint64_t count, unsigned width, unsigned perline, bool real)
{
    scientific_text_token line;
    uint64_t seen = 0;
    while (seen < count) {
        unsigned n;
        if (!scientific_text_line(c, &line) || !molecular_fixed(c->b, line, width, &n, real) || n != ((count - seen) < perline ? (unsigned)(count - seen) : perline))
            return false;
        seen += n;
    }
    return true;
}
static XXFC_MAYBE_UNUSED bool molecular_name(memory_blob *b, scientific_text_token v)
{
    uint64_t i;
    if (!v.n || v.n > 255) return false;
    for (i = 0; i < v.n; ++i) {
        uint8_t ch = b->p[(size_t)(v.at + i)];
        if (ch <= 32 || ch > 126) return false;
    }
    return true;
}
static XXFC_MAYBE_UNUSED double molecular_value(memory_blob *b, scientific_text_token v)
{
    char text[100];
    v = scientific_text_trim(b, v);
    xx_rt_memcpy(text, b->p + (size_t)v.at, (size_t)v.n);
    text[v.n] = 0;
    return xx_rt_strtod(text, NULL);
}
#endif
