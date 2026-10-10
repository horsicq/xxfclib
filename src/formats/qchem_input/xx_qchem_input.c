/* SPDX-License-Identifier: MIT
 * Primary reference: https://manual.q-chem.com/latest/Ch3.S3.html
 * Q-Chem molecule/rem input subset: balanced complete comment/rem/molecule sections, unique recognized settings, checked charge/multiplicity and finite element
 * coordinates for H through Xe and consistent charge/spin electron parity. Original settings/molecule sections exported; includes, basis/ECP blocks, fragments, variables
 * and job chains declined. Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/qchem_input/xx_qchem_input.h"
#include "../common/xx_component_text.h"

static bool scene_bitmap_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool scene_bitmap_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(scene_bitmap, 33554432, if (ok) s->size = available;)
static bool scene_bitmap_quick(Abstractformat *f, uint64_t n)
{
    uint8_t c;
    return n >= 32 && pm_read(f, 0, &c, 1) && (c == '$' || c == ' ' || c == '\n' || c == '\r');
}
static bool scene_bitmap_qtoken(component_text_cursor *q, char *out, size_t capacity)
{
    size_t z = 0;
    component_text_space(q);
    while (q->t < q->stop && q->b[q->t] != 32 && q->b[q->t] != 9) {
        uint8_t c = q->b[q->t++];
        if (z + 1 >= capacity ||
            !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '+' || c == '(' || c == ')' || c == '.'))
            return false;
        if (c >= 'a' && c <= 'z') c -= 32;
        out[z++] = (char)c;
    }
    out[z] = 0;
    return z != 0;
}
static unsigned scene_bitmap_element(const char *t)
{
    static const char *const names[] = {"H",  "HE", "LI", "BE", "B",  "C",  "N",  "O",  "F",  "NE", "NA", "MG", "AL", "SI", "P",  "S",  "CL", "AR",
                                        "K",  "CA", "SC", "TI", "V",  "CR", "MN", "FE", "CO", "NI", "CU", "ZN", "GA", "GE", "AS", "SE", "BR", "KR",
                                        "RB", "SR", "Y",  "ZR", "NB", "MO", "TC", "RU", "RH", "PD", "AG", "CD", "IN", "SN", "SB", "TE", "I",  "XE"};
    unsigned i;
    for (i = 0; i < 54; ++i)
        if (!xx_rt_strcmp(names[i], t)) return i + 1;
    return 0;
}
static bool scene_bitmap_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    component_text_cursor q = {b, 0, n, 0, 0, 0};
    unsigned seen = 0, section = 0, settings = 0, atoms = 0, electrons = 0;
    uint64_t start = 0;
    int32_t charge = 0, spin = 0;
    bool first = false;
    char key[64], value[128];
    static const char *const rem[] = {"JOBTYPE", "METHOD", "BASIS", "SYM_IGNORE", "SCF_CONVERGENCE", "MAX_SCF_CYCLES", "SCF_ALGORITHM", "THRESH"};
    if (!component_utf8(b, n, true, pd)) return false;
    while (component_text_next_poison_overflow(&q)) {
        if (xx_component_parser_stopped(pd)) return false;
        if (q.b[q.t] == '$') {
            unsigned next = 0;
            if (component_text_word_ci(&q, "$end")) {
                if (!component_text_done(&q) || !section || first || (section == 2 && (settings & 7) != 7) || (section == 4 && !atoms)) return false;
                if (!component_emit(f, s, section == 1 ? "comment.txt" : section == 2 ? "rem.txt" : "molecule.txt", start, q.p - start, n)) return false;
                section = 0;
                continue;
            }
            if (component_text_word_ci(&q, "$comment")) next = 1;
            else if (component_text_word_ci(&q, "$rem")) next = 2;
            else if (component_text_word_ci(&q, "$molecule")) next = 4;
            else return false;
            if (section || !component_text_done(&q) || (seen & next)) return false;
            seen |= next;
            section = next;
            start = q.start;
            first = next == 4;
            continue;
        }
        if (!section) return false;
        if (section == 1) continue;
        if (section == 2) {
            unsigned i;
            int32_t number;
            if (!scene_bitmap_qtoken(&q, key, sizeof(key))) return false;
            for (i = 0; i < 8 && xx_rt_strcmp(rem[i], key); ++i) {
            }
            if (i == 8 || (settings & (1U << i))) return false;
            settings |= 1U << i;
            if (i == 4 || i == 5 || i == 7) {
                if (!component_text_integer_delimited(&q, &number) || number < 1 || number > 10000) return false;
            } else {
                if (!scene_bitmap_qtoken(&q, value, sizeof(value))) return false;
                if (i == 0 && xx_rt_strcmp(value, "SP") && xx_rt_strcmp(value, "OPT") && xx_rt_strcmp(value, "FREQ")) return false;
                if (i == 3 && xx_rt_strcmp(value, "TRUE") && xx_rt_strcmp(value, "FALSE")) return false;
                if (i == 6 && xx_rt_strcmp(value, "DIIS") && xx_rt_strcmp(value, "GDM") && xx_rt_strcmp(value, "DIIS_GDM")) return false;
            }
            if (!component_text_done(&q)) return false;
        } else {
            if (first) {
                if (!component_text_integer_delimited(&q, &charge) || charge < -100 || charge > 100 || !component_text_integer_delimited(&q, &spin) || spin < 1 ||
                    spin > 100 || !component_text_done(&q))
                    return false;
                first = false;
            } else {
                unsigned element;
                if (++atoms > 4000 || !scene_bitmap_qtoken(&q, key, sizeof(key)) || !(element = scene_bitmap_element(key)) || !component_text_numbers_36_digits(&q, 3))
                    return false;
                electrons += element;
            }
        }
    }
    if (q.p != n || section || (seen & 6) != 6 || (int64_t)electrons - charge < spin - 1 || ((int64_t)electrons - charge - (spin - 1)) % 2) return false;
    return component_cover(f, s, "whitespace.txt", n);
}

void xx_qchem_input_init(xx_qchem_input *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_QCHEM_INPUT, "inp");
    }
}
xx_qchem_input *xx_qchem_input_create(xx_io_device *d, int64_t at)
{
    xx_qchem_input *r = (xx_qchem_input *)xx_mem_alloc(sizeof(*r));
    if (r) xx_qchem_input_init(r, d, at);
    return r;
}
void xx_qchem_input_destroy(xx_qchem_input *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_qchem_input_free(xx_qchem_input *r)
{
    if (r) {
        xx_qchem_input_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_qchem_input_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_qchem_input_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
