/* SPDX-License-Identifier: MIT
 * Independently implemented from https://manual.cp2k.org/trunk/CP2K_INPUT.html */
#include "xxfclib/formats/cp2k_input/xx_cp2k_input.h"
#include "../common/xx_quantum_chemistry_input.h"

static unsigned cp2k_input_cp2k_keyword_bit(memory_blob *b, scientific_text_token key)
{
    static const char *const keywords[] = {
        "ABC",    "PRINT_LEVEL", "PROJECT", "RUN_TYPE",  "METHOD",    "BASIS_SET_FILE_NAME", "POTENTIAL_FILE_NAME", "BASIS_SET",           "POTENTIAL",
        "CUTOFF", "REL_CUTOFF",  "EPS_SCF", "SCF_GUESS", "FORCE_PAW", "ALPHA_WEIGHTS",       "EPS_DEFAULT",         "GAPW_ACCURATE_XCINT", "GAPW_1C_BASIS"};
    unsigned i;
    for (i = 0; i < 18; ++i)
        if (structure_eq(b, key, keywords[i])) return 1U << i;
    return 0;
}

static bool cp2k_input_parse_components(Abstractformat *f, pm_stream *s, memory_blob *b)
{
    static const char *const sections[] = {"GLOBAL", "FORCE_EVAL", "DFT",           "MGRID",  "PRINT", "DERIVATIVES", "QS",
                                           "SCF",    "XC",         "XC_FUNCTIONAL", "SUBSYS", "CELL",  "COORD",       "KIND"};
    static const char *const runs[] = {"ENERGY", "ENERGY_FORCE"};
    static const char *const levels[] = {"SILENT", "LOW", "MEDIUM", "HIGH", "DEBUG"};
    static const char *const guesses[] = {"ATOMIC", "CORE", "RANDOM"};
    scientific_text_lines c = {0};
    scientific_text_token row, t[16], stack[16];
    unsigned seen[16] = {0}, n, depth = 0, global = 0, force = 0, subsys = 0, coords = 0, cells = 0, cellrows = 0, atoms = 0, method = 0;
    uint64_t topstart = 0;
    c.b = b;
    while (chemistry_words(&c, &row, t, 16, &n, "#!")) {
        if (b->p[(size_t)t[0].at] == '&') {
            scientific_text_token name = scientific_text_slice(t[0], 1, t[0].n - 1);
            if (structure_eq(b, name, "END")) {
                if (!depth || n > 2 || (n == 2 && !structure_same(b, t[1], stack[depth - 1]))) return false;
                if (structure_eq(b, stack[depth - 1], "COORD") && !atoms) return false;
                if (structure_eq(b, stack[depth - 1], "CELL") && cellrows != 1) return false;
                --depth;
                if (!depth && !blob_add(f, s, b, "input-section", topstart, c.at - topstart)) return false;
            } else {
                if (depth == 16 || !chemistry_in(b, name, sections, 14) || (structure_eq(b, name, "KIND") || structure_eq(b, name, "XC_FUNCTIONAL") ? n != 2 : n != 1))
                    return false;
                if (!depth) {
                    if (structure_eq(b, name, "GLOBAL")) {
                        if (global++ || force) return false;
                    } else if (structure_eq(b, name, "FORCE_EVAL")) {
                        if (!global || force++) return false;
                    } else return false;
                    topstart = chemistry_row_start(b, row);
                } else if (structure_eq(b, name, "SUBSYS")) {
                    if (!structure_eq(b, stack[depth - 1], "FORCE_EVAL") || subsys++) return false;
                } else if (structure_eq(b, name, "CELL") || structure_eq(b, name, "COORD") || structure_eq(b, name, "KIND")) {
                    if (!structure_eq(b, stack[depth - 1], "SUBSYS")) return false;
                    if (structure_eq(b, name, "CELL") && cells++) return false;
                    if (structure_eq(b, name, "COORD") && coords++) return false;
                    if (structure_eq(b, name, "KIND") && (n != 2 || !chemistry_atomic_number(b, t[1]))) return false;
                } else if (structure_eq(b, name, "DFT")) {
                    if (!structure_eq(b, stack[depth - 1], "FORCE_EVAL")) return false;
                } else if (structure_eq(b, name, "MGRID") || structure_eq(b, name, "QS") || structure_eq(b, name, "SCF") || structure_eq(b, name, "XC")) {
                    if (!structure_eq(b, stack[depth - 1], "DFT")) return false;
                } else if (structure_eq(b, name, "XC_FUNCTIONAL")) {
                    if (!structure_eq(b, stack[depth - 1], "XC") || n != 2 || !chemistry_identifier(b, t[1])) return false;
                } else if (structure_eq(b, name, "DERIVATIVES")) {
                    if (!structure_eq(b, stack[depth - 1], "PRINT")) return false;
                } else if (!structure_eq(b, name, "PRINT") || !structure_eq(b, stack[depth - 1], "DFT")) return false;
                seen[depth] = 0;
                stack[depth++] = name;
            }
        } else {
            scientific_text_token parent;
            if (!depth) return false;
            parent = stack[depth - 1];
            if (!structure_eq(b, parent, "COORD")) {
                unsigned bit = cp2k_input_cp2k_keyword_bit(b, t[0]);
                if (!bit || (seen[depth - 1] & bit)) return false;
                seen[depth - 1] |= bit;
            }
            if (structure_eq(b, parent, "COORD")) {
                if (n != 4 || ++atoms > 4090 || !chemistry_atom(b, t, n, 0, 1) || !chemistry_add_row(f, s, b, "atomic-position", row, c.at)) return false;
            } else if (structure_eq(b, parent, "CELL")) {
                if (cellrows++ || n != 4 || !structure_eq(b, t[0], "ABC") || !structure_positive(b, t[1]) || !structure_positive(b, t[2]) ||
                    !structure_positive(b, t[3]) || !chemistry_add_row(f, s, b, "cell-dimensions", row, c.at))
                    return false;
            } else if (structure_eq(b, parent, "GLOBAL")) {
                if (n != 2) return false;
                if (structure_eq(b, t[0], "PRINT_LEVEL")) {
                    if (!chemistry_in(b, t[1], levels, 5)) return false;
                } else if (structure_eq(b, t[0], "RUN_TYPE")) {
                    if (!chemistry_in(b, t[1], runs, 2)) return false;
                } else if (!structure_eq(b, t[0], "PROJECT") || !chemistry_identifier(b, t[1])) return false;
            } else if (structure_eq(b, parent, "FORCE_EVAL")) {
                if (n != 2 || !structure_eq(b, t[0], "METHOD") || !structure_eq(b, t[1], "Quickstep") || method++) return false;
            } else if (structure_eq(b, parent, "DFT")) {
                if (n != 2 || (!structure_eq(b, t[0], "BASIS_SET_FILE_NAME") && !structure_eq(b, t[0], "POTENTIAL_FILE_NAME")) || t[1].n > 255) return false;
            } else if (structure_eq(b, parent, "KIND")) {
                if (n != 2 || (!structure_eq(b, t[0], "BASIS_SET") && !structure_eq(b, t[0], "POTENTIAL")) || !chemistry_identifier(b, t[1])) return false;
            } else if (structure_eq(b, parent, "MGRID")) {
                if (n != 2 || (!structure_eq(b, t[0], "CUTOFF") && !structure_eq(b, t[0], "REL_CUTOFF")) || !structure_positive(b, t[1])) return false;
            } else if (structure_eq(b, parent, "SCF")) {
                if (n != 2 ||
                    (structure_eq(b, t[0], "EPS_SCF") ? !structure_positive(b, t[1]) : (!structure_eq(b, t[0], "SCF_GUESS") || !chemistry_in(b, t[1], guesses, 3))))
                    return false;
            } else if (structure_eq(b, parent, "QS")) {
                if (structure_eq(b, t[0], "FORCE_PAW")) {
                    if (n != 1) return false;
                } else if (n != 2) return false;
                else if (structure_eq(b, t[0], "ALPHA_WEIGHTS") || structure_eq(b, t[0], "EPS_DEFAULT")) {
                    if (!structure_positive(b, t[1])) return false;
                } else if (structure_eq(b, t[0], "GAPW_ACCURATE_XCINT")) {
                    if (!structure_eq(b, t[1], "T") && !structure_eq(b, t[1], "F")) return false;
                } else if ((!structure_eq(b, t[0], "METHOD") && !structure_eq(b, t[0], "GAPW_1C_BASIS")) || !chemistry_identifier(b, t[1])) return false;
            } else return false;
        }
    }
    return c.at == b->n && !depth && global == 1 && force == 1 && subsys == 1 && cells == 1 && coords == 1 && method == 1 && atoms && !binary_stop(b->pd);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b = {0};
    bool ok = false;
    BLOB_NEED(blob_load(f, &b, pd));
    ok = (f->file_type == XX_FILE_TYPE_CP2K_INPUT && cp2k_input_parse_components(f, s, &b));
    if (ok) s->size = (int64_t)b.n;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_cp2k_input_init(xx_cp2k_input *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_CP2K_INPUT, "cp2k_input");
    }
}
xx_cp2k_input *xx_cp2k_input_create(xx_io_device *d, int64_t b)
{
    xx_cp2k_input *r = (xx_cp2k_input *)xx_mem_alloc(sizeof(*r));
    if (r) xx_cp2k_input_init(r, d, b);
    return r;
}
void xx_cp2k_input_destroy(xx_cp2k_input *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_cp2k_input_free(xx_cp2k_input *r)
{
    if (r) {
        xx_cp2k_input_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_cp2k_input_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_cp2k_input_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
