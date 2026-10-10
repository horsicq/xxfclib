/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * HxC "Stream HFE" flux image reader. Written from the layout measured on
 * images produced by hxcfe (-conv:HXC_STREAMHFE); no HxC source was used.
 * See xx_hxc_stream_hfe.h for the layout.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/hxc_stream_hfe/xx_hxc_stream_hfe.h"

#include "xxfclib/algo/lz4/xx_lz4.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#ifdef HXC_STREAM_HFE
#define XX_HXC_STREAM_HFE_FILE_TYPE XX_FILE_TYPE_HXC_STREAM_HFE
#else
#define XX_HXC_STREAM_HFE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define HSH_FLAG_LZ4 1U
/* An LZ4 sequence expands at most ~255x; anything claiming more is not LZ4. */
#define HSH_LZ4_MAX_RATIO 256U

typedef struct hsh_track_s {
    int64_t offset; /**< Absolute. */
    uint32_t packed_size;
    uint32_t unpacked_size;
    uint32_t flags;
    uint16_t track;
    uint8_t side;
} hsh_track;

typedef struct hsh_parsed_s {
    hsh_track *tracks; /**< Present tracks only. */
    size_t count;
    int64_t header_offset;
    int64_t archive_end;
    uint32_t revision;
    uint32_t flags;
    uint32_t number_of_tracks;
    uint32_t number_of_sides;
    uint32_t tick_period;
} hsh_parsed;

typedef struct hsh_stream_s {
    hsh_parsed parsed;
    size_t index;
} hsh_stream;

static void hsh_vtable_destroy(Abstractformat *self);

static bool hsh_read_at(xx_io_device *device, int64_t offset, void *data, size_t size)
{
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static void hsh_parsed_cleanup(hsh_parsed *parsed)
{
    if (!parsed) return;
    if (parsed->tracks) xx_mem_free(parsed->tracks);
    xx_mem_zero(parsed, sizeof(*parsed));
}

/* Header and track list only; no stream is decoded here, so the probe costs
 * one 48-byte read plus at most 16 KiB of track list. */
static bool hsh_parse(Abstractformat *self, hsh_parsed *parsed, xx_pd_struct *pd)
{
    uint8_t header[XX_HXC_STREAM_HFE_HEADER_SIZE];
    uint8_t *list = NULL;
    int64_t input_size, avail;
    uint32_t tracks, sides, list_offset, index, entries;
    uint64_t list_end, previous_end;
    bool ok = false;

    if (parsed) xx_mem_zero(parsed, sizeof(*parsed));
    if (!self || !self->device || !parsed || self->base_address < 0 || (pd && xx_pd_is_stopped(pd))) return false;
    input_size = xx_io_total_size(self->device);
    if (input_size < self->base_address) return false;
    avail = input_size - self->base_address;
    if (avail < (int64_t)XX_HXC_STREAM_HFE_HEADER_SIZE || !hsh_read_at(self->device, self->base_address, header, sizeof(header)) ||
        xx_rt_memcmp(header, XX_HXC_STREAM_HFE_SIGNATURE, XX_HXC_STREAM_HFE_SIGNATURE_SIZE) != 0)
        return false;

    tracks = xx_data_get_u32(header, sizeof(header), 0x24U, false);
    sides = xx_data_get_u32(header, sizeof(header), 0x28U, false);
    list_offset = xx_data_get_u32(header, sizeof(header), 0x1CU, false);
    if (tracks == 0U || tracks > XX_HXC_STREAM_HFE_MAX_TRACKS || sides == 0U || sides > XX_HXC_STREAM_HFE_MAX_SIDES || list_offset < XX_HXC_STREAM_HFE_HEADER_SIZE)
        return false;
    entries = tracks * sides;
    list_end = (uint64_t)list_offset + (uint64_t)entries * XX_HXC_STREAM_HFE_ENTRY_SIZE;
    if (list_end > (uint64_t)avail) return false;

    list = (uint8_t *)xx_mem_alloc((size_t)entries * XX_HXC_STREAM_HFE_ENTRY_SIZE);
    parsed->tracks = (hsh_track *)xx_mem_calloc(entries, sizeof(hsh_track));
    if (!list || !parsed->tracks || !hsh_read_at(self->device, self->base_address + (int64_t)list_offset, list, (size_t)entries * XX_HXC_STREAM_HFE_ENTRY_SIZE))
        goto done;

    previous_end = list_end;
    parsed->archive_end = self->base_address + (int64_t)list_end;
    for (index = 0U; index < entries; ++index) {
        const uint8_t *e = list + (size_t)index * XX_HXC_STREAM_HFE_ENTRY_SIZE;
        const size_t es = XX_HXC_STREAM_HFE_ENTRY_SIZE;
        uint32_t flags = xx_data_get_u32(e, es, 0x00U, false);
        uint32_t offset = xx_data_get_u32(e, es, 0x04U, false);
        uint32_t packed = xx_data_get_u32(e, es, 0x08U, false);
        uint32_t unpacked = xx_data_get_u32(e, es, 0x0CU, false);
        hsh_track *t;
        if (flags & ~HSH_FLAG_LZ4) goto done;
        if (offset == 0U && packed == 0U && unpacked == 0U) continue;
        if (packed == 0U || unpacked == 0U || packed > XX_HXC_STREAM_HFE_MAX_STREAM || unpacked > XX_HXC_STREAM_HFE_MAX_STREAM) goto done;
        if ((flags & HSH_FLAG_LZ4) ? (uint64_t)unpacked > (uint64_t)packed * HSH_LZ4_MAX_RATIO : unpacked != packed) goto done;
        /* Streams follow the list in list order and never overlap: that is
         * how hxcfe writes them, and it keeps one packed span from being
         * reused as many tracks. */
        if ((uint64_t)offset < previous_end || (uint64_t)offset + packed > (uint64_t)avail) goto done;
        previous_end = (uint64_t)offset + packed;
        t = &parsed->tracks[parsed->count++];
        t->offset = self->base_address + (int64_t)offset;
        t->packed_size = packed;
        t->unpacked_size = unpacked;
        t->flags = flags;
        t->track = (uint16_t)(index / sides);
        t->side = (uint8_t)(index % sides);
        parsed->archive_end = self->base_address + (int64_t)previous_end;
    }
    if (parsed->count == 0U) goto done;
    parsed->header_offset = self->base_address;
    parsed->revision = xx_data_get_u32(header, sizeof(header), 0x10U, false);
    parsed->flags = xx_data_get_u32(header, sizeof(header), 0x14U, false);
    parsed->number_of_tracks = tracks;
    parsed->number_of_sides = sides;
    parsed->tick_period = xx_data_get_u32(header, sizeof(header), 0x2CU, false);
    ok = true;
done:
    if (list) xx_mem_free(list);
    if (!ok) hsh_parsed_cleanup(parsed);
    return ok;
}

/* Decode one track into a fresh buffer of exactly unpacked_size bytes. */
static uint8_t *hsh_load_track(xx_io_device *device, const hsh_track *t)
{
    uint8_t *packed = NULL, *out = NULL;
    size_t written = 0U;
    if (!device || !t) return NULL;
    packed = (uint8_t *)xx_mem_alloc(t->packed_size);
    if (!packed || !hsh_read_at(device, t->offset, packed, t->packed_size)) goto fail;
    if (!(t->flags & HSH_FLAG_LZ4)) return packed;
    out = (uint8_t *)xx_mem_alloc(t->unpacked_size);
    if (!out || !xx_lz4_decompress_block(packed, t->packed_size, out, t->unpacked_size, &written) || written != t->unpacked_size) goto fail;
    xx_mem_free(packed);
    return out;
fail:
    if (packed) xx_mem_free(packed);
    if (out) xx_mem_free(out);
    return NULL;
}

static bool hsh_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *hsh_find_option(const xx_list_s *options, uint32_t meta_id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static void hsh_track_name(const hsh_track *t, char *name, size_t size)
{
    xx_rt_snprintf(name, size, "track%03u_side%u.stream", (unsigned)t->track, (unsigned)t->side);
}

static bool hsh_populate_record(xx_archive_record *record, const hsh_parsed *parsed, const hsh_track *t)
{
    char name[48];
    if (!record || !parsed || !t) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    hsh_track_name(t, name, sizeof(name));
    record->header_offset = parsed->header_offset;
    record->header_size = XX_HXC_STREAM_HFE_HEADER_SIZE;
    record->data_offset = t->offset;
    record->compressed_size = t->packed_size;
    return xx_archive_record_set_original_name(record, name) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, t->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, t->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, (t->flags & HSH_FLAG_LZ4) ? 1U : 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void hsh_stream_free(void *pointer)
{
    hsh_stream *stream = (hsh_stream *)pointer;
    if (!stream) return;
    hsh_parsed_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

void xx_hxc_stream_hfe_init(xx_hxc_stream_hfe *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_HXC_STREAM_HFE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-hxc-stream-hfe");
    xx_format_set_extension(&archive->format, "hfe");
    archive->format.check_is_valid = xx_hxc_stream_hfe_check_is_valid;
    archive->format.handle_base_info = xx_hxc_stream_hfe_handle_base_info;
    archive->format.get_format_size = xx_hxc_stream_hfe_get_format_size;
    archive->format.get_number_of_archive_records = xx_hxc_stream_hfe_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_hxc_stream_hfe_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_hxc_stream_hfe_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_hxc_stream_hfe_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_hxc_stream_hfe_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_hxc_stream_hfe_free_archive_records_reading;
    archive->format.destroy = hsh_vtable_destroy;
    archive->archive_end = -1;
}

xx_hxc_stream_hfe *xx_hxc_stream_hfe_create(xx_io_device *device, int64_t base_address)
{
    xx_hxc_stream_hfe *archive = (xx_hxc_stream_hfe *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_hxc_stream_hfe_init(archive, device, base_address);
    return archive;
}

void xx_hxc_stream_hfe_destroy(xx_hxc_stream_hfe *archive)
{
    if (!archive) return;
    xx_format_cleanup_extra_parameters(&archive->format);
}

static void hsh_vtable_destroy(Abstractformat *self)
{
    xx_hxc_stream_hfe_destroy((xx_hxc_stream_hfe *)self);
}

void xx_hxc_stream_hfe_free(xx_hxc_stream_hfe *archive)
{
    if (!archive) return;
    xx_hxc_stream_hfe_destroy(archive);
    xx_mem_free(archive);
}

bool xx_hxc_stream_hfe_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    hsh_parsed parsed;
    bool result = hsh_parse(self, &parsed, pd);
    hsh_parsed_cleanup(&parsed);
    return result;
}

bool xx_hxc_stream_hfe_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_hxc_stream_hfe *archive = (xx_hxc_stream_hfe *)self;
    hsh_parsed parsed;
    int64_t total_size;
    if (!self) return false;
    if (!hsh_parse(self, &parsed, pd)) {
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    archive->number_of_records = parsed.count;
    archive->revision = parsed.revision;
    archive->flags = parsed.flags;
    archive->number_of_tracks = parsed.number_of_tracks;
    archive->number_of_sides = parsed.number_of_sides;
    archive->tick_period = parsed.tick_period;
    archive->archive_end = parsed.archive_end;
    self->format_size = parsed.archive_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed.archive_end) {
        self->overlay_offset = parsed.archive_end;
        self->overlay_size = total_size - parsed.archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed.count;
    self->is_valid = true;
    self->base_info_handled = true;
    hsh_parsed_cleanup(&parsed);
    return true;
}

int64_t xx_hxc_stream_hfe_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_hxc_stream_hfe_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) return 0U;
    return ((xx_hxc_stream_hfe *)self)->number_of_records;
}

xx_archive_record_state *xx_hxc_stream_hfe_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_archive_record_state *state;
    hsh_stream *stream;
    if (!self || !self->device || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (hsh_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!hsh_copy_options(&state->options, options) || !hsh_parse(self, &stream->parsed, pd)) {
        hsh_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = hsh_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U && hsh_populate_record(&state->current_record, &stream->parsed, &stream->parsed.tracks[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_hxc_stream_hfe_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_hxc_stream_hfe_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    hsh_stream *stream;
    if (!self || !state || state->format != self || !state->has_record || !state->internal_state || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (hsh_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!hsh_populate_record(&state->current_record, &stream->parsed, &stream->parsed.tracks[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_hxc_stream_hfe_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    hsh_stream *stream;
    const hsh_track *t;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    uint8_t *data = NULL;
    bool result = false;
    bool created = false;
    if (!self || !self->device || !state || state->format != self || !state->has_record || !state->internal_state || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (hsh_stream *)state->internal_state;
    if (stream->index >= stream->parsed.count) return false;
    t = &stream->parsed.tracks[stream->index];
    name = xx_archive_record_get_original_name(&state->current_record);
    if (!name || !name[0]) return false;

    data = hsh_load_track(self->device, t);
    if (!data) return false;
    option = hsh_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        /* No destination: report whether the stream decodes. */
        xx_mem_free(data);
        return true;
    }
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto cleanup;
    destination = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", name) : xx_str_concat(base, name);
    if (!destination || !xx_store_create_dirs_a(destination, false)) goto cleanup;
    {
        xx_io_device *out = xx_io_file_open(destination, "wb");
        size_t done = 0U;
        if (!out) goto cleanup;
        created = true;
        result = true;
        while (done < t->unpacked_size) {
            ssize_t wrote;
            if (pd && xx_pd_is_stopped(pd)) {
                result = false;
                break;
            }
            wrote = xx_io_write(out, data + done, t->unpacked_size - done);
            if (wrote <= 0 || (size_t)wrote > t->unpacked_size - done) {
                result = false;
                break;
            }
            done += (size_t)wrote;
        }
        if (xx_io_close(out) != 0) result = false;
    }
cleanup:
    if (!result && destination && created) xx_rt_remove(destination);
    if (data) xx_mem_free(data);
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_hxc_stream_hfe_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}
