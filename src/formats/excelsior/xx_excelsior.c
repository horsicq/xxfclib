/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently parsed from vendor-produced ExcelsiorII1 packages. No
 * installer instructions, imports, registration or postinstall code execute.
 * Offsets in the table are relative to the installer image, not the overlay.
 * Each stream is u64 output size, five LZMA properties, then raw LZMA1; the
 * complete output has x86 BCJ starting at address zero. There is no stored
 * per-member checksum. See tests/excelsior/provenance.json and FORMAT.md.
 */
#include "xxfclib/formats/excelsior/xx_excelsior.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "../ue2_indexed.h"
#include "../7zip/xx_7zip_branch.h"
#include "../7zip/xx_7zip_defs.h"

#define EI_TYPE ((xx_file_type_t)2767)
#define EI_LIMIT (UINT64_C(256) * 1024 * 1024)
#define EI_TABLE_LIMIT (UINT64_C(16) * 1024 * 1024)
#define EI_DICT_LIMIT (UINT64_C(32) * 1024 * 1024)
#define EI_MODEL_BUDGET (UINT64_C(4) * 1024 * 1024)
typedef struct ei_index {
    ue2_index records;
    int64_t stream, packed;
    uint64_t unpacked, dictionary, owned;
    uint8_t *decoded;
    xx_io_device *source_device;
    int64_t source_base;
} ei_index;
typedef struct ei_format { Abstractformat format; ei_index *index; uint64_t generation; } ei_format;
typedef struct ei_state {ue2_state record;uint64_t generation;} ei_state;

static bool ei_fail(xx_pd_struct *pd, const char *text) {
    if (pd) xx_pd_set_error(pd, 1, text);
    return false;
}
static uint64_t ei_budget(Abstractformat *f, const xx_list_s *opts) {
    const xx_var *v = xx_format_resolve_extra_parameter(f, opts, XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n = v ? xx_var_get_u64(v) : EI_LIMIT;
    return n < EI_LIMIT ? n : EI_LIMIT;
}
static bool ei_stream_header(Abstractformat *f, int64_t at, int64_t end,
                             uint8_t header[13], uint64_t *dictionary) {
    unsigned lc, lp, prop;
    if (!ue2_range(end, at, 18) || !ue2_read(f, at, header, 13)) return false;
    prop = header[8]; lc = prop % 9U; lp = (prop / 9U) % 5U;
    *dictionary = ue2_u32(header + 9);
    return prop < 225U && lc + lp <= 4U && *dictionary <= EI_DICT_LIMIT &&
           ue2_u64(header) > 0 && ue2_u64(header) <= EI_LIMIT;
}
static uint8_t *ei_decode(Abstractformat *f, int64_t at, int64_t end,
                          uint64_t cap, uint64_t *length, xx_pd_struct *pd) {
    uint8_t h[13], *out = NULL;
    uint64_t dictionary, n;
    size_t written = 0;
    if ((pd && xx_pd_is_stopped(pd)) || !ei_stream_header(f, at, end, h, &dictionary)) return NULL;
    n = ue2_u64(h);
    if (n > cap || dictionary > cap - n || EI_MODEL_BUDGET > cap - n - dictionary) return NULL;
    out = (uint8_t *)xx_mem_alloc((size_t)n);
    if (!out) return NULL;
    if (!xx_lzma_unpack_device_to_memory(f->device, at + 13, end - at - 13,
                                         h + 8, 5, (int64_t)n, out, (size_t)n, &written, pd) ||
        written != n || (pd && xx_pd_is_stopped(pd)) ||
        !xx_7zip_branch_decode(XX_7ZIP_METHOD_BCJ, NULL, 0, out, (size_t)n, out, (size_t)n)) {
        xx_mem_free(out); return NULL;
    }
    *length = n; return out;
}
/* Exact overlay from a PE section table, or a raw Excelsior header. No
 * searching a member or an arbitrary executable resource for a child magic. */
static bool ei_read_at(xx_io_device *device, int64_t at, void *output, size_t size) {
    size_t done = 0;
    if (!device || !ue2_range(xx_io_total_size(device), at, (int64_t)size) ||
        xx_io_seek64(device, at, XX_RT_SEEK_SET)) return false;
    while (done < size) {
        ssize_t got = xx_io_read(device, (uint8_t *)output + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}
static int64_t ei_header_device(xx_io_device *device, int64_t base) {
    uint8_t h[64], pe[24], sec[40];
    int64_t total = xx_io_total_size(device), end;
    uint32_t peoff; unsigned count, optional, i;
    if (base < 0 || base > total || total - base < 64 || !ei_read_at(device, base, h, 12)) return -1;
    if (!xx_rt_memcmp(h, "ExcelsiorII1", 12)) return base;
    if (!ei_read_at(device, base, h, 64) || h[0] != 'M' || h[1] != 'Z') return -1;
    peoff = ue2_u32(h + 60);
    if (peoff > 16U * 1024U * 1024U || !ue2_range(total - base, peoff, 24) || !ei_read_at(device, base + peoff, pe, 24) ||
        xx_rt_memcmp(pe, "PE\0\0", 4)) return -1;
    count = ue2_u16(pe + 6); optional = ue2_u16(pe + 20);
    if (!count || count > 96 || optional < 64 || optional > 4096 ||
        !ue2_range(total - base, (int64_t)peoff + 24, (int64_t)optional + count * 40) ||
        !ei_read_at(device, base + peoff + 24 + 60, h, 4)) return -1;
    end = ue2_u32(h);
    if (end > total - base) return -1;
    for (i = 0; i < count; ++i) {
        uint64_t finish;
        if (!ei_read_at(device, base + peoff + 24 + optional + i * 40, sec, 40)) return -1;
        finish = (uint64_t)ue2_u32(sec + 20) + ue2_u32(sec + 16);
        if (finish > (uint64_t)(total - base)) return -1;
        if ((int64_t)finish > end) end = (int64_t)finish;
    }
    return ei_read_at(device, base + end, h, 12) && !xx_rt_memcmp(h, "ExcelsiorII1", 12) ? base + end : -1;
}
static char *ei_name(const uint8_t *table, size_t size, uint64_t at, uint64_t room, uint64_t *allocation) {
    size_t i, n = 0; char *out;
    if (at >= size || (at & 1U)) return NULL;
    for (i = (size_t)at; i + 1 < size && ue2_u16(table + i); i += 2) {
        if (i - at > 8192) return NULL;
    }
    if (i + 1 >= size) return NULL;
    *allocation = (i - (size_t)at) * 2 + 1;
    if (*allocation > room) return NULL;
    out = (char *)xx_mem_alloc((size_t)*allocation);
    if (!out) return NULL;
    for (i = (size_t)at; ue2_u16(table + i); i += 2) {
        uint32_t c = ue2_u16(table + i);
        if (c >= 0xD800 && c <= 0xDBFF) {
            uint32_t low;
            if (i + 3 >= size || (low = ue2_u16(table + i + 2)) < 0xDC00 || low > 0xDFFF) goto bad;
            c = 0x10000 + ((c - 0xD800) << 10) + low - 0xDC00; i += 2;
        } else if (c >= 0xDC00 && c <= 0xDFFF) goto bad;
        if (c == '\\') c = '/';
        if (c < 0x80) out[n++] = (char)c;
        else if (c < 0x800) { out[n++] = (char)(0xC0 | (c >> 6)); out[n++] = (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { out[n++] = (char)(0xE0 | (c >> 12)); out[n++] = (char)(0x80 | ((c >> 6) & 63)); out[n++] = (char)(0x80 | (c & 63)); }
        else { out[n++] = (char)(0xF0 | (c >> 18)); out[n++] = (char)(0x80 | ((c >> 12) & 63)); out[n++] = (char)(0x80 | ((c >> 6) & 63)); out[n++] = (char)(0x80 | (c & 63)); }
    }
    out[n] = 0;
    if (ue2_safe_name(out)) return out;
bad:
    xx_mem_free(out); return NULL;
}
static ei_index *ei_parse(Abstractformat *f, const xx_list_s *opts, xx_pd_struct *pd) {
    uint8_t h[40], dh[13]; uint8_t *table = NULL;
    ei_index *ix = NULL;
    int64_t at, total = xx_io_total_size(f->device), saved = xx_io_tell(f->device), base = f->base_address;
    uint64_t index_at, table_size = 0, count, records, data, datasize, dictionary, stream, packed, i, names_at, budget=ei_budget(f,opts);
    at = ei_header_device(f->device, f->base_address);
    if (at < 0 || !ue2_read(f, at, h, 40)) goto done;
    index_at = ue2_u64(h + 16);
    if (index_at > (uint64_t)(total - base) || index_at < (uint64_t)(at - base + 40) ||
        ue2_u32(h + 24) > EI_TABLE_LIMIT || ue2_u32(h + 28) > EI_LIMIT) goto done;
    table = ei_decode(f, base + (int64_t)index_at, total, budget, &table_size, pd);
    if (!table || table_size != ue2_u32(h + 24) || table_size < 0x128 ||
        xx_rt_memcmp(table, "ExcelsiorII1", 12) || ue2_u32(table + 12) != 0x01000001 ||
        ue2_u32(table + 20) != 1 || ue2_u64(table + 24) != (UINT64_C(1) << 32)) goto done;
    stream = ue2_u64(table + 32); packed = ue2_u64(table + 40);
    count = ue2_u64(table + 80); data = ue2_u64(table + 88); datasize = ue2_u64(table + 96); records = ue2_u64(table + 104);
    if (!count || count > 100000 || records < 0x128 || records > table_size || count > (table_size - records) / 32 ||
        stream != ue2_u64(h + 32) || stream >= index_at || packed != index_at - stream ||
        !ei_stream_header(f, base + (int64_t)stream, base + (int64_t)index_at, dh, &dictionary) ||
        data > ue2_u64(dh) || datasize > ue2_u64(dh) - data || data != (uint64_t)ue2_u32(h + 28) + ue2_u64(table + 64)) goto done;
    names_at = records + count * 32;
    if (table_size > budget || sizeof(*ix) > budget - table_size) goto done;
    ix = (ei_index *)xx_mem_calloc(1, sizeof(*ix));
    if (!ix) goto done;
    ix->records.size = total - base; ix->stream = base + (int64_t)stream; ix->packed = (int64_t)packed;
    ix->unpacked = ue2_u64(dh); ix->dictionary = dictionary; ix->owned = sizeof(*ix);ix->source_device=f->device;ix->source_base=f->base_address;
    for (i = 0; i < count; ++i) {
        const uint8_t *r = table + (size_t)records + (size_t)i * 32;
        uint64_t size = ue2_u64(r + 16), name_at = ue2_u64(r);
        char *name; uint64_t temporary, name_bytes; size_t old_capacity=ix->records.capacity, new_capacity=old_capacity;
        if ((pd && xx_pd_is_stopped(pd)) || name_at < names_at || ix->owned > budget - table_size ||
            !(name = ei_name(table, (size_t)table_size, name_at, budget - table_size - ix->owned, &temporary))) goto bad;
        name_bytes=xx_rt_strlen(name)+1;
        if (ix->records.count==old_capacity) new_capacity=old_capacity?old_capacity*2:32;
        if (temporary > budget-table_size-ix->owned || name_bytes > budget-table_size-ix->owned-temporary ||
            (new_capacity!=old_capacity && new_capacity*sizeof(ue2_member)>budget-table_size-ix->owned-temporary-name_bytes)) {xx_str_free(name);goto bad;}
        if (size != UINT64_MAX && size > datasize) { xx_str_free(name); goto bad; }
        if (!ue2_add(&ix->records, name, (int64_t)data, size == UINT64_MAX ? 0 : (int64_t)size, ue2_u64(r + 8))) { xx_str_free(name); goto bad; }
        ix->owned += name_bytes+(new_capacity-old_capacity)*sizeof(ue2_member); xx_str_free(name);
        ix->records.members[ix->records.count - 1].is_folder = size == UINT64_MAX;
        if (size != UINT64_MAX) { data += size; datasize -= size; }
    }
    if (datasize || data > ix->unpacked) goto bad;
    goto done;
bad:
    ue2_index_free(&ix->records); ix = NULL;
done:
    xx_mem_free(table);
    if (saved >= 0) xx_io_seek64(f->device, saved, XX_RT_SEEK_SET);
    return ix;
}
static bool ei_info_options(Abstractformat *f, const xx_list_s *opts, xx_pd_struct *pd) {
    ei_index *ix;ei_format *format=(ei_format*)f;
    if(!f)return false;
    if(format->index){
        if(f->base_info_handled&&f->is_valid&&format->index->source_device==f->device&&format->index->source_base==f->base_address)return format->index->owned<=ei_budget(f,opts);
        xx_mem_free(format->index->decoded);ue2_index_free(&format->index->records);format->index=NULL;++format->generation;if(!format->generation)++format->generation;f->base_info_handled=false;f->is_valid=false;f->number_of_archive_records=0;
    }
    ix = ei_parse(f, opts, pd);
    return ix ? ue2_accept(f, &ix->records) : ei_fail(pd, "Excelsior Installer is damaged, unsupported, or exceeds the memory limit");
}
static bool ei_info(Abstractformat *f,xx_pd_struct *pd) {return ei_info_options(f,NULL,pd);}
static int64_t ei_size(Abstractformat *f,xx_pd_struct *pd) {return f&&ei_info(f,pd)?f->format_size:-1;}
static uint64_t ei_count(Abstractformat *f,xx_pd_struct *pd) {return f&&ei_info(f,pd)?f->number_of_archive_records:0;}
static xx_archive_record_state *ei_records(Abstractformat *f, const xx_list_s *opts, xx_pd_struct *pd) {
    ei_index *ix;xx_archive_record_state *s;ei_state *state;
    if (!ei_info_options(f, opts, pd)) return NULL;
    ix = ((ei_format *)f)->index;
    if (!ix || ix->owned > ei_budget(f, opts)) return NULL;
    s=ue2_records(f,opts,pd);if(!s)return NULL;
    state=(ei_state*)xx_mem_alloc(sizeof(*state));if(!state){xx_archive_record_state_free(s);return NULL;}
    state->record=*(ue2_state*)s->internal_state;state->generation=((ei_format*)f)->generation;xx_mem_free(s->internal_state);s->internal_state=state;return s;
}
static const xx_archive_record *ei_current(Abstractformat *f, xx_archive_record_state *s) {
    ei_index *ix = f ? ((ei_format *)f)->index : NULL;
    ue2_state *state = s ? (ue2_state *)s->internal_state : NULL;
    const xx_archive_record *r = ue2_current(f, s);
    if (!r || !ix || !f->base_info_handled || !f->is_valid || !state || ix->source_device!=f->device || ix->source_base!=f->base_address || ((ei_state*)state)->generation!=((ei_format*)f)->generation || state->index != &ix->records || state->cursor >= ix->records.count) return NULL;
    if (!xx_archive_record_set_meta_u64(&s->current_record, XX_META_ID_COMPRESSION_METHOD, 0x30101) ||
        !xx_archive_record_set_meta_u64(&s->current_record, XX_META_ID_TIMESTAMP, ix->records.members[state->cursor].tag)) return NULL;
    return r;
}
static bool ei_next(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd) {
    return ei_current(f, s) && ue2_next(f, s, pd);
}
static bool ei_unpack(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd) {
    const xx_archive_record *r = ei_current(f, s);
    ei_index *ix = ((ei_format *)f)->index;
    ue2_state *state = s ? (ue2_state *)s->internal_state : NULL;
    const xx_var *pathvar, *maxvar;
    uint64_t cap = ei_budget(f, s ? &s->options : NULL), n = 0;
    int64_t saved = xx_io_tell(f->device);
    const char *base = NULL; char *converted = NULL, *path = NULL;
    xx_io_device *memory = NULL;
    bool result = false, folder;
    if (!r || (pd && xx_pd_is_stopped(pd))) return false;
    folder = ix->records.members[state->cursor].is_folder;
    maxvar = xx_format_resolve_extra_parameter(f, &s->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (maxvar && (uint64_t)r->compressed_size > xx_var_get_u64(maxvar)) return false;
    if (ix->owned > cap || ix->unpacked > cap - ix->owned || ix->dictionary > cap - ix->owned - ix->unpacked ||
        EI_MODEL_BUDGET > cap - ix->owned - ix->unpacked - ix->dictionary) return ei_fail(pd, "Excelsior solid stream exceeds the memory limit");
    if (!folder && !ix->decoded) {
        ix->decoded = ei_decode(f, ix->stream, ix->stream + ix->packed, cap - ix->owned, &n, pd);
        if (!ix->decoded || n != ix->unpacked) goto done;
    }
    pathvar = xx_format_resolve_extra_parameter(f, &s->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!pathvar) { result = true; goto done; } /* TEST: only the bounded RAM stream above. */
    if (pathvar->type == XX_VAR_TYPE_STRING || pathvar->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(pathvar);
    else if (pathvar->type == XX_VAR_TYPE_WSTRING || pathvar->type == XX_VAR_TYPE_WSTRING_VIEW) { converted = xx_str_unicode_to_utf8(xx_var_get_wstr(pathvar)); base = converted; }
    if (!base || !ue2_safe_name(xx_archive_record_get_original_name(r)) ||
        !(path = xx_str_concat3(base, "/", xx_archive_record_get_original_name(r))) || !xx_store_create_dirs_a(path, folder)) goto done;
    if (folder) result = true;
    else if ((memory = xx_io_mem_open_ro(ix->decoded, (size_t)ix->unpacked)) != NULL)
        result = xx_store_unpack_device_to_file(memory, r->data_offset, r->compressed_size, path, pd);
done:
    xx_io_close(memory); xx_str_free(path); xx_str_free(converted);
    if (saved >= 0) xx_io_seek64(f->device, saved, XX_RT_SEEK_SET);
    return result;
}
static void ei_destroy(Abstractformat *f) {
    ei_index *ix = ((ei_format *)f)->index;
    if (ix) { xx_mem_free(ix->decoded); ue2_index_free(&ix->records); }
    ((ei_format *)f)->index = NULL;++((ei_format*)f)->generation;if(!((ei_format*)f)->generation)++((ei_format*)f)->generation;f->base_info_handled=false;f->is_valid=false;f->number_of_archive_records=0; xx_format_cleanup_extra_parameters(f);
}
Abstractformat *xx_excelsior_create(xx_io_device *device, int64_t base) {
    ei_format *f = (ei_format *)xx_mem_calloc(1, sizeof(*f));
    if (!f) { return NULL; } f->generation=1;
    ue2_init_format(&f->format, device, base, EI_TYPE, "exe", "application/x-excelsior-installer");
    f->format.check_is_valid = ei_info; f->format.handle_base_info = ei_info; f->format.create_archive_records_reading = ei_records;
    f->format.get_format_size=ei_size;f->format.get_number_of_archive_records=ei_count;
    f->format.get_current_archive_record = ei_current; f->format.archive_record_move_to_next = ei_next;
    f->format.unpack_current_archive_record = ei_unpack; f->format.destroy = ei_destroy;
    return &f->format;
}
void xx_excelsior_free(Abstractformat *f) { if (f) { ei_destroy(f); xx_mem_free(f); } }
xx_file_type_t xx_excelsior_detect_device(xx_io_device *device, xx_pd_struct *pd) {
    Abstractformat *f; xx_file_type_t result = XX_FILE_TYPE_UNKNOWN;
    int64_t saved;
    if (!device || (pd && xx_pd_is_stopped(pd))) return result;
    saved = xx_io_tell(device);
    if (ei_header_device(device, 0) < 0) {
        if (saved >= 0) xx_io_seek64(device, saved, XX_RT_SEEK_SET);
        return result;
    }
    if (saved >= 0) xx_io_seek64(device, saved, XX_RT_SEEK_SET);
    f = xx_excelsior_create(device, 0);
    if (f && ei_info(f, pd)) result = EI_TYPE;
    xx_excelsior_free(f); return result;
}
