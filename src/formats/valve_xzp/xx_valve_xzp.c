/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Valve XZP reader (Xbox Half-Life 2 packs, signature "piZx"/"tFzX").
 * Written from the on-disk structure; layout and acceptance rules were
 * measured against HLLib's CXZPFile as shipped in TotalObserver's valve.so
 * (used as a black-box oracle only; no code taken from it).
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/valve_xzp/xx_valve_xzp.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <stdio.h>

#ifdef VALVE_XZP
#define XX_VALVE_XZP_FILE_TYPE XX_FILE_TYPE_VALVE_XZP
#else
#define XX_VALVE_XZP_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XZP_HEADER_SIZE 36
#define XZP_FOOTER_SIZE 8
#define XZP_ENTRY_SIZE 12
#define XZP_VERSION 6U
#define XZP_MAX_ENTRIES 1000000U
#define XZP_MAX_NAME 255U

typedef struct xzp_member_s {
    char *name;
    int64_t entry_offset; /* absolute offset of the directory entry */
    int64_t data_offset;  /* absolute */
    int64_t size;
    uint32_t crc;
    bool named;
} xzp_member;

typedef struct xzp_stream_s {
    xzp_member *items;
    size_t count, index;
    int64_t archive_size;
    uint64_t unnamed;
} xzp_stream;

static void xx_valve_xzp_vtable_destroy(Abstractformat *self);

static uint32_t xzp_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool xzp_read(xx_io_device *dev, int64_t offset, uint8_t *out,
                     size_t size) {
    size_t done = 0U;
    if (!dev || offset < 0 || xx_io_seek64(dev, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t got = xx_io_read(dev, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static bool xzp_within(int64_t span, int64_t offset, int64_t size) {
    return offset >= 0 && size >= 0 && offset <= span && size <= span - offset;
}

static char xzp_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static char xzp_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* One path component is safe to create on any host: no control bytes or
 * reserved punctuation, not "." / ".." / dots-and-spaces only, no trailing
 * dot or space, and not a Windows device name (with or without extension). */
static bool xzp_component_safe(const char *at, size_t len) {
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL",
                                          "CONIN$", "CONOUT$", "CLOCK$"};
    size_t i, stem = 0U, d;
    bool meaningful = false;
    if (len == 0U) return false;
    for (i = 0U; i < len; ++i) {
        unsigned char c = (unsigned char)at[i];
        if (c < 0x20U || c == 0x7FU || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|')
            return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful || at[len - 1U] == '.' || at[len - 1U] == ' ') return false;
    while (stem < len && at[stem] != '.') ++stem;
    while (stem > 0U && at[stem - 1U] == ' ') --stem;
    for (d = 0U; d < sizeof(devices) / sizeof(devices[0]); ++d) {
        const char *w = devices[d];
        for (i = 0U; i < stem && w[i] && xzp_upper(at[i]) == w[i]; ++i) {
        }
        if (i == stem && w[i] == 0) return false;
    }
    if (stem == 4U && at[3] >= '0' && at[3] <= '9' &&
        ((xzp_upper(at[0]) == 'C' && xzp_upper(at[1]) == 'O' &&
          xzp_upper(at[2]) == 'M') ||
         (xzp_upper(at[0]) == 'L' && xzp_upper(at[1]) == 'P' &&
          xzp_upper(at[2]) == 'T')))
        return false;
    return true;
}

/* Names are already normalised to '/'-separated, non-empty components. */
static bool xzp_path_safe(const char *name) {
    const char *at = name;
    if (!name || !*name) return false;
    for (;;) {
        const char *end = at;
        while (*end && *end != '/') ++end;
        if (!xzp_component_safe(at, (size_t)(end - at))) return false;
        if (!*end) return true;
        at = end + 1;
    }
}

/* ------------------------------------------------------- name registry -- */

typedef struct xzp_names_s {
    const char **slots;
    size_t mask;
} xzp_names;

static uint32_t xzp_hash(const char *s) {
    uint32_t h = 2166136261U;
    while (*s) {
        h ^= (uint8_t)xzp_lower(*s++);
        h *= 16777619U;
    }
    return h;
}

static bool xzp_equal_ci(const char *a, const char *b) {
    while (*a && *b)
        if (xzp_lower(*a++) != xzp_lower(*b++)) return false;
    return *a == *b;
}

/* Returns true when @p s is already present; otherwise inserts it. */
static bool xzp_names_seen(xzp_names *t, const char *s, bool insert) {
    size_t i = (size_t)xzp_hash(s) & t->mask;
    while (t->slots[i]) {
        if (xzp_equal_ci(t->slots[i], s)) return true;
        i = (i + 1U) & t->mask;
    }
    if (insert) t->slots[i] = s;
    return false;
}

/* ------------------------------------------------------------- parsing -- */

static void xzp_stream_free(void *opaque) {
    xzp_stream *s = (xzp_stream *)opaque;
    size_t i;
    if (!s) return;
    for (i = 0U; i < s->count; ++i) xx_str_free(s->items[i].name);
    xx_mem_free(s->items);
    xx_mem_free(s);
}

/* Heap sort of 64-bit keys (CRC << 32 | item index). */
static void xzp_sift(uint64_t *a, size_t start, size_t n) {
    size_t root = start;
    for (;;) {
        size_t child = root * 2U + 1U, top = root;
        if (child >= n) return;
        if (a[child] > a[top]) top = child;
        if (child + 1U < n && a[child + 1U] > a[top]) top = child + 1U;
        if (top == root) return;
        {
            uint64_t t = a[root];
            a[root] = a[top];
            a[top] = t;
        }
        root = top;
    }
}

static void xzp_sort(uint64_t *a, size_t n) {
    size_t i;
    if (n < 2U) return;
    for (i = n / 2U; i-- > 0U;) xzp_sift(a, i, n);
    for (i = n - 1U; i > 0U; --i) {
        uint64_t t = a[0];
        a[0] = a[i];
        a[i] = t;
        xzp_sift(a, 0U, i);
    }
}

/* First (lowest index) item with this CRC, or SIZE_MAX. */
static size_t xzp_find(const uint64_t *keys, size_t n, uint32_t crc) {
    size_t lo = 0U, hi = n;
    uint64_t want = (uint64_t)crc << 32;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2U;
        if (keys[mid] < want) lo = mid + 1U; else hi = mid;
    }
    if (lo < n && (uint32_t)(keys[lo] >> 32) == crc)
        return (size_t)(uint32_t)keys[lo];
    return (size_t)-1;
}

/* Reads the name at an absolute archive offset, bounded by the item table,
 * and normalises it: '\' and '/' both separate, empty components (leading,
 * trailing or doubled separators) are dropped. Returns NULL when nothing
 * usable remains. */
static char *xzp_read_name(Abstractformat *self, int64_t name_offset,
                           int64_t table_offset, int64_t table_end) {
    uint8_t raw[XZP_MAX_NAME + 1U];
    char out[XZP_MAX_NAME + 1U];
    size_t avail, i, o = 0U;
    if (name_offset < table_offset || name_offset >= table_end) return NULL;
    avail = (table_end - name_offset) > (int64_t)XZP_MAX_NAME
                ? XZP_MAX_NAME
                : (size_t)(table_end - name_offset);
    if (!xzp_read(self->device, self->base_address + name_offset, raw, avail))
        return NULL;
    for (i = 0U; i < avail && raw[i]; ++i) {
        char c = (char)raw[i];
        if (c == '\\' || c == '/') {
            if (o && out[o - 1U] != '/') out[o++] = '/';
        } else {
            out[o++] = c;
        }
    }
    while (o && out[o - 1U] == '/') --o;
    if (!o) return NULL;
    out[o] = 0;
    return xx_str_dup(out);
}

static xzp_stream *xzp_parse(Abstractformat *self, bool members,
                             xx_pd_struct *pd) {
    uint8_t h[XZP_HEADER_SIZE], foot[XZP_FOOTER_SIZE];
    uint32_t pcount, ecount, pbytes, icount, ioff, ilen, i;
    int64_t total, span, tables_end, extent, archive_size = 0, data_end;
    uint8_t *entries = NULL, *itab = NULL;
    uint64_t *keys = NULL;
    xzp_names names = {NULL, 0U};
    xzp_stream *s = NULL;

    if (!self || !self->device || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd)))
        return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < XZP_HEADER_SIZE + XZP_FOOTER_SIZE ||
        !xzp_read(self->device, self->base_address, h, sizeof(h)) ||
        xx_rt_memcmp(h, "piZx", 4U) != 0 || xzp_le32(h + 4) != XZP_VERSION ||
        xzp_le32(h + 20) != XZP_HEADER_SIZE)
        return NULL;
    pcount = xzp_le32(h + 8);
    ecount = xzp_le32(h + 12);
    pbytes = xzp_le32(h + 16);
    icount = xzp_le32(h + 24);
    ioff = xzp_le32(h + 28);
    ilen = xzp_le32(h + 32);
    if (ecount > XZP_MAX_ENTRIES || pcount > XZP_MAX_ENTRIES) return NULL;
    tables_end = XZP_HEADER_SIZE + (int64_t)ecount * XZP_ENTRY_SIZE;
    if (pbytes)
        tables_end += (int64_t)pcount * XZP_ENTRY_SIZE + (int64_t)ecount * 2;
    if (tables_end > span - XZP_FOOTER_SIZE) return NULL;
    if (icount && (uint64_t)icount * XZP_ENTRY_SIZE > ilen) return NULL;

    if (ecount) {
        entries = (uint8_t *)xx_mem_alloc((size_t)ecount * XZP_ENTRY_SIZE);
        if (!entries ||
            !xzp_read(self->device, self->base_address + XZP_HEADER_SIZE,
                      entries, (size_t)ecount * XZP_ENTRY_SIZE))
            goto fail;
    }
    /* Footer: normally the last 8 bytes of the file. When the pack is
     * followed by other data, it sits right after the furthest structure. */
    extent = tables_end;
    for (i = 0U; i < ecount; ++i) {
        int64_t end = (int64_t)xzp_le32(entries + i * 12U + 8U) +
                      (int64_t)xzp_le32(entries + i * 12U + 4U);
        if (xzp_le32(entries + i * 12U + 4U) && end > extent) extent = end;
    }
    if (icount && (int64_t)ioff + ilen > extent) extent = (int64_t)ioff + ilen;
    if (xzp_read(self->device, self->base_address + span - XZP_FOOTER_SIZE,
                 foot, sizeof(foot)) &&
        xx_rt_memcmp(foot + 4, "tFzX", 4U) == 0 &&
        (int64_t)xzp_le32(foot) == span)
        archive_size = span;
    else if (extent <= span - XZP_FOOTER_SIZE &&
             xzp_read(self->device, self->base_address + extent, foot,
                      sizeof(foot)) &&
             xx_rt_memcmp(foot + 4, "tFzX", 4U) == 0 &&
             (int64_t)xzp_le32(foot) == extent + XZP_FOOTER_SIZE)
        archive_size = extent + XZP_FOOTER_SIZE;
    else
        goto fail;
    data_end = archive_size - XZP_FOOTER_SIZE;
    if (tables_end > data_end) goto fail;
    if (icount && (ioff < (uint32_t)tables_end ||
                   !xzp_within(data_end, (int64_t)ioff, (int64_t)ilen)))
        goto fail;
    for (i = 0U; i < ecount; ++i) {
        uint32_t len = xzp_le32(entries + i * 12U + 4U);
        uint32_t off = xzp_le32(entries + i * 12U + 8U);
        if (len && (off < XZP_HEADER_SIZE ||
                    !xzp_within(data_end, (int64_t)off, (int64_t)len)))
            goto fail;
    }

    s = (xzp_stream *)xx_mem_alloc(sizeof(*s));
    if (!s) goto fail;
    xx_mem_zero(s, sizeof(*s));
    s->archive_size = archive_size;
    if (!members) {
        /* validity only: no members materialised */
        xx_mem_free(entries);
        return s;
    }

    if (icount) {
        itab = (uint8_t *)xx_mem_alloc((size_t)icount * XZP_ENTRY_SIZE);
        keys = (uint64_t *)xx_mem_alloc((size_t)icount * sizeof(uint64_t));
        if (!itab || !keys ||
            !xzp_read(self->device, self->base_address + ioff, itab,
                      (size_t)icount * XZP_ENTRY_SIZE))
            goto fail;
        for (i = 0U; i < icount; ++i)
            keys[i] = ((uint64_t)xzp_le32(itab + i * 12U) << 32) | i;
        xzp_sort(keys, icount);
    }
    if (ecount) {
        size_t cap = 16U;
        s->items = (xzp_member *)xx_mem_alloc((size_t)ecount * sizeof(xzp_member));
        if (!s->items) goto fail;
        xx_mem_zero(s->items, (size_t)ecount * sizeof(xzp_member));
        while (cap < (size_t)ecount * 2U) cap *= 2U;
        names.slots = (const char **)xx_mem_alloc(cap * sizeof(char *));
        if (!names.slots) goto fail;
        xx_mem_zero((void *)names.slots, cap * sizeof(char *));
        names.mask = cap - 1U;
    }
    for (i = 0U; i < ecount; ++i) {
        xzp_member *m = &s->items[s->count];
        const uint8_t *e = entries + i * 12U;
        size_t item;
        char *name = NULL;
        if ((i & 1023U) == 0U && pd && xx_pd_is_stopped(pd)) goto fail;
        m->crc = xzp_le32(e);
        m->size = (int64_t)xzp_le32(e + 4);
        m->data_offset = self->base_address + (int64_t)xzp_le32(e + 8);
        m->entry_offset = self->base_address + XZP_HEADER_SIZE + (int64_t)i * 12;
        item = icount ? xzp_find(keys, icount, m->crc) : (size_t)-1;
        if (item != (size_t)-1)
            name = xzp_read_name(self, (int64_t)xzp_le32(itab + item * 12U + 4U),
                                 (int64_t)ioff, (int64_t)ioff + ilen);
        m->named = name != NULL;
        if (!name) {
            char tmp[48];
            xx_rt_snprintf(tmp, sizeof(tmp), "_unnamed/%08X.bin", m->crc);
            name = xx_str_dup(tmp);
            if (!name) goto fail;
            ++s->unnamed;
        }
        /* Keep duplicates apart: "name", "name__2", "name__3", ... */
        if (xzp_names_seen(&names, name, false)) {
            uint32_t suffix = 1U;
            char *base = name;
            name = NULL;
            do {
                char tail[16];
                xx_str_free(name);
                xx_rt_snprintf(tail, sizeof(tail), "__%u", ++suffix);
                name = xx_str_concat(base, tail);
                if (!name) {
                    xx_str_free(base);
                    goto fail;
                }
            } while (xzp_names_seen(&names, name, false));
            xx_str_free(base);
        }
        m->name = name;
        (void)xzp_names_seen(&names, name, true);
        ++s->count;
    }
    xx_mem_free((void *)names.slots);
    xx_mem_free(keys);
    xx_mem_free(itab);
    xx_mem_free(entries);
    return s;
fail:
    xx_mem_free((void *)names.slots);
    xx_mem_free(keys);
    xx_mem_free(itab);
    xx_mem_free(entries);
    xzp_stream_free(s);
    return NULL;
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_valve_xzp_init(xx_valve_xzp *archive, xx_io_device *device,
                       int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_VALVE_XZP_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-valve-xzp");
    xx_format_set_extension(&archive->format, "xzp");
    archive->format.check_is_valid = xx_valve_xzp_check_is_valid;
    archive->format.handle_base_info = xx_valve_xzp_handle_base_info;
    archive->format.get_format_size = xx_valve_xzp_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_valve_xzp_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_valve_xzp_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_valve_xzp_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_valve_xzp_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_valve_xzp_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_valve_xzp_free_archive_records_reading;
    archive->format.destroy = xx_valve_xzp_vtable_destroy;
}

xx_valve_xzp *xx_valve_xzp_create(xx_io_device *device, int64_t base_address) {
    xx_valve_xzp *archive = (xx_valve_xzp *)xx_mem_alloc(sizeof(*archive));
    if (!archive) return NULL;
    xx_valve_xzp_init(archive, device, base_address);
    return archive;
}

void xx_valve_xzp_destroy(xx_valve_xzp *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_valve_xzp_free(xx_valve_xzp *archive) {
    if (!archive) return;
    xx_valve_xzp_destroy(archive);
    xx_mem_free(archive);
}

static void xx_valve_xzp_vtable_destroy(Abstractformat *self) {
    xx_valve_xzp_destroy((xx_valve_xzp *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_valve_xzp_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xzp_stream *s;
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    s = xzp_parse(self, false, pd);
    if (!s) return false;
    xzp_stream_free(s);
    return true;
}

bool xx_valve_xzp_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_valve_xzp *archive = (xx_valve_xzp *)self;
    xzp_stream *s;
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    self->base_info_handled = true;
    s = xzp_parse(self, true, pd);
    if (!s) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = s->archive_size;
    self->number_of_archive_records = s->count;
    archive->number_of_records = s->count;
    archive->unnamed_members = s->unnamed;
    xzp_stream_free(s);
    return true;
}

int64_t xx_valve_xzp_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return 0;
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_valve_xzp_get_number_of_archive_records(Abstractformat *self,
                                                    xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return 0U;
    return self->is_valid ? ((xx_valve_xzp *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xzp_set_record(xx_archive_record *record, const xzp_member *m) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = m->entry_offset;
    record->header_size = XZP_ENTRY_SIZE;
    record->data_offset = m->data_offset;
    record->compressed_size = m->size;
    return xx_archive_record_set_original_name(record, m->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)m->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)m->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xzp_copy_options(xx_list_s *target, const xx_list_s *options) {
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

static const xx_var *xzp_get_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_valve_xzp_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xzp_stream *s;
    xx_archive_record_state *state;
    if (!self || !self->device) return NULL;
    s = xzp_parse(self, true, pd);
    if (!s) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xzp_stream_free(s);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = s;
    state->free_internal = xzp_stream_free;
    state->total_records = (int64_t)s->count;
    if (!xzp_copy_options(&state->options, options) ||
        (s->count && !xzp_set_record(&state->current_record, &s->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = s->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_valve_xzp_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_valve_xzp_archive_record_move_to_next(Abstractformat *self,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    xzp_stream *s;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    s = (xzp_stream *)state->internal_state;
    if (!s || s->index + 1U >= s->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++s->index;
    ++state->current_index;
    state->has_record =
        xzp_set_record(&state->current_record, &s->items[s->index]);
    return state->has_record;
}

bool xx_valve_xzp_unpack_current_archive_record(Abstractformat *self,
                                                xx_archive_record_state *state,
                                                xx_pd_struct *pd) {
    xzp_stream *s;
    const xzp_member *m;
    const xx_var *opt;
    const char *base = NULL;
    char *converted = NULL, *target = NULL;
    size_t blen;
    bool result;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    s = (xzp_stream *)state->internal_state;
    if (!s || s->index >= s->count) return false;
    m = &s->items[s->index];
    if (!xzp_path_safe(m->name)) return false;
    opt = xzp_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!opt) return true; /* stored data, range already validated */
    if (opt->type == XX_VAR_TYPE_STRING || opt->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(opt);
    } else if (opt->type == XX_VAR_TYPE_WSTRING ||
               opt->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted = xx_str_unicode_to_utf8(xx_var_get_wstr(opt));
        base = converted;
    }
    if (!base) {
        xx_str_free(converted);
        return false;
    }
    blen = xx_str_len(base);
    if (blen && base[blen - 1U] != '/' && base[blen - 1U] != '\\')
        target = xx_str_concat3(base, "/", m->name);
    else
        target = xx_str_concat(base, m->name);
    xx_str_free(converted);
    if (!target) return false;
    /* The store helper removes its own output on failure (cleanup
     * contract); this function never deletes the target itself. */
    result = xx_store_create_dirs_a(target, false) &&
             xx_store_unpack_device_to_file(self->device, m->data_offset,
                                            m->size, target, pd);
    xx_str_free(target);
    return result;
}

void xx_valve_xzp_free_archive_records_reading(Abstractformat *self,
                                               xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
