/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Valve VPK reader. Layout reference: https://github.com/ValveResourceFormat/ValvePak
 * Parsing and extraction are independently implemented with bounded I/O.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/valve_vpk/xx_valve_vpk.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#define XX_valve_vpk_MAX_MEMBERS 1000000U
typedef struct xx_valve_vpk_member_s {
    char *name;
    int64_t header_offset, header_size, data_offset;
    int64_t compressed_size, uncompressed_size;
    uint32_t method;
    uint64_t timestamp;
    bool is_folder;
    int64_t preload_offset;
    uint32_t preload_size, crc32;
    bool has_crc, unavailable;
    xx_io_device *data_device;
} xx_valve_vpk_member;
typedef struct xx_valve_vpk_stream_s {
    xx_valve_vpk_member *items;
    size_t count, capacity, index;
    int64_t archive_size;
    uint64_t unavailable_members, unsupported_members;
} xx_valve_vpk_stream;
static void xx_valve_vpk_vtable_destroy(Abstractformat *self);

static bool xx_valve_vpk_range_within(int64_t span, int64_t offset, int64_t size)
{
    return offset >= 0 && size >= 0 && offset <= span && size <= span - offset;
}
static bool xx_valve_vpk_read_from(xx_io_device *dev, int64_t offset, uint8_t *out, size_t size)
{
    size_t done = 0;
    if (!dev || offset < 0 || xx_io_seek64(dev, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t received = xx_io_read(dev, out + done, size - done);
        if (received <= 0 || (size_t)received > size - done) return false;
        done += (size_t)received;
    }
    return true;
}
static bool xx_valve_vpk_read_at(Abstractformat *self, int64_t offset, uint8_t *out, size_t size)
{
    return self && xx_valve_vpk_read_from(self->device, offset, out, size);
}
static bool xx_valve_vpk_read_rel(Abstractformat *self, int64_t span, int64_t offset, uint8_t *out, size_t size)
{
    return size <= (size_t)INT64_MAX && xx_valve_vpk_range_within(span, offset, (int64_t)size) && xx_valve_vpk_read_at(self, self->base_address + offset, out, size);
}
static bool xx_valve_vpk_path_safe(const char *name)
{
    const char *at = name;
    if (!at || !*at || *at == '/' || *at == '\\') return false;
    while (*at) {
        const char *end = at;
        size_t len;
        char stem[5] = {0};
        size_t i;
        while (*end && *end != '/' && *end != '\\') {
            unsigned char c = (unsigned char)*end;
            if (c < 32U || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') return false;
            ++end;
        }
        len = (size_t)(end - at);
        if (!len || (len == 1U && at[0] == '.') || (len == 2U && at[0] == '.' && at[1] == '.') || at[len - 1U] == '.' || at[len - 1U] == ' ') return false;
        for (i = 0; i < len && i < 4U && at[i] != '.'; ++i) {
            char c = at[i];
            stem[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
        }
        if ((i == 3U && (!xx_rt_strcmp(stem, "CON") || !xx_rt_strcmp(stem, "PRN") || !xx_rt_strcmp(stem, "AUX") || !xx_rt_strcmp(stem, "NUL"))) ||
            (i == 4U && (!xx_rt_strncmp(stem, "COM", 3U) || !xx_rt_strncmp(stem, "LPT", 3U)) && stem[3] >= '1' && stem[3] <= '9'))
            return false;
        at = *end ? end + 1 : end;
    }
    return true;
}
static void xx_valve_vpk_stream_free(void *pointer)
{
    xx_valve_vpk_stream *s = (xx_valve_vpk_stream *)pointer;
    size_t i;
    if (!s) return;
    for (i = 0; i < s->count; ++i) xx_str_free(s->items[i].name);
    xx_mem_free(s->items);
    xx_mem_free(s);
}
static bool xx_valve_vpk_names_equal(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned char ca = (unsigned char)*a++, cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return *a == *b;
}
/* Duplicate lump names are legal: preserve every record under a unique name. */
static bool xx_valve_vpk_add(xx_valve_vpk_stream *s, xx_valve_vpk_member *m)
{
    size_t i;
    char *original = m->name;
    unsigned suffix = 1U;
    if (s->count >= XX_valve_vpk_MAX_MEMBERS || !original) return false;
    for (;;) {
        bool found = false;
        for (i = 0; i < s->count; ++i)
            if (xx_valve_vpk_names_equal(s->items[i].name, m->name)) {
                found = true;
                break;
            }
        if (!found) break;
        {
            char tail[32];
            char *replacement;
            xx_rt_snprintf(tail, sizeof(tail), "__%u", ++suffix);
            replacement = xx_str_concat(original, tail);
            if (!replacement) {
                if (m->name != original) xx_str_free(m->name);
                m->name = original;
                return false;
            }
            if (m->name != original) xx_str_free(m->name);
            m->name = replacement;
        }
    }
    if (s->count == s->capacity) {
        size_t cap = s->capacity ? s->capacity * 2U : 16U;
        xx_valve_vpk_member *grown;
        if (cap > XX_valve_vpk_MAX_MEMBERS) cap = XX_valve_vpk_MAX_MEMBERS;
        grown = (xx_valve_vpk_member *)xx_mem_realloc(s->items, cap * sizeof(*grown));
        if (!grown) {
            if (m->name != original) xx_str_free(m->name);
            m->name = original;
            return false;
        }
        s->items = grown;
        s->capacity = cap;
    }
    if (m->name != original) xx_str_free(original);
    s->items[s->count++] = *m;
    return true;
}
static bool xx_valve_vpk_add_member(Abstractformat *self, xx_valve_vpk_stream *s, const char *name, int64_t h, int64_t hs, int64_t off, int64_t size, bool folder)
{
    xx_valve_vpk_member m;
    size_t i;
    xx_mem_zero(&m, sizeof(m));
    m.name = xx_str_dup(name);
    if (!m.name) return false;
    for (i = 0; m.name[i]; ++i)
        if (m.name[i] == '\\') m.name[i] = '/';
    m.header_offset = self->base_address + h;
    m.header_size = hs;
    m.data_offset = self->base_address + off;
    m.compressed_size = size;
    m.uncompressed_size = size;
    m.is_folder = folder;
    if (!xx_valve_vpk_add(s, &m)) {
        xx_str_free(m.name);
        return false;
    }
    if (off + size > s->archive_size) s->archive_size = off + size;
    return true;
}
static inline bool xx_valve_vpk_fixed_name(const uint8_t *src, size_t size, char *out)
{
    size_t i = 0;
    while (i < size && src[i]) {
        if (src[i] < 32U) return false;
        out[i] = (char)src[i];
        ++i;
    }
    out[i] = 0;
    return i != 0;
}
static inline bool xx_valve_vpk_string(Abstractformat *self, int64_t span, int64_t *offset, int64_t end, char *out, size_t capacity)
{
    size_t i = 0;
    while (*offset < end && i + 1U < capacity) {
        uint8_t c;
        if (!xx_valve_vpk_read_rel(self, span, (*offset)++, &c, 1U)) return false;
        out[i++] = (char)c;
        if (!c) return true;
        if (c < 32U) return false;
    }
    return false;
}
static inline bool xx_valve_vpk_pool_name(Abstractformat *self, int64_t span, int64_t pool, int64_t pool_size, uint32_t offset, char *out, size_t capacity)
{
    int64_t at = pool + (int64_t)offset;
    return (int64_t)offset < pool_size && xx_valve_vpk_string(self, span, &at, pool + pool_size, out, capacity) && out[0];
}

static bool xx_valve_vpk_decode(Abstractformat *self, const xx_valve_vpk_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd)
{
    uint8_t *buffer;
    size_t size;
    *out = NULL;
    *out_size = 0;
    if (member->unavailable || member->method != 0U || member->uncompressed_size < 0 || (uint64_t)member->uncompressed_size > SIZE_MAX || (pd && xx_pd_is_stopped(pd)))
        return false;
    size = (size_t)member->uncompressed_size;
    buffer = (uint8_t *)xx_mem_alloc(size ? size : 1U);
    if (!buffer) return false;
    if ((member->preload_size && !xx_valve_vpk_read_at(self, member->preload_offset, buffer, member->preload_size)) ||
        (size > member->preload_size && !xx_valve_vpk_read_from(member->data_device ? member->data_device : self->device, member->data_offset,
                                                                buffer + member->preload_size, size - member->preload_size)) ||
        (member->has_crc && xx_crc32(XX_CRC_TYPE_CRC32, buffer, size) != member->crc32)) {
        xx_mem_free(buffer);
        return false;
    }
    *out = buffer;
    *out_size = size;
    return true;
}

bool xx_valve_vpk_set_volume(xx_valve_vpk *archive, uint16_t index, xx_io_device *device)
{
    size_t i;
    xx_valve_vpk_volume *grown;
    if (!archive || index == 0x7FFFU) return false;
    for (i = 0; i < archive->volume_count; ++i)
        if (archive->volumes[i].index == index) {
            archive->volumes[i].device = device;
            archive->format.base_info_handled = false;
            return true;
        }
    if (!device) return true;
    if (archive->volume_count >= 65535U) return false;
    grown = (xx_valve_vpk_volume *)xx_mem_realloc(archive->volumes, (archive->volume_count + 1U) * sizeof(*grown));
    if (!grown) return false;
    archive->volumes = grown;
    grown[archive->volume_count].index = index;
    grown[archive->volume_count++].device = device;
    archive->format.base_info_handled = false;
    return true;
}
static xx_io_device *xx_valve_vpk_volume_device(Abstractformat *self, uint16_t index)
{
    xx_valve_vpk *archive = (xx_valve_vpk *)self;
    size_t i;
    for (i = 0; i < archive->volume_count; ++i)
        if (archive->volumes[i].index == index) return archive->volumes[i].device;
    return NULL;
}

static xx_valve_vpk_stream *xx_valve_vpk_parse(Abstractformat *self, xx_pd_struct *pd)
{
    int64_t total, span;
    xx_valve_vpk_stream *s;
    if (!self || !self->device || self->base_address < 0 || (pd && xx_pd_is_stopped(pd))) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    s = (xx_valve_vpk_stream *)xx_mem_alloc(sizeof(*s));
    if (!s) return NULL;
    xx_mem_zero(s, sizeof(*s));
    {
        uint8_t h[28], entry[18];
        uint32_t version, tree_size;
        int64_t header_size, tree_end, data_end, at;
        bool finished = false;
        if (!xx_valve_vpk_read_rel(self, span, 0, h, 12U) || xx_data_get_u32(h, 4, 0, false) != 0x55AA1234U) goto fail;
        version = xx_data_get_u32(h + 4, 4, 0, false);
        tree_size = xx_data_get_u32(h + 8, 4, 0, false);
        if ((version != 1U && version != 2U) || !tree_size) goto fail;
        header_size = version == 1U ? 12 : 28;
        if (!xx_valve_vpk_read_rel(self, span, 0, h, (size_t)header_size) || !xx_valve_vpk_range_within(span, header_size, tree_size)) goto fail;
        tree_end = header_size + tree_size;
        data_end = span;
        s->archive_size = tree_end;
        if (version == 2U) {
            int64_t archive_size = tree_end + (int64_t)xx_data_get_u32(h + 12, 4, 0, false) + xx_data_get_u32(h + 16, 4, 0, false) +
                                   xx_data_get_u32(h + 20, 4, 0, false) + xx_data_get_u32(h + 24, 4, 0, false);
            if (archive_size > span || xx_data_get_u32(h + 16, 4, 0, false) % 28U ||
                (xx_data_get_u32(h + 20, 4, 0, false) != 0U && xx_data_get_u32(h + 20, 4, 0, false) != 48U))
                goto fail;
            data_end = tree_end + xx_data_get_u32(h + 12, 4, 0, false);
            s->archive_size = archive_size;
        }
        at = header_size;
        while (at < tree_end) {
            char ext[512];
            if (!xx_valve_vpk_string(self, span, &at, tree_end, ext, sizeof(ext))) goto fail;
            if (!ext[0]) {
                finished = true;
                break;
            }
            for (;;) {
                char dir[2048];
                if (!xx_valve_vpk_string(self, span, &at, tree_end, dir, sizeof(dir))) goto fail;
                if (!dir[0]) break;
                for (;;) {
                    char base[1024], name[4096];
                    uint16_t preload, index;
                    uint32_t off, size;
                    int64_t record_at;
                    xx_valve_vpk_member *m;
                    xx_io_device *volume = NULL;
                    if ((pd && xx_pd_is_stopped(pd)) || !xx_valve_vpk_string(self, span, &at, tree_end, base, sizeof(base))) goto fail;
                    if (!base[0]) break;
                    record_at = at;
                    if (!xx_valve_vpk_range_within(tree_end, at, 18) || !xx_valve_vpk_read_rel(self, span, at, entry, sizeof(entry))) goto fail;
                    preload = xx_data_get_u16(entry + 4, 2, 0, false);
                    index = xx_data_get_u16(entry + 6, 2, 0, false);
                    off = xx_data_get_u32(entry + 8, 4, 0, false);
                    size = xx_data_get_u32(entry + 12, 4, 0, false);
                    at += 18;
                    if (xx_data_get_u16(entry + 16, 2, 0, false) != 0xFFFFU || !xx_valve_vpk_range_within(tree_end, at, preload)) goto fail;
                    {
                        int printed = xx_rt_snprintf(name, sizeof(name), "%s%s%s%s%s", xx_rt_strcmp(dir, " ") ? dir : "", xx_rt_strcmp(dir, " ") ? "/" : "", base,
                                                     xx_rt_strcmp(ext, " ") ? "." : "", xx_rt_strcmp(ext, " ") ? ext : "");
                        if (printed < 0 || (size_t)printed >= sizeof(name)) goto fail;
                    }
                    if (index == 0x7FFFU) {
                        int64_t offset = tree_end + (int64_t)off;
                        if (!xx_valve_vpk_range_within(data_end, offset, size) || !xx_valve_vpk_add_member(self, s, name, record_at, 18, offset, size, false)) goto fail;
                    } else {
                        volume = xx_valve_vpk_volume_device(self, index);
                        if (volume && !xx_valve_vpk_range_within(xx_io_total_size(volume), off, size)) goto fail;
                        if (!xx_valve_vpk_add_member(self, s, name, record_at, 18, 0, 0, false)) goto fail;
                    }
                    m = &s->items[s->count - 1U];
                    m->preload_offset = self->base_address + at;
                    m->preload_size = preload;
                    m->crc32 = xx_data_get_u32(entry, 4, 0, false);
                    m->has_crc = true;
                    m->compressed_size = (int64_t)size + preload;
                    m->uncompressed_size = m->compressed_size;
                    if (index != 0x7FFFU) {
                        m->data_offset = off;
                        m->data_device = volume;
                        m->unavailable = size != 0U && !volume;
                        if (m->unavailable) ++s->unavailable_members;
                    }
                    at += preload;
                }
            }
        }
        if (!finished || at != tree_end) goto fail;
    }
    return s;
fail:
    xx_valve_vpk_stream_free(s);
    return NULL;
}
/* ---------------------------------------------------------- lifecycle --- */

void xx_valve_vpk_init(xx_valve_vpk *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_VALVE_VPK;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-valve-vpk");
    xx_format_set_extension(&archive->format, "vpk");
    archive->format.check_is_valid = xx_valve_vpk_check_is_valid;
    archive->format.handle_base_info = xx_valve_vpk_handle_base_info;
    archive->format.get_format_size = xx_valve_vpk_get_format_size;
    archive->format.get_number_of_archive_records = xx_valve_vpk_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_valve_vpk_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_valve_vpk_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_valve_vpk_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_valve_vpk_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_valve_vpk_free_archive_records_reading;
    archive->format.destroy = xx_valve_vpk_vtable_destroy;
}

xx_valve_vpk *xx_valve_vpk_create(xx_io_device *device, int64_t base_address)
{
    xx_valve_vpk *archive = (xx_valve_vpk *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_valve_vpk_init(archive, device, base_address);
    return archive;
}

void xx_valve_vpk_destroy(xx_valve_vpk *archive)
{
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
    xx_mem_free(archive->volumes);
    archive->volumes = NULL;
    archive->volume_count = 0;
}

void xx_valve_vpk_free(xx_valve_vpk *archive)
{
    if (!archive) return;
    xx_valve_vpk_destroy(archive);
    xx_mem_free(archive);
}

static void xx_valve_vpk_vtable_destroy(Abstractformat *self)
{
    xx_valve_vpk_destroy((xx_valve_vpk *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_valve_vpk_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    xx_valve_vpk_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_valve_vpk_parse(self, pd);
    if (!stream) return false;
    xx_valve_vpk_stream_free(stream);
    return true;
}

bool xx_valve_vpk_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_valve_vpk *archive = (xx_valve_vpk *)self;
    xx_valve_vpk_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_valve_vpk_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    archive->unavailable_members = stream->unavailable_members;
    archive->unsupported_members = stream->unsupported_members;
    xx_valve_vpk_stream_free(stream);
    return true;
}

int64_t xx_valve_vpk_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_valve_vpk_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_valve_vpk *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_valve_vpk_set_record(xx_archive_record *record, const xx_valve_vpk_member *member)
{
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP, member->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, member->is_folder) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false);
}

static bool xx_valve_vpk_copy_options(xx_list_s *target, const xx_list_s *options)
{
    size_t index;

    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) || !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_valve_vpk_get_option(const xx_list_s *options, uint32_t meta_id)
{
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_valve_vpk_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_valve_vpk_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_valve_vpk_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_valve_vpk_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_valve_vpk_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_valve_vpk_copy_options(&state->options, options) || (stream->count != 0U && !xx_valve_vpk_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_valve_vpk_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_valve_vpk_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_valve_vpk_stream *stream;

    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_valve_vpk_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_valve_vpk_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_valve_vpk_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_valve_vpk_stream *stream;
    const xx_valve_vpk_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_valve_vpk_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_valve_vpk_path_safe(member->name)) return false;

    path_option = xx_valve_vpk_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_valve_vpk_decode(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' && base_path[xx_str_len(base_path) - 1U] != '/' && base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (member->is_folder) {
        result = xx_store_create_dirs_a(target_path, true);
        xx_str_free(target_path);
        return result;
    }
    if (!xx_store_create_dirs_a(target_path, false) || !xx_valve_vpk_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent = xx_io_write(output, plain + completed, plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_valve_vpk_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}
