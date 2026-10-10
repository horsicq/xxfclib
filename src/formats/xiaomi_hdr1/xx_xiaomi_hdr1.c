/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Layout from unblob's hdr1 handler (unblob/handlers/archive/xiaomi/hdr.py,
 * MIT); the code below is written from that structure description. */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/xiaomi_hdr1/xx_xiaomi_hdr1.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#ifdef XIAOMI_HDR1
#define XX_XIAOMI_HDR1_FILE_TYPE XX_FILE_TYPE_XIAOMI_HDR1
#else
#define XX_XIAOMI_HDR1_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define HDR1_HEADER_SIZE 48
#define HDR1_BLOB_HEADER_SIZE 48
#define HDR1_SIGNATURE_SIZE 272 /* u32 size + 12 padding + 0x100 content */
#define HDR1_CRC_START 12
#define HDR1_NAME_FIELD 32
#define HDR1_NAME_MAX 48

typedef struct hdr1_blob_s {
    int64_t header_offset;
    int64_t data_offset;
    int64_t size;
    uint32_t magic;
    uint32_t flash_offset;
    uint16_t type;
    bool name_safe;
    char name[HDR1_NAME_MAX];
} hdr1_blob;

typedef struct hdr1_parsed_s {
    int64_t archive_end;
    uint32_t signature_offset;
    uint32_t crc32;
    uint16_t device_id;
    size_t count;
    hdr1_blob blobs[XX_XIAOMI_HDR1_MAX_BLOBS];
} hdr1_parsed;

typedef struct hdr1_stream_s {
    hdr1_parsed parsed;
    size_t index;
} hdr1_stream;

static void hdr1_vtable_destroy(Abstractformat *self);

static bool hdr1_read_at(xx_io_device *device, int64_t offset, void *data, size_t size)
{
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    if (!device || !data || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static char hdr1_upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool hdr1_stem_is(const char *name, size_t stem, const char *word)
{
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || hdr1_upper(name[index]) != word[index]) return false;
    return word[stem] == 0;
}

/* The member is written as <base>/<name>: refuse separators, drive colons,
 * control and non-ASCII bytes, names made only of dots and spaces, and
 * Windows device names with or without an extension. */
static bool hdr1_safe_name(const char *name)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    size_t length, stem = 0U, index;
    bool meaningful = false;
    if (!name || !name[0]) return false;
    length = xx_str_len(name);
    for (index = 0U; index < length; ++index) {
        char c = name[index];
        if ((unsigned char)c < 0x20U || (unsigned char)c > 0x7EU || c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' ||
            c == '*')
            return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful) return false;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (hdr1_stem_is(name, stem, devices[index])) return false;
    if (stem == 4U && name[3] >= '0' && name[3] <= '9' &&
        ((hdr1_upper(name[0]) == 'C' && hdr1_upper(name[1]) == 'O' && hdr1_upper(name[2]) == 'M') ||
         (hdr1_upper(name[0]) == 'L' && hdr1_upper(name[1]) == 'P' && hdr1_upper(name[2]) == 'T')))
        return false;
    return true;
}

static bool hdr1_name_taken(const hdr1_parsed *parsed, size_t count, const char *name)
{
    size_t index;
    for (index = 0U; index < count; ++index) {
        const char *a = parsed->blobs[index].name;
        const char *b = name;
        while (*a && hdr1_upper(*a) == hdr1_upper(*b)) {
            ++a;
            ++b;
        }
        if (*a == 0 && *b == 0) return true;
    }
    return false;
}

static void hdr1_append_number(char *text, size_t capacity, unsigned value)
{
    char digits[12];
    size_t n = 0U, length = xx_str_len(text);
    do {
        digits[n++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value && n < sizeof(digits));
    while (n && length + 1U < capacity) text[length++] = digits[--n];
    text[length] = 0;
}

/* Builds the member name of blob @p index: the stored name up to its first
 * NUL, or "blob<N>.bin" when that is empty or unsafe; a name already used by
 * an earlier blob (case-insensitively) gets "_<N>" appended so no member
 * overwrites another. */
static void hdr1_make_name(hdr1_parsed *parsed, size_t index, const uint8_t *field)
{
    hdr1_blob *blob = &parsed->blobs[index];
    char base[HDR1_NAME_FIELD + 1];
    size_t length = 0U;
    unsigned attempt;
    while (length < HDR1_NAME_FIELD && field[length]) {
        base[length] = (char)field[length];
        ++length;
    }
    base[length] = 0;
    blob->name_safe = hdr1_safe_name(base);
    if (!blob->name_safe) {
        xx_rt_memcpy(base, "blob", 5U);
        hdr1_append_number(base, sizeof(base), (unsigned)index);
        length = xx_str_len(base);
        xx_rt_memcpy(base + length, ".bin", 5U);
    }
    xx_rt_memcpy(blob->name, base, xx_str_len(base) + 1U);
    for (attempt = 0U; attempt < 64U && hdr1_name_taken(parsed, index, blob->name); ++attempt) {
        xx_rt_memcpy(blob->name, base, xx_str_len(base) + 1U);
        length = xx_str_len(blob->name);
        blob->name[length] = '_';
        blob->name[length + 1U] = 0;
        hdr1_append_number(blob->name, sizeof(blob->name), (unsigned)(index + attempt * 8U));
    }
}

static bool hdr1_parse(Abstractformat *self, hdr1_parsed *parsed, xx_pd_struct *pd)
{
    uint8_t header[HDR1_HEADER_SIZE];
    int64_t total, available, end;
    size_t index;
    if (parsed) xx_rt_memset(parsed, 0, sizeof(*parsed));
    if (!self || !self->device || !parsed || self->base_address < 0 || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    available = total - self->base_address;
    if (available < HDR1_HEADER_SIZE + HDR1_SIGNATURE_SIZE || !hdr1_read_at(self->device, self->base_address, header, sizeof(header)) ||
        xx_rt_memcmp(header, "HDR1", 4U) != 0) {
        return false;
    }
    parsed->signature_offset = xx_data_get_u32(header, sizeof(header), 4U, false);
    parsed->crc32 = xx_data_get_u32(header, sizeof(header), 8U, false);
    parsed->device_id = xx_data_get_u16(header, sizeof(header), 14U, false);
    if (parsed->signature_offset < HDR1_HEADER_SIZE) return false;
    end = (int64_t)parsed->signature_offset + HDR1_SIGNATURE_SIZE;
    if (end > available) return false;
    if (xx_data_get_u32(header, sizeof(header), 16U, false) == 0U) return false;

    /* Cheap structural checks on every blob before the CRC pass. */
    for (index = 0U; index < XX_XIAOMI_HDR1_MAX_BLOBS; ++index) {
        uint8_t blob_header[HDR1_BLOB_HEADER_SIZE];
        uint32_t offset = xx_data_get_u32(header, sizeof(header), 16U + 4U * (uint32_t)index, false);
        hdr1_blob *blob = &parsed->blobs[index];
        if (offset == 0U) break;
        if (offset < HDR1_HEADER_SIZE || (uint64_t)offset + HDR1_BLOB_HEADER_SIZE > (uint64_t)parsed->signature_offset ||
            !hdr1_read_at(self->device, self->base_address + offset, blob_header, sizeof(blob_header))) {
            return false;
        }
        blob->magic = xx_data_get_u32(blob_header, sizeof(blob_header), 0U, false);
        blob->flash_offset = xx_data_get_u32(blob_header, sizeof(blob_header), 4U, false);
        blob->size = (int64_t)xx_data_get_u32(blob_header, sizeof(blob_header), 8U, false);
        blob->type = xx_data_get_u16(blob_header, sizeof(blob_header), 12U, false);
        blob->header_offset = self->base_address + offset;
        blob->data_offset = blob->header_offset + HDR1_BLOB_HEADER_SIZE;
        if (blob->size == 0 || (int64_t)offset + HDR1_BLOB_HEADER_SIZE + blob->size > (int64_t)parsed->signature_offset) {
            return false;
        }
        hdr1_make_name(parsed, index, blob_header + 16U);
        parsed->count = index + 1U;
    }
    if (parsed->count == 0U) return false;

    if (!xx_crc_verify_device(self->device, self->base_address + HDR1_CRC_START, end - HDR1_CRC_START, XX_CRC_TYPE_CRC32_JAMCRC, (uint64_t)parsed->crc32, pd)) {
        return false;
    }
    parsed->archive_end = self->base_address + end;
    return true;
}

static bool hdr1_copy_options(xx_list_s *destination, const xx_list_s *source)
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

static const xx_var *hdr1_find_option(const xx_list_s *options, uint32_t meta_id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool hdr1_populate_record(xx_archive_record *record, const hdr1_blob *blob)
{
    if (!record || !blob) return false;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = blob->header_offset;
    record->header_size = HDR1_BLOB_HEADER_SIZE;
    record->data_offset = blob->data_offset;
    record->compressed_size = blob->size;
    return xx_archive_record_set_original_name(record, blob->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)blob->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)blob->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void hdr1_stream_free(void *pointer)
{
    if (pointer) xx_mem_free(pointer);
}

void xx_xiaomi_hdr1_init(xx_xiaomi_hdr1 *hdr, xx_io_device *dev, int64_t base_address)
{
    if (!hdr) return;
    xx_rt_memset(hdr, 0, sizeof(*hdr));
    xx_format_init(&hdr->format, dev, base_address);
    hdr->format.endian = XX_ENDIAN_LITTLE;
    hdr->format.file_type = XX_XIAOMI_HDR1_FILE_TYPE;
    hdr->format.format_type = XX_TYPE_ARCHIVE;
    hdr->format.is_archive = true;
    xx_format_set_mime_type(&hdr->format, "application/x-xiaomi-hdr1");
    xx_format_set_extension(&hdr->format, "bin");
    hdr->format.check_is_valid = xx_xiaomi_hdr1_check_is_valid;
    hdr->format.handle_base_info = xx_xiaomi_hdr1_handle_base_info;
    hdr->format.get_format_size = xx_xiaomi_hdr1_get_format_size;
    hdr->format.get_number_of_archive_records = xx_xiaomi_hdr1_get_number_of_archive_records;
    hdr->format.create_archive_records_reading = xx_xiaomi_hdr1_create_archive_records_reading;
    hdr->format.get_current_archive_record = xx_xiaomi_hdr1_get_current_archive_record;
    hdr->format.unpack_current_archive_record = xx_xiaomi_hdr1_unpack_current_archive_record;
    hdr->format.archive_record_move_to_next = xx_xiaomi_hdr1_archive_record_move_to_next;
    hdr->format.free_archive_records_reading = xx_xiaomi_hdr1_free_archive_records_reading;
    hdr->format.destroy = hdr1_vtable_destroy;
    hdr->archive_end = -1;
}

xx_xiaomi_hdr1 *xx_xiaomi_hdr1_create(xx_io_device *dev, int64_t base_address)
{
    xx_xiaomi_hdr1 *hdr = (xx_xiaomi_hdr1 *)xx_mem_alloc(sizeof(*hdr));
    if (hdr) xx_xiaomi_hdr1_init(hdr, dev, base_address);
    return hdr;
}

void xx_xiaomi_hdr1_destroy(xx_xiaomi_hdr1 *hdr)
{
    if (!hdr) return;
    if (hdr->internal) {
        xx_mem_free(hdr->internal);
        hdr->internal = NULL;
    }
    xx_format_cleanup_extra_parameters(&hdr->format);
}

static void hdr1_vtable_destroy(Abstractformat *self)
{
    xx_xiaomi_hdr1_destroy((xx_xiaomi_hdr1 *)self);
}

void xx_xiaomi_hdr1_free(xx_xiaomi_hdr1 *hdr)
{
    if (!hdr) return;
    xx_xiaomi_hdr1_destroy(hdr);
    xx_mem_free(hdr);
}

bool xx_xiaomi_hdr1_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    hdr1_parsed *parsed;
    bool result;
    if (!self) return false;
    parsed = (hdr1_parsed *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed) return false;
    result = hdr1_parse(self, parsed, pd);
    xx_mem_free(parsed);
    return result;
}

bool xx_xiaomi_hdr1_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_xiaomi_hdr1 *hdr = (xx_xiaomi_hdr1 *)self;
    hdr1_parsed *parsed;
    int64_t total;
    if (!self) return false;
    parsed = (hdr1_parsed *)xx_mem_alloc(sizeof(*parsed));
    if (!parsed || !hdr1_parse(self, parsed, pd)) {
        if (parsed) xx_mem_free(parsed);
        self->is_valid = false;
        self->base_info_handled = false;
        return false;
    }
    if (hdr->internal) xx_mem_free(hdr->internal);
    hdr->internal = parsed;
    hdr->number_of_records = parsed->count;
    hdr->signature_offset = parsed->signature_offset;
    hdr->crc32 = parsed->crc32;
    hdr->device_id = parsed->device_id;
    hdr->archive_end = parsed->archive_end;
    self->format_size = parsed->archive_end - self->base_address;
    total = xx_io_total_size(self->device);
    if (total > parsed->archive_end) {
        self->overlay_offset = parsed->archive_end;
        self->overlay_size = total - parsed->archive_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = hdr->number_of_records;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_xiaomi_hdr1_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_xiaomi_hdr1_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) return 0U;
    return ((xx_xiaomi_hdr1 *)self)->number_of_records;
}

xx_archive_record_state *xx_xiaomi_hdr1_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_archive_record_state *state;
    hdr1_stream *stream;
    if (!self || !self->device || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (hdr1_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    if (!hdr1_copy_options(&state->options, options) || !hdr1_parse(self, &stream->parsed, pd)) {
        hdr1_stream_free(stream);
        xx_archive_record_state_free(state);
        return NULL;
    }
    stream->index = 0U;
    state->internal_state = stream;
    state->free_internal = hdr1_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (stream->parsed.count > 0U && hdr1_populate_record(&state->current_record, &stream->parsed.blobs[0])) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_xiaomi_hdr1_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_xiaomi_hdr1_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    hdr1_stream *stream;
    if (!self || !state || state->format != self || !state->has_record || !state->internal_state || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (hdr1_stream *)state->internal_state;
    ++stream->index;
    if (stream->index < stream->parsed.count && hdr1_populate_record(&state->current_record, &stream->parsed.blobs[stream->index])) {
        state->current_index = (int64_t)stream->index;
        return true;
    }
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_xiaomi_hdr1_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    const xx_archive_record *record;
    const xx_var *option;
    const char *name;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result = false;
    int64_t total;
    if (!self || !self->device || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    record = &state->current_record;
    name = xx_archive_record_get_original_name(record);
    /* Stored names were replaced by blob<N>.bin when unsafe; re-check the
     * final name anyway since it is what reaches the file system. */
    if (!name || !hdr1_safe_name(name)) return false;
    total = xx_io_total_size(self->device);
    if (record->data_offset < 0 || record->compressed_size < 0 || record->data_offset > total || record->compressed_size > total - record->data_offset) {
        return false;
    }
    option = hdr1_find_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) return true;
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (base) {
        size_t length = xx_str_len(base);
        if (length && base[length - 1U] != '/' && base[length - 1U] != '\\') {
            destination = xx_str_concat3(base, "/", name);
        } else {
            destination = xx_str_concat(base, name);
        }
    }
    /* xx_store_unpack_device_to_file deletes its own output on failure. */
    if (destination && xx_store_create_dirs_a(destination, false)) {
        result = xx_store_unpack_device_to_file(self->device, record->data_offset, record->compressed_size, destination, pd);
    }
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_xiaomi_hdr1_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}
