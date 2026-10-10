/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently derived from vendor packages; see SUPERDAT_PROVENANCE.json.
 */
#include "xxfclib/formats/superdat/xx_superdat.h"
#include "../ue2_indexed.h"
#include "xxfclib/algo/lzh/xx_lzh.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/data/xx_data.h"
#define SD_MEMBERS 16384U
#define SD_TAIL 65536U
#define SD_WORKSPACE 32768U
#define SD_DEFAULT_MEMORY (UINT64_C(256) * 1024 * 1024)
typedef struct sd_item {
    uint32_t crc;
    uint8_t date[6];
    bool has_crc, has_manifest, vendor_dat;
} sd_item;
typedef struct sd_index {
    ue2_index records;
    sd_item *items;
    xx_io_device *device;
    int64_t base;
    uint64_t name_bytes;
} sd_index;
typedef struct sd_state {
    ue2_state reading;
    uint64_t generation;
} sd_state;
typedef struct sd_sink {
    xx_io_device *output;
    xx_pd_struct *pd;
    uint32_t crc;
    uint64_t size;
    uint8_t first[48];
    size_t first_count;
} sd_sink;
static bool sd_fail(xx_pd_struct *pd, const char *why)
{
    xx_pd_set_error(pd, 1, why);
    return false;
}
static int sd_compare(const char *a, const char *b)
{
    while (*a && *b) {
        int x = xx_rt_ascii_tolower((unsigned char)*a++), y = xx_rt_ascii_tolower((unsigned char)*b++);
        if (x != y) return x - y;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
static void sd_free_index(sd_index *ix)
{
    if (ix) {
        xx_mem_free(ix->items);
        ue2_index_free(&ix->records);
    }
}
/* A signed PE ends at the security directory's file offset. The archive
 * footer is immediately before that certificate, rather than at physical EOF. */
static int64_t sd_archive_end(Abstractformat *f)
{
    uint8_t h[64], p[176];
    int64_t total = xx_io_total_size(f->device), pe, cert;
    unsigned off;
    if (f->base_address < 0 || total < f->base_address) return -1;
    if (!ue2_read(f, f->base_address, h, sizeof(h)) || h[0] != 'M' || h[1] != 'Z') return total;
    if ((uint64_t)xx_data_get_u32(h + 60, 4, 0, false) > (uint64_t)(total - f->base_address)) return total;
    pe = f->base_address + (int64_t)xx_data_get_u32(h + 60, 4, 0, false);
    if (!ue2_read(f, pe, p, sizeof(p)) || xx_rt_memcmp(p, "PE\0\0", 4)) return total;
    off = xx_data_get_u16(p + 24, 2, 0, false) == 0x10b ? 152U : xx_data_get_u16(p + 24, 2, 0, false) == 0x20b ? 168U : 0U;
    if (!off || xx_data_get_u16(p + 20, 2, 0, false) < off - 24 + 8) return total;
    if ((uint64_t)xx_data_get_u32(p + off, 4, 0, false) > (uint64_t)(total - f->base_address)) return total;
    cert = f->base_address + (int64_t)xx_data_get_u32(p + off, 4, 0, false);
    if (xx_data_get_u32(p + off + 4, 4, 0, false) && cert > f->base_address && ue2_range(total, cert, xx_data_get_u32(p + off + 4, 4, 0, false))) return cert;
    return total;
}
static bool sd_footer(Abstractformat *f, int64_t *location, uint32_t *size, uint32_t *start, uint32_t *count, unsigned *kind)
{
    uint8_t *tail;
    int64_t end = sd_archive_end(f), at;
    size_t n, i;
    bool ok = false;
    if (end < f->base_address || end - f->base_address < 33) return false;
    n = end - f->base_address > SD_TAIL ? SD_TAIL : (size_t)(end - f->base_address);
    at = end - (int64_t)n;
    tail = xx_mem_alloc(n);
    if (!tail) return false;
    if (!ue2_read(f, at, tail, n)) goto done;
    for (i = n - 33 + 1; i-- > 0;) {
        const uint8_t *p = tail + i;
        if (xx_rt_memcmp(p, "_SUPERDAT_HEADER\0", 17)) continue;
        *size = xx_data_get_u32(p + 17, 4, 0, false);
        *start = xx_data_get_u32(p + 21, 4, 0, false);
        *count = xx_data_get_u32(p + 25, 4, 0, false);
        *kind = *count == 0 ? 1U : 0U;
        if (*kind == 1U) {
            if (n - i < 37) continue;
            *count = xx_data_get_u32(p + 29, 4, 0, false);
        }
        if (!*size || !*count || *count > SD_MEMBERS || *start > (uint64_t)(at + (int64_t)i - f->base_address)) continue;
        *location = at + (int64_t)i;
        ok = true;
        break;
    }
    if (!ok && n >= 297U && !xx_rt_memcmp(tail + n - 297U, "_SUPERDAT_HEADER\0", 17) && xx_data_get_u32(tail + n - 280U, 4, 0, false) == 0U) {
        /* v1.2 keeps a 297-byte signing area, followed by the actual three
         * footer integers; its manifest entries are 170 bytes. */
        *size = xx_data_get_u32(tail + n - 12U, 4, 0, false);
        *start = xx_data_get_u32(tail + n - 8U, 4, 0, false);
        *count = xx_data_get_u32(tail + n - 4U, 4, 0, false);
        if (*size && *count && *count < SD_MEMBERS && *start < (uint64_t)(end - f->base_address - 297)) {
            *location = end - 297;
            *kind = 2U;
            ok = true;
        }
    }
    if (!ok && n >= 4U) {
        /* The original producer has no signing/manifest section: a final
         * u32 measures the complete NAILZHU group after its leading marker. */
        uint32_t length = xx_data_get_u32(tail + n - 4U, 4, 0, false);
        if (length > 17U && length <= (uint64_t)(end - f->base_address - 8)) {
            uint8_t magic[18];
            int64_t begin = end - 8 - (int64_t)length;
            if (ue2_read(f, begin, magic, sizeof(magic)) && xx_data_get_u32(magic, 4, 0, false) == 0xdeadbeefU && !xx_rt_memcmp(magic + 4, "__NAILZHUFLIB\0", 14)) {
                *location = end - 4;
                *size = length;
                *start = (uint32_t)(begin - f->base_address);
                *count = 0;
                *kind = 3U;
                ok = true;
            }
        }
    }
done:
    xx_mem_free(tail);
    return ok;
}
bool xx_superdat_has_candidate_device(xx_io_device *io, int64_t base)
{
    Abstractformat *f;
    int64_t at, saved;
    uint32_t size, start, count;
    unsigned kind;
    bool ok;
    if (!io || base < 0) {
        return false;
    }
    f = xx_mem_calloc(1, sizeof(*f));
    if (!f) return false;
    f->device = io;
    f->base_address = base;
    saved = xx_io_tell(io);
    ok = sd_footer(f, &at, &size, &start, &count, &kind);
    if (ok) {
        uint8_t h[18];
        ok = ue2_read(f, base + start, h, sizeof(h)) && xx_data_get_u32(h, 4, 0, false) == 0xdeadbeefU && !xx_rt_memcmp(h + 4, "__NAILZHUFLIB\0", 14);
    }
    if (saved >= 0 && xx_io_seek64(io, saved, XX_RT_SEEK_SET)) {
        ok = false;
    }
    xx_mem_free(f);
    return ok;
}
static bool sd_name(uint8_t *p, size_t capacity)
{
    size_t i;
    uint8_t *nul = xx_rt_memchr(p, 0, capacity);
    if (!nul || nul == p) return false;
    for (i = 0; i < (size_t)(nul - p); ++i) {
        if (p[i] >= 128) return false;
        if (p[i] == '\\') p[i] = '/';
    }
    return ue2_safe_name((const char *)p);
}
static uint64_t sd_memory(const sd_index *ix)
{
    return SD_WORKSPACE + sizeof(*ix) + ix->records.capacity * (sizeof(ue2_member) + sizeof(sd_item)) + ix->name_bytes;
}
static sd_index *sd_parse(Abstractformat *f, xx_pd_struct *pd, uint64_t limit)
{
    sd_index *ix = NULL;
    int64_t footer, at, group_start = 0, table, saved;
    uint32_t declared, start, count;
    size_t group_first = 0, group_count = 0, i, j;
    unsigned groups = 0, kind = 0;
    bool ok = false;
    uint8_t h[292];
    if (!f || !f->device || xx_pd_is_stopped(pd)) return NULL;
    /* The footer buffer is freed before the member index is allocated. Reserve
     * the fixed decoder workspace throughout parsing, and bound every index
     * growth before calling the allocator (including a moving realloc peak). */
    if (limit < SD_TAIL || limit < SD_WORKSPACE + sizeof(*ix)) {
        sd_fail(pd, "SuperDAT decoder exceeds configured memory limit");
        return NULL;
    }
    saved = xx_io_tell(f->device);
    if (!sd_footer(f, &footer, &declared, &start, &count, &kind)) goto done;
    ix = xx_mem_calloc(1, sizeof(*ix));
    if (!ix) goto done;
    ix->device = f->device;
    ix->base = f->base_address;
    at = f->base_address + start;
    for (;;) {
        if (++groups > 8 || !ue2_read(f, at, h, 4) || xx_data_get_u32(h, 4, 0, false) != 0xdeadbeefU) goto done;
        group_start = at;
        group_first = ix->records.count;
        at += 4;
        for (;;) {
            uint32_t plain, packed;
            size_t k, capacity, old_capacity, name_size;
            sd_item *items;
            uint64_t needed;
            if (at > footer - 17 || !ue2_read(f, at, h, 17)) goto done;
            if (!xx_rt_memcmp(h, "NAISIGN\0", 8)) break;
            if (xx_rt_memcmp(h, "__NAILZHUFLIB\0", 14) || at > footer - 292 || !ue2_read(f, at, h, sizeof(h))) goto done;
            plain = xx_data_get_u32(h + 274, 4, 0, false);
            packed = xx_data_get_u32(h + 278, 4, 0, false);
            if (packed < 4 || plain != xx_data_get_u32(h + 288, 4, 0, false) || !ue2_range(footer, at + 288, packed) || !sd_name(h + 14, 260) ||
                ix->records.count >= SD_MEMBERS)
                goto done;
            for (k = 0; k < ix->records.count; ++k)
                if (!sd_compare(ix->records.members[k].name, (char *)h + 14)) goto done;
            old_capacity = capacity = ix->records.capacity;
            name_size = xx_rt_strlen((char *)h + 14) + 1;
            if (ix->records.count == capacity) capacity = capacity ? capacity * 2U : 32U;
            needed = SD_WORKSPACE + sizeof(*ix) + capacity * (sizeof(ue2_member) + sizeof(sd_item)) + ix->name_bytes + name_size;
            if (capacity != ix->records.capacity) needed += ix->records.capacity * (sizeof(ue2_member) + sizeof(sd_item));
            if (needed > limit) {
                sd_fail(pd, "SuperDAT decoder exceeds configured memory limit");
                goto done;
            }
            if (!ue2_add(&ix->records, (char *)h + 14, at + 292, packed - 4, 0)) goto done;
            ix->name_bytes += name_size;
            ix->records.members[ix->records.count - 1].original_size = plain;
            items = ix->items;
            if (capacity != old_capacity) {
                items = xx_mem_realloc(ix->items, ix->records.capacity * sizeof(*items));
                if (!items) goto done;
                ix->items = items;
            }
            xx_mem_zero(items + ix->records.count - 1, sizeof(*items));
            xx_rt_memcpy(items[ix->records.count - 1].date, h + 282, 6);
            at += 288 + (int64_t)packed;
            if (xx_pd_is_stopped(pd)) goto done;
        }
        group_count = ix->records.count - group_first;
        if (!group_count || h[8] || h[9] || h[10] != 1 || xx_data_get_u16(h + 15, 2, 0, false) != 17) goto done;
        at += 17;
        if (!ue2_read(f, at, h, 4)) goto done;
        if (xx_data_get_u32(h, 4, 0, false) != 0xdeadbeefU) break;
    }
    table = at;
    if (table - group_start - 4 != declared) goto done;
    if (kind == 3U) {
        if (table != footer) goto done;
    } else {
        size_t bootstrap = kind == 2U ? 1U : 2U, record_size = kind == 2U ? 170U : 178U;
        if (group_count != count + bootstrap || footer - table != (int64_t)count * (int64_t)record_size + (kind == 1U ? 12 : 0)) goto done;
        for (i = 0; i < count; ++i) {
            uint8_t record[178];
            const ue2_member *m = NULL;
            uint32_t classification = 0;
            xx_mem_zero(record, sizeof(record));
            if (!ue2_read(f, table + (int64_t)i * (int64_t)record_size, record, record_size) || !sd_name(record, 144)) goto done;
            for (j = group_first + bootstrap; j < ix->records.count; ++j)
                if (!sd_compare(ix->records.members[j].name, (char *)record)) {
                    m = &ix->records.members[j];
                    break;
                }
            if (!m || ix->items[j].has_manifest || m->original_size != xx_data_get_u32(record + 160, 4, 0, false)) goto done;
            if (kind != 2U) classification = xx_data_get_u32(record + 170, 4, 0, false);
            /* Modern type2 Olympus DAT metadata records a vendor DAT checksum. It is
             * not CRC32 over the decoded Olympus member (verified against independent
             * reference extraction); other types use reflected CRC init0/xorout0.
             * Keep the declared value in info, without misreporting a failure. */
            ix->items[j].has_manifest = true;
            ix->items[j].has_crc = true;
            ix->items[j].vendor_dat = classification == 2U;
            ix->items[j].crc = xx_data_get_u32(record + 144, 4, 0, false);
        }
    }
    ix->records.size = xx_io_total_size(f->device) - f->base_address;
    ok = true;
done:
    if (saved >= 0 && xx_io_seek64(f->device, saved, XX_RT_SEEK_SET)) ok = false;
    if (!ok) {
        sd_free_index(ix);
        return NULL;
    }
    return ix;
}
static bool sd_valid(Abstractformat *f, xx_pd_struct *pd)
{
    sd_index *ix = sd_parse(f, pd, SD_DEFAULT_MEMORY);
    bool ok = ix != NULL;
    sd_free_index(ix);
    return ok;
}
static bool sd_info_limit(Abstractformat *f, xx_pd_struct *pd, uint64_t limit)
{
    xx_superdat *a = (xx_superdat *)f;
    sd_index *ix;
    sd_free_index(a->index);
    a->index = NULL;
    ++a->generation;
    if (!a->generation) ++a->generation;
    f->base_info_handled = f->is_valid = false;
    f->format_size = -1;
    f->number_of_archive_records = 0;
    ix = sd_parse(f, pd, limit);
    if (!ix) {
        if (!xx_pd_is_stopped(pd) && (!pd || !pd->last_error)) sd_fail(pd, "Damaged or unsupported SuperDAT package");
        return false;
    }
    a->index = ix;
    f->format_size = ix->records.size;
    f->number_of_archive_records = ix->records.count;
    f->base_info_handled = f->is_valid = true;
    return true;
}
static bool sd_info(Abstractformat *f, xx_pd_struct *pd)
{
    return sd_info_limit(f, pd, SD_DEFAULT_MEMORY);
}
static xx_archive_record_state *sd_records(Abstractformat *f, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_archive_record_state *s;
    sd_state *state;
    sd_index *ix;
    uint64_t limit = SD_DEFAULT_MEMORY;
    size_t i;
    for (i = 0; options && i < options->count; ++i) {
        const xx_meta *m = xx_list_at((const xx_list_t *)options, i);
        if (m && m->meta_id == XX_META_ID_OPT_MEMORY_LIMIT) limit = xx_var_get_u64(&m->var);
    }
    if (!f || xx_pd_is_stopped(pd)) return NULL;
    if (limit < SD_TAIL || limit < SD_WORKSPACE + sizeof(sd_index)) return sd_fail(pd, "SuperDAT decoder exceeds configured memory limit"), NULL;
    ix = ((xx_superdat *)f)->index;
    if (!f->base_info_handled || !ix || ix->device != f->device || ix->base != f->base_address) {
        if (!sd_info_limit(f, pd, limit)) return NULL;
        ix = ((xx_superdat *)f)->index;
    }
    if (sd_memory(ix) > limit) {
        sd_fail(pd, "SuperDAT decoder exceeds configured memory limit");
        return NULL;
    }
    s = ue2_records(f, options, pd);
    if (!s) return NULL;
    state = xx_mem_realloc(s->internal_state, sizeof(*state));
    if (!state) {
        ue2_free_records(f, s);
        return NULL;
    }
    s->internal_state = state;
    state->generation = ((xx_superdat *)f)->generation;
    return s;
}
static const xx_archive_record *sd_current(Abstractformat *f, xx_archive_record_state *s)
{
    const xx_archive_record *r = ue2_current(f, s);
    sd_state *state = s ? s->internal_state : NULL;
    sd_index *ix = f ? ((xx_superdat *)f)->index : NULL;
    const sd_item *item;
    if (!r || !state || !ix || !f->base_info_handled || !f->is_valid || ix->device != f->device || ix->base != f->base_address || state->reading.index != &ix->records ||
        state->generation != ((xx_superdat *)f)->generation || state->reading.cursor >= ix->records.count)
        return NULL;
    item = ix->items + state->reading.cursor;
    if (!xx_archive_record_set_meta_u64(&s->current_record, XX_META_ID_COMPRESSION_METHOD, 1)) return NULL;
    if (item->has_crc && !item->vendor_dat && !xx_archive_record_set_meta_u64(&s->current_record, XX_META_ID_CRC32, item->crc)) return NULL;
    if (item->has_manifest && item->vendor_dat) {
        char comment[96];
        xx_rt_snprintf(comment, sizeof(comment), "Vendor DAT checksum: %08X (Olympus checksum domain not verified)", (unsigned)item->crc);
        if (!xx_archive_record_set_meta_str(&s->current_record, XX_META_ID_COMMENT, comment)) return NULL;
    }
    if (item->date[3] >= 80 && item->date[4] >= 1 && item->date[4] <= 12 && item->date[5] >= 1 && item->date[5] <= 31 && item->date[0] < 24 && item->date[1] < 60 &&
        item->date[2] < 60) {
        uint32_t date = ((uint32_t)item->date[3] - 80) * 512 + item->date[4] * 32 + item->date[5];
        uint32_t time = item->date[0] * 2048 + item->date[1] * 32 + item->date[2] / 2;
        if (!xx_archive_record_set_meta_u64(&s->current_record, XX_META_ID_LAST_MOD_DATE, date) ||
            !xx_archive_record_set_meta_u64(&s->current_record, XX_META_ID_LAST_MOD_TIME, time))
            return NULL;
    }
    return r;
}
static bool sd_next(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd)
{
    return sd_current(f, s) && ue2_next(f, s, pd);
}
static ssize_t sd_write(xx_io_device *d, const void *bytes, size_t n)
{
    sd_sink *s = d->priv;
    size_t at = 0;
    if (s->first_count < sizeof(s->first)) {
        size_t take = n < sizeof(s->first) - s->first_count ? n : sizeof(s->first) - s->first_count;
        xx_rt_memcpy(s->first + s->first_count, bytes, take);
        s->first_count += take;
    }
    if (s->output)
        while (at < n) {
            ssize_t wrote;
            if (xx_pd_is_stopped(s->pd)) return -1;
            wrote = xx_io_write(s->output, (const uint8_t *)bytes + at, n - at);
            if (wrote <= 0 || (size_t)wrote > n - at) return -1;
            at += (size_t)wrote;
        }
    /* SuperDAT uses reflected CRC32 with init=0 and xorout=0. */
    s->crc = xx_crc32_calc(s->crc ^ UINT32_C(0xffffffff), bytes, n) ^ UINT32_C(0xffffffff);
    s->size += n;
    return (ssize_t)n;
}
static bool sd_decode(Abstractformat *f, const ue2_member *m, const sd_item *item, xx_io_device *out, xx_pd_struct *pd)
{
    xx_io_device *input, sink;
    sd_sink work = {out, pd, 0, 0, {0}, 0};
    uint64_t written = 0;
    bool ok;
    input = xx_io_sub_open_ro(f->device, m->offset, m->size);
    if (!input) return false;
    xx_mem_zero(&sink, sizeof(sink));
    sink.priv = &work;
    sink.write = sd_write;
    ok = xx_lzh1_decode_to_device(input, (uint64_t)m->size, &sink, (uint64_t)m->original_size, &written, pd) && written == (uint64_t)m->original_size &&
         (!item->has_crc || work.crc == item->crc ||
          (item->vendor_dat && work.first_count == 48U && !xx_rt_memcmp(work.first, "Copyright (c) Network Associates Inc.\x1a\0Olympus\0", 48U)));
    xx_io_close(input);
    if (!ok && !xx_pd_is_stopped(pd)) sd_fail(pd, "SuperDAT LH1 data or CRC32 is damaged");
    return ok;
}
static bool sd_unpack(Abstractformat *f, xx_archive_record_state *s, xx_pd_struct *pd)
{
    const xx_archive_record *r = sd_current(f, s);
    sd_state *state = s ? s->internal_state : NULL;
    sd_index *ix = f ? ((xx_superdat *)f)->index : NULL;
    const ue2_member *m;
    const sd_item *item;
    const xx_var *option = NULL;
    const char *base = NULL;
    char *owned = NULL, *path = NULL;
    xx_io_device *out = NULL;
    size_t i;
    int64_t saved;
    bool ok = false, created = false;
    if (!r || xx_pd_is_stopped(pd)) {
        return false;
    }
    m = ix->records.members + state->reading.cursor;
    item = ix->items + state->reading.cursor;
    saved = xx_io_tell(f->device);
    for (i = 0; i < s->options.count; ++i) {
        const xx_meta *meta = xx_list_at((const xx_list_t *)&s->options, i);
        if (!meta) continue;
        if (meta->meta_id == XX_META_ID_OPT_UNPACK_PATH) option = &meta->var;
        if (meta->meta_id == XX_META_ID_OPT_MAX_MEMBER_SIZE && (uint64_t)m->original_size > xx_var_get_u64(&meta->var))
            return sd_fail(pd, "SuperDAT member exceeds configured size limit");
    }
    if (!sd_decode(f, m, item, NULL, pd)) {
        goto done;
    }
    if (!option) {
        ok = true;
        goto done;
    }
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned;
    }
    if (base && ue2_safe_name(m->name)) {
        path = xx_str_concat3(base, "/", m->name);
    }
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    out = xx_io_file_open(path, "wb");
    if (!out) goto done;
    created = true;
    ok = sd_decode(f, m, item, out, pd);
done:
    if (out && xx_io_close(out)) {
        ok = false;
    }
    if (saved >= 0 && xx_io_seek64(f->device, saved, XX_RT_SEEK_SET)) ok = false;
    if (!ok && created) {
        xx_io_file_remove_a(path);
    }
    xx_str_free(path);
    xx_str_free(owned);
    return ok;
}
static void sd_destroy(Abstractformat *f)
{
    xx_superdat *a = (xx_superdat *)f;
    sd_free_index(a->index);
    a->index = NULL;
    ++a->generation;
    if (!a->generation) ++a->generation;
    f->base_info_handled = f->is_valid = false;
    f->number_of_archive_records = 0;
    f->format_size = -1;
    xx_format_cleanup_extra_parameters(f);
}
void xx_superdat_init(xx_superdat *a, xx_io_device *io, int64_t base)
{
    if (!a) {
        return;
    }
    xx_mem_zero(a, sizeof(*a));
    ue2_init_format(&a->format, io, base, XX_FILE_TYPE_SUPERDAT, "exe", "application/x-superdat");
    a->format.check_is_valid = sd_valid;
    a->format.handle_base_info = sd_info;
    a->format.create_archive_records_reading = sd_records;
    a->format.get_current_archive_record = sd_current;
    a->format.archive_record_move_to_next = sd_next;
    a->format.unpack_current_archive_record = sd_unpack;
    a->format.destroy = sd_destroy;
}
xx_superdat *xx_superdat_create(xx_io_device *io, int64_t base)
{
    xx_superdat *a = xx_mem_alloc(sizeof(*a));
    if (a) xx_superdat_init(a, io, base);
    return a;
}
void xx_superdat_destroy(xx_superdat *a)
{
    if (a) sd_destroy(&a->format);
}
void xx_superdat_free(xx_superdat *a)
{
    if (a) {
        xx_superdat_destroy(a);
        xx_mem_free(a);
    }
}
