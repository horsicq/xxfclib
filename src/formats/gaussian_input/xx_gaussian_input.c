/* SPDX-License-Identifier: MIT
 * Independently implemented from https://gaussian.com/input/ */
#include "xxfclib/formats/gaussian_input/xx_gaussian_input.h"
#include "../common/xx_quantum_chemistry_input.h"

static bool gaussian_input_gaussian_link0(memory_blob *b, scientific_text_token key, scientific_text_token value)
{
    static const char *const units[] = {"B", "W", "KB", "KW", "MB", "MW", "GB", "GW"};
    uint64_t x, k = 0;
    value = scientific_text_trim(b, value);
    if (!value.n || value.n > 255) return false;
    if (structure_eq(b, key, "chk")) {
        for (k = 0; k < value.n; ++k)
            if (b->p[(size_t)(value.at + k)] <= 32) return false;
        return true;
    }
    if (structure_eq(b, key, "nprocshared")) return scientific_text_uint(b, value, &x) && x && x <= 65536;
    if (!structure_eq(b, key, "mem")) return false;
    while (k < value.n && b->p[(size_t)(value.at + k)] >= '0' && b->p[(size_t)(value.at + k)] <= '9') ++k;
    if (!k || !scientific_text_uint(b, scientific_text_slice(value, 0, k), &x) || !x || x > 1048576) return false;
    return k == value.n || chemistry_in(b, scientific_text_slice(value, k, value.n - k), units, 8);
}

static bool gaussian_input_parse_components(Abstractformat *f, pm_stream *s, memory_blob *b)
{
    scientific_text_lines c = {0};
    scientific_text_token row, t[16], cell[3][3];
    unsigned n, atoms = 0, vectors = 0, title = 0;
    uint64_t mult, begin;
    bool route = false;
    c.b = b;
    while (chemistry_line(&c, &row)) {
        row = scientific_text_trim(b, row);
        if (!row.n) {
            if (route) break;
            return false;
        }
        if (b->p[(size_t)row.at] == '%') {
            uint64_t k = 0;
            if (route || row.n > 255) return false;
            while (k < row.n && b->p[(size_t)(row.at + k)] != '=') ++k;
            if (k < 2 || k == row.n) return false;
            if (!gaussian_input_gaussian_link0(b, scientific_text_slice(row, 1, k - 1), scientific_text_slice(row, k + 1, row.n - k - 1))) return false;
        } else {
            if (!route && b->p[(size_t)row.at] != '#') {
                return false;
            }
            route = true;
            if (row.n > 1024 || !scientific_text_chars(b, row, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 \t#!=/()_,.+-*", true)) return false;
        }
    }
    if (!route || !row.n) {
        if (!route) return false;
    }
    while (chemistry_line(&c, &row)) {
        row = scientific_text_trim(b, row);
        if (!row.n) break;
        if (++title > 8 || row.n > 255) return false;
    }
    if (!title || !chemistry_words(&c, &row, t, 16, &n, "!") || n != 2 || !scientific_text_range(b, t[0], 1000, 1000) || !scientific_text_uint(b, t[1], &mult) || !mult ||
        mult > 1000)
        return false;
    begin = c.at;
    if (!blob_add(f, s, b, "calculation-header", 0, begin)) return false;
    while (chemistry_line(&c, &row)) {
        row = scientific_text_trim(b, row);
        if (!row.n) break;
        if (!scientific_text_split(b, row, t, 16, &n, false) || n != 4 || !structure_floats(b, t + 1, 3)) return false;
        if (structure_eq(b, t[0], "TV")) {
            unsigned j;
            if (!atoms || vectors == 3) return false;
            for (j = 0; j < 3; ++j) cell[vectors][j] = t[j + 1];
            ++vectors;
        } else if (vectors || !chemistry_atomic_number(b, t[0]) || ++atoms > 4090) return false;
        if (!chemistry_add_row(f, s, b, vectors ? "lattice-vector" : "atomic-position", row, c.at)) return false;
    }
    return atoms && (!vectors || (vectors == 3 && chemistry_cell_valid(b, cell))) && chemistry_finish(&c, "");
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    ok = (f->file_type == XX_FILE_TYPE_GAUSSIAN_INPUT && gaussian_input_parse_components(f, s, &b));
    if (ok) s->size = (int64_t)b.n;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_gaussian_input_init(xx_gaussian_input *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_GAUSSIAN_INPUT, "gaussian_input");
    }
}
xx_gaussian_input *xx_gaussian_input_create(xx_io_device *d, int64_t b)
{
    xx_gaussian_input *r = (xx_gaussian_input *)xx_mem_alloc(sizeof(*r));
    if (r) xx_gaussian_input_init(r, d, b);
    return r;
}
void xx_gaussian_input_destroy(xx_gaussian_input *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_gaussian_input_free(xx_gaussian_input *r)
{
    if (r) {
        xx_gaussian_input_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_gaussian_input_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_gaussian_input_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
