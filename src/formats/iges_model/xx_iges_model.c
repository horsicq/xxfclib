/* SPDX-License-Identifier: MIT
 * Primary reference: https://github.com/pyvista/pyiges
 * IGES5.x ASCII CAD subset: complete80-column S/G/D/P/T sequence/count framing, bounded Hollerith/numeric parameter grammar, typed supported entities and resolved local directory references; original descriptor/directory/entity records exported; unsupported entity forms and external references declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/iges_model/xx_iges_model.h"
#include "../common/xx_component_lexer.h"

static bool image_document_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool image_document_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(image_document, 33554432, if (ok) s->size = available;)
static bool image_document_quick(Abstractformat *f, uint64_t n) {
    uint8_t b[80];
    return n >= 405 && pm_read(f, 0, b, 80) && b[72] == 'S';
}
typedef struct image_document_iges_tokens {
    const uint8_t *b;
    uint64_t p, n;
    xx_pd_struct *pd;
    bool final;
} image_document_iges_tokens;
typedef struct image_document_iges_entity {
    uint32_t type, first, count, form, transform, parent;
    int32_t refs[4];
} image_document_iges_entity;
static bool image_document_iges_intfield(const uint8_t *b, unsigned z, int32_t *v) {
    component_text_cursor q = {b, 0, z, 0, z, 0};
    component_text_space(&q);
    if (q.t == q.stop) {
        *v = 0;
        return true;
    }
    return component_text_integer_delimited(&q, v) && component_text_done(&q);
}
static bool image_document_iges_token(image_document_iges_tokens *q, double *value, int32_t *integer, bool *empty,
                                      bool *string) {
    uint64_t start, p, end;
    uint32_t len = 0;
    unsigned digits = 0;
    uint8_t text[96];
    component_text_cursor t;
    *empty = *string = false;
    if (q->final || xx_component_parser_stopped(q->pd))
        return false;
    p = q->p;
    while (p < q->n && q->b[p] == 32)
        ++p;
    start = p;
    while (p < q->n && q->b[p] >= '0' && q->b[p] <= '9') {
        if (++digits > 8)
            break;
        len = len * 10 + q->b[p++] - '0';
    }
    if (digits && digits <= 8 && p < q->n && (q->b[p] == 'H' || q->b[p] == 'h')) {
        ++p;
        if (len > 8192 || !component_span(p, len, q->n))
            return false;
        end = p + len;
        while (end < q->n && q->b[end] == 32)
            ++end;
        *string = true;
    } else {
        end = start;
        while (end < q->n && q->b[end] != ',' && q->b[end] != ';')
            ++end;
        p = end;
        while (p > start && q->b[p - 1] == 32)
            --p;
        if (p == start)
            *empty = true;
        else {
            uint64_t i, z = p - start;
            if (z >= sizeof(text))
                return false;
            for (i = 0; i < z; ++i)
                text[i] = q->b[start + i] == 'D' || q->b[start + i] == 'd' ? 'E' : q->b[start + i];
            t.b = text;
            t.p = t.start = t.t = 0;
            t.stop = t.end = z;
            if (!component_text_number_36_digits(&t, value) || !component_text_done(&t))
                return false;
            if (integer) {
                t.t = 0;
                if (!component_text_integer_delimited(&t, integer) || !component_text_done(&t))
                    return false;
            }
        }
    }
    if (end == q->n)
        return false;
    q->final = q->b[end] == ';';
    q->p = end + 1;
    return q->b[end] == ',' || q->b[end] == ';';
}
static bool image_document_iges_real(image_document_iges_tokens *q, double *v) {
    bool empty, string;
    return image_document_iges_token(q, v, NULL, &empty, &string) && !empty && !string;
}
static bool image_document_iges_integer(image_document_iges_tokens *q, int32_t *v) {
    double d;
    bool empty, string;
    return image_document_iges_token(q, &d, v, &empty, &string) && !empty && !string;
}
static bool image_document_iges_end(image_document_iges_tokens *q) {
    while (q->p < q->n && q->b[q->p] == 32)
        ++q->p;
    return q->final && q->p == q->n;
}
static bool image_document_iges_ref(uint32_t value, uint32_t count) {
    return value && value % 2 == 1 && value <= count * 2 - 1;
}
static bool image_document_iges_entity_parse(image_document_iges_entity *e, const uint8_t *bytes, uint64_t n,
                                             uint32_t count, xx_pd_struct *pd) {
    image_document_iges_tokens q = {bytes, 0, n, pd, false};
    int32_t type, v;
    unsigned i;
    double a[12];
    if (!image_document_iges_integer(&q, &type) || (uint32_t)type != e->type)
        return false;
    switch (type) {
    case 100:
        for (i = 0; i < 7; ++i)
            if (!image_document_iges_real(&q, &a[i]))
                return false;
        {
            double x = a[3] - a[1], y = a[4] - a[2], u = a[5] - a[1], w = a[6] - a[2], r = x * x + y * y,
                   t = u * u + w * w, diff = r - t;
            if (diff < 0)
                diff = -diff;
            if (r <= 0 || diff > r * 1e-8)
                return false;
        }
        break;
    case 104:
        for (i = 0; i < 11; ++i)
            if (!image_document_iges_real(&q, &a[i]))
                return false;
        if (a[0] == 0 && a[1] == 0 && a[2] == 0)
            return false;
        break;
    case 110:
        for (i = 0; i < 6; ++i)
            if (!image_document_iges_real(&q, &a[i]))
                return false;
        if (a[0] == a[3] && a[1] == a[4] && a[2] == a[5])
            return false;
        break;
    case 116:
        for (i = 0; i < 3; ++i)
            if (!image_document_iges_real(&q, &a[i]))
                return false;
        if (!image_document_iges_integer(&q, &v) || v)
            return false;
        break;
    case 124:
        for (i = 0; i < 12; ++i)
            if (!image_document_iges_real(&q, &a[i]))
                return false;
        {
            double d = a[0] * (a[5] * a[10] - a[6] * a[9]) - a[1] * (a[4] * a[10] - a[6] * a[8]) +
                       a[2] * (a[4] * a[9] - a[5] * a[8]);
            if (d == 0 || d > 3.402823466e38 || d < -3.402823466e38)
                return false;
        }
        break;
    case 126: {
        int32_t k, m, flags[4];
        uint32_t j, knots;
        double previous = 0, minimum = 0, maximum = 0, v0, v1;
        if (!image_document_iges_integer(&q, &k) || !image_document_iges_integer(&q, &m) || k < 1 || k > 100000 ||
            m < 1 || m > 16 || m > k)
            return false;
        for (i = 0; i < 4; ++i)
            if (!image_document_iges_integer(&q, &flags[i]) || flags[i] < 0 || flags[i] > 1)
                return false;
        knots = (uint32_t)k + (uint32_t)m + 2;
        for (j = 0; j < knots; ++j) {
            double value;
            if (!image_document_iges_real(&q, &value) || (j && value < previous))
                return false;
            if (j == (uint32_t)m)
                minimum = value;
            if (j == (uint32_t)k + 1)
                maximum = value;
            previous = value;
        }
        if (maximum <= minimum)
            return false;
        for (j = 0; j < (uint32_t)k + 1; ++j) {
            double weight;
            if (!image_document_iges_real(&q, &weight) || weight <= 0)
                return false;
        }
        for (j = 0; j < ((uint32_t)k + 1) * 3; ++j)
            if (!image_document_iges_real(&q, &a[0]))
                return false;
        if (!image_document_iges_real(&q, &v0) || !image_document_iges_real(&q, &v1) || v0 < minimum - 1e-8 ||
            v1 > maximum + 1e-8 || v0 >= v1)
            return false;
        for (j = 0; j < 3; ++j)
            if (!image_document_iges_real(&q, &a[j]))
                return false;
        if (flags[0] && a[0] == 0 && a[1] == 0 && a[2] == 0)
            return false;
    } break;
    case 402:
        if (e->form != 1 || !image_document_iges_integer(&q, &v) || v < 1 || v > 1024)
            return false;
        for (i = 0; i < (unsigned)v; ++i) {
            int32_t ref;
            if (!image_document_iges_integer(&q, &ref) || ref < 0 || !image_document_iges_ref((uint32_t)ref, count))
                return false;
        }
        break;
    default:
        return false;
    }
    /* Optional zero associativity/property lists are fully framed. */
    if (!q.final) {
        for (i = 0; i < 2; ++i) {
            if (!image_document_iges_integer(&q, &v) || v)
                return false;
            if (q.final)
                break;
        }
    }
    return image_document_iges_end(&q);
}
static bool image_document_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    uint64_t p = 0, *offsets = NULL;
    uint32_t records = 0, sections[5] = {0}, first[5] = {0}, lastsection = 0, entities = 0, i, j;
    image_document_iges_entity *entry = NULL;
    uint8_t *global = NULL, *params = NULL;
    bool ok = false;
    char label[48];
    if (n < 405)
        return false;
    offsets = (uint64_t *)xx_mem_alloc(sizeof(*offsets) * 65536);
    if (!offsets)
        return false;
    while (p < n) {
        uint64_t at = p;
        uint32_t section, sequence;
        int32_t v;
        uint8_t c;
        if (records >= 65536 || !component_span(p, 80, n) || xx_component_parser_stopped(pd))
            goto done;
        for (j = 0; j < 80; ++j)
            if (b[p + j] < 32 || b[p + j] > 126)
                goto done;
        c = b[p + 72];
        section = c == 'S' ? 0 : c == 'G' ? 1 : c == 'D' ? 2 : c == 'P' ? 3 : c == 'T' ? 4 : 5;
        if (section > 4 || section < lastsection || (section > lastsection + 1) || (records == 0 && section))
            goto done;
        if (section != lastsection) {
            if (!sections[lastsection])
                goto done;
            first[section] = records;
            lastsection = section;
        }
        if (!image_document_iges_intfield(b + p + 73, 7, &v) || v < 1 ||
            (sequence = (uint32_t)v) != ++sections[section])
            goto done;
        offsets[records++] = at;
        p += 80;
        if (p == n || (b[p] != 10 && b[p] != 13))
            goto done;
        if (b[p] == 13) {
            ++p;
            if (p == n || b[p] != 10)
                goto done;
        }
        ++p;
    }
    if (lastsection != 4 || sections[4] != 1 || !sections[2] || sections[2] % 2 || (entities = sections[2] / 2) > 1024)
        goto done;
    {
        const uint8_t *t = b + offsets[first[4]];
        for (i = 0; i < 4; ++i) {
            int32_t v;
            if (t[i * 8] != (uint8_t)"SGDP"[i] || !image_document_iges_intfield(t + i * 8 + 1, 7, &v) ||
                v != (int32_t)sections[i])
                goto done;
        }
        if (!component_zero(t + 32, 0))
            goto done;
        for (i = 32; i < 72; ++i)
            if (t[i] != 32)
                goto done;
    }
    global = (uint8_t *)xx_mem_alloc((size_t)sections[1] * 72);
    entry = (image_document_iges_entity *)xx_mem_alloc(sizeof(*entry) * entities);
    if (!global || !entry)
        goto done;
    xx_mem_zero(entry, sizeof(*entry) * entities);
    for (i = 0; i < sections[1]; ++i)
        xx_rt_memcpy(global + i * 72, b + offsets[first[1] + i], 72);
    {
        image_document_iges_tokens q = {global, 0, (uint64_t)sections[1] * 72, pd, false};
        unsigned tokens = 0;
        bool empty, string;
        double value;
        while (!q.final) {
            uint64_t before = q.p;
            if (++tokens > 26 || !image_document_iges_token(&q, &value, NULL, &empty, &string))
                goto done;
            if (tokens <= 2) {
                if (!empty && !(string && q.p - before == 4 && global[before] == '1' && global[before + 1] == 'H' &&
                                global[before + 2] == (tokens == 1 ? ',' : ';')))
                    goto done;
            } else if (tokens == 7 || tokens == 8 || tokens == 9 || tokens == 10 || tokens == 11 || tokens == 14 ||
                       tokens == 16 || tokens == 23 || tokens == 24) {
                if (string || (!empty && (value < 0 || value > 2147483647 || value != (int32_t)value)))
                    goto done;
            }
        }
        if (tokens < 23 || !image_document_iges_end(&q))
            goto done;
    }
    if (!component_emit(f, s, "start-and-global.iges", 0, offsets[first[2]], n)) {
        goto done;
    }
    for (i = 0; i < entities; ++i) {
        const uint8_t *a = b + offsets[first[2] + i * 2], *d = b + offsets[first[2] + i * 2 + 1];
        int32_t fields[17], status;
        for (j = 0; j < 9; ++j)
            if (!image_document_iges_intfield(a + j * 8, 8, &fields[j]))
                goto done;
        for (j = 0; j < 5; ++j)
            if (!image_document_iges_intfield(d + j * 8, 8, &fields[j + 9]))
                goto done;
        if (fields[0] != fields[9] || fields[0] < 1 || fields[1] < 1 || fields[12] < 1 ||
            fields[12] > (int32_t)sections[3] || fields[13] < 0 || fields[13] > 63)
            goto done;
        entry[i].type = (uint32_t)fields[0];
        entry[i].first = (uint32_t)fields[1];
        entry[i].count = (uint32_t)fields[12];
        entry[i].form = (uint32_t)fields[13];
        if (entry[i].first > sections[3] || entry[i].count > sections[3] - entry[i].first + 1)
            goto done;
        if (entry[i].type != 100 && entry[i].type != 104 && entry[i].type != 110 && entry[i].type != 116 &&
            entry[i].type != 124 && entry[i].type != 126 && entry[i].type != 402)
            goto done;
        if (entry[i].type != 104 && entry[i].type != 402 && entry[i].form)
            goto done;
        if (entry[i].type == 104 && entry[i].form != 1 && entry[i].form != 2 && entry[i].form != 3)
            goto done;
        if (fields[2] || fields[3] < 0 || fields[3] > 5 || fields[4] < 0 || fields[5] || fields[7] || fields[10] < 0 ||
            fields[11] < 0 || fields[11] > 8 || fields[6] < 0)
            goto done;
        if (fields[6] && !image_document_iges_ref((uint32_t)fields[6], entities))
            goto done;
        entry[i].transform = (uint32_t)fields[6];
        status = fields[8];
        if (status < 0 || status / 1000000 > 1 || (status / 10000) % 100 > 3 || (status / 100) % 100 > 6 ||
            status % 100 > 2)
            goto done;
        for (j = 40; j < 56; ++j)
            if (d[j] != 32 && d[j] != '0')
                goto done;
        for (j = 56; j < 64; ++j)
            if (d[j] != 32 && !component_lexer_identifier_char(d[j]))
                goto done;
        {
            int32_t sub;
            if (!image_document_iges_intfield(d + 64, 8, &sub) || sub < 0)
                goto done;
        }
        xx_rt_snprintf(label, sizeof(label), "directory-%u-type-%u.iges", i * 2 + 1, entry[i].type);
        if (!component_emit(f, s, label, offsets[first[2] + i * 2],
                            offsets[first[2] + i * 2 + 2] - offsets[first[2] + i * 2], n))
            goto done;
    }
    {
        uint32_t next = 1;
        for (i = 0; i < entities; ++i) {
            image_document_iges_entity *e = &entry[i];
            if (e->first != next)
                goto done;
            next += e->count;
            if (e->transform && entry[(e->transform - 1) / 2].type != 124)
                goto done; /* Matrix references must be acyclic. */
            {
                uint32_t current = i, steps = 0;
                while (entry[current].transform) {
                    if (++steps > entities || xx_component_parser_stopped(pd))
                        goto done;
                    current = (entry[current].transform - 1) / 2;
                }
            }
            params = (uint8_t *)xx_mem_alloc((size_t)e->count * 64);
            if (!params)
                goto done;
            for (j = 0; j < e->count; ++j) {
                const uint8_t *v = b + offsets[first[3] + e->first - 1 + j];
                int32_t ptr;
                if (!image_document_iges_intfield(v + 64, 8, &ptr) || ptr != (int32_t)(i * 2 + 1))
                    goto done;
                xx_rt_memcpy(params + j * 64, v, 64);
            }
            if (!image_document_iges_entity_parse(e, params, (uint64_t)e->count * 64, entities, pd))
                goto done;
            xx_mem_free(params);
            params = NULL;
            xx_rt_snprintf(label, sizeof(label), "entity-%u-type-%u.iges", i * 2 + 1, e->type);
            if (!component_emit(f, s, label, offsets[first[3] + e->first - 1],
                                offsets[first[3] + e->first + e->count - 1] - offsets[first[3] + e->first - 1], n))
                goto done;
        }
        if (next != sections[3] + 1)
            goto done;
    }
    ok = component_emit(f, s, "termination.iges", offsets[first[4]], n - offsets[first[4]], n);
done:
    xx_mem_free(offsets);
    xx_mem_free(global);
    xx_mem_free(params);
    xx_mem_free(entry);
    return ok;
}

void xx_iges_model_init(xx_iges_model *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_IGES_MODEL, "igs");
    }
}
xx_iges_model *xx_iges_model_create(xx_io_device *d, int64_t at) {
    xx_iges_model *r = (xx_iges_model *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_iges_model_init(r, d, at);
    return r;
}
void xx_iges_model_destroy(xx_iges_model *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_iges_model_free(xx_iges_model *r) {
    if (r) {
        xx_iges_model_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_iges_model_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_iges_model_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
