/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * MS-DOS 2.0-3.2 BACKUP: one backed-up file (or one fragment of it) behind a
 * 128-byte header.  xx_ms_dos_backup.h carries the field table.
 *
 * The header checks follow de_identify_dosbackup20() and the fragment rules
 * of dbk20_scan_one_input_file() in Deark's modules/dosbackup.c
 * (Copyright (C) 2025 Jason Summers, MIT license), rewritten here; this
 * reader adds a control-byte check on the path.
 */

#include "xxfclib/global/xx_global.h"
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ms_dos_backup/xx_ms_dos_backup.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

#ifdef MS_DOS_BACKUP
#define XX_MS_DOS_BACKUP_FILE_TYPE XX_FILE_TYPE_MS_DOS_BACKUP
#else
#define XX_MS_DOS_BACKUP_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define MDB_HEADER XX_MS_DOS_BACKUP_HEADER_SIZE
#define MDB_PATH_OFFSET 5U
#define MDB_LENGTH_OFFSET 83U
#define MDB_PATH_FIELD 78U
/* Path bytes (without NUL) plus ".NNNNN" plus NUL. */
#define MDB_RAW_MAX (MDB_PATH_FIELD + 8U)

typedef struct mdb_context_s {
    uint8_t raw[MDB_RAW_MAX]; /**< Relative path, '/'-separated, cp437. */
    char *name;               /**< UTF-8 form of raw (records only). */
    uint32_t sequence;
    bool last_fragment;
    int64_t data_offset;
    int64_t data_size;
    int64_t header_offset;
} mdb_context;

static bool mdb_read_at(xx_io_device *device, int64_t offset, uint8_t *buffer, size_t size)
{
    size_t done = 0U;
    if (!device || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static size_t mdb_decimal(uint8_t *out, uint32_t value)
{
    uint8_t digits[12];
    size_t count = 0U, index;
    do {
        digits[count++] = (uint8_t)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count < 3U) digits[count++] = (uint8_t)'0';
    for (index = 0U; index < count; ++index) out[index] = digits[count - 1U - index];
    return count;
}

/* Header checks.  Fills @p out when it is non-NULL. */
static bool mdb_parse(Abstractformat *format, mdb_context *out)
{
    uint8_t header[MDB_HEADER];
    int64_t total, size;
    uint32_t sequence, length, index, nul, used = 0U;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)MDB_HEADER || !mdb_read_at(format->device, format->base_address, header, sizeof(header))) return false;
    if (header[0] != 0x00U && header[0] != 0xFFU) return false;
    sequence = (uint32_t)header[1] | ((uint32_t)header[2] << 8U);
    if (sequence == 0U || sequence > 255U) return false;
    if (header[3] != 0U || header[4] != 0U) return false;
    if (header[MDB_PATH_OFFSET] != '\\' && header[MDB_PATH_OFFSET] != '/') return false;
    length = header[MDB_LENGTH_OFFSET]; /* counts the terminating NUL */
    if (length < 3U || length > MDB_PATH_FIELD) return false;
    nul = MDB_PATH_OFFSET + length - 1U;
    /* The path is printable up to its NUL, the last character is not a
     * space, and everything from the NUL to the end of the header (except
     * the length byte) is zero. */
    for (index = MDB_PATH_OFFSET; index < nul; ++index)
        if (header[index] < 0x20U || header[index] == 0x7FU) return false;
    if (header[nul - 1U] <= 0x20U) return false;
    for (index = nul; index < MDB_HEADER; ++index)
        if (index != MDB_LENGTH_OFFSET && header[index] != 0U) return false;
    if (!out) return true;

    xx_mem_zero(out, sizeof(*out));
    /* "\DIR\FILE.EXT" -> "DIR/FILE.EXT": leading separators dropped, the
     * rest mapped to '/'.  Safety of the result is judged at extraction. */
    index = MDB_PATH_OFFSET;
    while (index < nul && (header[index] == '\\' || header[index] == '/')) ++index;
    for (; index < nul; ++index) out->raw[used++] = header[index] == '\\' ? (uint8_t)'/' : header[index];
    out->sequence = sequence;
    out->last_fragment = header[0] == 0xFFU;
    if (!(out->last_fragment && sequence == 1U) && used != 0U) {
        out->raw[used++] = (uint8_t)'.';
        used += (uint32_t)mdb_decimal(out->raw + used, sequence);
    }
    out->raw[used] = 0U;
    out->header_offset = format->base_address;
    out->data_offset = format->base_address + MDB_HEADER;
    out->data_size = size - MDB_HEADER;
    return true;
}

/* Code page 437, 0x80..0xFF, as Unicode. */
static const uint16_t mdb_cp437_high[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5, 0x00C9, 0x00E6, 0x00C6,
    0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192, 0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1,
    0x00AA, 0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB, 0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, 0x2555,
    0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510, 0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566,
    0x2560, 0x2550, 0x256C, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590,
    0x2580, 0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229, 0x2261, 0x00B1,
    0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};

static char *mdb_to_utf8(const uint8_t *raw)
{
    size_t length = xx_str_len((const char *)raw), out = 0U, index;
    char *text;
    if (length >= MDB_RAW_MAX) return NULL;
    text = (char *)xx_mem_alloc(length * 3U + 1U);
    if (!text) return NULL;
    for (index = 0U; index < length; ++index) {
        uint32_t code = raw[index] < 0x80U ? (uint32_t)raw[index] : mdb_cp437_high[raw[index] - 0x80U];
        if (code < 0x80U) {
            text[out++] = (char)code;
        } else if (code < 0x800U) {
            text[out++] = (char)(0xC0U | (code >> 6));
            text[out++] = (char)(0x80U | (code & 0x3FU));
        } else {
            text[out++] = (char)(0xE0U | (code >> 12));
            text[out++] = (char)(0x80U | ((code >> 6) & 0x3FU));
            text[out++] = (char)(0x80U | (code & 0x3FU));
        }
    }
    text[out] = '\0';
    return text;
}

static uint8_t mdb_upper(uint8_t c)
{
    return (c >= 'a' && c <= 'z') ? (uint8_t)(c - 0x20U) : c;
}

static bool mdb_is_word(const uint8_t *text, size_t length, const char *word)
{
    size_t index;
    for (index = 0U; index < length; ++index)
        if (word[index] == '\0' || mdb_upper(text[index]) != (uint8_t)word[index]) return false;
    return word[length] == '\0';
}

/* CON, PRN, AUX, NUL, CONIN$, CONOUT$, CLOCK$, COM0-9, LPT0-9 (also with
 * the cp437 superscripts), with or without an extension. */
static bool mdb_is_device(const uint8_t *component, size_t length)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    size_t stem = 0U, index;
    while (stem < length && component[stem] != (uint8_t)'.') ++stem;
    while (stem > 0U && component[stem - 1U] == (uint8_t)' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (mdb_is_word(component, stem, devices[index])) return true;
    if (stem == 4U && ((component[3] >= (uint8_t)'0' && component[3] <= (uint8_t)'9') || component[3] == 0xFDU))
        return mdb_is_word(component, 3U, "COM") || mdb_is_word(component, 3U, "LPT");
    return false;
}

/* Refused: empty, absolute, empty/"."/".." components, components ending in
 * '.' or ' ', control bytes, drive/stream colons and the other characters
 * Windows reserves, device names. */
static bool mdb_name_safe(const uint8_t *raw)
{
    size_t start = 0U;
    if (!raw || raw[0] == 0U || raw[0] == (uint8_t)'/') return false;
    for (;;) {
        size_t end = start, index;
        while (raw[end] != 0U && raw[end] != (uint8_t)'/') ++end;
        if (end == start) return false;
        if (raw[end - 1U] == (uint8_t)'.' || raw[end - 1U] == (uint8_t)' ') return false;
        for (index = start; index < end; ++index) {
            uint8_t byte = raw[index];
            if (byte < 0x20U || byte == 0x7FU || byte == (uint8_t)':' || byte == (uint8_t)'<' || byte == (uint8_t)'>' || byte == (uint8_t)'"' || byte == (uint8_t)'|' ||
                byte == (uint8_t)'?' || byte == (uint8_t)'*' || byte == (uint8_t)'\\')
                return false;
        }
        if (mdb_is_device(raw + start, end - start)) return false;
        if (raw[end] == 0U) return true;
        start = end + 1U;
    }
}

static void mdb_context_free(void *opaque)
{
    mdb_context *context = (mdb_context *)opaque;
    if (!context) return;
    if (context->name) xx_mem_free(context->name);
    xx_mem_free(context);
}

static bool mdb_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    size_t index;
    if (!source) return true;
    for (index = 0U; index < source->count; ++index) {
        const xx_meta *original = (const xx_meta *)xx_list_at((const xx_list_t *)source, index);
        xx_meta copy;
        if (!original) continue;
        xx_meta_init(&copy, original->meta_id);
        if (!xx_var_copy(&copy.var, &original->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static bool mdb_set_record(xx_archive_record *record, const mdb_context *context)
{
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = context->header_offset;
    record->header_size = MDB_HEADER;
    record->data_offset = context->data_offset;
    record->compressed_size = context->data_size;
    return xx_archive_record_set_original_name(record, context->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)context->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)context->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_ms_dos_backup_init(xx_ms_dos_backup *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_MS_DOS_BACKUP_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ms-dos-backup");
    xx_format_set_extension(&archive->format, "bak");
    archive->format.check_is_valid = xx_ms_dos_backup_check_is_valid;
    archive->format.handle_base_info = xx_ms_dos_backup_handle_base_info;
    archive->format.get_format_size = xx_ms_dos_backup_get_format_size;
    archive->format.get_number_of_archive_records = xx_ms_dos_backup_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_ms_dos_backup_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_ms_dos_backup_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_ms_dos_backup_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_ms_dos_backup_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_ms_dos_backup_free_archive_records_reading;
    archive->data_size = -1;
}

xx_ms_dos_backup *xx_ms_dos_backup_create(xx_io_device *device, int64_t base_address)
{
    xx_ms_dos_backup *archive = (xx_ms_dos_backup *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_ms_dos_backup_init(archive, device, base_address);
    return archive;
}

void xx_ms_dos_backup_destroy(xx_ms_dos_backup *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_ms_dos_backup_free(xx_ms_dos_backup *archive)
{
    if (!archive) return;
    xx_ms_dos_backup_destroy(archive);
    xx_mem_free(archive);
}

bool xx_ms_dos_backup_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    (void)pd;
    return mdb_parse(format, NULL);
}

bool xx_ms_dos_backup_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    mdb_context context;
    xx_ms_dos_backup *archive;
    (void)pd;
    if (!format || !mdb_parse(format, &context)) return false;
    archive = (xx_ms_dos_backup *)format;
    archive->number_of_records = 1U;
    archive->sequence = context.sequence;
    archive->last_fragment = context.last_fragment;
    archive->data_size = context.data_size;
    format->number_of_archive_records = 1U;
    format->format_size = MDB_HEADER + context.data_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_ms_dos_backup_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_ms_dos_backup_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_ms_dos_backup_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_ms_dos_backup_handle_base_info(format, pd)) ? ((xx_ms_dos_backup *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_ms_dos_backup_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    mdb_context *context;
    xx_archive_record_state *state;
    (void)pd;
    context = (mdb_context *)xx_mem_calloc(1U, sizeof(*context));
    if (!context) return NULL;
    if (!mdb_parse(format, context) || !(context->name = mdb_to_utf8(context->raw))) {
        mdb_context_free(context);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        mdb_context_free(context);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = context;
    state->free_internal = mdb_context_free;
    state->total_records = 1;
    if (!mdb_copy_options(&state->options, options) || !mdb_set_record(&state->current_record, context)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_ms_dos_backup_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_ms_dos_backup_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    (void)format;
    (void)pd;
    if (state) state->has_record = false;
    return false;
}

bool xx_ms_dos_backup_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    mdb_context *context;
    mdb_context check;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    size_t base_length;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record || !(context = (mdb_context *)state->internal_state) || (pd && xx_pd_is_stopped(pd)))
        return false;
    /* The device must still hold what was listed. */
    if (!mdb_parse(format, &check) || check.data_offset != context->data_offset || check.data_size != context->data_size) return false;
    option = xx_format_resolve_extra_parameter(format, &state->options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (option && (uint64_t)context->data_size > xx_var_get_u64(option)) return false;
    option = xx_format_resolve_extra_parameter(format, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!option) return true; /* stored data, already bounds-checked */
    if (!mdb_name_safe(context->raw)) return false;
    if (option->type == XX_VAR_TYPE_STRING || option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING || option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto done;
    base_length = xx_str_len(base);
    path = (base_length != 0U && base[base_length - 1U] != '/' && base[base_length - 1U] != '\\') ? xx_str_concat3(base, "/", context->name)
                                                                                                  : xx_str_concat(base, context->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    /* Self-cleaning on failure: never remove path here. */
    result = xx_store_unpack_device_to_file(format->device, context->data_offset, context->data_size, path, pd);
done:
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_ms_dos_backup_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}
