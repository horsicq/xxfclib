/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Xiaomi HDR2 firmware container.  xx_xiaomi_hdr2.h carries the field table.
 * The layout follows unblob's hdr2 handler (unblob/handlers/archive/xiaomi/
 * hdr.py, MIT); the code below is written from that structure description. */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/xiaomi_hdr2/xx_xiaomi_hdr2.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here; this picks up the real file type once XIAOMI_HDR2 is registered. */
#ifdef XIAOMI_HDR2
#define XX_XIAOMI_HDR2_FILE_TYPE XX_FILE_TYPE_XIAOMI_HDR2
#else
#define XX_XIAOMI_HDR2_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define HDR2_HEADER_SIZE XX_XIAOMI_HDR2_HEADER_SIZE
#define HDR2_SIGNATURE_SIZE XX_XIAOMI_HDR2_SIGNATURE_SIZE
#define HDR2_BLOB_HEADER_SIZE 48
#define HDR2_CRC_START 12
#define HDR2_OFFSETS_AT 0x30U
#define HDR2_DEVICE_AT 0x10U
#define HDR2_REGION_AT 0x18U
#define HDR2_NAME_FIELD 32
#define HDR2_NAME_MAX 48

typedef struct hdr2_blob_s {
    int64_t header_offset;
    int64_t data_offset;
    int64_t size;
    uint32_t magic;
    uint32_t flash_offset;
    uint16_t type;
    char name[HDR2_NAME_MAX];
} hdr2_blob;

typedef struct hdr2_parsed_s {
    int64_t archive_end;
    uint32_t signature_offset;
    uint32_t crc32;
    char device_id[9];
    char region[9];
    size_t count;
    hdr2_blob blobs[XX_XIAOMI_HDR2_MAX_BLOBS];
} hdr2_parsed;

typedef struct hdr2_stream_s {
    hdr2_parsed parsed;
    size_t index;
} hdr2_stream;

static bool hdr2_read_at(xx_io_device *device, int64_t offset, void *data,
                         size_t size) {
    uint8_t *out = (uint8_t *)data;
    size_t done = 0U;
    if (!device || !data || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t got = xx_io_read(device, out + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static char hdr2_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool hdr2_stem_is(const char *name, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || hdr2_upper(name[index]) != word[index])
            return false;
    return word[stem] == 0;
}

/* The member is written as <base>/<name>: refuse separators, drive colons,
 * control and non-ASCII bytes, names made only of dots and spaces (".",
 * ".."), and Windows device names with or without an extension. */
static bool hdr2_safe_name(const char *name) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t length, stem = 0U, index;
    bool meaningful = false;
    if (!name || !name[0]) return false;
    length = xx_str_len(name);
    for (index = 0U; index < length; ++index) {
        char c = name[index];
        if ((unsigned char)c < 0x20U || (unsigned char)c > 0x7EU ||
            c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' ||
            c == '"' || c == '|' || c == '?' || c == '*')
            return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful) return false;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (hdr2_stem_is(name, stem, devices[index])) return false;
    if (stem == 4U && name[3] >= '0' && name[3] <= '9' &&
        ((hdr2_upper(name[0]) == 'C' && hdr2_upper(name[1]) == 'O' &&
          hdr2_upper(name[2]) == 'M') ||
         (hdr2_upper(name[0]) == 'L' && hdr2_upper(name[1]) == 'P' &&
          hdr2_upper(name[2]) == 'T')))
        return false;
    return true;
}

static bool hdr2_name_taken(const hdr2_parsed *parsed, size_t count,
                            const char *name) {
    size_t index;
    for (index = 0U; index < count; ++index) {
        const char *a = parsed->blobs[index].name;
        const char *b = name;
        while (*a && hdr2_upper(*a) == hdr2_upper(*b)) {
            ++a;
            ++b;
        }
        if (*a == 0 && *b == 0) return true;
    }
    return false;
}

static void hdr2_append_number(char *text, size_t capacity, unsigned value) {
    char digits[12];
    size_t n = 0U, length = xx_str_len(text);
    do {
        digits[n++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value && n < sizeof(digits));
    while (n && length + 1U < capacity) text[length++] = digits[--n];
    text[length] = 0;
}

/* Member name of blob @p index: the stored name up to its first NUL, or
 * "blob<N>.bin" when that is empty or unsafe.  A name an earlier blob already
 * uses (case-insensitively) gets "_<N>" appended, so no member overwrites
 * another.  At most eight blobs exist, so the loop always finds a free name. */
static void hdr2_make_name(hdr2_parsed *parsed, size_t index,
                           const uint8_t *field) {
    hdr2_blob *blob = &parsed->blobs[index];
    char base[HDR2_NAME_FIELD + 1];
    size_t length = 0U;
    unsigned attempt;
    while (length < HDR2_NAME_FIELD && field[length]) {
        base[length] = (char)field[length];
        ++length;
    }
    base[length] = 0;
    if (!hdr2_safe_name(base)) {
        xx_rt_memcpy(base, "blob", 5U);
        hdr2_append_number(base, sizeof(base), (unsigned)index);
        length = xx_str_len(base);
        xx_rt_memcpy(base + length, ".bin", 5U);
    }
    xx_rt_memcpy(blob->name, base, xx_str_len(base) + 1U);
    for (attempt = 0U;
         attempt < 64U && hdr2_name_taken(parsed, index, blob->name);
         ++attempt) {
        xx_rt_memcpy(blob->name, base, xx_str_len(base) + 1U);
        length = xx_str_len(blob->name);
        blob->name[length] = '_';
        blob->name[length + 1U] = 0;
        hdr2_append_number(blob->name, sizeof(blob->name),
                           (unsigned)(index + attempt * 8U));
    }
}

/* Printable prefix of an 8-byte NUL-padded string field. */
static void hdr2_copy_label(char *out, const uint8_t *field) {
    size_t index;
    for (index = 0U; index < 8U && field[index]; ++index) {
        if (field[index] < 0x20U || field[index] > 0x7EU) break;
        out[index] = (char)field[index];
    }
    out[index] = 0;
}

static bool hdr2_parse(Abstractformat *self, hdr2_parsed *parsed,
                       bool verify_crc, xx_pd_struct *pd) {
    uint8_t header[HDR2_HEADER_SIZE];
    int64_t total, available, end;
    size_t index;
    if (parsed) xx_rt_memset(parsed, 0, sizeof(*parsed));
    if (!self || !self->device || !parsed || self->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return false;
    available = total - self->base_address;
    if (available < HDR2_HEADER_SIZE + HDR2_SIGNATURE_SIZE ||
        !hdr2_read_at(self->device, self->base_address, header,
                      sizeof(header)) ||
        xx_rt_memcmp(header, "HDR2", 4U) != 0)
        return false;
    parsed->signature_offset =
        xx_data_get_u32(header, sizeof(header), 4U, false);
    parsed->crc32 = xx_data_get_u32(header, sizeof(header), 8U, false);
    if (parsed->signature_offset < HDR2_HEADER_SIZE) return false;
    end = (int64_t)parsed->signature_offset + HDR2_SIGNATURE_SIZE;
    if (end > available) return false;
    if (xx_data_get_u32(header, sizeof(header), HDR2_OFFSETS_AT, false) == 0U)
        return false;
    hdr2_copy_label(parsed->device_id, header + HDR2_DEVICE_AT);
    hdr2_copy_label(parsed->region, header + HDR2_REGION_AT);

    /* Cheap structural checks on every listed blob before the CRC pass. */
    for (index = 0U; index < XX_XIAOMI_HDR2_MAX_BLOBS; ++index) {
        uint8_t blob_header[HDR2_BLOB_HEADER_SIZE];
        uint32_t offset = xx_data_get_u32(
            header, sizeof(header), HDR2_OFFSETS_AT + 4U * (uint32_t)index,
            false);
        hdr2_blob *blob = &parsed->blobs[index];
        if (offset == 0U) break;
        if (offset < HDR2_HEADER_SIZE ||
            (uint64_t)offset + HDR2_BLOB_HEADER_SIZE >
                (uint64_t)parsed->signature_offset ||
            !hdr2_read_at(self->device, self->base_address + offset,
                          blob_header, sizeof(blob_header)))
            return false;
        blob->magic = xx_data_get_u32(blob_header, sizeof(blob_header), 0U,
                                      false);
        blob->flash_offset =
            xx_data_get_u32(blob_header, sizeof(blob_header), 4U, false);
        blob->size = (int64_t)xx_data_get_u32(blob_header,
                                              sizeof(blob_header), 8U, false);
        blob->type = xx_data_get_u16(blob_header, sizeof(blob_header), 12U,
                                     false);
        blob->header_offset = self->base_address + offset;
        blob->data_offset = blob->header_offset + HDR2_BLOB_HEADER_SIZE;
        if (blob->size == 0 ||
            (int64_t)offset + HDR2_BLOB_HEADER_SIZE + blob->size >
                (int64_t)parsed->signature_offset)
            return false;
        hdr2_make_name(parsed, index, blob_header + 16U);
        parsed->count = index + 1U;
    }
    if (parsed->count == 0U) return false;

    if (verify_crc &&
        !xx_crc_verify_device(self->device, self->base_address + HDR2_CRC_START,
                              end - HDR2_CRC_START, XX_CRC_TYPE_CRC32_JAMCRC,
                              (uint64_t)parsed->crc32, pd))
        return false;
    parsed->archive_end = self->base_address + end;
    return true;
}

static bool hdr2_copy_options(xx_list_s *destination,
                              const xx_list_s *source) {
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *hdr2_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool hdr2_set_record(xx_archive_record *record, const hdr2_blob *blob) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = blob->header_offset;
    record->header_size = HDR2_BLOB_HEADER_SIZE;
    record->data_offset = blob->data_offset;
    record->compressed_size = blob->size;
    return xx_archive_record_set_original_name(record, blob->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)blob->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)blob->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void hdr2_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

void xx_xiaomi_hdr2_init(xx_xiaomi_hdr2 *hdr, xx_io_device *device,
                         int64_t base_address) {
    if (!hdr) return;
    xx_mem_zero(hdr, sizeof(*hdr));
    xx_format_init(&hdr->format, device, base_address);
    hdr->format.endian = XX_ENDIAN_LITTLE;
    hdr->format.file_type = XX_XIAOMI_HDR2_FILE_TYPE;
    hdr->format.format_type = XX_TYPE_ARCHIVE;
    hdr->format.is_archive = true;
    xx_format_set_mime_type(&hdr->format, "application/octet-stream");
    xx_format_set_extension(&hdr->format, "bin");
    hdr->format.check_is_valid = xx_xiaomi_hdr2_check_is_valid;
    hdr->format.handle_base_info = xx_xiaomi_hdr2_handle_base_info;
    hdr->format.get_format_size = xx_xiaomi_hdr2_get_format_size;
    hdr->format.get_number_of_archive_records =
        xx_xiaomi_hdr2_get_number_of_archive_records;
    hdr->format.create_archive_records_reading =
        xx_xiaomi_hdr2_create_archive_records_reading;
    hdr->format.get_current_archive_record =
        xx_xiaomi_hdr2_get_current_archive_record;
    hdr->format.unpack_current_archive_record =
        xx_xiaomi_hdr2_unpack_current_archive_record;
    hdr->format.archive_record_move_to_next =
        xx_xiaomi_hdr2_archive_record_move_to_next;
    hdr->format.free_archive_records_reading =
        xx_xiaomi_hdr2_free_archive_records_reading;
    hdr->archive_end = -1;
}

xx_xiaomi_hdr2 *xx_xiaomi_hdr2_create(xx_io_device *device,
                                      int64_t base_address) {
    xx_xiaomi_hdr2 *hdr = (xx_xiaomi_hdr2 *)xx_mem_alloc(sizeof(*hdr));
    if (hdr) xx_xiaomi_hdr2_init(hdr, device, base_address);
    return hdr;
}

void xx_xiaomi_hdr2_destroy(xx_xiaomi_hdr2 *hdr) {
    if (hdr) xx_format_cleanup_extra_parameters(&hdr->format);
}

void xx_xiaomi_hdr2_free(xx_xiaomi_hdr2 *hdr) {
    if (!hdr) return;
    xx_xiaomi_hdr2_destroy(hdr);
    xx_mem_free(hdr);
}

bool xx_xiaomi_hdr2_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    hdr2_parsed parsed;
    return hdr2_parse(self, &parsed, true, pd);
}

bool xx_xiaomi_hdr2_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    hdr2_parsed parsed;
    xx_xiaomi_hdr2 *hdr;
    if (!self || !hdr2_parse(self, &parsed, true, pd)) return false;
    hdr = (xx_xiaomi_hdr2 *)self;
    hdr->number_of_records = (uint64_t)parsed.count;
    hdr->signature_offset = parsed.signature_offset;
    hdr->crc32 = parsed.crc32;
    xx_rt_memcpy(hdr->device_id, parsed.device_id, sizeof(hdr->device_id));
    xx_rt_memcpy(hdr->region, parsed.region, sizeof(hdr->region));
    hdr->archive_end = parsed.archive_end;
    self->number_of_archive_records = (uint64_t)parsed.count;
    self->format_size = parsed.archive_end - self->base_address;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_xiaomi_hdr2_get_format_size(Abstractformat *self,
                                       xx_pd_struct *pd) {
    return self && (self->base_info_handled ||
                    xx_xiaomi_hdr2_handle_base_info(self, pd))
               ? self->format_size : -1;
}

uint64_t xx_xiaomi_hdr2_get_number_of_archive_records(Abstractformat *self,
                                                      xx_pd_struct *pd) {
    return self && (self->base_info_handled ||
                    xx_xiaomi_hdr2_handle_base_info(self, pd))
               ? ((xx_xiaomi_hdr2 *)self)->number_of_records : 0U;
}

xx_archive_record_state *xx_xiaomi_hdr2_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    hdr2_stream *stream;
    xx_archive_record_state *state;
    stream = (hdr2_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    /* The CRC was checked by the probe / handle_base_info; listing only
     * re-reads the structure. */
    if (!hdr2_parse(self, &stream->parsed, !self || !self->base_info_handled,
                    pd)) {
        xx_mem_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = hdr2_stream_free;
    state->total_records = (int64_t)stream->parsed.count;
    if (!hdr2_copy_options(&state->options, options) ||
        !hdr2_set_record(&state->current_record, &stream->parsed.blobs[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_xiaomi_hdr2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_xiaomi_hdr2_archive_record_move_to_next(Abstractformat *self,
                                                xx_archive_record_state *state,
                                                xx_pd_struct *pd) {
    hdr2_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        !(stream = (hdr2_stream *)state->internal_state) ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    ++stream->index;
    if (stream->index >= stream->parsed.count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    if (!hdr2_set_record(&state->current_record,
                         &stream->parsed.blobs[stream->index])) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_xiaomi_hdr2_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    hdr2_stream *stream;
    const hdr2_blob *blob;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    bool result = false;
    int64_t total;
    if (!self || !self->device || !state || state->format != self ||
        !state->has_record ||
        !(stream = (hdr2_stream *)state->internal_state) ||
        stream->index >= stream->parsed.count || (pd && xx_pd_is_stopped(pd)))
        return false;
    blob = &stream->parsed.blobs[stream->index];
    total = xx_io_total_size(self->device);
    if (blob->data_offset < 0 || blob->size <= 0 || blob->data_offset > total ||
        blob->size > total - blob->data_offset)
        return false;
    option = hdr2_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) return true; /* The span was checked above. */
    if (!hdr2_safe_name(blob->name)) return false;
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
        base[xx_str_len(base) - 1U] != '\\')
        destination = xx_str_concat3(base, "/", blob->name);
    else
        destination = xx_str_concat(base, blob->name);
    if (!destination || !xx_store_create_dirs_a(destination, false))
        goto cleanup;
    /* The store helper removes its own output on failure. */
    result = xx_store_unpack_device_to_file(self->device, blob->data_offset,
                                            blob->size, destination, pd);
cleanup:
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

void xx_xiaomi_hdr2_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
