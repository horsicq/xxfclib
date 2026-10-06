/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native PDF container reader, following ISO 32000-1 clauses 7.2--7.5 and
 * 7.11 (https://opensource.adobe.com/dc-acrobat-sdk-docs/standards/
 * pdfstandards/pdf/PDF32000_2008.pdf). _mylibs/XPDF supplies the attachment
 * naming and stream-extraction conventions. This reader does not use DIE,
 * execute document actions, or render pages.
 *
 * Cross-reference entries, not searches for object headers in binary data,
 * determine object boundaries. Newer revisions override older entries,
 * including freed objects. Stream extents always use a resolved /Length.
 */
#include "xxfclib/formats/pdf/xxpdf.h"
#include "xxpdf_decode.h"
#include "xxpdf_date.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/store/xx_store.h"
#include "../../io/platforms/xx_io_platform.h"

#define PDF_DEFAULT_LIMIT ((size_t)256U * 1024U * 1024U)
#define PDF_MAX_ENTRIES 262144U
#define PDF_MAX_NODES 1048576U
#define PDF_MAX_DEPTH 64U
#define PDF_MAX_FILTERS 8U
#define PDF_MAX_REVISIONS 64U
#define PDF_MAX_TEXT (1024U * 1024U)

typedef enum { PV_NULL, PV_INT, PV_OTHER, PV_NAME, PV_STRING, PV_ARRAY,
               PV_DICT, PV_REF } pdf_value_type;
typedef struct pdf_value {
    pdf_value_type type;
    int64_t integer;
    uint32_t generation;
    uint8_t *text;
    size_t size;
    uint8_t *raw;                 /* lexical hex/number token for DIE queries */
    size_t raw_size;
    bool hex_string;
    struct pdf_value *child, *next;
} pdf_value;
typedef struct {
    uint32_t id, generation, kind, rank;
    int64_t offset;
    uint32_t ordinal;
    pdf_value *value;
    int64_t stream_offset, stream_size, object_end;
    xx_pdf_filter_spec filters[PDF_MAX_FILTERS];
    size_t filter_count;
    char *attachment;
    uint64_t decoded_size;
    bool size_known, loaded, loading, objects_expanded;
} pdf_entry;
typedef struct pdf_document {
    Abstractformat *format;
    xx_pd_struct *pd;
    size_t limit, used, nodes;
    unsigned loading_depth;
    int64_t span, end, cache_start, latest_xref_end, header_offset;
    size_t cache_size;
    uint8_t cache[8192];
    pdf_entry *entries;
    size_t count, capacity;
    size_t *members;
    size_t member_count;
    int64_t visited[PDF_MAX_REVISIONS * 2U];
    size_t visited_count;
    uint32_t root_id, root_generation;
    uint32_t info_id, info_generation;
    pdf_value *encryption;
    bool has_info, recovered;
    bool has_root, encrypted, encryption_known;
} pdf_document;
typedef struct {
    pdf_document *doc;
    int64_t pos, end;
    const uint8_t *memory;
} pdf_cursor;
typedef union { size_t size; long double alignment; void *pointer; } pdf_alloc_header;
typedef struct { size_t index; } pdf_record_cursor;

static bool pdf_load(pdf_document *, pdf_entry *);
static pdf_value *pdf_resolve(pdf_document *, pdf_value *);

static void *pdf_alloc(pdf_document *d, size_t n) {
    pdf_alloc_header *p;
    if (!n) n = 1;
    if (n > SIZE_MAX - sizeof(*p) || d->used > d->limit ||
        n + sizeof(*p) > d->limit - d->used) return NULL;
    p = (pdf_alloc_header *)xx_mem_alloc(sizeof(*p) + n);
    if (!p) return NULL;
    p->size = n + sizeof(*p); d->used += p->size;
    xx_mem_zero(p + 1, n);
    return p + 1;
}
static void pdf_release(pdf_document *d, void *p) {
    pdf_alloc_header *h;
    if (!p) return;
    h = ((pdf_alloc_header *)p) - 1;
    d->used -= h->size; xx_mem_free(h);
}
static void *pdf_resize(pdf_document *d, void *p, size_t old, size_t n) {
    void *next = pdf_alloc(d, n);
    if (!next) return NULL;
    if (p) xx_rt_memcpy(next, p, old < n ? old : n);
    pdf_release(d, p); return next;
}
static bool pdf_read(pdf_document *d, int64_t offset, void *out, size_t n) {
    size_t done = 0;
    if (offset < 0 || (uint64_t)n > (uint64_t)d->span ||
        offset > d->span - (int64_t)n || xx_pd_is_stopped(d->pd) ||
        xx_io_seek64(d->format->device, d->format->base_address + offset, SEEK_SET)) return false;
    while (done < n) {
        ssize_t got = xx_io_read(d->format->device, (uint8_t *)out + done, n - done);
        if (got <= 0 || (size_t)got > n - done || xx_pd_is_stopped(d->pd)) return false;
        done += (size_t)got;
    }
    return true;
}
static int pdf_at(pdf_cursor *c, int64_t at) {
    pdf_document *d = c->doc;
    size_t n;
    if (at < 0 || at >= c->end || xx_pd_is_stopped(d->pd)) return -1;
    if (c->memory) return c->memory[(size_t)at];
    if (at < d->cache_start || at - d->cache_start >= (int64_t)d->cache_size) {
        d->cache_start = at - at % (int64_t)sizeof(d->cache);
        n = (size_t)((d->span - d->cache_start) < (int64_t)sizeof(d->cache) ?
                     d->span - d->cache_start : (int64_t)sizeof(d->cache));
        d->cache_size = 0;
        if (!pdf_read(d, d->cache_start, d->cache, n)) return -1;
        d->cache_size = n;
    }
    return d->cache[(size_t)(at - d->cache_start)];
}
static bool pdf_space(int ch) { return ch == 0 || ch == 9 || ch == 10 || ch == 12 || ch == 13 || ch == 32; }
static bool pdf_delimiter(int ch) {
    return ch < 0 || pdf_space(ch) || ch == '(' || ch == ')' || ch == '<' ||
           ch == '>' || ch == '[' || ch == ']' || ch == '{' || ch == '}' || ch == '/' || ch == '%';
}
static void pdf_skip(pdf_cursor *c) {
    int ch;
    while ((ch = pdf_at(c, c->pos)) >= 0) {
        if (pdf_space(ch)) { ++c->pos; continue; }
        if (ch != '%') break;
        while ((ch = pdf_at(c, c->pos)) >= 0 && ch != 10 && ch != 13) ++c->pos;
    }
}
static bool pdf_word(pdf_cursor *c, const char *s) {
    size_t i, n = xx_rt_strlen(s);
    pdf_skip(c);
    for (i = 0; i < n; ++i) if (pdf_at(c, c->pos + (int64_t)i) != (uint8_t)s[i]) return false;
    if (!pdf_delimiter(pdf_at(c, c->pos + (int64_t)n))) return false;
    c->pos += (int64_t)n; return true;
}
static bool pdf_uint(pdf_cursor *c, uint64_t *out) {
    uint64_t n = 0;
    int ch;
    bool digit = false;
    pdf_skip(c);
    if (pdf_at(c, c->pos) == '+') ++c->pos;
    while ((ch = pdf_at(c, c->pos)) >= '0' && ch <= '9') {
        if (n > (UINT64_MAX - (uint64_t)(ch - '0')) / 10U) return false;
        n = n * 10U + (uint64_t)(ch - '0'); ++c->pos; digit = true;
    }
    if (!digit || !pdf_delimiter(ch)) return false;
    *out = n; return true;
}
static int pdf_hex(int ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}
static void pdf_value_free(pdf_document *d, pdf_value *v) {
    while (v) {
        pdf_value *next = v->next;
        pdf_value_free(d, v->child); pdf_release(d, v->text); pdf_release(d, v->raw);
        pdf_release(d, v); --d->nodes; v = next;
    }
}
static bool pdf_text_add(pdf_document *d, pdf_value *v, size_t *cap, int ch) {
    if (v->size >= PDF_MAX_TEXT) return false;
    if (v->size + 1U >= *cap) {
        size_t n = *cap ? *cap * 2U : 32U;
        uint8_t *next = (uint8_t *)pdf_resize(d, v->text, v->size, n);
        if (!next) return false;
        v->text = next; *cap = n;
    }
    v->text[v->size++] = (uint8_t)ch; v->text[v->size] = 0; return true;
}
static bool pdf_raw(pdf_cursor *c, pdf_value *v, int64_t start) {
    size_t i;
    if (c->pos < start || (uint64_t)(c->pos - start) > PDF_MAX_TEXT) return false;
    v->raw_size = (size_t)(c->pos - start);
    v->raw = (uint8_t *)pdf_alloc(c->doc, v->raw_size + 1U);
    if (!v->raw) return false;
    for (i = 0; i < v->raw_size; ++i) {
        int ch = pdf_at(c, start + (int64_t)i);
        if (ch < 0) return false;
        v->raw[i] = (uint8_t)ch;
    }
    return true;
}
static pdf_value *pdf_parse_value(pdf_cursor *c, unsigned depth) {
    pdf_document *d = c->doc;
    pdf_value *v, **tail;
    int ch;
    int64_t start;
    size_t cap = 0;
    if (depth > PDF_MAX_DEPTH || d->nodes >= PDF_MAX_NODES || xx_pd_is_stopped(d->pd)) return NULL;
    pdf_skip(c); ch = pdf_at(c, c->pos);
    start = c->pos;
    if (ch < 0) return NULL;
    v = (pdf_value *)pdf_alloc(d, sizeof(*v));
    if (!v) return NULL;
    ++d->nodes;
    if (ch == '[' || (ch == '<' && pdf_at(c, c->pos + 1) == '<')) {
        bool dict = ch == '<';
        v->type = dict ? PV_DICT : PV_ARRAY; c->pos += dict ? 2 : 1; tail = &v->child;
        for (;;) {
            pdf_value *item;
            pdf_skip(c); ch = pdf_at(c, c->pos);
            if ((!dict && ch == ']') || (dict && ch == '>' && pdf_at(c, c->pos + 1) == '>')) {
                c->pos += dict ? 2 : 1; return v;
            }
            item = pdf_parse_value(c, depth + 1U);
            if (!item || (dict && item->type != PV_NAME)) { pdf_value_free(d, item); goto fail; }
            *tail = item; tail = &item->next;
            if (dict) {
                item = pdf_parse_value(c, depth + 1U);
                if (!item) goto fail;
                *tail = item; tail = &item->next;
            }
        }
    }
    if (ch == '/') {
        v->type = PV_NAME; ++c->pos;
        while (!pdf_delimiter(ch = pdf_at(c, c->pos))) {
            ++c->pos;
            if (ch == '#') {
                int h = pdf_hex(pdf_at(c, c->pos)), l = pdf_hex(pdf_at(c, c->pos + 1));
                if (h < 0 || l < 0) goto fail;
                ch = (h << 4) | l; c->pos += 2;
            }
            if (!pdf_text_add(d, v, &cap, ch)) goto fail;
        }
        if (!v->text && !pdf_text_add(d, v, &cap, 0)) goto fail;
        if (!v->size || v->text[0] == 0) v->size = 0;
        if (!pdf_raw(c, v, start)) goto fail;
        return v;
    }
    if (ch == '(') {
        unsigned nesting = 1;
        v->type = PV_STRING; ++c->pos;
        while (nesting) {
            ch = pdf_at(c, c->pos++);
            if (ch < 0) goto fail;
            if (ch == '\\') {
                unsigned i;
                ch = pdf_at(c, c->pos++);
                if (ch < 0) goto fail;
                if (ch == 10 || ch == 13) {
                    if (ch == 13 && pdf_at(c, c->pos) == 10) ++c->pos;
                    continue;
                }
                if (ch == 'n') ch = 10; else if (ch == 'r') ch = 13;
                else if (ch == 't') ch = 9; else if (ch == 'b') ch = 8; else if (ch == 'f') ch = 12;
                else if (ch >= '0' && ch <= '7') {
                    int oct = ch - '0';
                    for (i = 0; i < 2U && (ch = pdf_at(c, c->pos)) >= '0' && ch <= '7'; ++i) {
                        oct = oct * 8 + ch - '0'; ++c->pos;
                    }
                    ch = oct & 255;
                }
            } else if (ch == '(') {
                if (++nesting > PDF_MAX_DEPTH) goto fail;
            } else if (ch == ')') {
                if (--nesting == 0) break;
            } else if (ch == 13) {
                if (pdf_at(c, c->pos) == 10) ++c->pos;
                ch = 10;
            }
            if (!pdf_text_add(d, v, &cap, ch)) goto fail;
        }
        return v;
    }
    if (ch == '<') {
        int high = -1;
        v->type = PV_STRING; v->hex_string = true; ++c->pos;
        for (;;) {
            ch = pdf_at(c, c->pos++);
            if (ch == '>') break;
            if (ch < 0) goto fail;
            if (pdf_space(ch)) continue;
            ch = pdf_hex(ch); if (ch < 0) goto fail;
            if (high < 0) high = ch;
            else { if (!pdf_text_add(d, v, &cap, (high << 4) | ch)) goto fail; high = -1; }
        }
        if (high >= 0 && !pdf_text_add(d, v, &cap, high << 4)) goto fail;
        if (!pdf_raw(c, v, start)) goto fail;
        return v;
    }
    if ((ch >= '0' && ch <= '9') || ch == '+' || ch == '-') {
        int64_t start = c->pos;
        bool negative = ch == '-';
        uint64_t n, generation;
        if (negative) ++c->pos;
        if (pdf_uint(c, &n) && n <= INT64_MAX) {
            int64_t after = c->pos;
            v->type = PV_INT; v->integer = negative ? -(int64_t)n : (int64_t)n;
            if (!negative && n <= INT32_MAX && pdf_uint(c, &generation) && generation <= 65535U && pdf_word(c, "R")) {
                v->type = PV_REF; v->generation = (uint32_t)generation;
            } else c->pos = after;
            if (!pdf_raw(c, v, start)) goto fail;
            return v;
        }
        c->pos = start;
    }
    if (pdf_word(c, "null")) { v->type = PV_NULL; return v; }
    if (pdf_word(c, "true") || pdf_word(c, "false")) {
        v->type = PV_OTHER; if (!pdf_raw(c, v, start)) goto fail; return v;
    }
    /* Real numbers have no bearing on stream extents, but are legal values. */
    { bool digit = false, dot = false; size_t n = 0;
      ch = pdf_at(c, c->pos); if (ch == '+' || ch == '-') { ++c->pos; ++n; }
      while ((ch = pdf_at(c, c->pos)) >= 0 && !pdf_delimiter(ch)) {
          if (ch >= '0' && ch <= '9') digit = true;
          else if (ch == '.' && !dot) dot = true;
          else goto fail;
          if (++n > 64U) goto fail;
          ++c->pos;
      }
      if (digit && dot) { v->type = PV_OTHER; if (!pdf_raw(c, v, start)) goto fail; return v; } }
fail:
    pdf_value_free(d, v); return NULL;
}
static bool pdf_name(pdf_value *v, const char *s) {
    size_t n = xx_rt_strlen(s);
    return v && v->type == PV_NAME && v->size == n && (!n || !xx_rt_memcmp(v->text, s, n));
}
static pdf_value *pdf_key(pdf_value *dict, const char *key) {
    pdf_value *p, *result = NULL;
    if (!dict || dict->type != PV_DICT) return NULL;
    for (p = dict->child; p && p->next; p = p->next->next)
        if (pdf_name(p, key)) result = p->next;
    return result;
}
static bool pdf_integer(pdf_value *v, uint64_t *n) {
    if (!v || v->type != PV_INT || v->integer < 0) return false;
    *n = (uint64_t)v->integer; return true;
}
static pdf_entry *pdf_lookup(pdf_document *d, uint32_t id) {
    size_t low = 0, high = d->count;
    while (low < high) { size_t mid = low + (high - low) / 2U;
        if (d->entries[mid].id < id) low = mid + 1U; else high = mid; }
    return low < d->count && d->entries[low].id == id ? &d->entries[low] : NULL;
}
static pdf_value *pdf_resolve(pdf_document *d, pdf_value *v) {
    unsigned i;
    for (i = 0; v && v->type == PV_REF && i < PDF_MAX_DEPTH; ++i) {
        pdf_entry *e = pdf_lookup(d, (uint32_t)v->integer);
        if (!e || !e->kind || e->generation != v->generation || !pdf_load(d, e)) return NULL;
        v = e->value;
    }
    return v && v->type != PV_REF ? v : NULL;
}
static bool pdf_resolved_int(pdf_document *d, pdf_value *v, uint64_t *n) {
    return pdf_integer(pdf_resolve(d, v), n);
}
static bool pdf_filters(pdf_document *d, pdf_value *dict, pdf_entry *e, bool direct) {
    pdf_value *filter = pdf_key(dict, "Filter"), *params = pdf_key(dict, "DecodeParms"), *p;
    size_t i = 0;
    if (!direct) {
        pdf_value *resolved = pdf_resolve(d, filter);
        if (filter && filter->type != PV_NULL && !resolved) return false;
        filter = resolved; resolved = pdf_resolve(d, params);
        if (params && params->type != PV_NULL && !resolved) return false;
        params = resolved;
    }
    if (!filter || filter->type == PV_NULL) { e->filter_count = 0; return true; }
    p = filter->type == PV_ARRAY ? filter->child : filter;
    for (; p; p = filter->type == PV_ARRAY ? p->next : NULL) {
        xx_pdf_filter_spec *s;
        pdf_value *item = direct ? p : pdf_resolve(d, p), *dp = params;
        uint64_t n = 0;
        const char *keys[] = { "Predictor", "Colors", "BitsPerComponent", "Columns", "EarlyChange" };
        uint32_t *values[5]; size_t k;
        if (i >= PDF_MAX_FILTERS || !item || item->type != PV_NAME) return false;
        s = &e->filters[i]; xx_mem_zero(s, sizeof(*s));
        s->params.predictor = 1; s->params.colors = 1; s->params.bits_per_component = 8;
        s->params.columns = 1; s->params.early_change = 1;
        s->filter = XX_PDF_FILTER_UNSUPPORTED;
        if (pdf_name(item, "FlateDecode") || pdf_name(item, "Fl")) s->filter = XX_PDF_FILTER_FLATE;
        else if (pdf_name(item, "LZWDecode") || pdf_name(item, "LZW")) s->filter = XX_PDF_FILTER_LZW;
        else if (pdf_name(item, "ASCII85Decode") || pdf_name(item, "A85")) s->filter = XX_PDF_FILTER_ASCII85;
        else if (pdf_name(item, "ASCIIHexDecode") || pdf_name(item, "AHx")) s->filter = XX_PDF_FILTER_ASCIIHEX;
        else if (pdf_name(item, "RunLengthDecode") || pdf_name(item, "RL")) s->filter = XX_PDF_FILTER_RUNLENGTH;
        else if (pdf_name(item, "DCTDecode") || pdf_name(item, "DCT")) s->filter = XX_PDF_FILTER_DCT;
        else if (pdf_name(item, "JPXDecode")) s->filter = XX_PDF_FILTER_JPX;
        if (params && params->type == PV_ARRAY) {
            size_t j; dp = params->child; for (j = 0; dp && j < i; ++j) dp = dp->next;
        } else if (params && params->type != PV_NULL && filter->type == PV_ARRAY && i) return false;
        if (!direct) {
            pdf_value *resolved = pdf_resolve(d, dp);
            if (dp && dp->type != PV_NULL && !resolved) return false;
            dp = resolved;
        }
        if (dp && dp->type != PV_NULL) {
            if (dp->type != PV_DICT) return false;
            values[0] = &s->params.predictor; values[1] = &s->params.colors;
            values[2] = &s->params.bits_per_component; values[3] = &s->params.columns; values[4] = &s->params.early_change;
            for (k = 0; k < 5U; ++k) {
                pdf_value *v = pdf_key(dp, keys[k]);
                if (v && (!(direct ? pdf_integer(v, &n) : pdf_resolved_int(d, v, &n)) || n > UINT32_MAX)) return false;
                if (v) *values[k] = (uint32_t)n;
            }
        }
        ++i;
    }
    e->filter_count = i; return i != 0;
}
static bool pdf_indirect(pdf_document *d, int64_t offset, pdf_entry *e, bool direct) {
    pdf_cursor c = { d, offset, d->end, NULL };
    uint64_t id, gen, length;
    int ch;
    if (!pdf_uint(&c, &id) || !id || id > INT32_MAX || !pdf_uint(&c, &gen) || gen > 65535U ||
        !pdf_word(&c, "obj") || (e->id && (id != e->id || gen != e->generation))) return false;
    e->id = (uint32_t)id; e->generation = (uint32_t)gen;
    e->value = pdf_parse_value(&c, 0); e->stream_offset = -1;
    if (!e->value) return false;
    if (e->value->type == PV_DICT && pdf_word(&c, "stream")) {
        /* The stream keyword is followed by CRLF or LF, not arbitrary space. */
        ch = pdf_at(&c, c.pos++);
        if (ch == 13) { if (pdf_at(&c, c.pos) == 10) ++c.pos; else return false; }
        else if (ch != 10) return false;
        if (!(direct ? pdf_integer(pdf_key(e->value, "Length"), &length) :
              pdf_resolved_int(d, pdf_key(e->value, "Length"), &length)) || length > INT64_MAX ||
            (int64_t)length > c.end - c.pos) return false;
        e->stream_offset = c.pos; e->stream_size = (int64_t)length; c.pos += (int64_t)length;
        ch = pdf_at(&c, c.pos);
        if (ch == 13) { ++c.pos; if (pdf_at(&c, c.pos) == 10) ++c.pos; }
        else if (ch == 10) ++c.pos;
        /* Do not skip arbitrary whitespace here: a bad /Length must fail. */
        if (pdf_at(&c, c.pos) != 'e' || !pdf_word(&c, "endstream") || !pdf_filters(d, e->value, e, direct)) return false;
    }
    if (!pdf_word(&c, "endobj")) return false;
    e->object_end = c.pos;
    return true;
}
static bool pdf_decode(pdf_document *d, pdf_entry *e, size_t output_limit,
                       size_t memory_limit, uint8_t **out, size_t *size, size_t *capacity) {
    uint8_t *input = NULL;
    bool ok = false;
    *out = NULL; *size = 0; *capacity = 0;
    if (e->stream_offset < 0 || (uint64_t)e->stream_size > SIZE_MAX ||
        (uint64_t)e->stream_size > memory_limit || d->used > memory_limit ||
        (uint64_t)e->stream_size > memory_limit - d->used) return false;
    if (e->stream_size) {
        input = (uint8_t *)pdf_alloc(d, (size_t)e->stream_size);
        if (!input || !pdf_read(d, e->stream_offset, input, (size_t)e->stream_size)) goto done;
    }
    if (d->used > memory_limit) goto done;
    ok = xx_pdf_decode_stream_ex(input, (size_t)e->stream_size, e->filters, e->filter_count,
                             output_limit, memory_limit - d->used, d->pd, out, size, capacity);
done:
    pdf_release(d, input); return ok;
}
static bool pdf_add_entry(pdf_document *d, uint32_t id, uint32_t gen, uint32_t kind,
                          uint64_t offset, uint32_t ordinal, uint32_t rank) {
    pdf_entry *e;
    if (id > INT32_MAX || gen > 65535U || kind > 2U || offset > INT64_MAX || d->count >= PDF_MAX_ENTRIES ||
        (kind == 1U && (!offset || offset >= (uint64_t)d->end))) return false;
    if (d->count == d->capacity) {
        size_t n = d->capacity ? d->capacity * 2U : 256U;
        pdf_entry *next = (pdf_entry *)pdf_resize(d, d->entries, d->count * sizeof(*e), n * sizeof(*e));
        if (!next) return false;
        d->entries = next; d->capacity = n;
    }
    e = &d->entries[d->count++]; e->id = id; e->generation = gen;
    e->kind = kind; e->offset = (int64_t)offset; e->ordinal = ordinal; e->rank = rank; e->stream_offset = -1;
    return true;
}
static bool pdf_xref(pdf_document *, int64_t, unsigned, bool);
static bool pdf_trailer(pdf_document *d, pdf_value *dict, unsigned revision, bool hybrid) {
    pdf_value *v;
    uint64_t offset;
    if (!dict || dict->type != PV_DICT) return false;
    v = pdf_key(dict, "Root");
    if (!d->has_root && v) {
        if (v->type != PV_REF || v->integer <= 0) return false;
        d->has_root = true; d->root_id = (uint32_t)v->integer; d->root_generation = v->generation;
    }
    v = pdf_key(dict, "Encrypt");
    if (!d->encryption_known && v) {
        d->encrypted = v->type != PV_NULL; d->encryption_known = true;
        d->encryption = (pdf_value *)pdf_alloc(d, sizeof(*v));
        if (!d->encryption) return false;
        ++d->nodes; *d->encryption = *v; d->encryption->next = NULL;
        /* Transfer the trailer value before its temporary dictionary is freed. */
        v->child = NULL; v->text = NULL; v->raw = NULL; v->type = PV_NULL;
    }
    v = pdf_key(dict, "Info");
    if (!d->has_info && v && v->type == PV_REF) {
        d->has_info = true; d->info_id = (uint32_t)v->integer; d->info_generation = v->generation;
    }
    v = pdf_key(dict, "XRefStm");
    if (!hybrid && v) {
        if (!pdf_integer(v, &offset) || offset >= (uint64_t)d->end ||
            !pdf_xref(d, (int64_t)offset, revision, true)) return false;
    }
    v = pdf_key(dict, "Prev");
    if (!hybrid && v) {
        if (!pdf_integer(v, &offset) || offset >= (uint64_t)d->end ||
            !pdf_xref(d, (int64_t)offset, revision + 1U, false)) return false;
    }
    return true;
}
static bool pdf_xref(pdf_document *d, int64_t offset, unsigned revision, bool hybrid) {
    pdf_cursor c = { d, offset, d->end, NULL };
    pdf_value *dict = NULL;
    size_t i;
    bool ok = false;
    if (offset < 0 || offset >= d->end || revision >= PDF_MAX_REVISIONS ||
        d->visited_count >= PDF_MAX_REVISIONS * 2U || xx_pd_is_stopped(d->pd)) return false;
    for (i = 0; i < d->visited_count; ++i) if (d->visited[i] == offset) return false;
    d->visited[d->visited_count++] = offset;
    if (pdf_word(&c, "xref")) {
        if (hybrid) return false;
        while (!pdf_word(&c, "trailer")) {
            uint64_t first, count, j;
            if (!pdf_uint(&c, &first) || first > INT32_MAX || !pdf_uint(&c, &count) ||
                count > PDF_MAX_ENTRIES - d->count || count > (uint64_t)INT32_MAX + 1U - first) goto done;
            for (j = 0; j < count; ++j) {
                uint64_t position, generation; uint32_t kind;
                if (!pdf_uint(&c, &position) || !pdf_uint(&c, &generation) || generation > 65535U) goto done;
                if (pdf_word(&c, "n")) kind = 1;
                else if (pdf_word(&c, "f")) kind = 0;
                else goto done;
                if (!pdf_add_entry(d, (uint32_t)(first + j), (uint32_t)generation, kind,
                                   position, 0, revision * 2U + 1U)) goto done;
            }
        }
        dict = pdf_parse_value(&c, 0);
        if (!revision) d->latest_xref_end = c.pos;
        ok = pdf_trailer(d, dict, revision, false);
    } else {
        pdf_entry e;
        pdf_value *w, *index, *p;
        uint64_t width[3], total, expected = 0;
        uint8_t *data = NULL; size_t size = 0, capacity = 0, at = 0;
        xx_mem_zero(&e, sizeof(e));
        if (!pdf_indirect(d, offset, &e, true)) { pdf_value_free(d, e.value); goto done; }
        dict = e.value;
        if (!revision && !hybrid) d->latest_xref_end = e.object_end;
        if (!pdf_name(pdf_key(dict, "Type"), "XRef") || e.stream_offset < 0 ||
            !pdf_integer(pdf_key(dict, "Size"), &total) || !total || total > (uint64_t)INT32_MAX + 1U) goto done;
        w = pdf_key(dict, "W");
        if (!w || w->type != PV_ARRAY) goto done;
        p = w->child;
        for (i = 0; i < 3U; ++i) { if (!p || !pdf_integer(p, &width[i]) || width[i] > 8U) goto done; p = p->next; }
        if (p || !(width[0] + width[1] + width[2])) goto done;
        index = pdf_key(dict, "Index");
        if (index && index->type != PV_ARRAY) goto done;
        p = index ? index->child : NULL;
        do {
            uint64_t first = 0, count = total;
            if (index) {
                if (!p || !p->next || !pdf_integer(p, &first) || !pdf_integer(p->next, &count)) goto done;
                p = p->next->next;
            }
            if (first > total || count > total - first || count > PDF_MAX_ENTRIES ||
                expected > PDF_MAX_ENTRIES - count) goto done;
            expected += count;
        } while (p);
        if (expected > (PDF_MAX_ENTRIES - d->count) ||
            !pdf_decode(d, &e, (size_t)(expected * (width[0] + width[1] + width[2])), d->limit, &data, &size, &capacity)) goto done;
        if ((uint64_t)size != expected * (width[0] + width[1] + width[2])) { xx_mem_free(data); goto done; }
        if (capacity > d->limit - d->used) { xx_mem_free(data); goto done; }
        d->used += capacity;
        p = index ? index->child : NULL;
        do {
            uint64_t first = 0, count = total, j;
            if (index) { (void)pdf_integer(p, &first); (void)pdf_integer(p->next, &count); p = p->next->next; }
            for (j = 0; j < count; ++j) {
                uint64_t fields[3] = { width[0] ? 0U : 1U, 0U, 0U }; size_t k, b;
                for (k = 0; k < 3U; ++k) for (b = 0; b < (size_t)width[k]; ++b) fields[k] = (fields[k] << 8) | data[at++];
                if (fields[0] > 2U || fields[2] > UINT32_MAX ||
                    (fields[0] != 2U && fields[2] > 65535U) ||
                    (fields[0] == 2U && (!fields[1] || fields[1] > INT32_MAX)) ||
                    !pdf_add_entry(d, (uint32_t)(first + j), fields[0] == 2U ? 0U : (uint32_t)fields[2],
                                   (uint32_t)fields[0], fields[1], (uint32_t)fields[2], revision * 2U)) {
                    d->used -= capacity; xx_mem_free(data); goto done;
                }
            }
        } while (p);
        d->used -= capacity; xx_mem_free(data); ok = pdf_trailer(d, dict, revision, hybrid);
    }
done:
    pdf_value_free(d, dict); return ok;
}
static int pdf_entry_compare(const void *a, const void *b) {
    const pdf_entry *x = (const pdf_entry *)a, *y = (const pdf_entry *)b;
    if (x->id != y->id) return x->id < y->id ? -1 : 1;
    return x->rank < y->rank ? -1 : x->rank > y->rank ? 1 : 0;
}
static bool pdf_compact(pdf_document *d) {
    size_t i, n = 0;
    xx_rt_qsort(d->entries, d->count, sizeof(*d->entries), pdf_entry_compare);
    /* Validate before moving owned AST pointers: failure must leave every
     * allocation with a single owner for document cleanup. */
    for (i = 1; i < d->count; ++i) {
        pdf_entry *a = &d->entries[i - 1U], *b = &d->entries[i];
        if (a->id == b->id && a->rank == b->rank && (a->kind != b->kind || a->offset != b->offset ||
            a->generation != b->generation || a->ordinal != b->ordinal)) return false;
    }
    for (i = 0; i < d->count; ++i) {
        if (n && d->entries[n - 1U].id == d->entries[i].id) {
            pdf_entry *b = &d->entries[i];
            pdf_value_free(d, b->value); pdf_release(d, b->attachment);
            b->value = NULL; b->attachment = NULL;
            continue;
        }
        if (n != i) { d->entries[n] = d->entries[i]; d->entries[i].value = NULL; d->entries[i].attachment = NULL; }
        ++n;
    }
    d->count = n; return true;
}
static void pdf_discard_recovered_members(pdf_document *d, uint32_t container_id) {
    size_t i, n = 0;
    for (i = 0; i < d->count; ++i) {
        pdf_entry *e = &d->entries[i];
        if (e->kind == 2U && e->offset == container_id) {
            pdf_value_free(d, e->value); pdf_release(d, e->attachment);
            e->value = NULL; e->attachment = NULL; continue;
        }
        if (n != i) { d->entries[n] = *e; e->value = NULL; e->attachment = NULL; }
        ++n;
    }
    d->count = n;
}
static bool pdf_object_stream(pdf_document *d, pdf_entry *container) {
    uint64_t count, first;
    uint8_t *data = NULL; size_t size = 0, capacity = 0, i;
    uint32_t *ids = NULL; int64_t *positions = NULL;
    pdf_cursor c;
    bool ok = false;
    uint32_t container_id = container->id;
    if (container->objects_expanded) return true;
    if (d->encrypted || !pdf_load(d, container) ||
        !pdf_name(pdf_key(container->value, "Type"), "ObjStm") ||
        !pdf_resolved_int(d, pdf_key(container->value, "N"), &count) || !count || count > PDF_MAX_ENTRIES ||
        !pdf_resolved_int(d, pdf_key(container->value, "First"), &first) ||
        !pdf_decode(d, container, d->limit / 2U, d->limit, &data, &size, &capacity) || first > size) goto done;
    /* Charge retained decoded payload while allocating its object ASTs. */
    if (capacity > d->limit - d->used) goto done;
    d->used += capacity;
    ids = (uint32_t *)pdf_alloc(d, (size_t)count * sizeof(*ids));
    positions = (int64_t *)pdf_alloc(d, (size_t)count * sizeof(*positions));
    c.doc = d; c.pos = 0; c.end = (int64_t)first; c.memory = data;
    if (!ids || !positions) goto charged_done;
    for (i = 0; i < (size_t)count; ++i) {
        uint64_t id, pos;
        if (!pdf_uint(&c, &id) || !id || id > INT32_MAX || !pdf_uint(&c, &pos) ||
            pos >= size - first || (i && pos <= (uint64_t)positions[i - 1U])) goto charged_done;
        ids[i] = (uint32_t)id; positions[i] = (int64_t)pos;
    }
    pdf_skip(&c); if (c.pos != c.end) goto charged_done;
    if (d->recovered) {
        /* With no usable xref, recover the declared members of an ObjStm.
         * Direct objects always win over this fallback index. */
        for (i = 0; i < (size_t)count; ++i)
            if (!pdf_add_entry(d, ids[i], 0, 2, container_id, (uint32_t)i, UINT32_MAX)) goto charged_done;
        if (!pdf_compact(d)) goto charged_done;
    }
    for (i = 0; i < (size_t)count; ++i) {
        pdf_entry *e = pdf_lookup(d, ids[i]);
        if (!e || e->kind != 2U || e->offset != container_id || e->ordinal != i) continue;
        if (e->loaded) continue;
        c.pos = (int64_t)first + positions[i];
        c.end = i + 1U < (size_t)count ? (int64_t)first + positions[i + 1U] : (int64_t)size;
        e->value = pdf_parse_value(&c, 0);
        pdf_skip(&c);
        if (!e->value || c.pos != c.end) goto charged_done;
        e->loaded = true;
    }
    ok = !xx_pd_is_stopped(d->pd);
    if (ok) { pdf_entry *e = pdf_lookup(d, container_id); if (e) e->objects_expanded = true; }
charged_done:
    /* Recovery must publish an entire validated ObjStm or none of it.
     * Roll back partial appended entries, retaining the sorted direct index. */
    if (!ok && d->recovered) pdf_discard_recovered_members(d, container_id);
    pdf_release(d, ids); pdf_release(d, positions); d->used -= capacity;
done:
    xx_mem_free(data); return ok;
}
static bool pdf_load(pdf_document *d, pdf_entry *e) {
    bool ok;
    if (!e || !e->kind || xx_pd_is_stopped(d->pd)) return false;
    if (e->loaded) return true;
    /* Recovery expands object streams once, before exposing the index. A
     * failed or absent compressed member cannot trigger index mutation from
     * a lazy query holding an entry pointer. */
    if (d->recovered) return false;
    if (e->loading || d->loading_depth >= PDF_MAX_DEPTH) return false;
    ++d->loading_depth;
    e->loading = true;
    if (e->kind == 1U) ok = pdf_indirect(d, e->offset, e, false);
    else {
        pdf_entry *container = pdf_lookup(d, (uint32_t)e->offset);
        ok = container && container->kind == 1U && pdf_object_stream(d, container) && e->loaded;
    }
    e->loading = false;
    --d->loading_depth;
    if (ok) e->loaded = true;
    return ok;
}
static void pdf_document_free(pdf_document *d) {
    size_t i;
    if (!d) return;
    for (i = 0; i < d->count; ++i) { pdf_value_free(d, d->entries[i].value); pdf_release(d, d->entries[i].attachment); }
    pdf_value_free(d, d->encryption);
    pdf_release(d, d->entries); pdf_release(d, d->members); xx_mem_free(d);
}
static bool pdf_footer(pdf_document *d, int64_t marker, int64_t *xref, int64_t *end, int64_t *startxref) {
    pdf_cursor c = { d, marker, d->span, NULL };
    int64_t p = marker - 1, number_end, number_start, keyword;
    uint64_t n;
    size_t i;
    if (marker < 12 || !pdf_space(pdf_at(&c, marker - 1))) return false;
    for (i = 0; i < 5U; ++i) if (pdf_at(&c, marker + (int64_t)i) != (uint8_t)"%%EOF"[i]) return false;
    if (!pdf_delimiter(pdf_at(&c, marker + 5))) return false;
    while (p >= 0 && pdf_space(pdf_at(&c, p))) --p;
    number_end = p + 1;
    while (p >= 0 && pdf_at(&c, p) >= '0' && pdf_at(&c, p) <= '9') --p;
    number_start = p + 1;
    if (number_start == number_end || number_end - number_start > 19 || !pdf_space(pdf_at(&c, p))) return false;
    while (p >= 0 && pdf_space(pdf_at(&c, p))) --p;
    keyword = p - 8;
    if (keyword < 0 || marker - keyword > 128 || (keyword && !pdf_delimiter(pdf_at(&c, keyword - 1)))) return false;
    for (i = 0; i < 9U; ++i) if (pdf_at(&c, keyword + (int64_t)i) != (uint8_t)"startxref"[i]) return false;
    c.pos = number_start; if (!pdf_uint(&c, &n) || n >= (uint64_t)keyword || !n) return false;
    *xref = (int64_t)n; *end = marker + 5; *startxref = keyword;
    if (pdf_at(&c, *end) == 13) { ++*end; if (pdf_at(&c, *end) == 10) ++*end; }
    else if (pdf_at(&c, *end) == 10) ++*end;
    return true;
}
static bool pdf_utf8_add(char *out, size_t capacity, size_t *n, uint32_t cp) {
    size_t bytes = cp < 0x80 ? 1U : cp < 0x800 ? 2U : cp < 0x10000 ? 3U : 4U;
    if (*n + bytes >= capacity || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    if (bytes == 1U) out[(*n)++] = (char)cp;
    else {
        if (bytes == 2U) out[(*n)++] = (char)(0xC0 | (cp >> 6));
        else if (bytes == 3U) { out[(*n)++] = (char)(0xE0 | (cp >> 12)); out[(*n)++] = (char)(0x80 | ((cp >> 6) & 63)); }
        else { out[(*n)++] = (char)(0xF0 | (cp >> 18)); out[(*n)++] = (char)(0x80 | ((cp >> 12) & 63)); out[(*n)++] = (char)(0x80 | ((cp >> 6) & 63)); }
        out[(*n)++] = (char)(0x80 | (cp & 63));
    }
    out[*n] = 0; return true;
}
static char *pdf_attachment_name(pdf_document *d, pdf_value *v, uint32_t id) {
    char text[1024], name[1100]; size_t n = 0, i = 0, base = 0;
    bool wide = false, little = false;
    char *result; size_t length;
    v = pdf_resolve(d, v);
    if (!v || v->type != PV_STRING || !v->size) return NULL;
    if (v->size >= 2U && ((v->text[0] == 0xFE && v->text[1] == 0xFF) ||
                         (v->text[0] == 0xFF && v->text[1] == 0xFE))) {
        wide = true; little = v->text[0] == 0xFF; i = 2;
        if ((v->size - i) % 2U) return NULL;
    }
    if (!wide && v->size >= 3U && !xx_rt_memcmp(v->text, "\xEF\xBB\xBF", 3)) {
        i = 3; if (v->size - i >= sizeof(text)) return NULL;
        for (; i < v->size; ++i) { text[n++] = (char)v->text[i]; } text[n] = 0;
    } else {
        for (; i < v->size; ++i) {
            uint32_t cp = v->text[i];
            if (wide) {
                cp = little ? v->text[i] | ((uint32_t)v->text[i + 1U] << 8) :
                              ((uint32_t)v->text[i] << 8) | v->text[i + 1U]; ++i;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    uint32_t low; if (i + 2U >= v->size) return NULL;
                    low = little ? v->text[i + 1U] | ((uint32_t)v->text[i + 2U] << 8) :
                                   ((uint32_t)v->text[i + 1U] << 8) | v->text[i + 2U];
                    if (low < 0xDC00 || low > 0xDFFF) return NULL;
                    cp = 0x10000U + ((cp - 0xD800U) << 10) + low - 0xDC00U; i += 2;
                }
            }
            if (cp < 32U || cp == 127U) cp = '_';
            if (!pdf_utf8_add(text, sizeof(text), &n, cp)) return NULL;
        }
    }
    for (i = 0; i < n; ++i) if (text[i] == '/' || text[i] == '\\') base = i + 1U;
    if (base == n) return NULL;
    for (i = base; i < n; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (ch < 32 || ch == 127 || text[i] == ':' || text[i] == '*' || text[i] == '?' ||
            text[i] == '"' || text[i] == '<' || text[i] == '>' || text[i] == '|') text[i] = '_';
    }
    while (n > base && (text[n - 1U] == '.' || text[n - 1U] == ' ')) text[--n] = 0;
    if (base == n || !xx_rt_strcmp(text + base, ".") || !xx_rt_strcmp(text + base, "..")) return NULL;
    /* Object prefix gives aliases/colliding basenames distinct destinations. */
    (void)xx_rt_snprintf(name, sizeof(name), "attachments/%u-%s", (unsigned)id, text + base);
    length = xx_rt_strlen(name); result = (char *)pdf_alloc(d, length + 1U);
    if (result) xx_rt_memcpy(result, name, length + 1U);
    return result;
}
static void pdf_inventory_filespec(pdf_document *d, pdf_value *dict) {
    static const char *keys[] = { "UF", "F", "DOS", "Mac", "Unix" };
    pdf_value *ef;
    size_t i;
    if (!dict || dict->type != PV_DICT ||
        !(ef = pdf_resolve(d, pdf_key(dict, "EF"))) || ef->type != PV_DICT) return;
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        pdf_value *ref = pdf_key(ef, keys[i]), *name;
        pdf_entry *stream;
        /* An optional file-system mapping can be absent or unusable without
         * invalidating the document or its other embedded file mappings. */
        if (!ref || ref->type != PV_REF || ref->integer <= 0 || ref->integer > UINT32_MAX ||
            !(stream = pdf_lookup(d, (uint32_t)ref->integer)) || !stream->kind ||
            stream->generation != ref->generation || stream->stream_offset < 0 || stream->attachment) continue;
        name = pdf_key(dict, "UF");
        if (!name) name = pdf_key(dict, keys[i]);
        if (!name) name = pdf_key(dict, "F");
        stream->attachment = pdf_attachment_name(d, name, stream->id);
    }
}
static bool pdf_inventory(pdf_document *d) {
    size_t i;
    for (i = 0; i < d->count; ++i) {
        pdf_entry *e = &d->entries[i];
        if (!e->kind || (!e->id && !e->kind)) continue;
        if (e->kind == 2U && d->encrypted) continue;
        if (!pdf_load(d, e)) return false;
    }
    if (!d->encrypted) {
        for (i = 0; i < d->count; ++i) {
            if (xx_pd_is_stopped(d->pd)) return false;
            pdf_inventory_filespec(d, d->entries[i].value);
        }
    }
    d->members = (size_t *)pdf_alloc(d, (d->count ? d->count : 1U) * sizeof(*d->members));
    if (!d->members) return false;
    for (i = 0; i < d->count; ++i) {
        pdf_entry *e = &d->entries[i]; uint64_t size;
        if (!e->loaded || e->stream_offset < 0) continue;
        if (d->member_count >= 65535U) return false;
        d->members[d->member_count++] = i;
        if (!e->filter_count) { e->size_known = true; e->decoded_size = (uint64_t)e->stream_size; }
        else {
            pdf_value *params = pdf_resolve(d, pdf_key(e->value, "Params"));
            if (params && pdf_resolved_int(d, pdf_key(params, "Size"), &size)) { e->size_known = true; e->decoded_size = size; }
        }
    }
    return true;
}
static pdf_document *pdf_parse(Abstractformat *f, xx_pd_struct *pd) {
    pdf_document *d;
    uint8_t header[9];
    int64_t total, marker;
    size_t limit = PDF_DEFAULT_LIMIT, attempts = 0;
    const xx_var *option;
    if (!f || !f->device || f->base_address < 0 || xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_size(f->device);
    if (total < f->base_address || total - f->base_address < 20) return NULL;
    option = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    if (option && xx_var_get_u64(option) < limit) limit = (size_t)xx_var_get_u64(option);
    if (limit < sizeof(*d)) return NULL;
    d = (pdf_document *)xx_mem_alloc(sizeof(*d)); if (!d) return NULL;
    xx_mem_zero(d, sizeof(*d)); d->format = f; d->pd = pd; d->limit = limit;
    d->used = sizeof(*d); d->span = total - f->base_address; d->end = d->span; d->cache_start = -1;
    if (!pdf_read(d, 0, header, sizeof(header)) || !xx_pdf_check_magic(header, sizeof(header))) goto fail;
    for (marker = d->span - 5; marker >= 9; --marker) {
        pdf_cursor c = { d, marker, d->span, NULL };
        int64_t xref, end, startxref;
        bool xref_ok;
        if (xx_pd_is_stopped(pd)) goto fail;
        if (pdf_at(&c, marker) != '%' || pdf_at(&c, marker + 1) != '%' || !pdf_footer(d, marker, &xref, &end, &startxref)) continue;
        if (++attempts > 128U) goto fail;
        d->end = end; d->latest_xref_end = 0;
        xref_ok = pdf_xref(d, xref, 0, false);
        if (d->latest_xref_end > 0) {
            pdf_entry *root;
            /* Bind the footer to the physical end of its xref section. This
             * rejects repeated footer text in overlays and concatenated PDFs
             * whose startxref happens to have the same numeric value. */
            c.pos = d->latest_xref_end; c.end = d->end; pdf_skip(&c);
            if (c.pos == startxref) {
                if (!xref_ok || !d->has_root || !pdf_compact(d)) goto fail;
                root = pdf_lookup(d, d->root_id);
                if (!root || !root->kind || root->generation != d->root_generation ||
                    (!d->encrypted && (!pdf_load(d, root) || !pdf_name(pdf_key(root->value, "Type"), "Catalog"))) ||
                    !pdf_inventory(d)) goto fail;
                return d;
            }
        }
        /* Invalid footer candidate: discard every allocation before retrying. */
        { size_t i; for (i = 0; i < d->count; ++i) { pdf_value_free(d, d->entries[i].value); pdf_release(d, d->entries[i].attachment); }
          pdf_release(d, d->entries); pdf_release(d, d->members); }
        d->entries = NULL; d->members = NULL; d->count = d->capacity = d->member_count = 0;
        pdf_value_free(d, d->encryption); d->encryption = NULL; d->has_info = false;
        d->visited_count = 0; d->has_root = d->encrypted = d->encryption_known = false; d->end = d->span;
    }
fail:
    pdf_document_free(d); return NULL;
}
static void pdf_member_name(pdf_entry *e, char *name, size_t capacity) {
    const char *ext = "dat", *directory = "streams";
    if (e->attachment) { (void)xx_rt_snprintf(name, capacity, "%s", e->attachment); return; }
    if (e->filter_count && e->filters[e->filter_count - 1U].filter == XX_PDF_FILTER_DCT) ext = "jpg";
    else if (e->filter_count && e->filters[e->filter_count - 1U].filter == XX_PDF_FILTER_JPX) ext = "jp2";
    if (pdf_name(pdf_key(e->value, "Type"), "EmbeddedFile")) { directory = "attachments"; ext = "bin"; }
    else if (pdf_name(pdf_key(e->value, "Subtype"), "XML")) ext = "xml";
    (void)xx_rt_snprintf(name, capacity, "%s/obj-%u-%u.%s", directory, (unsigned)e->id, (unsigned)e->generation, ext);
}
static bool pdf_record(Abstractformat *f, xx_archive_record_state *state) {
    pdf_document *d = (pdf_document *)((xx_pdf *)f)->document;
    pdf_record_cursor *cursor = (pdf_record_cursor *)state->internal_state;
    pdf_entry *e; char name[1100], methods[128]; size_t i, n = 0;
    static const char *labels[] = { "Flate", "LZW", "ASCII85", "ASCIIHex", "RunLength", "DCT", "JPX", "unsupported" };
    if (!d || !cursor || cursor->index >= d->member_count) return false;
    e = &d->entries[d->members[cursor->index]]; pdf_member_name(e, name, sizeof(name)); methods[0] = 0;
    for (i = 0; i < e->filter_count; ++i) {
        const char *s = labels[(unsigned)e->filters[i].filter]; size_t k = xx_rt_strlen(s);
        if (n + k + 3U >= sizeof(methods)) return false;
        if (n) { methods[n++] = ','; methods[n++] = ' '; }
        xx_rt_memcpy(methods + n, s, k); n += k; methods[n] = 0;
    }
    xx_archive_record_cleanup(&state->current_record); xx_archive_record_init(&state->current_record);
    state->current_record.header_offset = e->kind == 1U ? f->base_address + e->offset : -1;
    state->current_record.data_offset = f->base_address + e->stream_offset;
    state->current_record.compressed_size = e->stream_size;
    if (!xx_archive_record_set_meta_str(&state->current_record, XX_META_ID_ORIGINAL_NAME, name) ||
        !xx_archive_record_set_meta_u64(&state->current_record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)e->stream_size) ||
        !xx_archive_record_set_meta_str(&state->current_record, XX_META_ID_COMPRESSION_METHOD, e->filter_count ? methods : "Store") ||
        !xx_archive_record_set_meta_bool(&state->current_record, XX_META_ID_IS_FOLDER, false) ||
        !xx_archive_record_set_meta_bool(&state->current_record, XX_META_ID_IS_ENCRYPTED, d->encrypted)) return false;
    return !e->size_known || xx_archive_record_set_meta_u64(&state->current_record, XX_META_ID_UNCOMPRESSED_SIZE, e->decoded_size);
}
static xx_archive_record_state *pdf_records_create(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    pdf_record_cursor *cursor;
    size_t i;
    if (!f || xx_pd_is_stopped(pd) || (!f->base_info_handled && !xx_pdf_handle_base_info(f, pd))) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state)); cursor = (pdf_record_cursor *)xx_mem_alloc(sizeof(*cursor));
    if (!state || !cursor) { xx_mem_free(state); xx_mem_free(cursor); return NULL; }
    xx_archive_record_state_init(state, f); cursor->index = 0;
    state->internal_state = cursor; state->free_internal = xx_mem_free;
    state->total_records = (int64_t)((pdf_document *)((xx_pdf *)f)->document)->member_count;
    for (i = 0; options && i < options->count; ++i) {
        const xx_meta *in = (const xx_meta *)xx_list_at((const xx_list_t *)options, i); xx_meta out;
        xx_mem_zero(&out, sizeof(out)); out.meta_id = in->meta_id; xx_var_init(&out.var);
        if (!xx_var_copy(&out.var, &in->var) || !xx_list_append(&state->options, &out)) {
            xx_var_cleanup(&out.var); xx_archive_record_state_free(state); return NULL;
        }
    }
    state->has_record = state->total_records && pdf_record(f, state);
    if (state->total_records && !state->has_record) { xx_archive_record_state_free(state); return NULL; }
    return state;
}
static const xx_archive_record *pdf_records_current(Abstractformat *f, xx_archive_record_state *s) {
    return s && s->format == f && s->has_record ? &s->current_record : NULL;
}
static bool pdf_records_next(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd) {
    if (!f || !s || s->format != f || !s->internal_state || !s->has_record || xx_pd_is_stopped(pd)) return false;
    ++((pdf_record_cursor *)s->internal_state)->index; ++s->current_index;
    s->has_record = pdf_record(f, s); return s->has_record;
}
static void pdf_records_free(Abstractformat *f, xx_archive_record_state *s) { (void)f; xx_archive_record_state_free(s); }
static xx_io_device *pdf_records_stage(const char *destination, char **stage_path) {
    static const char hex[] = "0123456789abcdef";
    unsigned attempt;
    *stage_path = NULL;
    for (attempt = 0; attempt < 8U; ++attempt) {
        uint8_t random[16];
        char suffix[44] = ".xxpdf.tmp.";
        char *candidate;
        xx_io_device *out;
        size_t i;
        if (!xx_io_platform_secure_random(random, sizeof(random))) return NULL;
        for (i = 0; i < sizeof(random); ++i) {
            suffix[11U + i * 2U] = hex[random[i] >> 4U];
            suffix[12U + i * 2U] = hex[random[i] & 15U];
        }
        suffix[43] = 0;
        candidate = xx_str_concat(destination, suffix);
        if (!candidate) return NULL;
        out = xx_io_file_open(candidate, "wbx");
        if (out) { *stage_path = candidate; return out; }
        xx_str_free(candidate);
    }
    return NULL;
}
static bool pdf_records_unpack(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd) {
    pdf_document *d;
    pdf_entry *e;
    pdf_record_cursor *cursor;
    const xx_var *option;
    size_t limit = PDF_DEFAULT_LIMIT, memory_limit, size = 0, capacity = 0, done = 0;
    uint8_t *data = NULL;
    char name[1100], *owned = NULL, *path = NULL, *stage_path = NULL;
    const char *directory = NULL;
    xx_io_device *out = NULL;
    bool ok = false, created = false, overwrite = false;
    if (!f || !s || s->format != f || !s->has_record || !s->internal_state || xx_pd_is_stopped(pd) ||
        !(d = (pdf_document *)((xx_pdf *)f)->document) || d->encrypted) return false;
    cursor = (pdf_record_cursor *)s->internal_state;
    if (cursor->index >= d->member_count) return false;
    e = &d->entries[d->members[cursor->index]]; memory_limit = d->limit; d->pd = pd;
    option = xx_format_resolve_extra_parameter(f, &s->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (option && xx_var_get_u64(option) < limit) limit = (size_t)xx_var_get_u64(option);
    option = xx_format_resolve_extra_parameter(f, &s->options, XX_META_ID_OPT_MEMORY_LIMIT);
    if (option && xx_var_get_u64(option) < memory_limit) memory_limit = (size_t)xx_var_get_u64(option);
    if ((e->size_known && e->decoded_size > limit) || !pdf_decode(d, e, limit, memory_limit, &data, &size, &capacity) ||
        (e->size_known && size != e->decoded_size) || xx_pd_is_stopped(pd)) goto cleanup;
    option = xx_format_resolve_extra_parameter(f, &s->options, XX_META_ID_OPT_UNPACK_PATH);
    if (option) {
        if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) directory = xx_var_get_str(option);
        else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
            owned = xx_str_unicode_to_utf8(xx_var_get_wstr(option)); directory = owned;
        }
        if (!directory) goto cleanup;
        pdf_member_name(e, name, sizeof(name)); path = xx_str_concat3(directory, "/", name);
        if (!path || !xx_store_create_dirs_a(path, false)) goto cleanup;
        option = xx_format_resolve_extra_parameter(f, &s->options, XX_META_ID_OPT_OVERWRITE);
        overwrite = option && xx_var_get_bool(option);
        if (!overwrite && xx_io_file_exists_a(path)) goto cleanup;
        out = pdf_records_stage(path, &stage_path); if (!out) goto cleanup; created = true;
        while (done < size) {
            size_t request = size - done < 65536U ? size - done : 65536U;
            ssize_t n;
            if (xx_pd_is_stopped(pd)) goto cleanup;
            n = xx_io_write(out, data + done, request);
            if (n <= 0 || (size_t)n > request) goto cleanup;
            done += (size_t)n;
        }
        if (xx_io_close(out)) { out = NULL; goto cleanup; }
        out = NULL;
        if (xx_pd_is_stopped(pd)) goto cleanup;
        ok = xx_io_file_replace_a(stage_path, path, overwrite);
        if (ok) created = false;
    } else {
        ok = !xx_pd_is_stopped(pd);
    }
cleanup:
    if (out && xx_io_close(out)) ok = false;
    if (created && stage_path) (void)xx_io_file_remove_a(stage_path);
    xx_mem_free(data); xx_str_free(stage_path); xx_str_free(path); xx_str_free(owned); return ok;
}
static void pdf_destroy_callback(Abstractformat *f) { xx_pdf_destroy((xx_pdf *)f); }
void xx_pdf_init(xx_pdf *pdf, xx_io_device *device, int64_t base_address) {
    if (!pdf) return;
    xx_mem_zero(pdf, sizeof(*pdf)); xx_format_init(&pdf->format, device, base_address);
    pdf->format.file_type = XX_FILE_TYPE_PDF; pdf->format.format_type = XX_TYPE_ARCHIVE;
    pdf->format.is_archive = true; xx_format_set_mime_type(&pdf->format, "application/pdf"); xx_format_set_extension(&pdf->format, "pdf");
    pdf->format.check_is_valid = xx_pdf_check_is_valid; pdf->format.handle_base_info = xx_pdf_handle_base_info;
    pdf->format.get_format_size = xx_pdf_get_format_size; pdf->format.destroy = pdf_destroy_callback;
    pdf->format.create_archive_records_reading = pdf_records_create;
    pdf->format.get_current_archive_record = pdf_records_current;
    pdf->format.archive_record_move_to_next = pdf_records_next;
    pdf->format.unpack_current_archive_record = pdf_records_unpack;
    pdf->format.free_archive_records_reading = pdf_records_free;
}
xx_pdf *xx_pdf_create(xx_io_device *device, int64_t base_address) {
    xx_pdf *pdf = (xx_pdf *)xx_mem_alloc(sizeof(*pdf)); if (pdf) xx_pdf_init(pdf, device, base_address); return pdf;
}
void xx_pdf_destroy(xx_pdf *pdf) {
    if (!pdf) return;
    pdf_document_free((pdf_document *)pdf->document); pdf->document = NULL;
    xx_format_cleanup_extra_parameters(&pdf->format);
}
void xx_pdf_free(xx_pdf *pdf) { if (pdf) { xx_pdf_destroy(pdf); xx_mem_free(pdf); } }
bool xx_pdf_check_magic(const uint8_t *data, size_t size) {
    return data && size >= 9U && !xx_rt_memcmp(data, "%PDF-", 5U) &&
        ((data[5] == '1' && data[6] == '.' && data[7] >= '0' && data[7] <= '7') ||
         (data[5] == '2' && data[6] == '.' && data[7] == '0')) && (data[8] == 10 || data[8] == 13);
}
bool xx_pdf_check_is_valid(Abstractformat *f, xx_pd_struct *pd) {
    pdf_document *d = pdf_parse(f, pd); bool ok = d != NULL; pdf_document_free(d); return ok;
}
bool xx_pdf_handle_base_info(Abstractformat *f, xx_pd_struct *pd) {
    xx_pdf *pdf = (xx_pdf *)f;
    pdf_document *d; uint8_t header[8]; int64_t end;
    if (!f || !(d = pdf_parse(f, pd))) { if (f) { f->is_valid = false; f->base_info_handled = false; } return false; }
    if (!pdf_read(d, 0, header, sizeof(header))) { pdf_document_free(d); return false; }
    pdf_document_free((pdf_document *)pdf->document); pdf->document = d;
    xx_rt_memcpy(pdf->version, header + 5, 3U); pdf->version[3] = 0; xx_format_set_version(f, pdf->version);
    pdf->object_count = (uint32_t)d->count; pdf->stream_count = (uint32_t)d->member_count; pdf->encrypted = d->encrypted;
    f->format_size = d->end; end = f->base_address + d->end;
    f->overlay_offset = d->span > d->end ? end : -1; f->overlay_size = d->span - d->end;
    f->number_of_archive_records = d->member_count; f->number_of_resources = d->member_count;
    f->is_crypted = d->encrypted; f->is_archive = true; f->is_valid = true; f->base_info_handled = true;
    return true;
}
int64_t xx_pdf_get_format_size(Abstractformat *f, xx_pd_struct *pd) {
    return f && (f->base_info_handled || xx_pdf_handle_base_info(f, pd)) ? f->format_size : -1;
}

/* Tolerant inspection reuses the value parser. It never searches through a
 * stream with an unknown extent, so object/action-looking bytes in payloads
 * cannot become dictionaries. This fallback is deliberately not an archive
 * index and is never accepted by check_is_valid/handle_base_info. */
static pdf_document *pdf_recover(Abstractformat *f, xx_pd_struct *pd) {
    pdf_document *d;
    pdf_cursor c;
    const xx_var *option;
    int64_t total, at, scan_end;
    size_t limit = PDF_DEFAULT_LIMIT, i;
    bool header = false;
    uint8_t bytes[9];
    if (!f || !f->device || f->base_address < 0 || xx_pd_is_stopped(pd)) return NULL;
    total = xx_io_size(f->device);
    if (total < f->base_address || total - f->base_address < 9) return NULL;
    option = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    if (option && xx_var_get_u64(option) < limit) limit = (size_t)xx_var_get_u64(option);
    if (limit < sizeof(*d)) return NULL;
    d = (pdf_document *)xx_mem_alloc(sizeof(*d)); if (!d) return NULL;
    xx_mem_zero(d, sizeof(*d)); d->format = f; d->pd = pd; d->limit = limit; d->used = sizeof(*d);
    d->span = total - f->base_address; d->end = d->span; d->cache_start = -1; d->recovered = true;
    for (at = 0; at <= 1024 && at <= d->span - 9; ++at) {
        if (!pdf_read(d, at, bytes, sizeof(bytes))) goto fail;
        if (xx_pdf_check_magic(bytes, sizeof(bytes))) { d->header_offset = at; header = true; break; }
    }
    if (!header) goto fail;
    scan_end = d->span < (int64_t)64 * 1024 * 1024 ? d->span : (int64_t)64 * 1024 * 1024;
    c.doc = d; c.pos = d->header_offset + 9; c.end = scan_end; c.memory = NULL;
    while (c.pos < c.end && d->count < PDF_MAX_ENTRIES) {
        int64_t start;
        uint64_t id, gen;
        pdf_value *value;
        pdf_skip(&c); start = c.pos;
        if (start >= c.end || xx_pd_is_stopped(pd)) break;
        if (pdf_word(&c, "trailer")) {
            value = pdf_parse_value(&c, 0);
            if (value && value->type == PV_DICT) {
                pdf_value *v = pdf_key(value, "Encrypt"), *info = pdf_key(value, "Info");
                /* A physically later trailer is the active revision. */
                if (v) {
                    pdf_value_free(d, d->encryption); d->encryption = NULL;
                    d->encryption = (pdf_value *)pdf_alloc(d, sizeof(*v));
                    if (!d->encryption) { pdf_value_free(d, value); goto fail; }
                    ++d->nodes; *d->encryption = *v; d->encryption->next = NULL;
                    d->encrypted = v->type != PV_NULL; d->encryption_known = true;
                    v->text = v->raw = NULL; v->child = NULL;
                }
                if (info && info->type == PV_REF) {
                    d->has_info = true; d->info_id = (uint32_t)info->integer; d->info_generation = info->generation;
                }
            }
            pdf_value_free(d, value); continue;
        }
        c.pos = start;
        if (pdf_uint(&c, &id) && id && id <= INT32_MAX && pdf_uint(&c, &gen) && gen <= 65535U && pdf_word(&c, "obj")) {
            pdf_entry *e;
            bool boundary = true;
            value = pdf_parse_value(&c, 0);
            if (!value) break;
            if (!pdf_add_entry(d, (uint32_t)id, (uint32_t)gen, 1, (uint64_t)start, 0,
                               UINT32_MAX - (uint32_t)d->count - 1U)) { pdf_value_free(d, value); goto fail; }
            e = &d->entries[d->count - 1U]; e->value = value; e->loaded = true;
            if (value->type == PV_DICT && pdf_word(&c, "stream")) {
                uint64_t length = 0;
                int ch = pdf_at(&c, c.pos++);
                if (ch == 13 && pdf_at(&c, c.pos) == 10) ++c.pos;
                else if (ch != 10) boundary = false;
                if (!pdf_integer(pdf_key(value, "Length"), &length) || length > INT64_MAX ||
                    (int64_t)length > c.end - c.pos) boundary = false;
                if (boundary) {
                    e->stream_offset = c.pos; e->stream_size = (int64_t)length; c.pos += (int64_t)length;
                    ch = pdf_at(&c, c.pos);
                    if (ch == 13) { ++c.pos; if (pdf_at(&c, c.pos) == 10) ++c.pos; }
                    else if (ch == 10) ++c.pos;
                    boundary = pdf_at(&c, c.pos) == 'e' && pdf_word(&c, "endstream");
                    if (!pdf_filters(d, value, e, true)) {
                        e->filter_count = 1; e->filters[0].filter = XX_PDF_FILTER_UNSUPPORTED;
                    }
                }
                if (!boundary) { e->stream_offset = -1; e->stream_size = 0; }
            }
            if (!boundary || !pdf_word(&c, "endobj")) break;
            e->object_end = c.pos;
            continue;
        }
        c.pos = start;
        /* Consume complete lexical values, including strings/comments, when
         * seeking the next header. An invalid token advances one byte. */
        value = pdf_parse_value(&c, 0);
        if (!value) {
            int ch = pdf_at(&c, start);
            /* An unterminated composite value has no trustworthy next token.
             * Never resynchronize into its string contents. */
            if (ch == '(' || ch == '<' || ch == '[') break;
            c.pos = start + 1;
        } else { pdf_value_free(d, value); if (c.pos <= start) c.pos = start + 1; }
    }
    if (xx_pd_is_stopped(pd) || !pdf_compact(d)) goto fail;
    if (!d->encryption_known) {
        for (i = 0; i < d->count; ++i) {
            pdf_entry *e = &d->entries[i];
            if (pdf_name(pdf_key(e->value, "Filter"), "Standard")) {
                d->encryption = (pdf_value *)pdf_alloc(d, sizeof(*d->encryption)); if (!d->encryption) goto fail;
                ++d->nodes; d->encryption->type = PV_REF; d->encryption->integer = e->id;
                d->encryption->generation = e->generation; d->encrypted = true; break;
            }
        }
    }
    if (!d->encrypted) {
        for (i = 0; i < d->count; ++i) {
            uint32_t id = d->entries[i].id;
            if (pdf_name(pdf_key(d->entries[i].value, "Type"), "ObjStm")) {
                (void)pdf_object_stream(d, &d->entries[i]);
                /* Expansion may resize/reorder the index. Resume after the
                 * same object, avoiding stale pointers and repeated work. */
                { pdf_entry *e = pdf_lookup(d, id); if (e) i = (size_t)(e - d->entries); }
            }
        }
    }
    if (xx_pd_is_stopped(pd)) goto fail;
    return d;
fail:
    pdf_document_free(d); return NULL;
}
bool xx_pdf_analyze(xx_pdf *pdf, xx_pd_struct *pd) {
    pdf_document *d;
    uint8_t header[8];
    size_t i;
    if (!pdf || xx_pd_is_stopped(pd)) return false;
    if (pdf->document) { ((pdf_document *)pdf->document)->pd = pd; return true; }
    if (xx_pdf_handle_base_info(&pdf->format, pd)) return true;
    if (!(d = pdf_recover(&pdf->format, pd))) return false;
    if (!pdf_read(d, d->header_offset, header, sizeof(header))) { pdf_document_free(d); return false; }
    pdf->document = d; xx_rt_memcpy(pdf->version, header + 5, 3); pdf->version[3] = 0;
    pdf->object_count = (uint32_t)d->count; pdf->encrypted = d->encrypted;
    pdf->stream_count = 0;
    for (i = 0; i < d->count; ++i) if (d->entries[i].stream_offset >= 0) ++pdf->stream_count;
    return true;
}
const char *xx_pdf_get_version(const xx_pdf *pdf) { return pdf ? pdf->version : ""; }
bool xx_pdf_is_encrypted(const xx_pdf *pdf) { return pdf && pdf->encrypted; }

/* Text strings use PDFDocEncoding unless a Unicode BOM is present. Strings
 * are decoded once by the parser; octal escapes and nested parentheses never
 * participate in dictionary or suspicious-name matching. */
static char *pdf_string_text(pdf_value *v) {
    static const uint16_t special[32] = {
        0x2022,0x2020,0x2021,0x2026,0x2014,0x2013,0x0192,0x2044,
        0x2039,0x203A,0x2212,0x2030,0x201E,0x201C,0x201D,0x2018,
        0x2019,0x201A,0x2122,0xFB01,0xFB02,0x0141,0x0152,0x0160,
        0x0178,0x017D,0x0131,0x0142,0x0153,0x0161,0x017E,0xFFFD };
    static const uint16_t accents[8] = {0x02D8,0x02C7,0x02C6,0x02D9,0x02DD,0x02DB,0x02DA,0x02DC};
    size_t i = 0, n = 0, capacity;
    bool wide = false, little = false;
    char *out;
    if (!v || v->type != PV_STRING || v->size > (SIZE_MAX - 1U) / 4U) return NULL;
    capacity = v->size * 4U + 1U; out = (char *)xx_mem_alloc(capacity); if (!out) return NULL; out[0] = 0;
    if (v->size >= 3 && !xx_rt_memcmp(v->text, "\xEF\xBB\xBF", 3)) {
        xx_rt_memcpy(out, v->text + 3, v->size - 3); out[v->size - 3] = 0; return out;
    }
    if (v->size >= 2 && ((v->text[0] == 0xFE && v->text[1] == 0xFF) || (v->text[0] == 0xFF && v->text[1] == 0xFE))) {
        wide = true; little = v->text[0] == 0xFF; i = 2;
        if ((v->size - i) % 2U) { xx_mem_free(out); return xx_str_create(""); }
    }
    for (; i < v->size; ++i) {
        uint32_t cp = v->text[i];
        if (wide) {
            cp = little ? v->text[i] | ((uint32_t)v->text[i + 1] << 8) : ((uint32_t)v->text[i] << 8) | v->text[i + 1]; ++i;
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 2 < v->size) {
                uint32_t low = little ? v->text[i + 1] | ((uint32_t)v->text[i + 2] << 8) : ((uint32_t)v->text[i + 1] << 8) | v->text[i + 2];
                if (low >= 0xDC00 && low <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + low - 0xDC00; i += 2; }
            }
            if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0xFFFD;
        } else if (cp >= 0x18 && cp <= 0x1F) cp = accents[cp - 0x18];
        else if (cp >= 0x80 && cp <= 0x9F) cp = special[cp - 0x80];
        else if (cp == 0xA0) cp = 0x20AC;
        else if (cp == 0x7F || cp == 0xAD) cp = 0xFFFD;
        /* JS strings and the legacy API are zero-terminated. */
        if (!cp) break;
        if (!pdf_utf8_add(out, capacity, &n, cp)) { xx_mem_free(out); return NULL; }
    }
    return out;
}
static char *pdf_token_text(pdf_value *v, int delimiter, bool decoded_hex) {
    char text[80];
    if (delimiter) return xx_str_create(delimiter == 1 ? "<<" : delimiter == 2 ? "[" : delimiter == -1 ? ">>" : "]");
    if (!v) return xx_str_create("");
    if (v->type == PV_STRING) return v->hex_string && !decoded_hex ? xx_str_create((char *)v->raw) : pdf_string_text(v);
    if (v->raw) return xx_str_create((char *)v->raw);
    if (v->type == PV_NAME) return xx_str_concat("/", (char *)v->text);
    if (v->type == PV_INT) { (void)xx_rt_snprintf(text, sizeof(text), "%lld", (long long)v->integer); return xx_str_create(text); }
    if (v->type == PV_REF) {
        (void)xx_rt_snprintf(text, sizeof(text), "%u %u R", (unsigned)v->integer, (unsigned)v->generation); return xx_str_create(text);
    }
    return xx_str_create(v->type == PV_NULL ? "null" : "");
}
typedef bool (*pdf_visit_fn)(pdf_document *, pdf_value *, int, void *);
static bool pdf_visit(pdf_document *d, pdf_value *v, size_t *count, size_t limit, pdf_visit_fn fn, void *context) {
    for (; v && *count < limit; v = v->next) {
        int delimiter = v->type == PV_DICT ? 1 : v->type == PV_ARRAY ? 2 : 0;
        if (xx_pd_is_stopped(d->pd)) return false;
        ++*count; if (!fn(d, v, delimiter, context)) return false;
        if (delimiter) {
            if (!pdf_visit(d, v->child, count, limit, fn, context)) return false;
            if (*count < limit) { ++*count; if (!fn(d, v, -delimiter, context)) return false; }
        }
    }
    return true;
}
static void pdf_strings_free(xx_list_s *list) {
    size_t i; for (i = 0; i < list->count; ++i) xx_str_free(*(char **)xx_list_at(list, i)); xx_list_cleanup(list);
}
static bool pdf_unique(xx_list_s *list, char *text, size_t *used, size_t limit) {
    size_t i, n;
    if (!text) return false;
    for (i = 0; i < list->count; ++i) {
        const char *previous = *(char **)xx_list_at(list, i);
        if (previous && !xx_rt_strcmp(previous, text)) { xx_str_free(text); return true; }
    }
    n = xx_rt_strlen(text) + 1U + sizeof(char *);
    if (*used > limit || n > limit - *used || !xx_list_append(list, &text)) { xx_str_free(text); return false; }
    *used += n; return true;
}
typedef struct { const char *key; bool strings_only, pending; xx_list_s *out; size_t used, limit; } pdf_query;
static bool pdf_query_token(pdf_document *d, pdf_value *v, int delimiter, void *context) {
    pdf_query *q = (pdf_query *)context; (void)d;
    if (q->pending && (!q->strings_only || (!delimiter && v->type == PV_STRING && !v->hex_string)))
        if (!pdf_unique(q->out, pdf_token_text(v, delimiter, false), &q->used, q->limit)) return false;
    q->pending = !delimiter && pdf_name(v, q->key);
    return true;
}
bool xx_pdf_get_values_by_key(xx_pdf *pdf, const char *key, bool strings_only, size_t part_limit, xx_list_s *out, xx_pd_struct *pd) {
    pdf_document *d;
    pdf_query q;
    size_t i;
    if (!key || !out || out->elem_size != sizeof(char *) || !xx_pdf_analyze(pdf, pd)) return false;
    d = (pdf_document *)pdf->document; xx_mem_zero(&q, sizeof(q));
    q.key = key[0] == '/' ? key + 1 : key; q.strings_only = strings_only; q.out = out; q.limit = d->limit - d->used;
    for (i = 0; i < d->count; ++i) {
        size_t count = 0; q.pending = false;
        if (!d->entries[i].loaded) continue;
        if (!pdf_visit(d, d->entries[i].value, &count, part_limit, pdf_query_token, &q)) return false;
    }
    return !xx_pd_is_stopped(pd);
}
typedef struct { const char *key; bool pending, found; } pdf_hex_query;
static bool pdf_hex_token(pdf_document *d, pdf_value *v, int delimiter, void *context) {
    pdf_hex_query *q = (pdf_hex_query *)context; (void)d;
    if (q->pending && !delimiter && v->type == PV_STRING && v->hex_string) { q->found = true; return false; }
    q->pending = !delimiter && pdf_name(v, q->key); return true;
}
bool xx_pdf_is_values_hex_by_key(xx_pdf *pdf, const char *key, size_t part_limit, xx_pd_struct *pd) {
    pdf_hex_query q;
    pdf_document *d;
    size_t i;
    if (!key || !xx_pdf_analyze(pdf, pd)) return false;
    d = (pdf_document *)pdf->document; xx_mem_zero(&q, sizeof(q)); q.key = key[0] == '/' ? key + 1 : key;
    for (i = 0; i < d->count; ++i) {
        size_t count = 0; q.pending = false;
        if (!d->entries[i].loaded) continue;
        if (!pdf_visit(d, d->entries[i].value, &count, part_limit, pdf_hex_token, &q))
            return q.found && !xx_pd_is_stopped(pd);
    }
    return false;
}
static int pdf_string_compare(const void *a, const void *b) { return xx_rt_strcmp(*(const char *const *)a, *(const char *const *)b); }
static bool pdf_join(char **out, const char *text, const char *separator) {
    char *next;
    size_t a, b, s;
    if (!text || !*text) return true;
    a = *out ? xx_rt_strlen(*out) : 0; b = xx_rt_strlen(text); s = a ? xx_rt_strlen(separator) : 0;
    if (a > PDF_MAX_TEXT || b > PDF_MAX_TEXT - a || s > PDF_MAX_TEXT - a - b) return false;
    next = (char *)xx_mem_alloc(a + b + s + 1); if (!next) return false;
    if (a) xx_rt_memcpy(next, *out, a);
    if (s) xx_rt_memcpy(next + a, separator, s);
    xx_rt_memcpy(next + a + s, text, b + 1); xx_str_free(*out); *out = next; return true;
}
typedef struct { xx_list_s names; size_t used, limit; bool pending, found; } pdf_filter_query;
static bool pdf_filter_token(pdf_document *d, pdf_value *v, int delimiter, void *context) {
    pdf_filter_query *q = (pdf_filter_query *)context;
    if (q->pending) {
        pdf_value *p = pdf_resolve(d, v); bool array = p && p->type == PV_ARRAY;
        q->pending = false; q->found = true;
        p = array ? p->child : p;
        for (; p; p = array ? p->next : NULL) {
            pdf_value *name = pdf_resolve(d, p);
            if (name && name->type == PV_NAME && !pdf_unique(&q->names, xx_str_concat("/", (char *)name->text), &q->used, q->limit)) return false;
        }
    }
    if (!q->found && !delimiter && pdf_name(v, "Filter")) q->pending = true;
    return true;
}
static char *pdf_filters_text(xx_pdf *pdf, xx_pd_struct *pd, size_t part_limit) {
    pdf_document *d;
    pdf_filter_query q;
    size_t i;
    char *out = NULL;
    if (!xx_pdf_analyze(pdf, pd)) return NULL;
    d = (pdf_document *)pdf->document; xx_mem_zero(&q, sizeof(q)); q.limit = d->limit - d->used;
    if (!xx_list_init(&q.names, sizeof(char *), NULL)) return NULL;
    for (i = 0; i < d->count; ++i) {
        size_t count = 0; q.pending = q.found = false;
        if (!d->entries[i].loaded) continue;
        if (!pdf_visit(d, d->entries[i].value, &count, part_limit, pdf_filter_token, &q)) goto fail;
    }
    xx_rt_qsort(q.names.data, q.names.count, sizeof(char *), pdf_string_compare);
    for (i = 0; i < q.names.count; ++i) if (!pdf_join(&out, *(char **)xx_list_at(&q.names, i), ", ")) goto fail;
    pdf_strings_free(&q.names); return out ? out : xx_str_create("");
fail:
    pdf_strings_free(&q.names); xx_str_free(out); return NULL;
}
char *xx_pdf_get_filters(xx_pdf *pdf, xx_pd_struct *pd) { return pdf_filters_text(pdf, pd, 100); }
static pdf_value *pdf_security_dict(pdf_document *d) {
    pdf_value *v = pdf_resolve(d, d->encryption);
    return v && v->type == PV_DICT ? v : NULL;
}
static int64_t pdf_signed(pdf_document *d, pdf_value *dict, const char *key) {
    pdf_value *v = pdf_resolve(d, pdf_key(dict, key)); return v && v->type == PV_INT ? v->integer : 0;
}
char *xx_pdf_get_encryption(xx_pdf *pdf, xx_pd_struct *pd) {
    pdf_document *d;
    pdf_value *dict, *filter, *cf, *stm, *cfm;
    int64_t version, revision, length, permissions;
    char text[256], bit_text[40] = "", permission_text[48] = "";
    const char *method;
    if (!xx_pdf_analyze(pdf, pd)) return NULL;
    if (!pdf->encrypted) return xx_str_create("");
    d = (pdf_document *)pdf->document; dict = pdf_security_dict(d);
    if (!dict) return xx_str_create("Encrypted");
    filter = pdf_resolve(d, pdf_key(dict, "Filter"));
    if (!pdf_name(filter, "Standard")) return filter && filter->type == PV_NAME ? xx_str_create((char *)filter->text) : xx_str_create("Encrypted");
    version = pdf_signed(d, dict, "V"); revision = pdf_signed(d, dict, "R"); length = pdf_signed(d, dict, "Length");
    permissions = pdf_signed(d, dict, "P");
    if (!version) version = revision >= 5 ? 5 : revision == 4 ? 4 : revision == 3 ? 2 : revision == 2 ? 1 : 0;
    if (length > 0 && length < 40) length *= 8;
    if (length > 0) (void)xx_rt_snprintf(bit_text, sizeof(bit_text), " %lld-bit", (long long)length);
    stm = pdf_resolve(d, pdf_key(dict, "StmF")); cf = pdf_resolve(d, pdf_key(dict, "CF")); cfm = NULL;
    if (stm && stm->type == PV_NAME && cf && cf->type == PV_DICT) {
        pdf_value *item = pdf_resolve(d, pdf_key(cf, (char *)stm->text));
        cfm = pdf_resolve(d, pdf_key(item, "CFM"));
    }
    if (!cfm) cfm = pdf_resolve(d, pdf_key(dict, "CFM"));
    method = cfm && cfm->type == PV_NAME ? (char *)cfm->text : version >= 5 ? "AESV3" : version == 4 ? "AESV2/RC4" : "RC4";
    if (!xx_rt_strcmp(method, "V2")) method = "RC4";
    if (pdf_key(dict, "P")) (void)xx_rt_snprintf(permission_text, sizeof(permission_text), " P=%lld", (long long)permissions);
    (void)xx_rt_snprintf(text, sizeof(text), "Standard V%lld R%lld%s %s%s", (long long)version, (long long)revision, bit_text, method, permission_text);
    return xx_str_create(text);
}
char *xx_pdf_get_permissions(xx_pdf *pdf, xx_pd_struct *pd) {
    static const uint32_t bits[] = {4,8,16,32,256,512,1024,2048};
    static const char *names[] = {"print","modify","copy","annotate","fill-forms","extract-a11y","assemble","print-hires"};
    pdf_document *d;
    pdf_value *dict;
    uint32_t permissions;
    size_t i;
    char *out = NULL;
    if (!xx_pdf_analyze(pdf, pd)) return NULL;
    if (!pdf->encrypted) return xx_str_create("");
    d = (pdf_document *)pdf->document; dict = pdf_security_dict(d);
    if (!dict || !pdf_key(dict, "P")) return xx_str_create("");
    permissions = (uint32_t)pdf_signed(d, dict, "P");
    for (i = 0; i < sizeof(bits) / sizeof(bits[0]); ++i)
        if ((permissions & bits[i]) && !pdf_join(&out, names[i], ", ")) { xx_str_free(out); return NULL; }
    return out ? out : xx_str_create("none");
}
char *xx_pdf_get_header_comment_hex(xx_pdf *pdf, xx_pd_struct *pd) {
    pdf_document *d;
    pdf_cursor c;
    char text[81]; size_t count = 0;
    int ch;
    if (!xx_pdf_analyze(pdf, pd)) return NULL;
    d = (pdf_document *)pdf->document; c.doc = d; c.pos = d->header_offset; c.end = d->span; c.memory = NULL;
    while (c.pos < c.end && c.pos - d->header_offset < 100 && (ch = pdf_at(&c, c.pos)) != 0 && ch != 10 && ch != 13) ++c.pos;
    while ((ch = pdf_at(&c, c.pos)) == 10 || ch == 13) ++c.pos;
    if (pdf_at(&c, c.pos) == '%') {
        ++c.pos;
        while (count < 40 && (ch = pdf_at(&c, c.pos++)) > 0 && ch != 10 && ch != 13) {
            static const char hex[] = "0123456789abcdef";
            text[count * 2] = hex[(unsigned)ch >> 4]; text[count * 2 + 1] = hex[ch & 15]; ++count;
        }
    }
    if (xx_pd_is_stopped(pd)) return NULL;
    text[count * 2] = 0; return xx_str_create(text);
}
typedef struct { const char *key; char *value; bool pending, ok; } pdf_meta_query;
static bool pdf_meta_token(pdf_document *d, pdf_value *v, int delimiter, void *context) {
    pdf_meta_query *q = (pdf_meta_query *)context;
    if (q->pending && !q->value && !delimiter) {
        pdf_value *resolved = pdf_resolve(d, v);
        char *value = pdf_token_text(resolved, 0, true);
        if (value && resolved && resolved->type == PV_INT) {
            char number[32]; (void)xx_rt_snprintf(number, sizeof(number), "%lld", (long long)resolved->integer);
            xx_str_free(value); value = xx_str_create(number);
        }
        if (value && resolved && resolved->type == PV_STRING &&
            (!xx_rt_strcmp(q->key, "CreationDate") || !xx_rt_strcmp(q->key, "ModDate"))) {
            char *date = xx_pdf_date_text(value); xx_str_free(value); value = date;
        }
        if (!value) { q->ok = false; return false; }
        if (*value) q->value = value; else xx_str_free(value);
    }
    q->pending = !delimiter && pdf_name(v, q->key); return true;
}
static char *pdf_meta_text(pdf_document *d, const char *key) {
    pdf_meta_query q;
    size_t i, count = 0;
    xx_mem_zero(&q, sizeof(q)); q.key = key; q.ok = true;
    if (d->has_info) {
        pdf_entry *info = pdf_lookup(d, d->info_id);
        if (info && info->generation == d->info_generation && pdf_load(d, info))
            if (!pdf_visit(d, info->value, &count, 256, pdf_meta_token, &q)) { xx_str_free(q.value); return NULL; }
    }
    for (i = 0; !q.value && i < d->count; ++i) {
        count = 0; q.pending = false;
        if (!d->entries[i].loaded) continue;
        if (!pdf_visit(d, d->entries[i].value, &count, 256, pdf_meta_token, &q)) { xx_str_free(q.value); return NULL; }
    }
    return q.ok ? q.value ? q.value : xx_str_create("") : NULL;
}
typedef struct { uint32_t suspicious; bool linearized, length_pending; int64_t length; } pdf_info_query;
static bool pdf_info_token(pdf_document *d, pdf_value *v, int delimiter, void *context) {
    static const char *names[] = {"OpenAction","AA","JavaScript","JS","Launch","EmbeddedFile","URI","SubmitForm","GoToR","RichMedia","AcroForm","XFA"};
    pdf_info_query *q = (pdf_info_query *)context; size_t i;
    if (q->length_pending && !delimiter) { pdf_value *r = pdf_resolve(d, v); if (r && r->type == PV_INT) q->length = r->integer; }
    q->length_pending = !delimiter && pdf_name(v, "L");
    if (!delimiter && v->type == PV_NAME) {
        if (pdf_name(v, "Linearized")) q->linearized = true;
        for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) if (pdf_name(v, names[i])) q->suspicious |= 1U << i;
    }
    return true;
}
static bool pdf_labeled_info(char **out, const char *label, const char *text) {
    char *line; bool ok;
    if (!*text) return true;
    line = xx_str_concat3(label, ": ", text); if (!line) return false;
    ok = pdf_join(out, line, "; "); xx_str_free(line); return ok;
}
char *xx_pdf_get_info(xx_pdf *pdf, xx_pd_struct *pd) {
    static const char *keys[] = {"Title","Author","Subject","Keywords","Creator","Producer","CreationDate","ModDate"};
    static const char *suspicious[] = {"/OpenAction","/AA","/JavaScript","/JS","/Launch","/EmbeddedFile","/URI","/SubmitForm","/GoToR","/RichMedia","/AcroForm","/XFA"};
    pdf_document *d;
    char *out = NULL, *text, *line, *flags = NULL;
    char linearized[128];
    size_t i; uint32_t mask = 0; bool has_linearized = false;
    if (!xx_pdf_analyze(pdf, pd)) return NULL;
    d = (pdf_document *)pdf->document;
    text = pdf_filters_text(pdf, pd, 256); if (!text) goto fail;
    { bool ok = pdf_labeled_info(&out, "Filters", text); xx_str_free(text); if (!ok) goto fail; }
    /* DIE's bundled PDF script appends the dedicated security getters.
     * Ciphertext metadata is omitted from this general options string. */
    if (!pdf->encrypted) for (i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        text = pdf_meta_text(d, keys[i]); if (!text) goto fail;
        { bool ok = pdf_labeled_info(&out, keys[i], text); xx_str_free(text); if (!ok) goto fail; }
    }
    for (i = 0; i < d->count; ++i) {
        pdf_info_query q; size_t count = 0; xx_mem_zero(&q, sizeof(q));
        if (!d->entries[i].loaded) continue;
        if (!pdf_visit(d, d->entries[i].value, &count, 256, pdf_info_token, &q)) goto fail;
        mask |= q.suspicious;
        if (q.linearized && !has_linearized) {
            has_linearized = true;
            if (q.length > 0 && q.length != d->span)
                (void)xx_rt_snprintf(linearized, sizeof(linearized), "Linearized (declared L=%lld, actual=%lld)", (long long)q.length, (long long)d->span);
            else (void)xx_rt_snprintf(linearized, sizeof(linearized), "Linearized");
            if (!pdf_join(&out, linearized, "; ")) goto fail;
        }
    }
    for (i = 0; i < sizeof(suspicious) / sizeof(suspicious[0]); ++i)
        if ((mask & (1U << i)) && !pdf_join(&flags, suspicious[i], ", ")) goto fail;
    if (flags) { line = xx_str_concat("Suspicious: ", flags); if (!line) goto fail;
        { bool ok = pdf_join(&out, line, "; "); xx_str_free(line); if (!ok) goto fail; } }
    xx_str_free(flags); flags = NULL;
    if (out) {
        char *normalized; size_t n = xx_rt_strlen(out), extra = 0, at = 0;
        for (i = 0; i < n; ++i) if (out[i] == '\n' || out[i] == '\r') ++extra;
        if (extra > PDF_MAX_TEXT - n) goto fail;
        normalized = (char *)xx_mem_alloc(n + extra + 1); if (!normalized) goto fail;
        for (i = 0; i < n; ++i) {
            if (out[i] == '\n' || out[i] == '\r') { normalized[at++] = ';'; normalized[at++] = ' '; }
            else normalized[at++] = out[i];
        }
        normalized[at] = 0; xx_str_free(out); out = normalized;
    }
    return out ? out : xx_str_create("");
fail:
    xx_str_free(out); xx_str_free(flags); return NULL;
}
