/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/wince/xx_wince.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* Registration placeholder. xxfc_defs.h is shared and is not edited from
 * here, so the file-type constant is supplied locally until the enumerator
 * lands. Delete this block once XX_FILE_TYPE_WINCE exists in the enum. */
#ifdef WINCE
#define XX_WINCE_FILE_TYPE XX_FILE_TYPE_WINCE
#else
#define XX_WINCE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_WINCE_MAGIC_SIZE 7
#define XX_WINCE_IMAGE_HEADER_SIZE 15
#define XX_WINCE_RECORD_HEADER_SIZE 12
#define XX_WINCE_MAX_RECORDS 65536U
#define XX_WINCE_CHECKSUM_CHUNK 65536U

static const uint8_t xx_wince_magic[XX_WINCE_MAGIC_SIZE] = {
    0x42U, 0x30U, 0x30U, 0x30U, 0x46U, 0x46U, 0x0AU /* "B000FF\n" */
};

typedef struct xx_wince_entry_s {
    char *name;             /**< "<ADDRESS>.bin", the address in upper hex. */
    int64_t header_offset;
    int64_t data_offset;
    uint32_t address;
    uint32_t length;
    uint32_t checksum;
} xx_wince_entry;

typedef struct xx_wince_private_s {
    xx_wince_entry *entries;
    size_t count;
    size_t capacity;
    int64_t input_size;
    int64_t archive_end;
    uint32_t image_start;
    uint32_t image_length;
} xx_wince_private;

typedef struct xx_wince_archive_stream_s {
    xx_wince_private parsed;
    size_t index;
} xx_wince_archive_stream;

static void xx_wince_vtable_destroy(Abstractformat *self);

/* All reads go through seek64: the record chain addresses a whole device and
 * `long` is 32-bit on Win64, which would silently cap at 2 GiB. */
static bool xx_wince_read_at(xx_io_device *device, int64_t offset, void *data,
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

static bool xx_wince_add(int64_t left, uint64_t right, int64_t *result) {
    if (!result || left < 0 || right > (uint64_t)(INT64_MAX - left)) {
        return false;
    }
    *result = left + (int64_t)right;
    return true;
}

/* True when [offset, offset + size) lies inside [0, total_size). */
static bool xx_wince_range_within(int64_t total_size, int64_t offset,
                                  int64_t size) {
    return (total_size >= 0) && (offset >= 0) && (size >= 0) &&
           (offset <= total_size) && (size <= total_size - offset);
}

/* Format `value` as upper-case hex followed by ".bin". The library is CRT
 * free, so the digits are emitted by hand rather than through snprintf. */
static char *xx_wince_make_name(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    char buffer[16];
    size_t used = 0U;
    size_t index;
    char *name;
    /* At least one digit, and a fixed eight so the listing sorts naturally. */
    for (index = 0U; index < 8U; ++index) {
        buffer[index] = digits[(value >> ((7U - index) * 4U)) & 0xFU];
    }
    used = 8U;
    buffer[used++] = '.';
    buffer[used++] = 'b';
    buffer[used++] = 'i';
    buffer[used++] = 'n';
    name = (char *)xx_mem_alloc(used + 1U);
    if (!name) return NULL;
    xx_mem_copy(name, buffer, used);
    name[used] = '\0';
    return name;
}

static void xx_wince_private_cleanup(xx_wince_private *parsed) {
    size_t index;
    if (!parsed) return;
    for (index = 0U; index < parsed->count; ++index) {
        if (parsed->entries[index].name) {
            xx_str_free(parsed->entries[index].name);
        }
    }
    if (parsed->entries) xx_mem_free(parsed->entries);
    xx_mem_zero(parsed, sizeof(*parsed));
    parsed->input_size = -1;
    parsed->archive_end = -1;
}

static bool xx_wince_append_entry(xx_wince_private *parsed,
                                  xx_wince_entry *entry) {
    xx_wince_entry *grown;
    size_t capacity;
    if (!parsed || !entry || !entry->name ||
        parsed->count >= XX_WINCE_MAX_RECORDS) {
        return false;
    }
    if (parsed->count == parsed->capacity) {
        capacity = parsed->capacity ? parsed->capacity * 2U : 32U;
        if (capacity < parsed->count ||
            capacity > SIZE_MAX / sizeof(*parsed->entries)) return false;
        grown = (xx_wince_entry *)xx_mem_realloc(
            parsed->entries, capacity * sizeof(*parsed->entries));
        if (!grown) return false;
        parsed->entries = grown;
        parsed->capacity = capacity;
    }
    parsed->entries[parsed->count++] = *entry;
    xx_mem_zero(entry, sizeof(*entry));
    return true;
}

/* The record checksum is the plain 32-bit wrapping sum of the payload bytes.
 * The payload is streamed through a fixed stack buffer so a record declaring
 * a gigabyte does not become a gigabyte allocation. */
static bool xx_wince_checksum_matches(xx_io_device *device, int64_t offset,
                                      uint32_t length, uint32_t expected,
                                      xx_pd_struct *pd) {
    uint8_t buffer[1024];
    uint32_t sum = 0U;
    uint32_t remaining = length;
    if (xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (remaining != 0U) {
        size_t want = remaining > sizeof(buffer) ? sizeof(buffer)
                                                 : (size_t)remaining;
        ssize_t got;
        size_t index;
        if (pd && xx_pd_is_stopped(pd)) return false;
        got = xx_io_read(device, buffer, want);
        if (got <= 0 || (size_t)got > want) return false;
        for (index = 0U; index < (size_t)got; ++index) sum += buffer[index];
        remaining -= (uint32_t)got;
    }
    return sum == expected;
}

static bool xx_wince_parse(Abstractformat *self, xx_wince_private *parsed,
                           xx_pd_struct *pd) {
    uint8_t header[XX_WINCE_IMAGE_HEADER_SIZE];
    int64_t total_size;
    int64_t offset;
    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->input_size = -1;
        parsed->archive_end = -1;
    }
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total_size = xx_io_total_size(self->device);
    if (!xx_wince_range_within(total_size, self->base_address,
                               XX_WINCE_IMAGE_HEADER_SIZE) ||
        !xx_wince_read_at(self->device, self->base_address, header,
                          sizeof(header)) ||
        xx_rt_memcmp(header, xx_wince_magic, XX_WINCE_MAGIC_SIZE) != 0) {
        goto fail;
    }
    parsed->input_size = total_size;
    parsed->image_start = xx_data_get_u32(header, sizeof(header),
                                          XX_WINCE_MAGIC_SIZE, false);
    parsed->image_length = xx_data_get_u32(header, sizeof(header),
                                           XX_WINCE_MAGIC_SIZE + 4U, false);
    offset = self->base_address + XX_WINCE_IMAGE_HEADER_SIZE;
    for (;;) {
        uint8_t record[XX_WINCE_RECORD_HEADER_SIZE];
        uint32_t address;
        uint32_t length;
        uint32_t checksum;
        int64_t data_offset;
        xx_wince_entry entry;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (parsed->count >= XX_WINCE_MAX_RECORDS) goto fail;
        if (!xx_wince_range_within(total_size, offset,
                                   XX_WINCE_RECORD_HEADER_SIZE) ||
            !xx_wince_read_at(self->device, offset, record, sizeof(record))) {
            goto fail;
        }
        address = xx_data_get_u32(record, sizeof(record), 0U, false);
        length = xx_data_get_u32(record, sizeof(record), 4U, false);
        checksum = xx_data_get_u32(record, sizeof(record), 8U, false);
        if (!xx_wince_add(offset, XX_WINCE_RECORD_HEADER_SIZE, &data_offset)) {
            goto fail;
        }
        if (address == 0U) {
            /* Terminator: its length field carries the entry point and no
             * payload follows, so the image ends with this header. */
            parsed->archive_end = data_offset;
            break;
        }
        if (!xx_wince_range_within(total_size, data_offset, (int64_t)length) ||
            !xx_wince_checksum_matches(self->device, data_offset, length,
                                       checksum, pd)) {
            goto fail;
        }
        xx_mem_zero(&entry, sizeof(entry));
        entry.name = xx_wince_make_name(address);
        if (!entry.name) goto fail;
        entry.header_offset = offset;
        entry.data_offset = data_offset;
        entry.address = address;
        entry.length = length;
        entry.checksum = checksum;
        if (!xx_wince_append_entry(parsed, &entry)) {
            if (entry.name) xx_str_free(entry.name);
            goto fail;
        }
        if (!xx_wince_add(data_offset, length, &offset)) goto fail;
    }
    if (parsed->count == 0U) goto fail;
    return true;
fail:
    xx_wince_private_cleanup(parsed);
    return false;
}

static bool xx_wince_copy_options(xx_list_s *destination,
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

static const xx_var *xx_wince_find_option(const xx_list_s *options,
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

static bool xx_wince_populate_record(xx_archive_record *record,
                                     const xx_wince_entry *entry) {
    if (!record || !entry || !entry->name) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = entry->header_offset;
    record->header_size = XX_WINCE_RECORD_HEADER_SIZE;
    record->data_offset = entry->data_offset;
    record->compressed_size = entry->length;
    return xx_archive_record_set_original_name(record, entry->name) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_UNCOMPRESSED_SIZE,
                                          entry->length) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          entry->length) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

static void xx_wince_archive_stream_free(void *pointer) {
    xx_wince_archive_stream *stream = (xx_wince_archive_stream *)pointer;
    if (!stream) return;
    xx_wince_private_cleanup(&stream->parsed);
    xx_mem_free(stream);
}

void xx_wince_init(xx_wince *wince, xx_io_device *dev, int64_t base_address) {
    if (!wince) return;
    xx_mem_zero(wince, sizeof(*wince));
    xx_format_init(&wince->format, dev, base_address);
    wince->format.endian = XX_ENDIAN_LITTLE;
    wince->format.file_type = XX_WINCE_FILE_TYPE;
    wince->format.format_type = XX_TYPE_ARCHIVE;
    wince->format.is_archive = true;
    xx_format_set_mime_type(&wince->format, "application/x-wince-bin");
    xx_format_set_extension(&wince->format, "bin");
    wince->format.check_is_valid = xx_wince_check_is_valid;
    wince->format.handle_base_info = xx_wince_handle_base_info;
    wince->format.get_format_size = xx_wince_get_format_size;
    wince->format.get_number_of_archive_records =
        xx_wince_get_number_of_archive_records;
    wince->format.create_archive_records_reading =
        xx_wince_create_archive_records_reading;
    wince->format.get_current_archive_record =
        xx_wince_get_current_archive_record;
    wince->format.unpack_current_archive_record =
        xx_wince_unpack_current_archive_record;
    wince->format.archive_record_move_to_next =
        xx_wince_archive_record_move_to_next;
    wince->format.free_archive_records_reading =
        xx_wince_free_archive_records_reading;
    wince->format.destroy = xx_wince_vtable_destroy;
    wince->archive_end = -1;
}

xx_wince *xx_wince_create(xx_io_device *dev, int64_t base_address) {
    xx_wince *wince = (xx_wince *)xx_mem_alloc(sizeof(*wince));
    if (wince) xx_wince_init(wince, dev, base_address);
    return wince;
}

void xx_wince_destroy(xx_wince *wince) {
    if (!wince) return;
    if (wince->internal) {
        xx_wince_private_cleanup((xx_wince_private *)wince->internal);
        xx_mem_free(wince->internal);
        wince->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&wince->format);
}

static void xx_wince_vtable_destroy(Abstractformat *self) {
    xx_wince_destroy((xx_wince *)self);
}

void xx_wince_free(xx_wince *wince) {
    if (!wince) return;
    xx_wince_destroy(wince);
    xx_mem_free(wince);
}

bool xx_wince_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_wince_private parsed;
    bool result = xx_wince_parse(self, &parsed, pd);
    xx_wince_private_cleanup(&parsed);
    return result;
}

bool xx_wince_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_wince_private *parsed;
    xx_wince *wince = (xx_wince *)self;
    int64_t total_size;
    if (!self || !wince) return false;
    parsed = (xx_wince_private *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !xx_wince_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (wince->internal) {
        xx_wince_private_cleanup((xx_wince_private *)wince->internal);
        xx_mem_free(wince->internal);
    }
    wince->internal = parsed;
    wince->number_of_records = parsed->count;
    wince->number_of_members = parsed->count;
    wince->image_start = parsed->image_start;
    wince->image_length = parsed->image_length;
    wince->archive_end = parsed->archive_end;
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

int64_t xx_wince_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_wince_get_number_of_archive_records(Abstractformat *self,
                                                xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return ((xx_wince *)self)->number_of_records;
}

xx_archive_record_state *xx_wince_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_wince_archive_stream *stream;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_wince_archive_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!xx_wince_copy_options(&state->options, options) ||
        !xx_wince_parse(self, &stream->parsed, pd)) {
        xx_wince_archive_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = xx_wince_archive_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count != 0U &&
        xx_wince_populate_record(&state->current_record,
                                 &stream->parsed.entries[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_wince_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_wince_archive_record_move_to_next(Abstractformat *self,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    xx_wince_archive_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_wince_archive_stream *)state->internal_state;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!xx_wince_populate_record(&state->current_record,
                                  &stream->parsed.entries[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_wince_unpack_current_archive_record(Abstractformat *self,
                                            xx_archive_record_state *state,
                                            xx_pd_struct *pd) {
    const xx_archive_record *record;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    /* The name is produced here from a hex address, so it is always a single
     * safe component; the guard only catches a record that lost its name. */
    if (!name || !name[0]) return false;
    option = xx_wince_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) {
        int64_t total = xx_io_total_size(self->device);
        return record->data_offset >= 0 && record->compressed_size >= 0 &&
               record->data_offset <= total &&
               record->compressed_size <= total - record->data_offset;
    }
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
    if (xx_store_create_dirs_a(destination, false)) {
        result = xx_store_unpack_device_to_file(self->device,
                                                record->data_offset,
                                                record->compressed_size,
                                                destination, pd);
    } else {
        result = false;
    }
    if (owned_base) xx_str_free(owned_base);
    xx_str_free(destination);
    return result;
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return false;
}

void xx_wince_free_archive_records_reading(Abstractformat *self,
                                           xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

uint64_t xx_wince_get_number_of_records(const xx_wince *wince) {
    return wince ? wince->number_of_records : 0U;
}
uint64_t xx_wince_get_number_of_members(const xx_wince *wince) {
    return wince ? wince->number_of_members : 0U;
}
uint32_t xx_wince_get_image_start(const xx_wince *wince) {
    return wince ? wince->image_start : 0U;
}
uint32_t xx_wince_get_image_length(const xx_wince *wince) {
    return wince ? wince->image_length : 0U;
}
int64_t xx_wince_get_archive_end(const xx_wince *wince) {
    return wince ? wince->archive_end : -1;
}
