/* SPDX-License-Identifier: MIT. Checked container primitives. */
#ifndef XX_PROTOCOL_FRAMING_H
#define XX_PROTOCOL_FRAMING_H
#include "xx_container_wire_helpers.h"
#include "xx_asn1_der.h"
#include "xxfclib/algo/hash/xx_hash.h"
static XXFC_MAYBE_UNUSED bool protocol_mem(Abstractformat *f, pm_stream *s, const char *name, uint8_t **p, uint64_t n) {
    if (n > 67108864 || s->count >= 4096 || !pm_add(f, s, name, 0, 0))
        return false;
    s->items[s->count - 1].memory = *p;
    s->items[s->count - 1].size = (int64_t)n;
    s->items[s->count - 1].packed_size = (int64_t)n;
    *p = NULL;
    return true;
}
static bool protocol_take(memory_blob *b, uint64_t *at, uint64_t end, uint64_t n) {
    if (!record_span(*at, n, end) || !blob_span(b, *at, n))
        return false;
    *at += n;
    return true;
}
static XXFC_MAYBE_UNUSED bool protocol_line(memory_blob *b, uint64_t *at, uint64_t *start, uint64_t *n, bool crlf) {
    uint64_t p = *at;
    *start = p;
    while (p < b->n && b->p[(size_t)p] != '\n') {
        if (p - *at > 65536 || !blob_span(b, p, 1) || !b->p[(size_t)p])
            return false;
        ++p;
    }
    if (p == b->n || (crlf && (p == *at || b->p[(size_t)p - 1] != '\r')))
        return false;
    *n = p - *at - (crlf ? 1U : 0U);
    *at = p + 1;
    return true;
}
static XXFC_MAYBE_UNUSED bool protocol_eq(memory_blob *b, uint64_t at, uint64_t n, const char *s) {
    return xx_rt_strlen(s) == n && blob_span(b, at, n) && !xx_rt_memcmp(b->p + (size_t)at, s, (size_t)n);
}
static XXFC_MAYBE_UNUSED bool protocol_dec(memory_blob *b, uint64_t at, uint64_t n, uint64_t *v) {
    uint64_t u = 0;
    if (!n || n > 10 || !blob_span(b, at, n))
        return false;
    for (uint64_t i = 0; i < n; ++i) {
        uint8_t c = b->p[(size_t)(at + i)];
        if (c < 48 || c > 57)
            return false;
        u = u * 10 + c - 48;
        if (u > 67108864)
            return false;
    }
    *v = u;
    return true;
}
static XXFC_MAYBE_UNUSED bool protocol_uint(memory_blob *b, der_tlv *t, uint64_t *v) {
    uint64_t n = t->end - t->value, u = 0;
    if ((t->tag != 2 && t->tag != 10) || !n || n > 9 || (b->p[(size_t)t->value] & 128) ||
        (n > 1 && !b->p[(size_t)t->value] && !(b->p[(size_t)t->value + 1] & 128)))
        return false;
    if (n == 9 && b->p[(size_t)t->value])
        return false;
    for (uint64_t i = t->value; i < t->end; ++i)
        u = (u << 8) | b->p[(size_t)i];
    *v = u;
    return true;
}
static XXFC_MAYBE_UNUSED bool protocol_tlv_add(Abstractformat *f, pm_stream *s, memory_blob *b, const char *name,
                                               der_tlv *t) {
    return blob_add(f, s, b, name, t->start, t->end - t->start);
}
static bool protocol_mutf(memory_blob *b, uint64_t at, uint64_t n) {
    uint64_t end = at + n;
    while (at < end) {
        uint8_t c = b->p[(size_t)at++];
        if (!blob_span(b, at - 1, 1) || !c)
            return false;
        if (c < 128)
            continue;
        if ((c & 224) == 192) {
            if (at >= end || (b->p[(size_t)at] & 192) != 128)
                return false;
            uint16_t v = (uint16_t)(((c & 31) << 6) | (b->p[(size_t)at++] & 63));
            if (v < 128 && v)
                return false;
        } else if ((c & 240) == 224) {
            if (end - at < 2 || (b->p[(size_t)at] & 192) != 128 || (b->p[(size_t)at + 1] & 192) != 128)
                return false;
            uint16_t v = (uint16_t)(((c & 15) << 12) | ((b->p[(size_t)at] & 63) << 6) | (b->p[(size_t)at + 1] & 63));
            if (v < 2048)
                return false;
            at += 2;
        } else
            return false;
    }
    return true;
}
static XXFC_MAYBE_UNUSED bool protocol_utf16string(memory_blob *b, uint64_t *at, uint64_t end, uint64_t *start,
                                                   uint64_t *n) {
    if (!protocol_take(b, at, end, 2))
        return false;
    *n = xx_data_get_u16(b->p + (size_t)*at - 2, 2, 0, true);
    *start = *at;
    return protocol_take(b, at, end, *n) && protocol_mutf(b, *start, *n);
}
static bool protocol_tree(memory_blob *b, uint64_t at, uint64_t end, unsigned depth, unsigned *work) {
    der_tlv t;
    uint64_t prev = 0, prevn = 0;
    if (depth > 32)
        return false;
    while (at < end) {
        if (++*work > 65536 || !der_read(b, &at, end, &t))
            return false;
        uint64_t n = t.end - t.value;
        uint8_t *v = b->p + (size_t)t.value;
        if (!(t.tag & 192) && (t.tag & 32) && t.tag != 48 && t.tag != 49)
            return false;
        if (t.tag & 32) {
            if (!protocol_tree(b, t.value, t.end, depth + 1, work))
                return false;
            if (t.tag == 49) {
                uint64_t pos = t.value;
                der_tlv a;
                while (pos < t.end) {
                    if (!der_read(b, &pos, t.end, &a) ||
                        (prevn &&
                         serialized_cmp(b->p + (size_t)prev, prevn, b->p + (size_t)a.start, a.end - a.start) > 0))
                        return false;
                    prev = a.start;
                    prevn = a.end - a.start;
                }
                prevn = 0;
            }
        } else if (t.tag == 1) {
            if (n != 1 || (*v != 0 && *v != 255))
                return false;
        } else if ((t.tag == 2 || t.tag == 10)) {
            if (!n || (n > 1 && ((v[0] == 0 && !(v[1] & 128)) || (v[0] == 255 && (v[1] & 128)))))
                return false;
        } else if (t.tag == 3) {
            if (!n || v[0] > 7 || (n == 1 && v[0]) || (n > 1 && (v[n - 1] & ((1U << v[0]) - 1))))
                return false;
        } else if (t.tag == 5) {
            if (n)
                return false;
        } else if (t.tag == 6) {
            if (!der_oid(b, &t))
                return false;
        } else if (t.tag == 12) {
            if (!serialized_utf(b, t.value, n))
                return false;
        } else if (t.tag == 23 || t.tag == 24) {
            if (!der_date(v, n, t.tag))
                return false;
        } else if (t.tag == 19) {
            for (uint64_t i = 0; i < n; ++i) {
                uint8_t c = v[i];
                if ((c < 'a' || c > 'z') && (c < 'A' || c > 'Z') && (c < '0' || c > '9') && c != ' ' && c != 39 &&
                    c != '(' && c != ')' && c != '+' && c != ',' && c != '-' && c != '.' && c != '/' && c != ':' &&
                    c != '=' && c != '?')
                    return false;
            }
        } else if (t.tag == 22) {
            for (uint64_t i = 0; i < n; ++i)
                if (v[i] > 127)
                    return false;
        } else if (t.tag == 30) {
            if (n & 1)
                return false;
            for (uint64_t i = 0; i < n; i += 2) {
                uint16_t c = xx_data_get_u16(v + (size_t)i, 2, 0, true);
                if (c >= 0xd800 && c <= 0xdfff)
                    return false;
            }
        } else if (t.tag != 4 && t.tag != 20 && (t.tag & 192) != 128)
            return false;
    }
    return at == end;
}
static XXFC_MAYBE_UNUSED bool protocol_extensions(memory_blob *b, der_tlv *t) {
    uint64_t at = t->value, p, q, seen[1024], length[1024];
    unsigned count = 0, work = 0;
    der_tlv list, entry, oid, critical, value;
    if (!der_take(b, &at, t->end, 48, &list) || at != t->end || list.value == list.end)
        return false;
    at = list.value;
    while (at < list.end) {
        if (count >= 1024 || !der_take(b, &at, list.end, 48, &entry))
            return false;
        p = entry.value;
        if (!der_take(b, &p, entry.end, 6, &oid) || !der_oid(b, &oid))
            return false;
        for (unsigned i = 0; i < count; ++i)
            if (length[i] == oid.end - oid.value &&
                !xx_rt_memcmp(b->p + (size_t)seen[i], b->p + (size_t)oid.value, (size_t)length[i]))
                return false;
        seen[count] = oid.value;
        length[count++] = oid.end - oid.value;
        if (p < entry.end && b->p[(size_t)p] == 1) {
            if (!der_take(b, &p, entry.end, 1, &critical) || critical.end - critical.value != 1 ||
                b->p[(size_t)critical.value] != 255)
                return false;
        }
        if (!der_take(b, &p, entry.end, 4, &value) || value.value == value.end || p != entry.end)
            return false;
        q = value.value;
        if (!protocol_tree(b, value.value, value.end, 0, &work) || !der_read(b, &q, value.end, &critical) ||
            q != value.end)
            return false;
    }
    return at == list.end;
}
#endif
