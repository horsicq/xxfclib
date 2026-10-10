/* Copyright (c) 2026 hors<horsicq@gmail.com> -- SPDX-License-Identifier: MIT
 * SAR.DOC / XArchive archives/xsar.cpp: LHA level 0/1, tags LH0/LH4/LH5.
 * Reuse the complete checked LHA member-header grammar with a selected-only
 * tag policy. Decoding uses the library's existing LZH and CRC algorithms. */
#include "xxfclib/formats/sar/xx_sar.h"
#include "xxfclib/algo/lzh/xx_lzh.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "../xx_payload_members.h"
#define SAR_MAX_BUFFER (UINT64_C(256) * 1024U * 1024U)
typedef struct sar_member {
    uint32_t method;
    uint16_t crc;
    uint64_t timestamp;
    int64_t header_offset, header_size;
} sar_member;
static bool sar_safe_name(const char *name)
{
    const char *part = name, *at = name;
    if (!name || !*name || *name == '/' || *name == '\\') return false;
    for (;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (!c || c == '/') {
            size_t n = (size_t)(at - part);
            if (!n || (n == 1 && part[0] == '.') || (n == 2 && part[0] == '.' && part[1] == '.')) return false;
            if (!c) break;
            part = at + 1;
        } else if (c < 32 || c == 127 || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
    }
    return true;
}
static bool sar_read_all(Abstractformat *f, pm_member *m, xx_io_device *out, xx_pd_struct *pd)
{
    sar_member *context = (sar_member *)m->context;
    uint8_t *packed = NULL, *plain = NULL;
    uint64_t budget;
    size_t i, written = 0, at = 0;
    uint16_t crc = 0;
    bool valid = false;
    const xx_var *limit = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    if (!context || m->size < 0 || m->packed_size < 0 || (uint64_t)m->size > SAR_MAX_BUFFER || (uint64_t)m->packed_size > SAR_MAX_BUFFER || (pd && xx_pd_is_stopped(pd)))
        return false;
    budget = (uint64_t)m->packed_size + (context->method == UINT32_C(0x6c6830) ? 0U : (uint64_t)m->size);
    if (limit && budget > xx_var_get_u64(limit)) return false;
    packed = (uint8_t *)xx_mem_alloc(m->packed_size ? (size_t)m->packed_size : 1U);
    if (!packed) goto done;
    while (at < (size_t)m->packed_size) {
        size_t n = (size_t)m->packed_size - at;
        if (n > 65536) n = 65536;
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, m->offset - f->base_address + (int64_t)at, packed + at, n)) goto done;
        at += n;
    }
    if (context->method == UINT32_C(0x6c6830)) {
        if (m->size != m->packed_size) goto done;
        plain = packed;
        written = (size_t)m->size;
    } else {
        plain = (uint8_t *)xx_mem_alloc(m->size ? (size_t)m->size : 1U);
        if (!plain) goto done;
        if (!m->packed_size || !m->size ||
            !xx_lzh5_decode_memory(packed, (size_t)m->packed_size, plain, (size_t)m->size, context->method == UINT32_C(0x6c6834) ? 4 : 5, &written))
            goto done;
    }
    if (written != (size_t)m->size) goto done;
    for (i = 0; i < written;) {
        size_t n = written - i;
        if (n > 16384) n = 16384;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        crc = xx_crc16_arc_calc(crc, plain + i, n);
        i += n;
    }
    if (crc != context->crc) {
        xx_pd_set_error(pd, 1, "SAR member payload CRC mismatch");
        goto done;
    }
    for (i = 0; out && i < written;) {
        size_t n = written - i;
        ssize_t count;
        if (n > 65536) n = 65536;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        count = xx_io_write(out, plain + i, n);
        if (count <= 0 || (size_t)count > n) goto done;
        i += (size_t)count;
    }
    valid = !(pd && xx_pd_is_stopped(pd));
done:
    if (plain && plain != packed) xx_mem_free(plain);
    xx_mem_free(packed);
    return valid;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    xx_lha reader;
    xx_archive_record_state *st = NULL;
    bool valid = false;
    xx_lha_init(&reader, f->device, f->base_address);
    reader.sar_tags = true;
    if (!xx_lha_handle_base_info(&reader.format, pd) || !(st = xx_lha_create_archive_records_reading(&reader.format, NULL, pd))) goto done;
    while (st->has_record) {
        const xx_archive_record *r = xx_lha_get_current_archive_record(&reader.format, st);
        const char *name;
        sar_member *context;
        pm_member *m;
        uint8_t prefix[22], crc[2];
        uint64_t size, method;
        if (!r || (pd && xx_pd_is_stopped(pd))) goto done;
        name = xx_archive_record_get_original_name(r);
        size = xx_archive_record_get_meta_u64(r, XX_META_ID_UNCOMPRESSED_SIZE, UINT64_MAX);
        method = xx_archive_record_get_meta_u64(r, XX_META_ID_COMPRESSION_METHOD, 0);
        if (!sar_safe_name(name) || size > INT64_MAX || (method != UINT32_C(0x6c6830) && method != UINT32_C(0x6c6834) && method != UINT32_C(0x6c6835)) ||
            !pm_read(f, r->header_offset - f->base_address, prefix, sizeof(prefix)) || !pm_read(f, r->header_offset - f->base_address + 22 + prefix[21], crc, 2) ||
            !pm_add(f, s, "payload", r->data_offset - f->base_address, r->compressed_size))
            goto done;
        m = &s->items[s->count - 1];
        m->size = (int64_t)size;
        m->display_name = xx_str_dup(name);
        m->read_all = sar_read_all;
        context = (sar_member *)xx_mem_alloc(sizeof(*context));
        if (!context || !m->display_name) {
            xx_mem_free(context);
            goto done;
        }
        context->method = (uint32_t)method;
        context->crc = (uint16_t)(crc[0] | ((uint16_t)crc[1] << 8));
        context->timestamp = xx_archive_record_get_meta_u64(r, XX_META_ID_TIMESTAMP, 0);
        context->header_offset = r->header_offset;
        context->header_size = r->header_size;
        m->context = context;
        m->free_context = xx_mem_free;
        if (!xx_lha_archive_record_move_to_next(&reader.format, st, pd)) break;
    }
    s->size = reader.format.format_size;
    if (s->count) {
        char version[4] = {'L', 'H', 0, 0};
        version[2] = (char)(((sar_member *)s->items[0].context)->method & 0xffU);
        xx_format_set_version(f, version);
    }
    valid = s->count != 0 && !(pd && xx_pd_is_stopped(pd));
done:
    if (st) xx_lha_free_archive_records_reading(&reader.format, st);
    xx_lha_destroy(&reader);
    return valid;
}
static bool sar_record(xx_archive_record_state *st)
{
    pm_stream *s = (pm_stream *)st->internal_state;
    sar_member *m = (sar_member *)s->items[s->index].context;
    st->current_record.header_offset = m->header_offset;
    st->current_record.header_size = m->header_size;
    return xx_archive_record_set_meta_u64(&st->current_record, XX_META_ID_COMPRESSION_METHOD, m->method) &&
           xx_archive_record_set_meta_u64(&st->current_record, XX_META_ID_TIMESTAMP, m->timestamp);
}
static xx_archive_record_state *sar_records(Abstractformat *f, const xx_list_s *opts, xx_pd_struct *pd)
{
    xx_archive_record_state *st = pm_create_records(f, opts, pd);
    if (st && st->has_record && !sar_record(st)) {
        pm_free_records(f, st);
        return NULL;
    }
    return st;
}
static bool sar_next(Abstractformat *f, xx_archive_record_state *st, xx_pd_struct *pd)
{
    return pm_next(f, st, pd) && sar_record(st);
}
void xx_sar_init(xx_sar *r, xx_io_device *d, int64_t base)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, base, XX_FILE_TYPE_SAR, "sar");
        r->sar_tags = true;
        xx_format_set_mime_type(&r->format, "application/x-lzh-compressed");
        r->format.create_archive_records_reading = sar_records;
        r->format.archive_record_move_to_next = sar_next;
    }
}
xx_sar *xx_sar_create(xx_io_device *d, int64_t base)
{
    xx_sar *r = (xx_sar *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sar_init(r, d, base);
    return r;
}
void xx_sar_destroy(xx_sar *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sar_free(xx_sar *r)
{
    if (r) {
        xx_sar_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_sar_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_sar_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
xx_file_type_t xx_sar_detect(xx_io_device *d, int64_t base)
{
    xx_sar r;
    bool valid;
    xx_sar_init(&r, d, base);
    valid = pm_valid(&r.format, NULL);
    xx_sar_destroy(&r);
    return valid ? XX_FILE_TYPE_SAR : XX_FILE_TYPE_UNKNOWN;
}
