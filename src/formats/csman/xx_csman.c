/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/csman/xx_csman.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is supplied locally until the enumerator
 * lands. Delete this block once XX_FILE_TYPE_CSMAN exists in the enum. */
#ifdef CSMAN
#define XX_CSMAN_FILE_TYPE XX_FILE_TYPE_CSMAN
#else
#define XX_CSMAN_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_CSMAN_HEADER_SIZE 16
#define XX_CSMAN_ENTRY_HEADER_SIZE 6
#define XX_CSMAN_MAX_ENTRIES 8192U
/* Bounds on attacker-declared sizes. The published extractor caps inflate at
 * 100 MiB; that is kept lower here on purpose, because a configuration store
 * is kilobytes in practice and the decompressed size is a header field, so a
 * 100 MiB cap lets a few hundred compressed bytes demand 100 MiB of memory
 * before anything has been validated. */
#define XX_CSMAN_MAX_DATA_SIZE (16U * 1024U * 1024U)
#define XX_CSMAN_MAX_PLAIN_SIZE (16U * 1024U * 1024U)

typedef struct xx_csman_entry_s {
    char *name;            /**< "<KEY>.dat", key in eight upper-case hex. */
    int64_t header_offset; /**< Device offset, or -1 in the compressed case. */
    int64_t data_offset;   /**< Device offset, or -1 in the compressed case. */
    size_t plain_offset;   /**< Offset of the value inside the plain buffer. */
    uint32_t key;
    uint16_t size;
} xx_csman_entry;

typedef struct xx_csman_private_s {
    xx_csman_entry *entries;
    size_t count;
    size_t capacity;
    uint8_t *plain;      /**< The entry table, inflated when necessary. */
    size_t plain_size;
    int64_t input_size;
    int64_t data_offset; /**< Device offset of the data region. */
    int64_t archive_end;
    uint32_t compressed_size;
    uint32_t decompressed_size;
    bool is_compressed;
    bool is_big_endian;
} xx_csman_private;

typedef struct xx_csman_archive_stream_s {
    xx_csman_private parsed;
    size_t index;
} xx_csman_archive_stream;

static void xx_csman_vtable_destroy(Abstractformat *self);

/* seek64 rather than seek: `long` is 32-bit on Win64 and a DAT region can sit
 * anywhere in a multi-gigabyte firmware dump. */
static bool xx_csman_read_at(xx_io_device *device, int64_t offset, void *data,
                             size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    if (!device || (!data && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static bool xx_csman_range_within(int64_t total_size, int64_t offset,
                                  int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

/* "<KEY>.dat" with the key as eight upper-case hex digits. When the same key
 * appears twice the ordinal is appended, mirroring the published extractor,
 * so a listing never names two members identically. */
static char *xx_csman_make_name(uint32_t key, size_t duplicate_index) {
    static const char digits[] = "0123456789ABCDEF";
    char buffer[32];
    size_t used = 0U;
    size_t index;
    char *name;
    for (index = 0U; index < 8U; ++index) {
        buffer[used++] = digits[(key >> ((7U - index) * 4U)) & 0xFU];
    }
    if (duplicate_index != 0U) {
        char decimal[8];
        size_t digit_count = 0U;
        size_t value = duplicate_index;
        buffer[used++] = '_';
        while (value != 0U && digit_count < sizeof(decimal)) {
            decimal[digit_count++] = (char)('0' + (value % 10U));
            value /= 10U;
        }
        while (digit_count != 0U) buffer[used++] = decimal[--digit_count];
    }
    buffer[used++] = '.';
    buffer[used++] = 'd';
    buffer[used++] = 'a';
    buffer[used++] = 't';
    name = (char *)xx_mem_alloc(used + 1U);
    if (!name) return NULL;
    xx_mem_copy(name, buffer, used);
    name[used] = '\0';
    return name;
}

static void xx_csman_private_cleanup(xx_csman_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->count; ++index) {
        if (parsed->entries[index].name) {
            xx_str_free(parsed->entries[index].name);
        }
    }
    if (parsed->entries) xx_mem_free(parsed->entries);
    if (parsed->plain) xx_mem_free(parsed->plain);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->data_offset = -1;
    parsed->archive_end = -1;
}

static bool xx_csman_append_entry(xx_csman_private *parsed,
                                  xx_csman_entry *entry) {
    xx_csman_entry *grown;
    size_t capacity;
    if (!parsed || !entry || !entry->name ||
        parsed->count >= XX_CSMAN_MAX_ENTRIES) {
        return false;
    }
    if (parsed->count == parsed->capacity) {
        capacity = parsed->capacity ? parsed->capacity * 2U : 32U;
        if (capacity < parsed->count ||
            capacity > SIZE_MAX / sizeof(*parsed->entries)) return false;
        grown = (xx_csman_entry *)xx_mem_realloc(
            parsed->entries, capacity * sizeof(*parsed->entries));
        if (!grown) return false;
        parsed->entries = grown;
        parsed->capacity = capacity;
    }
    parsed->entries[parsed->count++] = *entry;
    xx_mem_zero(entry, sizeof(*entry));
    return true;
}

/* How many earlier entries already carry this key. The entry cap keeps this
 * linear scan bounded; a configuration store holds thousands of keys at most.
 */
static size_t xx_csman_duplicate_index(const xx_csman_private *parsed,
                                       uint32_t key) {
    size_t index;
    size_t seen = 0U;
    for (index = 0U; index < parsed->count; ++index) {
        if (parsed->entries[index].key == key) ++seen;
    }
    return seen;
}

/* Load the entry table into parsed->plain, inflating the data region first
 * when the header says the two sizes differ. */
static bool xx_csman_load_plain(Abstractformat *self, xx_csman_private *parsed,
                                xx_pd_struct *pd) {
    uint8_t *raw;
    uint8_t *plain;
    size_t written = 0U;
    if (parsed->compressed_size > XX_CSMAN_MAX_DATA_SIZE ||
        parsed->decompressed_size > XX_CSMAN_MAX_PLAIN_SIZE ||
        parsed->compressed_size == 0U || parsed->decompressed_size == 0U) {
        return false;
    }
    raw = (uint8_t *)xx_mem_alloc(parsed->compressed_size);
    if (!raw) return false;
    if (!xx_csman_read_at(self->device, parsed->data_offset, raw,
                          parsed->compressed_size)) {
        xx_mem_free(raw);
        return false;
    }
    if (!parsed->is_compressed) {
        parsed->plain = raw;
        parsed->plain_size = parsed->compressed_size;
        return true;
    }
    /* A compressed region must open with a zlib header; the published parser
     * checks exactly this byte before attempting to inflate. */
    if (raw[0] != 0x78U) {
        xx_mem_free(raw);
        return false;
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(raw);
        return false;
    }
    plain = (uint8_t *)xx_mem_alloc(parsed->decompressed_size);
    if (!plain) {
        xx_mem_free(raw);
        return false;
    }
    if (!xx_zlib_stream_decode_memory(raw, parsed->compressed_size, plain,
                                      parsed->decompressed_size, &written) ||
        written == 0U || written > parsed->decompressed_size) {
        xx_mem_free(raw);
        xx_mem_free(plain);
        return false;
    }
    xx_mem_free(raw);
    parsed->plain = plain;
    parsed->plain_size = written;
    return true;
}

static bool xx_csman_parse(Abstractformat *self, xx_csman_private *parsed,
                           xx_pd_struct *pd) {
    uint8_t header[XX_CSMAN_HEADER_SIZE];
    int64_t total_size;
    size_t cursor = 0U;
    bool terminated = false;
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->data_offset = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (!xx_csman_range_within(total_size, self->base_address,
                               XX_CSMAN_HEADER_SIZE) ||
        !xx_csman_read_at(self->device, self->base_address, header,
                          sizeof(header))) {
        goto fail;
    }
    /* The magic spells "CS" in a little endian image and "SC" in a big endian
     * one, so the two magic bytes also decide how every later field reads. */
    if (header[0] == 'C' && header[1] == 'S') {
        parsed->is_big_endian = false;
    } else if (header[0] == 'S' && header[1] == 'C') {
        parsed->is_big_endian = true;
    } else {
        goto fail;
    }
    parsed->compressed_size = xx_data_get_u32(header, sizeof(header), 4U,
                                              parsed->is_big_endian);
    parsed->decompressed_size = xx_data_get_u32(header, sizeof(header), 12U,
                                                parsed->is_big_endian);
    parsed->is_compressed =
        parsed->compressed_size != parsed->decompressed_size;
    parsed->data_offset = self->base_address + XX_CSMAN_HEADER_SIZE;
    if (!xx_csman_range_within(total_size, parsed->data_offset,
                               (int64_t)parsed->compressed_size)) {
        goto fail;
    }
    parsed->input_size = total_size;
    parsed->archive_end = parsed->data_offset + (int64_t)parsed->compressed_size;
    if (!xx_csman_load_plain(self, parsed, pd)) goto fail;
    while (cursor + 4U <= parsed->plain_size) {
        uint32_t key;
        uint16_t size;
        xx_csman_entry entry;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        key = xx_data_get_u32(parsed->plain, parsed->plain_size, cursor,
                              parsed->is_big_endian);
        if (key == 0U) {
            /* The bare zero key is the end-of-table marker. */
            terminated = true;
            cursor += 4U;
            break;
        }
        if (cursor + XX_CSMAN_ENTRY_HEADER_SIZE > parsed->plain_size) {
            goto fail;
        }
        size = xx_data_get_u16(parsed->plain, parsed->plain_size, cursor + 4U,
                               parsed->is_big_endian);
        if ((size_t)size >
            parsed->plain_size - cursor - XX_CSMAN_ENTRY_HEADER_SIZE) {
            goto fail;
        }
        if (parsed->count >= XX_CSMAN_MAX_ENTRIES) goto fail;
        xx_mem_zero(&entry, sizeof(entry));
        entry.name = xx_csman_make_name(
            key, xx_csman_duplicate_index(parsed, key));
        if (!entry.name) goto fail;
        entry.key = key;
        entry.size = size;
        entry.plain_offset = cursor + XX_CSMAN_ENTRY_HEADER_SIZE;
        if (parsed->is_compressed) {
            /* The plain bytes only exist in memory, so there is no device
             * offset to report for them. */
            entry.header_offset = -1;
            entry.data_offset = -1;
        } else {
            entry.header_offset = parsed->data_offset + (int64_t)cursor;
            entry.data_offset = parsed->data_offset +
                                (int64_t)entry.plain_offset;
        }
        if (!xx_csman_append_entry(parsed, &entry)) {
            if (entry.name) xx_str_free(entry.name);
            goto fail;
        }
        cursor += XX_CSMAN_ENTRY_HEADER_SIZE + (size_t)size;
    }
    if (!terminated || parsed->count == 0U) goto fail;
    return true;
fail:
    xx_csman_private_cleanup(parsed);
    return false;
}

static bool xx_csman_copy_options(xx_list_s *destination,
                                  const xx_list_s *source) {
    size_t index;
    if (!destination || !source) return source == NULL;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)source, index);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_csman_find_option(const xx_list_s *options,
                                          uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at(
            (const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_csman_populate_record(xx_archive_record *record,
                                     const xx_csman_entry *entry) {
    if (!record || !entry || !entry->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = entry->header_offset;
    record->header_size = XX_CSMAN_ENTRY_HEADER_SIZE;
    record->data_offset = entry->data_offset;
    record->compressed_size = entry->size;
    return xx_archive_record_set_original_name(record, entry->name) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          entry->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          entry->size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

static void xx_csman_archive_stream_free(void *pointer) {
    xx_csman_archive_stream *stream = (xx_csman_archive_stream *)pointer;
    if (!stream) return;
    xx_csman_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

void xx_csman_init(xx_csman *csman, xx_io_device *dev, int64_t base_address) {
    if (!csman) return;
    xx_mem_zero(csman, sizeof(*csman));
    xx_format_init(&csman->format, dev, base_address);
    csman->format.endian = XX_ENDIAN_LITTLE;
    csman->format.file_type = XX_CSMAN_FILE_TYPE;
    csman->format.format_type = XX_TYPE_ARCHIVE;
    csman->format.is_archive = true;
    xx_format_set_mime_type(&csman->format, "application/x-csman-dat");
    xx_format_set_extension(&csman->format, "dat");
    csman->format.check_is_valid = xx_csman_check_is_valid;
    csman->format.handle_base_info = xx_csman_handle_base_info;
    csman->format.get_format_size = xx_csman_get_format_size;
    csman->format.get_number_of_archive_records =
        xx_csman_get_number_of_archive_records;
    csman->format.create_archive_records_reading =
        xx_csman_create_archive_records_reading;
    csman->format.get_current_archive_record =
        xx_csman_get_current_archive_record;
    csman->format.unpack_current_archive_record =
        xx_csman_unpack_current_archive_record;
    csman->format.archive_record_move_to_next =
        xx_csman_archive_record_move_to_next;
    csman->format.free_archive_records_reading =
        xx_csman_free_archive_records_reading;
    csman->format.destroy = xx_csman_vtable_destroy;
    csman->archive_end = -1;
}

xx_csman *xx_csman_create(xx_io_device *dev, int64_t base_address) {
    xx_csman *csman = (xx_csman *)xx_mem_alloc(sizeof(*csman));
    if (csman) xx_csman_init(csman, dev, base_address);
    return csman;
}

void xx_csman_destroy(xx_csman *csman) {
    if (!csman) return;
    if (csman->internal) {
        xx_csman_private_cleanup((xx_csman_private *)csman->internal);
        xx_mem_free(csman->internal);
        csman->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&csman->format);
}

static void xx_csman_vtable_destroy(Abstractformat *self) {
    xx_csman_destroy((xx_csman *)self);
}

void xx_csman_free(xx_csman *csman) {
    if (!csman) return;
    xx_csman_destroy(csman);
    xx_mem_free(csman);
}

bool xx_csman_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_csman_private parsed;
    bool result = xx_csman_parse(self, &parsed, pd);
    xx_csman_private_cleanup(&parsed);
    return result;
}

bool xx_csman_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_csman_private *parsed;
    xx_csman *csman = (xx_csman *)self;
    int64_t total_size;
    if (!self || !csman) return false;
    parsed = (xx_csman_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_csman_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (csman->internal) {
        xx_csman_private_cleanup((xx_csman_private *)csman->internal);
        xx_mem_free(csman->internal);
    }
    csman->internal = parsed;
    csman->number_of_records = parsed->count;
    csman->number_of_members = parsed->count;
    csman->compressed_size = parsed->compressed_size;
    csman->decompressed_size = parsed->decompressed_size;
    csman->is_compressed = parsed->is_compressed;
    csman->archive_end = parsed->archive_end;
    self->endian = parsed->is_big_endian ? XX_ENDIAN_BIG : XX_ENDIAN_LITTLE;
    self->format_size = parsed->archive_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > parsed->archive_end) {
        self->overlay_offset = parsed->archive_end;
        self->overlay_size = total_size - parsed->archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = parsed->count;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_csman_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_csman_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return ((xx_csman *)self)->number_of_records;
}

xx_archive_record_state *xx_csman_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_csman_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_csman_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_csman_copy_options(&state->options, options) ||
        !xx_csman_parse(self, &stream->parsed, pd)) {
        xx_csman_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_csman_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_csman_populate_record(&state->current_record,
                                 &stream->parsed.entries[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_csman_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_csman_archive_record_move_to_next(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_csman_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_csman_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_csman_populate_record(&state->current_record,
                                  &stream->parsed.entries[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_csman_unpack_current_archive_record(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    const xx_csman_archive_stream *stream;
    const xx_csman_entry *entry;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    xx_io_device *memory = NULL;
    bool result = false;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || !state->internal_state ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (const xx_csman_archive_stream *)state->internal_state;
    if (stream->index >= stream->parsed.count) return false;
    entry = &stream->parsed.entries[stream->index];
    name = xx_archive_record_get_original_name(&state->current_record);
    /* The name is built here from a hex key, so it is always a single safe
     * component; the guard only catches a record that lost its name. */
    if (!name || !name[0]) return false;
    /* The value bytes always exist in the plain buffer, compressed or not,
     * so they are served from there in both cases. */
    if (entry->plain_offset > stream->parsed.plain_size ||
        (size_t)entry->size >
            stream->parsed.plain_size - entry->plain_offset) {
        return false;
    }
    option = xx_csman_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) return true;
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto cleanup;
    if (base[0] && base[xx_str_len(base) - 1U] != '/' &&
        base[xx_str_len(base) - 1U] != '\\') {
        destination = xx_str_concat3(base, "/", name);
    } else {
        destination = xx_str_concat(base, name);
    }
    if (!destination) goto cleanup;
    memory = xx_io_mem_open_ro(stream->parsed.plain,
                               stream->parsed.plain_size);
    if (!memory) goto cleanup;
    if (xx_store_create_dirs_a(destination, false)) {
        result = xx_store_unpack_device_to_file(memory,
                                                (int64_t)entry->plain_offset,
                                                (int64_t)entry->size,
                                                destination, pd);
    }
cleanup:
    if (memory) xx_io_close(memory);
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_csman_free_archive_records_reading(Abstractformat *self,
                                           xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_csman_get_number_of_records(const xx_csman *csman) {
    return csman ? csman->number_of_records : 0U;
}
uint32_t xx_csman_get_compressed_size(const xx_csman *csman) {
    return csman ? csman->compressed_size : 0U;
}
uint32_t xx_csman_get_decompressed_size(const xx_csman *csman) {
    return csman ? csman->decompressed_size : 0U;
}
bool xx_csman_get_is_compressed(const xx_csman *csman) {
    return csman ? csman->is_compressed : false;
}
int64_t xx_csman_get_archive_end(const xx_csman *csman) {
    return csman ? csman->archive_end : -1;
}
