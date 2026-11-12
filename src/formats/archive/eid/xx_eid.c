/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * EID container grammar established from local U3 reader behavior and ARC9.
 * The cipher and hash are independent native xxfclib implementations.
 */
#include "xxfclib/formats/archive/eid/xx_eid.h"
#include "../../xx_payload_members.h"
#include "xxfclib/algo/eid/xx_eid_crypto.h"
#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/codepages/xx_codepage.h"

#define EID_HEADER 152U
#define EID_MEMORY_CAP (256U * 1024U * 1024U)
#define EID_DIRECTORY_CAP (16U * 1024U * 1024U)
#define EID_RECORD_CAP 65536U
#define EID_MAX_SLOTS 1024U
/* Covers native bzip2/Deflate decoder working tables and I/O wrappers. */
#define EID_DECODE_WORKSPACE (8U * 1024U * 1024U)

typedef struct eid_member {
    uint8_t key[32];
    uint32_t attributes;
    uint64_t timestamp;
    uint64_t resident;
} eid_member;

#include "xx_eid_master_keys.inc"

static uint32_t eid_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void eid_clear(void *data, size_t size)
{
    volatile uint8_t *p = (volatile uint8_t *)data;
    while (size-- > 0U) *p++ = 0U;
}

static void eid_free_member(void *opaque)
{
    if (opaque) { eid_clear(opaque, sizeof(eid_member)); xx_mem_free(opaque); }
}

static uint64_t eid_budget(Abstractformat *f)
{
    const xx_var *limit = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n = limit ? xx_var_get_u64(limit) : EID_MEMORY_CAP;
    return n < EID_MEMORY_CAP ? n : EID_MEMORY_CAP;
}

static bool eid_header(const uint8_t h[EID_HEADER], int64_t available)
{
    uint32_t slots = eid_u32(h+104), count = eid_u32(h+116), tail = eid_u32(h+132);
    uint64_t start = EID_HEADER + (uint64_t)slots*64U;
    return h[0] < 104U && slots && slots <= EID_MAX_SLOTS && count && count <= 1000000U &&
           eid_u32(h+108) == 4U && eid_u32(h+112) == 4U && eid_u32(h+120) == 0U &&
           eid_u32(h+124) == 0x3ff00000U && eid_u32(h+128) == 64U && h[136] == 8U &&
           tail >= 50U && tail <= EID_DIRECTORY_CAP && count <= tail/50U &&
           available >= 0 && start <= (uint64_t)available && tail <= (uint64_t)available-start;
}

static bool eid_string(const uint8_t *record, size_t length, size_t *at, const uint8_t **text, size_t *size)
{
    uint32_t n;
    size_t i;
    if (length-*at < 4U) return false;
    n = eid_u32(record+*at); *at += 4U;
    if (!n || n > length-*at || record[*at+n-1U]) return false;
    for (i = 0; i+1U < n; ++i) if (!record[*at+i]) return false;
    *text = record+*at; *size = n-1U; *at += n;
    return true;
}

static bool eid_record(const uint8_t *record, size_t length, const uint8_t **name, size_t *name_size,
                       const uint8_t **path, size_t *path_size, const uint8_t **meta)
{
    size_t at = 0;
    if (!eid_string(record, length, &at, name, name_size) || !*name_size ||
        !eid_string(record, length, &at, path, path_size) || length-at != 36U) return false;
    *meta = record+at;
    return eid_u32(*meta+24U) <= INT32_MAX && eid_u32(*meta+32U) <= INT32_MAX;
}

/* Relative member paths retain directories. Reject traversal, rooted paths
 * and Windows special names instead of allowing a write outside the root. */
static bool eid_safe_path(const char *name)
{
    const char *start = name, *p = name;
    if (!name[0] || name[0] == '/') return false;
    for (;;) {
        unsigned char c = (unsigned char)*p;
        if (c && (c < 32U || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*')) return false;
        if (!c || c == '/') {
            size_t n = (size_t)(p-start), stem = 0;
            char upper[5] = {0};
            if (!n || start[n-1U] == '.' || start[n-1U] == ' ') return false;
            while (stem < n && start[stem] != '.' && stem < 4U) {
                char a = start[stem]; upper[stem++] = a >= 'a' && a <= 'z' ? (char)(a-'a'+'A') : a;
            }
            if ((stem == 3U && (!xx_rt_memcmp(upper,"CON",3U) || !xx_rt_memcmp(upper,"PRN",3U) ||
                               !xx_rt_memcmp(upper,"AUX",3U) || !xx_rt_memcmp(upper,"NUL",3U))) ||
                (stem == 4U && (!xx_rt_memcmp(upper,"COM",3U) || !xx_rt_memcmp(upper,"LPT",3U)) &&
                 upper[3] >= '1' && upper[3] <= '9' && (n == 4U || start[4] == '.'))) return false;
            if (!c) return true;
            start = p+1;
        }
        ++p;
    }
}

static char *eid_name(const uint8_t *name, size_t name_size, const uint8_t *path, size_t path_size)
{
    const xx_codepage *cp = xx_codepage_by_number(1252U);
    size_t pass, i, out = 0, total = name_size+path_size;
    char *result = (char *)xx_mem_alloc(total*3U+2U);
    if (!result || !cp) { xx_mem_free(result); return NULL; }
    while (path_size && (path[path_size-1U] == '/' || path[path_size-1U] == '\\')) --path_size;
    for (pass = 0; pass < 2U; ++pass) {
        const uint8_t *text = pass ? name : path;
        size_t size = pass ? name_size : path_size;
        if (pass && out) result[out++] = '/';
        for (i = 0; i < size; ++i) {
            size_t n;
            if (text[i] == '\\' || text[i] == '/') { result[out++] = '/'; continue; }
            n = xx_codepage_byte_to_utf8(cp, text[i], (unsigned char *)result+out, 3U);
            if (!n) { xx_mem_free(result); return NULL; }
            out += n;
        }
    }
    result[out] = 0;
    if (!eid_safe_path(result)) { xx_mem_free(result); return NULL; }
    return result;
}

static bool eid_decode(Abstractformat *f, pm_member *m, xx_io_device *output, xx_pd_struct *pd)
{
    eid_member *context = (eid_member *)m->context;
    uint8_t *packed = NULL, *plain = NULL;
    xx_io_device *sink = NULL;
    uint64_t budget = eid_budget(f), needed;
    size_t consumed = 0, at = 0;
    bool ok = false;
    const xx_var *limit = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (xx_pd_is_stopped(pd) || !context || (limit && (uint64_t)m->size > xx_var_get_u64(limit))) return false;
    if (!m->packed_size) return !m->size;
    needed = context->resident + EID_DECODE_WORKSPACE + (uint64_t)m->packed_size + (uint64_t)m->size + 1U;
    if (needed > budget) return false;
    packed = (uint8_t *)xx_mem_alloc((size_t)m->packed_size);
    plain = (uint8_t *)xx_mem_alloc(m->size ? (size_t)m->size : 1U);
    if (!packed || !plain || !pm_read(f, m->offset-f->base_address, packed, (size_t)m->packed_size) ||
        xx_pd_is_stopped(pd) || !xx_eid_rijndael_decrypt(packed, (size_t)m->packed_size, context->key, packed) ||
        xx_pd_is_stopped(pd)) goto done;
    sink = xx_io_mem_open(plain, (size_t)m->size);
    if (!sink) goto done;
    if (m->packed_size >= 4 && !xx_rt_memcmp(packed, "BZh", 3U)) {
        ok = xx_bzip2_unpack_memory_to_device_ex(packed, (size_t)m->packed_size, sink, &consumed, pd) && consumed == (size_t)m->packed_size;
    } else if (m->packed_size >= 6 && xx_zlib_stream_header_is_valid(packed, (size_t)m->packed_size)) {
        ok = xx_deflate_unpack_memory_to_device_ex(packed+2U, (size_t)m->packed_size-6U, sink, &consumed, false, pd) &&
             consumed == (size_t)m->packed_size-6U && xx_io_tell(sink) == m->size &&
             xx_zlib_stream_trailer_matches(packed, (size_t)m->packed_size, plain, (size_t)m->size);
    }
    ok = ok && xx_io_tell(sink) == m->size && !xx_pd_is_stopped(pd);
    while (ok && output && at < (size_t)m->size) {
        size_t n = (size_t)m->size-at;
        ssize_t written;
        if (xx_pd_is_stopped(pd)) { ok = false; break; }
        if (n > 65536U) n = 65536U;
        written = xx_io_write(output, plain+at, n);
        if (written <= 0 || (size_t)written > n) { ok = false; break; }
        at += (size_t)written;
    }
done:
    if (sink) xx_io_close(sink);
    xx_mem_free(packed); xx_mem_free(plain);
    return ok;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[EID_HEADER], slot[64], key[32] = {0};
    uint8_t *directory = NULL, *record = NULL;
    const uint8_t *name, *path, *meta;
    size_t name_size, path_size, at = 0, index, capacity = 8U;
    uint32_t slots, count, tail, first_size;
    uint64_t payload, directory_start, resident, budget = eid_budget(f);
    int64_t available = pm_available(f);
    bool found = false, ok = false;
    if (xx_pd_is_stopped(pd) || available < EID_HEADER || !pm_read(f, 0, h, sizeof(h)) || !eid_header(h, available)) return false;
    slots = eid_u32(h+104); count = eid_u32(h+116); tail = eid_u32(h+132);
    payload = EID_HEADER + (uint64_t)slots*64U;
    directory_start = (uint64_t)available-tail;
    while (capacity < count) capacity *= 2U;
    resident = sizeof(pm_stream) + (uint64_t)capacity*sizeof(pm_member) + (uint64_t)count*sizeof(eid_member);
    if (resident > budget || tail+EID_RECORD_CAP > budget-resident) return false;
    directory = (uint8_t *)xx_mem_alloc(tail);
    record = (uint8_t *)xx_mem_alloc(EID_RECORD_CAP);
    if (!directory || !record || !pm_read(f, (int64_t)directory_start, directory, tail)) goto done;
    first_size = eid_u32(directory);
    if (!first_size || first_size > EID_RECORD_CAP || first_size > tail-4U) goto done;
    for (index = 0; index < slots && !found; ++index) {
        size_t candidate;
        if (!pm_read(f, EID_HEADER+(int64_t)index*64, slot, sizeof(slot))) goto done;
        for (candidate = 0; candidate < 12U; ++candidate) {
            if (xx_pd_is_stopped(pd)) goto done;
            if (!xx_eid_derive_key(eid_master_keys[candidate], slot, key) ||
                !xx_eid_rijndael_decrypt(directory+4U, first_size, key, record)) goto done;
            if (eid_record(record, first_size, &name, &name_size, &path, &path_size, &meta)) { found = true; break; }
        }
    }
    if (!found) goto done;
    for (index = 0; index < count; ++index) {
        uint32_t length, size, packed_size, attributes;
        char *full_name;
        eid_member *context;
        pm_member *m;
        const xx_var *limit;
        if (xx_pd_is_stopped(pd) || tail-at < 4U) goto done;
        length = eid_u32(directory+at); at += 4U;
        if (!length || length > EID_RECORD_CAP || length > tail-at ||
            !xx_eid_rijndael_decrypt(directory+at, length, key, record) ||
            !eid_record(record, length, &name, &name_size, &path, &path_size, &meta)) goto done;
        size = eid_u32(meta+24U); attributes = eid_u32(meta+28U); packed_size = eid_u32(meta+32U);
        if (packed_size > directory_start-payload || (!packed_size && size) ||
            ((attributes & 0x10U) && (size || packed_size))) goto done;
        limit = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MAX_MEMBER_SIZE);
        if (limit && size > xx_var_get_u64(limit)) goto done;
        if ((name_size+path_size)*3U+2U > budget-resident-tail-EID_RECORD_CAP) goto done;
        full_name = eid_name(name, name_size, path, path_size);
        if (!full_name) goto done;
        resident += xx_rt_strlen(full_name)+1U;
        context = (eid_member *)xx_mem_alloc(sizeof(*context));
        if (!context) { xx_mem_free(full_name); goto done; }
        xx_mem_zero(context, sizeof(*context));
        xx_rt_memcpy(context->key, key, 32U); context->attributes = attributes;
        /* Delphi TDateTime: little-endian binary64 days from 1899-12-30. */
        {
            uint64_t bits = (uint64_t)eid_u32(meta) | ((uint64_t)eid_u32(meta+4U) << 32);
            double days;
            xx_rt_memcpy(&days, &bits, sizeof(days));
            if (days >= 25569.0 && days <= 3000000.0) context->timestamp = (uint64_t)((days-25569.0)*86400.0);
        }
        if (!pm_add(f, s, "eid", (int64_t)payload, packed_size)) {
            xx_mem_free(full_name); eid_free_member(context); goto done;
        }
        m = &s->items[s->count-1U];
        m->display_name = full_name; m->size = size; m->source_encrypted = true;
        m->directory = (attributes & 0x10U) != 0;
        m->context = context; m->free_context = eid_free_member; m->read_all = eid_decode;
        if (packed_size) {
            uint8_t prefix[16];
            size_t take = packed_size < sizeof(prefix) ? packed_size : sizeof(prefix);
            m->compression_method = UINT16_MAX;
            if (!pm_read(f, (int64_t)payload, prefix, take) || !xx_eid_rijndael_decrypt(prefix, take, key, prefix)) goto done;
            if (take >= 4U && !xx_rt_memcmp(prefix, "BZh", 3U)) m->compression_method = 12U;
            else if (xx_zlib_stream_header_is_valid(prefix, take)) m->compression_method = 8U;
            eid_clear(prefix, sizeof(prefix));
        }
        payload += packed_size; at += length;
    }
    if (at != tail || payload != directory_start) goto done;
    for (index = 0; index < s->count; ++index) ((eid_member *)s->items[index].context)->resident = resident;
    s->size = available;
    ok = !xx_pd_is_stopped(pd);
done:
    eid_clear(slot, sizeof(slot)); eid_clear(key, sizeof(key));
    if (record) eid_clear(record, EID_RECORD_CAP);
    xx_mem_free(record); xx_mem_free(directory);
    return ok;
}

static const xx_archive_record *eid_current(Abstractformat *f, xx_archive_record_state *state)
{
    const xx_archive_record *record = pm_current(f, state);
    if (record) {
        pm_stream *s = (pm_stream *)state->internal_state;
        eid_member *context = (eid_member *)s->items[s->index].context;
        state->current_record.header_size = EID_HEADER;
        if (!xx_archive_record_set_meta_u64(&state->current_record, XX_META_ID_ATTRIBUTES, context->attributes) ||
            !xx_archive_record_set_meta_u64(&state->current_record, XX_META_ID_TIMESTAMP, context->timestamp)) return NULL;
    }
    return record;
}

Abstractformat *xx_eid_create(xx_io_device *device, int64_t base_address)
{
    Abstractformat *format = (Abstractformat *)xx_mem_alloc(sizeof(*format));
    if (format) { pm_init(format, device, base_address, XX_FILE_TYPE_EID, "eid"); format->get_current_archive_record = eid_current; }
    return format;
}

void xx_eid_free(Abstractformat *format)
{
    if (format) { xx_format_destroy(format); xx_mem_free(format); }
}

#include "../../xx_format_abstract_extractor_adapter.h"
static Abstractformat *eid_open(xx_io_device *device) { return xx_eid_create(device, 0); }
static const xx_file_type_t eid_types[] = {XX_FILE_TYPE_EID};
static const uint8_t eid_anchor[] = {4,0,0,0,4,0,0,0};
static const xx_format_search_anchor eid_anchors[] = {{eid_anchor, 8U, 108U}};
const xx_format_search_desc xx_eid_search_desc = {eid_types, 1U, eid_anchors, 1U, eid_open, xx_eid_free, true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(eid, xx_eid_search_desc)
