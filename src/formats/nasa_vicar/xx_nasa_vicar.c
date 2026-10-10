/* SPDX-License-Identifier: MIT
 * Primary reference: https://www-mipl.jpl.nasa.gov/external/VICAR_file_fmt.pdf
 * NASA VICAR image subset: complete counted ASCII labels and BSQ/BIL/BIP sample layout with checked dimensions/record padding and original integer/finite floating
 * planes/records. End-of-line labels, binary prefixes/header records and unknown label syntax declined. Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/nasa_vicar/xx_nasa_vicar.h"
#include "../common/xx_component_lexer.h"

static bool image_document_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool image_document_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(image_document, 33554432, if (ok) s->size = available;)
static bool image_document_quick(Abstractformat *f, uint64_t n)
{
    uint8_t b[8];
    return n >= 64 && pm_read(f, 0, b, 8) && component_tag(b, "LBLSIZE=", 8);
}
static bool image_document_vicar_string(component_lexer *q, const char *value)
{
    uint64_t p, z;
    return component_lexer_quoted_hash_cpp_comments(q, '\'', &p, &z) && z == xx_rt_strlen(value) && component_tag(q->b + p, value, (size_t)z);
}
static bool image_document_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    component_lexer q = {b, 0, n, pd, 0, false, false, false};
    uint32_t seen = 0, labelsize = 0, ns = 0, nl = 0, nb = 0, record = 0, bytes = 0, n1 = 0, n2 = 0, n3 = 0, i, blocks;
    int32_t value;
    uint64_t key, z, p = 0, raw;
    unsigned org = 0;
    bool le = false, isfloat = false;
    char label[48];
    static const char *const keys[] = {"LBLSIZE", "FORMAT", "TYPE", "BUFSIZE", "DIM", "EOL", "RECSIZE", "ORG",  "NL",     "NS",
                                       "NB",      "N1",     "N2",   "N3",      "N4",  "NBB", "NLB",     "TASK", "INTFMT", "REALFMT"};
    if (n < 64) return false;
    while (component_lexer_skip_hash_cpp_comments(&q) && q.p < q.n && b[q.p]) {
        unsigned field;
        if (!component_lexer_identifier_hash_cpp_comments(&q, &key, &z) || !component_lexer_char_hash_cpp_comments(&q, '=')) return false;
        for (field = 0; field < 20 && (z != xx_rt_strlen(keys[field]) || !component_tag(b + key, keys[field], (size_t)z)); ++field) {
        }
        if (field == 20 || (seen & (1U << field))) return false;
        seen |= 1U << field;
        if (field == 0) {
            if (key || !component_lexer_integer_hash_cpp_comments_delimited(&q, &value) || value < 64 || value > 1048576 || (uint64_t)value > n) return false;
            labelsize = (uint32_t)value;
            q.n = labelsize;
            {
                uint64_t j;
                for (j = q.p; j < labelsize; ++j)
                    if (!b[j]) {
                        q.n = j;
                        break;
                    }
            }
        } else if (field == 1) {
            component_lexer copy = q;
            if (image_document_vicar_string(&q, "BYTE")) bytes = 1;
            else {
                q = copy;
                if (image_document_vicar_string(&q, "HALF")) bytes = 2;
                else {
                    q = copy;
                    if (image_document_vicar_string(&q, "FULL")) bytes = 4;
                    else {
                        q = copy;
                        if (image_document_vicar_string(&q, "REAL")) {
                            bytes = 4;
                            isfloat = true;
                        } else {
                            q = copy;
                            if (image_document_vicar_string(&q, "DOUB")) {
                                bytes = 8;
                                isfloat = true;
                            } else return false;
                        }
                    }
                }
            }
        } else if (field == 2) {
            if (!image_document_vicar_string(&q, "IMAGE")) return false;
        } else if (field == 7) {
            component_lexer copy = q;
            if (image_document_vicar_string(&q, "BSQ")) org = 1;
            else {
                q = copy;
                if (image_document_vicar_string(&q, "BIL")) org = 2;
                else {
                    q = copy;
                    if (image_document_vicar_string(&q, "BIP")) org = 3;
                    else return false;
                }
            }
        } else if (field == 17) {
            uint64_t at, len;
            if (!component_lexer_quoted_hash_cpp_comments(&q, '\'', &at, &len) || !len || len > 255 || !component_utf8(b + at, len, true, pd)) return false;
        } else if (field == 18 || field == 19) {
            component_lexer copy = q;
            if (image_document_vicar_string(&q, field == 18 ? "LOW" : "RIEEE")) {
                if (field == 19) le = true;
            } else {
                q = copy;
                if (!image_document_vicar_string(&q, field == 18 ? "HIGH" : "IEEE")) return false;
                if (field == 19) le = false;
            }
        } else {
            if (!component_lexer_integer_hash_cpp_comments_delimited(&q, &value) || value < 0) return false;
            switch (field) {
                case 3: break;
                case 4:
                    if (value != 2 && value != 3) return false;
                    break;
                case 5:
                case 14:
                case 15:
                case 16:
                    if (value) return false;
                    break;
                case 6: record = (uint32_t)value; break;
                case 8: nl = (uint32_t)value; break;
                case 9: ns = (uint32_t)value; break;
                case 10: nb = (uint32_t)value; break;
                case 11: n1 = (uint32_t)value; break;
                case 12: n2 = (uint32_t)value; break;
                case 13: n3 = (uint32_t)value; break;
                default: return false;
            }
        }
        if (q.p < q.n && b[q.p] && b[q.p] != 32 && b[q.p] != 9 && b[q.p] != 10 && b[q.p] != 13) return false;
    }
    /* The label's NUL terminator and remaining record padding are explicit framing. */
    p = q.p;
    if (!labelsize || !bytes || !org || !ns || !nl || !nb || ns > 8192 || nl > 4095 || nb > 16 || (uint64_t)ns * nl * nb > 8388608 || (seen & 0x7ffU) != 0x7ffU ||
        !record || record > 1048576)
        return false;
    while (p < labelsize)
        if (b[p++] != 0 && b[p - 1] != 32 && b[p - 1] != 9 && b[p - 1] != 10 && b[p - 1] != 13) return false;
    {
        uint32_t expected1 = org == 3 ? nb : ns, expected2 = org == 1 ? nl : org == 2 ? nb : ns, expected3 = org == 1 ? nb : nl;
        if ((n1 && n1 != expected1) || (n2 && n2 != expected2) || (n3 && n3 != expected3) || record < (uint64_t)expected1 * bytes) return false;
        blocks = expected3;
        raw = (uint64_t)record * expected2;
        if (blocks > 4095 || n - labelsize != raw * blocks || !component_emit(f, s, "descriptor.vic", 0, labelsize, n)) return false;
        for (i = 0; i < blocks; ++i) {
            uint32_t row;
            uint64_t at = labelsize + (uint64_t)i * raw;
            if (isfloat)
                for (row = 0; row < expected2; ++row) {
                    uint32_t sample;
                    for (sample = 0; sample < expected1; ++sample) {
                        uint64_t v = at + (uint64_t)row * record + (uint64_t)sample * bytes;
                        uint32_t u = le ? xx_data_get_u32(b + v + (bytes == 8 ? 4 : 0), 4, 0, false) : xx_data_get_u32(b + v, 4, 0, true);
                        if ((bytes == 4 ? !component_is_finite32(u) : (u & 0x7ff00000U) == 0x7ff00000U) || xx_component_parser_stopped(pd)) return false;
                    }
                }
            xx_rt_snprintf(label, sizeof(label), "sample-block-%u.bin", i);
            if (xx_component_parser_stopped(pd) || !component_emit(f, s, label, at, raw, n)) return false;
        }
    }
    return true;
}

void xx_nasa_vicar_init(xx_nasa_vicar *r, xx_io_device *d, int64_t at)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_NASA_VICAR, "vic");
    }
}
xx_nasa_vicar *xx_nasa_vicar_create(xx_io_device *d, int64_t at)
{
    xx_nasa_vicar *r = (xx_nasa_vicar *)xx_mem_alloc(sizeof(*r));
    if (r) xx_nasa_vicar_init(r, d, at);
    return r;
}
void xx_nasa_vicar_destroy(xx_nasa_vicar *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_nasa_vicar_free(xx_nasa_vicar *r)
{
    if (r) {
        xx_nasa_vicar_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_nasa_vicar_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_nasa_vicar_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
