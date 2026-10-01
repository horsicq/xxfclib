/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * FMOD Sample Bank (FSB1..FSB5) reader; xx_fmod_sample_bank.h carries the
 * layouts.  Written from the FMOD 3/4 sample-bank structures and the FSB5
 * sample-header bit layout; the FSB5 field positions agree with GARbro's
 * ArcFormats/Unity/AudioFSB5.cs (MIT, after python-fsb5), which was read for
 * reference only.  Member placement rules that are not in the structures
 * (FSB4 flag 0x40 aligning each next sample to 32 bank bytes, FSB5 sizes
 * running to the next sample's offset) were measured against fsbext 0.3.8a.
 *
 * Every sample is extracted as stored.  All counts, sizes and offsets are
 * validated against the device before a record exists, the header tables are
 * read once under a size cap, and member names are made unique (name, name_1,
 * name_2, ... as fsbext does) through a hash so hostile banks of identical
 * names stay linear.
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/fmod_sample_bank/xx_fmod_sample_bank.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef FMOD_SAMPLE_BANK
#define XX_FMOD_SAMPLE_BANK_FILE_TYPE XX_FILE_TYPE_FMOD_SAMPLE_BANK
#else
#define XX_FMOD_SAMPLE_BANK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define FSB_MAX_SAMPLES 0x100000U
#define FSB_MAX_TABLE (64U * 1024U * 1024U)
#define FSB_MAX_NAME 255U
#define FSB_MIN_FULL_HEADER 0x40U
#define FSB_FLAG_BASICHEADERS 0x02U
#define FSB_FLAG_ALIGN32 0x40U
#define FSB5_MAX_CODEC 0x40U

typedef struct fsb_member_s {
    char *name;
    int64_t header_offset, header_size; /* relative to the bank */
    int64_t data_offset, size;          /* relative to the bank */
} fsb_member;

typedef struct fsb_stream_s {
    fsb_member *items;
    size_t count, index;
    int64_t archive_size;
    uint32_t version, codec;
} fsb_stream;

static void fmod_sample_bank_vtable_destroy(Abstractformat *self);

static uint32_t fsb_u16(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U);
}
static uint32_t fsb_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}
static uint64_t fsb_u64(const uint8_t *p) {
    return (uint64_t)fsb_u32(p) | ((uint64_t)fsb_u32(p + 4) << 32U);
}

static bool fsb_read_at(xx_io_device *dev, int64_t offset, uint8_t *out,
                        size_t size) {
    size_t done = 0U;
    if (!dev || offset < 0 || (!out && size) ||
        xx_io_seek64(dev, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t got = xx_io_read(dev, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

/* Reads [offset, offset + size) of the bank into a fresh buffer. */
static uint8_t *fsb_read_table(Abstractformat *self, int64_t offset,
                               uint32_t size) {
    uint8_t *buffer;
    if (size > FSB_MAX_TABLE) return NULL;
    buffer = (uint8_t *)xx_mem_alloc(size ? size : 1U);
    if (!buffer) return NULL;
    if (!fsb_read_at(self->device, self->base_address + offset, buffer, size)) {
        xx_mem_free(buffer);
        return NULL;
    }
    return buffer;
}

static void fsb_stream_free(void *pointer) {
    fsb_stream *s = (fsb_stream *)pointer;
    size_t i;
    if (!s) return;
    for (i = 0; i < s->count; ++i) xx_str_free(s->items[i].name);
    xx_mem_free(s->items);
    xx_mem_free(s);
}

/* Name from a fixed field (or a table string): up to the first NUL, at most
 * @p limit bytes.  Non-ASCII bytes become '_' so the name is valid UTF-8;
 * control bytes are kept and refused at extraction. */
static char *fsb_name(const uint8_t *field, size_t limit, uint32_t index) {
    char buffer[FSB_MAX_NAME + 1U];
    size_t i = 0U;
    if (field) {
        while (i < limit && i < FSB_MAX_NAME && field[i]) {
            buffer[i] = field[i] >= 0x80U ? '_' : (char)field[i];
            ++i;
        }
    }
    if (i == 0U) {
        xx_rt_snprintf(buffer, sizeof(buffer), "%08u.dat", (unsigned)index);
    } else {
        buffer[i] = 0;
    }
    return xx_str_dup(buffer);
}

static bool fsb_add(fsb_stream *s, size_t capacity, char *name,
                    int64_t header_offset, int64_t header_size,
                    int64_t data_offset, int64_t size) {
    fsb_member *m;
    if (!name) return false;
    if (s->count >= capacity) {
        xx_str_free(name);
        return false;
    }
    m = &s->items[s->count++];
    m->name = name;
    m->header_offset = header_offset;
    m->header_size = header_size;
    m->data_offset = data_offset;
    m->size = size;
    return true;
}

/* ------------------------------------------------------ unique names --- */

typedef struct fsb_slot_s {
    const char *name;
    uint32_t next_suffix;
} fsb_slot;

static char fsb_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static uint32_t fsb_hash(const char *name) {
    uint32_t h = 2166136261U;
    while (*name) {
        h ^= (uint8_t)fsb_lower(*name++);
        h *= 16777619U;
    }
    return h;
}

static bool fsb_same(const char *a, const char *b) {
    while (*a && *b)
        if (fsb_lower(*a++) != fsb_lower(*b++)) return false;
    return *a == *b;
}

static fsb_slot *fsb_find(fsb_slot *table, size_t mask, const char *name) {
    size_t at = fsb_hash(name) & mask;
    for (;;) {
        if (!table[at].name || fsb_same(table[at].name, name)) return &table[at];
        at = (at + 1U) & mask;
    }
}

/* Gives every member a name no other member shares (case-insensitively). */
static bool fsb_unique_names(fsb_stream *s) {
    size_t size = 16U, i;
    fsb_slot *table;
    bool ok = true;
    while (size < s->count * 2U + 2U) size <<= 1U;
    table = (fsb_slot *)xx_mem_calloc(size, sizeof(*table));
    if (!table) return false;
    for (i = 0; i < s->count && ok; ++i) {
        fsb_slot *slot = fsb_find(table, size - 1U, s->items[i].name);
        if (slot->name) {
            fsb_slot *base = slot;
            uint32_t suffix = base->next_suffix ? base->next_suffix : 1U;
            char *candidate = NULL;
            for (;;) {
                char tail[16];
                xx_rt_snprintf(tail, sizeof(tail), "_%u", (unsigned)suffix);
                candidate = xx_str_concat(s->items[i].name, tail);
                if (!candidate) { ok = false; break; }
                slot = fsb_find(table, size - 1U, candidate);
                ++suffix;
                if (!slot->name) break;
                xx_str_free(candidate);
                candidate = NULL;
                if (suffix == 0U) { ok = false; break; }
            }
            base->next_suffix = suffix;
            if (!ok) break;
            xx_str_free(s->items[i].name);
            s->items[i].name = candidate;
        }
        slot->name = s->items[i].name;
    }
    xx_mem_free(table);
    return ok;
}

/* ------------------------------------------------------------ parsing --- */

/* FSB1: one 0x40-byte header per sample, data back to back. */
static bool fsb_parse_v1(Abstractformat *self, int64_t span,
                         const uint8_t *head, fsb_stream *s, bool records) {
    uint32_t n = fsb_u32(head + 4), data_size = fsb_u32(head + 8), i;
    int64_t table = 0x10, data, end, at;
    uint8_t *headers;
    bool ok = false;
    if (n == 0U || n > FSB_MAX_SAMPLES ||
        (uint64_t)n * 0x40U > FSB_MAX_TABLE ||
        (int64_t)n * 0x40 > span - table)
        return false;
    data = table + (int64_t)n * 0x40;
    end = data + (int64_t)data_size;
    if (end > span) return false;
    headers = fsb_read_table(self, table, n * 0x40U);
    if (!headers) return false;
    at = data;
    for (i = 0; i < n; ++i) {
        const uint8_t *h = headers + (size_t)i * 0x40U;
        int64_t length = (int64_t)fsb_u32(h + 0x24);
        if (length > end - at) goto done;
        if (records &&
            !fsb_add(s, n, fsb_name(h, 32U, i), table + (int64_t)i * 0x40,
                     0x40, at, length))
            goto done;
        at += length;
    }
    s->archive_size = end;
    ok = true;
done:
    xx_mem_free(headers);
    return ok;
}

/* FSB2 / FSB3 / FSB4. */
static bool fsb_parse_v234(Abstractformat *self, int64_t span,
                           const uint8_t *head, uint32_t version,
                           fsb_stream *s, bool records) {
    int64_t fixed = version == 2U ? 0x10 : (version == 3U ? 0x18 : 0x30);
    uint32_t n, table_size, data_size, flags = 0U, i;
    int64_t data, end, at;
    size_t pos = 0U;
    uint8_t *headers;
    bool ok = false, align;
    if (span < fixed) return false;
    n = fsb_u32(head + 4);
    table_size = fsb_u32(head + 8);
    data_size = fsb_u32(head + 12);
    if (version >= 3U) {
        uint32_t revision = fsb_u32(head + 16);
        flags = fsb_u32(head + 20);
        if (version == 3U && revision != 0x30000U && revision != 0x30001U)
            return false;
        if (version == 4U && revision != 0x40000U) return false;
    }
    if (n == 0U || n > FSB_MAX_SAMPLES || table_size < FSB_MIN_FULL_HEADER ||
        table_size > FSB_MAX_TABLE || table_size / 8U < n ||
        (int64_t)table_size > span - fixed)
        return false;
    data = fixed + (int64_t)table_size;
    end = data + (int64_t)data_size;
    if (end > span) return false;
    headers = fsb_read_table(self, fixed, table_size);
    if (!headers) return false;
    align = version == 4U && (flags & FSB_FLAG_ALIGN32) != 0U;
    at = data;
    for (i = 0; i < n; ++i) {
        const uint8_t *h = headers + pos;
        uint32_t header_size;
        int64_t length;
        char *name = NULL;
        if (i > 0U && align && (at & 31) != 0) at = (at + 31) & ~(int64_t)31;
        if (at > end) goto done;
        if (i > 0U && (flags & FSB_FLAG_BASICHEADERS)) {
            if (table_size - pos < 8U) goto done;
            header_size = 8U;
            length = (int64_t)fsb_u32(h + 4);
        } else {
            if (table_size - pos < FSB_MIN_FULL_HEADER) goto done;
            header_size = fsb_u16(h);
            if (header_size < FSB_MIN_FULL_HEADER ||
                header_size > table_size - pos)
                goto done;
            length = (int64_t)fsb_u32(h + 0x24);
            if (records) name = fsb_name(h + 2, 30U, i);
        }
        if (length > end - at) {
            xx_str_free(name);
            goto done;
        }
        if (records) {
            if (!name) name = fsb_name(NULL, 0U, i);
            if (!fsb_add(s, n, name, fixed + (int64_t)pos, header_size, at,
                         length))
                goto done;
        }
        pos += header_size;
        at += length;
    }
    s->archive_size = end;
    ok = true;
done:
    xx_mem_free(headers);
    return ok;
}

/* FSB5. */
static bool fsb_parse_v5(Abstractformat *self, int64_t span,
                         const uint8_t *head, fsb_stream *s, bool records) {
    uint32_t revision = fsb_u32(head + 4), n = fsb_u32(head + 8);
    uint32_t table_size = fsb_u32(head + 12), names_size = fsb_u32(head + 16);
    uint32_t data_size = fsb_u32(head + 20), codec = fsb_u32(head + 24), i;
    int64_t fixed, names, data, end, previous = 0;
    uint8_t *headers = NULL, *name_table = NULL;
    int64_t *offsets = NULL;
    int64_t *header_at = NULL;
    size_t pos = 0U;
    bool ok = false;
    if (revision > 1U) return false;
    fixed = revision == 0U ? 0x40 : 0x3C;
    if (span < fixed || n == 0U || n > FSB_MAX_SAMPLES ||
        codec > FSB5_MAX_CODEC || table_size > FSB_MAX_TABLE ||
        names_size > FSB_MAX_TABLE || table_size / 8U < n ||
        (names_size != 0U && names_size / 4U < n))
        return false;
    names = fixed + (int64_t)table_size;
    data = names + (int64_t)names_size;
    end = data + (int64_t)data_size;
    if (end > span) return false;
    headers = fsb_read_table(self, fixed, table_size);
    if (!headers) return false;
    if (records) {
        offsets = (int64_t *)xx_mem_alloc(sizeof(int64_t) * ((size_t)n + 1U));
        header_at = (int64_t *)xx_mem_alloc(sizeof(int64_t) * ((size_t)n + 1U));
        if (!offsets || !header_at) goto done;
    }
    for (i = 0; i < n; ++i) {
        uint64_t raw;
        int64_t offset;
        size_t start = pos;
        bool more;
        if (table_size - pos < 8U) goto done;
        raw = fsb_u64(headers + pos);
        pos += 8U;
        more = (raw & 1U) != 0U;
        /* Each chunk consumes at least its 4-byte prefix, so this loop is
         * bounded by the table size. */
        while (more) {
            uint32_t chunk, chunk_size;
            if (table_size - pos < 4U) goto done;
            chunk = fsb_u32(headers + pos);
            pos += 4U;
            more = (chunk & 1U) != 0U;
            chunk_size = (chunk >> 1U) & 0xFFFFFFU;
            if (chunk_size > table_size - pos) goto done;
            pos += chunk_size;
        }
        offset = (int64_t)(((raw >> 7U) & 0x7FFFFFFU) << 5U);
        if (offset > (int64_t)data_size || offset < previous) goto done;
        previous = offset;
        if (records) {
            offsets[i] = offset;
            header_at[i] = fixed + (int64_t)start;
        }
    }
    if (records) {
        if (names_size) {
            name_table = fsb_read_table(self, names, names_size);
            if (!name_table) goto done;
        }
        for (i = 0; i < n; ++i) {
            int64_t next = i + 1U < n ? offsets[i + 1U] : (int64_t)data_size;
            const uint8_t *field = NULL;
            size_t limit = 0U;
            if (name_table) {
                uint32_t name_offset = fsb_u32(name_table + (size_t)i * 4U);
                if (name_offset >= names_size) goto done;
                field = name_table + name_offset;
                limit = names_size - name_offset;
            }
            if (!fsb_add(s, n, fsb_name(field, limit, i), header_at[i],
                         (i + 1U < n ? header_at[i + 1U] : names) - header_at[i],
                         data + offsets[i], next - offsets[i]))
                goto done;
        }
    } else if (names_size) {
        /* Probe: the name offsets must land inside the name table. */
        uint8_t *index_table = fsb_read_table(self, names, n * 4U);
        if (!index_table) goto done;
        for (i = 0; i < n; ++i)
            if (fsb_u32(index_table + (size_t)i * 4U) >= names_size) break;
        xx_mem_free(index_table);
        if (i != n) goto done;
    }
    s->archive_size = end;
    s->codec = codec;
    ok = true;
done:
    xx_mem_free(headers);
    xx_mem_free(name_table);
    xx_mem_free(offsets);
    xx_mem_free(header_at);
    return ok;
}

static fsb_stream *fsb_parse(Abstractformat *self, bool records,
                             xx_pd_struct *pd) {
    uint8_t head[0x40];
    int64_t total, span;
    uint32_t version;
    fsb_stream *s;
    bool ok;
    if (!self || !self->device || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd)))
        return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < 0x10) return NULL;
    xx_rt_memset(head, 0, sizeof(head));
    if (!fsb_read_at(self->device, self->base_address, head,
                     span < (int64_t)sizeof(head) ? (size_t)span : sizeof(head)))
        return NULL;
    if (head[0] != 'F' || head[1] != 'S' || head[2] != 'B' || head[3] < '1' ||
        head[3] > '5')
        return NULL;
    version = (uint32_t)(head[3] - '0');
    if (version == 5U && span < 0x3C) return NULL;
    s = (fsb_stream *)xx_mem_calloc(1U, sizeof(*s));
    if (!s) return NULL;
    s->version = version;
    if (records) {
        uint32_t n = fsb_u32(head + (version == 5U ? 8 : 4));
        /* Sized from the declared count, which the parsers bound by the
         * header table they have already checked against the device; cap it
         * here too so the allocation never precedes that check. */
        if (n == 0U || n > FSB_MAX_SAMPLES ||
            (int64_t)n * 8 > span) {
            xx_mem_free(s);
            return NULL;
        }
        s->items = (fsb_member *)xx_mem_calloc(n, sizeof(fsb_member));
        if (!s->items) {
            xx_mem_free(s);
            return NULL;
        }
    }
    if (version == 1U)
        ok = fsb_parse_v1(self, span, head, s, records);
    else if (version == 5U)
        ok = fsb_parse_v5(self, span, head, s, records);
    else
        ok = fsb_parse_v234(self, span, head, version, s, records);
    if (ok && records) ok = fsb_unique_names(s);
    if (!ok) {
        fsb_stream_free(s);
        return NULL;
    }
    return s;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_fmod_sample_bank_init(xx_fmod_sample_bank *archive,
                              xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FMOD_SAMPLE_BANK_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-fmod-fsb");
    xx_format_set_extension(&archive->format, "fsb");
    archive->format.check_is_valid = xx_fmod_sample_bank_check_is_valid;
    archive->format.handle_base_info = xx_fmod_sample_bank_handle_base_info;
    archive->format.get_format_size = xx_fmod_sample_bank_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_fmod_sample_bank_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_fmod_sample_bank_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_fmod_sample_bank_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_fmod_sample_bank_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_fmod_sample_bank_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_fmod_sample_bank_free_archive_records_reading;
    archive->format.destroy = fmod_sample_bank_vtable_destroy;
}

xx_fmod_sample_bank *xx_fmod_sample_bank_create(xx_io_device *device,
                                                int64_t base_address) {
    xx_fmod_sample_bank *archive =
        (xx_fmod_sample_bank *)xx_mem_alloc(sizeof(*archive));
    if (!archive) return NULL;
    xx_fmod_sample_bank_init(archive, device, base_address);
    return archive;
}

void xx_fmod_sample_bank_destroy(xx_fmod_sample_bank *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_fmod_sample_bank_free(xx_fmod_sample_bank *archive) {
    if (!archive) return;
    xx_fmod_sample_bank_destroy(archive);
    xx_mem_free(archive);
}

static void fmod_sample_bank_vtable_destroy(Abstractformat *self) {
    xx_fmod_sample_bank_destroy((xx_fmod_sample_bank *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_fmod_sample_bank_check_is_valid(Abstractformat *self,
                                        xx_pd_struct *pd) {
    fsb_stream *s = fsb_parse(self, false, pd);
    if (!s) return false;
    fsb_stream_free(s);
    return true;
}

bool xx_fmod_sample_bank_handle_base_info(Abstractformat *self,
                                          xx_pd_struct *pd) {
    xx_fmod_sample_bank *archive = (xx_fmod_sample_bank *)self;
    fsb_stream *s;
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    self->base_info_handled = true;
    s = fsb_parse(self, true, pd);
    if (!s) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = s->archive_size;
    self->number_of_archive_records = s->count;
    archive->number_of_records = s->count;
    archive->version = s->version;
    archive->codec = s->codec;
    fsb_stream_free(s);
    return true;
}

int64_t xx_fmod_sample_bank_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return 0;
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_fmod_sample_bank_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return 0U;
    return self->is_valid ? ((xx_fmod_sample_bank *)self)->number_of_records
                          : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool fsb_set_record(Abstractformat *self, xx_archive_record *record,
                           const fsb_member *m) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = self->base_address + m->header_offset;
    record->header_size = m->header_size;
    record->data_offset = self->base_address + m->data_offset;
    record->compressed_size = m->size;
    return xx_archive_record_set_original_name(record, m->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)m->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)m->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool fsb_copy_options(xx_list_s *target, const xx_list_s *options) {
    size_t index;
    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) ||
            !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *fsb_get_option(const xx_list_s *options,
                                    uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

static bool fsb_stem_is(const char *name, size_t stem, const char *word) {
    size_t i;
    for (i = 0U; i < stem; ++i) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (!word[i] || c != word[i]) return false;
    }
    return word[stem] == 0;
}

/* Members are written flat into the target directory: no separators, no
 * drive colons, no control or reserved characters, no names Windows would
 * resolve to "." / ".." or to a device (CON, COM1.X, CONIN$, ...). */
static bool fsb_name_safe(const char *name) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t length, stem = 0U, i;
    bool meaningful = false;
    if (!name || !name[0]) return false;
    length = xx_str_len(name);
    for (i = 0U; i < length; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x20U || c == 0x7FU || c == '/' || c == '\\' || c == ':' ||
            c == '<' || c == '>' || c == '"' || c == '|' || c == '?' ||
            c == '*')
            return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful || name[length - 1U] == ' ') return false;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (i = 0U; i < sizeof(devices) / sizeof(devices[0]); ++i)
        if (fsb_stem_is(name, stem, devices[i])) return false;
    if (stem == 4U && name[3] >= '0' && name[3] <= '9' &&
        (fsb_stem_is(name, 3U, "COM") || fsb_stem_is(name, 3U, "LPT")))
        return false;
    return true;
}

/* Reads the member through without writing it anywhere. */
static bool fsb_verify(Abstractformat *self, const fsb_member *m,
                       xx_pd_struct *pd) {
    uint8_t buffer[0x4000];
    int64_t done = 0;
    if (xx_io_seek64(self->device, self->base_address + m->data_offset,
                     SEEK_SET) != 0)
        return false;
    while (done < m->size) {
        size_t want = m->size - done < (int64_t)sizeof(buffer)
                          ? (size_t)(m->size - done) : sizeof(buffer);
        ssize_t got;
        if (pd && xx_pd_is_stopped(pd)) return false;
        got = xx_io_read(self->device, buffer, want);
        if (got <= 0 || (size_t)got > want) return false;
        done += got;
    }
    return true;
}

xx_archive_record_state *xx_fmod_sample_bank_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    fsb_stream *s;
    xx_archive_record_state *state;
    if (!self || !self->device) return NULL;
    s = fsb_parse(self, true, pd);
    if (!s) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        fsb_stream_free(s);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = s;
    state->free_internal = fsb_stream_free;
    state->total_records = (int64_t)s->count;
    if (!fsb_copy_options(&state->options, options) ||
        (s->count != 0U &&
         !fsb_set_record(self, &state->current_record, &s->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = s->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_fmod_sample_bank_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_fmod_sample_bank_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    fsb_stream *s;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    s = (fsb_stream *)state->internal_state;
    if (!s || s->index + 1U >= s->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++s->index;
    ++state->current_index;
    state->has_record =
        fsb_set_record(self, &state->current_record, &s->items[s->index]);
    return state->has_record;
}

bool xx_fmod_sample_bank_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    fsb_stream *s;
    const fsb_member *m;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted = NULL, *target = NULL;
    bool result = false;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    s = (fsb_stream *)state->internal_state;
    if (!s || s->index >= s->count) return false;
    m = &s->items[s->index];
    path_option = fsb_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return fsb_verify(self, m, pd);
    if (!fsb_name_safe(m->name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted;
    }
    if (!base_path) goto done;
    if (base_path[0] && base_path[xx_str_len(base_path) - 1U] != '/' &&
        base_path[xx_str_len(base_path) - 1U] != '\\')
        target = xx_str_concat3(base_path, "/", m->name);
    else
        target = xx_str_concat(base_path, m->name);
    if (!target || !xx_store_create_dirs_a(target, false)) goto done;
    /* The store helper removes its own output on failure (and only output
     * it created), so nothing is removed here. */
    result = xx_store_unpack_device_to_file(
        self->device, self->base_address + m->data_offset, m->size, target, pd);
done:
    if (target) xx_str_free(target);
    if (converted) xx_str_free(converted);
    return result;
}

void xx_fmod_sample_bank_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
