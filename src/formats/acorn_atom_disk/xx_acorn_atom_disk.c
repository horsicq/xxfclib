/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Acorn Atom disk image: a raw 40-track, 10-sector, 256-byte-sector dump
 * (MAME's "atom" format) carrying an Atom DOS filesystem, whose catalogue is
 * the one Acorn DFS inherited.  The layout and every rule the parser
 * enforces are listed in xx_acorn_atom_disk.h.  The code is written from
 * the published catalogue layout; no reference code was copied.
 *
 * The parse is also the late detection probe: an exact-size test, then one
 * 512-byte catalogue read validated field by field, and only then any
 * allocation.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/acorn_atom_disk/xx_acorn_atom_disk.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Self-healing shim: the file compiles before the enum exists. */
#ifdef ACORN_ATOM_DISK
#define XX_ACORN_ATOM_DISK_FILE_TYPE XX_FILE_TYPE_ACORN_ATOM_DISK
#else
#define XX_ACORN_ATOM_DISK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define ATOM_SECTOR 256
#define ATOM_TRACKS 40
#define ATOM_SPT 10
#define ATOM_TOTAL_SECTORS (ATOM_TRACKS * ATOM_SPT)
#define ATOM_IMAGE_SIZE ((int64_t)ATOM_TOTAL_SECTORS * ATOM_SECTOR)
#define ATOM_CATALOGUE_BYTES (2 * ATOM_SECTOR)
#define ATOM_MAX_FILES 31U
#define ATOM_FIRST_DATA_SECTOR 2U
/* "Q." + "_" + 7 + "~31" + NUL = 2 + 1 + 7 + 3 + 1 = 14. */
#define ATOM_NAME_BUFFER 24

typedef struct atom_member_s {
    char name[ATOM_NAME_BUFFER];
    uint32_t index;        /**< Catalogue slot, 1..31. */
    uint32_t start_sector;
    uint32_t length;
    uint32_t load_address;
    uint32_t exec_address;
    bool locked;
} atom_member;

typedef struct atom_stream_s {
    atom_member items[ATOM_MAX_FILES];
    size_t count;
    size_t index;
    uint32_t disc_sectors;
    uint32_t cycle;
    char title[13];
} atom_stream;

static bool atom_read_at(xx_io_device *device, int64_t offset, void *buffer,
                         size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool atom_size_ok(Abstractformat *format) {
    int64_t total;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    return total >= format->base_address &&
           total - format->base_address == ATOM_IMAGE_SIZE;
}

/* 7 name characters: printable ASCII (no bit 7, no space), then only
 * spaces; the first one must not be a space. */
static bool atom_check_name(const uint8_t *name) {
    size_t index;
    bool padding = false;
    for (index = 0U; index < 7U; ++index) {
        uint8_t c = name[index];
        if (c == ' ') {
            if (index == 0U) return false;
            padding = true;
        } else if (padding || c < 0x21U || c > 0x7EU) {
            return false;
        }
    }
    return true;
}

static uint32_t atom_sectors_of(uint32_t length) {
    return (length + ATOM_SECTOR - 1U) / ATOM_SECTOR;
}

/* Validate the catalogue and fill the member table (names not yet built).
 * Nothing is allocated. */
static bool atom_probe(Abstractformat *format, uint8_t *catalogue,
                       atom_stream *stream) {
    const uint8_t *s0 = catalogue;
    const uint8_t *s1 = catalogue + ATOM_SECTOR;
    uint32_t files, sectors, index, other;
    if (!atom_size_ok(format) ||
        !atom_read_at(format->device, format->base_address, catalogue,
                      ATOM_CATALOGUE_BYTES))
        return false;
    if ((s1[5] & 7U) != 0U) return false;
    files = (uint32_t)s1[5] >> 3U;
    if (files == 0U || files > ATOM_MAX_FILES) return false;
    /* Boot option and the unused bits 2-3 / 6-7 must be clear. */
    if ((s1[6] & 0xFCU) != 0U) return false;
    sectors = ((uint32_t)(s1[6] & 3U) << 8U) | s1[7];
    if (sectors < ATOM_FIRST_DATA_SECTOR || sectors > ATOM_TOTAL_SECTORS)
        return false;

    xx_mem_zero(stream, sizeof(*stream));
    for (index = 1U; index <= files; ++index) {
        const uint8_t *name = s0 + index * 8U;
        const uint8_t *info = s1 + index * 8U;
        atom_member *member = &stream->items[index - 1U];
        uint8_t qualifier = (uint8_t)(name[7] & 0x7FU);
        uint8_t mixed = info[6];
        uint32_t start, length, used;
        if (!atom_check_name(name) || qualifier < 0x20U || qualifier > 0x7EU)
            return false;
        /* 16-bit Atom: no load / exec address b17..b16. */
        if ((mixed & 0xCCU) != 0U) return false;
        start = ((uint32_t)(mixed & 3U) << 8U) | info[7];
        length = ((uint32_t)((mixed >> 4U) & 3U) << 16U) |
                 ((uint32_t)info[5] << 8U) | info[4];
        used = atom_sectors_of(length);
        if (start > sectors || used > sectors - start) return false;
        if (used != 0U && start < ATOM_FIRST_DATA_SECTOR) return false;
        member->index = index;
        member->start_sector = start;
        member->length = length;
        member->load_address = (uint32_t)info[0] | ((uint32_t)info[1] << 8U);
        member->exec_address = (uint32_t)info[2] | ((uint32_t)info[3] << 8U);
        member->locked = (name[7] & 0x80U) != 0U;
    }
    /* No two files may share a sector (31 * 30 / 2 comparisons at most). */
    for (index = 0U; index < files; ++index) {
        const atom_member *a = &stream->items[index];
        uint32_t a_end = a->start_sector + atom_sectors_of(a->length);
        if (a_end == a->start_sector) continue;
        for (other = index + 1U; other < files; ++other) {
            const atom_member *b = &stream->items[other];
            uint32_t b_end = b->start_sector + atom_sectors_of(b->length);
            if (b_end == b->start_sector) continue;
            if (a->start_sector < b_end && b->start_sector < a_end)
                return false;
        }
    }
    stream->count = files;
    stream->disc_sectors = sectors;
    stream->cycle = s1[4];
    {
        size_t at = 0U, keep = 0U;
        for (index = 0U; index < 12U; ++index) {
            uint8_t c = index < 8U ? s0[index] : s1[index - 8U];
            c = (uint8_t)(c & 0x7FU);
            if (c == 0U) break;
            stream->title[at++] = (c < 0x20U || c > 0x7EU) ? '_' : (char)c;
            if (c != ' ') keep = at;
        }
        stream->title[keep] = 0;
    }
    return true;
}

static char atom_out_char(uint8_t c) {
    switch (c) {
    case '/': case '\\': case ':': case '*': case '?': case '"': case '<':
    case '>': case '|':
        return '_';
    default:
        return (c < 0x21U || c > 0x7EU) ? '_' : (char)c;
    }
}

static char atom_fold(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool atom_stem_is(const char *stem, size_t length, const char *word) {
    size_t index;
    for (index = 0U; index < length; ++index)
        if (word[index] == 0 || atom_fold(stem[index]) != word[index])
            return false;
    return word[length] == 0;
}

static bool atom_is_device_stem(const char *stem, size_t length) {
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL",
                                          "CONIN$", "CONOUT$", "CLOCK$"};
    size_t index;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (atom_stem_is(stem, length, devices[index])) return true;
    if (length == 4U && stem[3] >= '0' && stem[3] <= '9' &&
        (atom_stem_is(stem, 3U, "COM") || atom_stem_is(stem, 3U, "LPT")))
        return true;
    return false;
}

static size_t atom_stem_length(const char *text, size_t length) {
    size_t index;
    for (index = 0U; index < length; ++index)
        if (text[index] == '.') return index;
    return length;
}

/* "[Q.][_]NAME[~K]" into `out` (ATOM_NAME_BUFFER bytes). */
static void atom_make_name(const uint8_t *entry, bool suffix, uint32_t key,
                           char *out) {
    char body[7];
    size_t body_length = 0U, at = 0U, index;
    uint8_t qualifier = (uint8_t)(entry[7] & 0x7FU);
    for (index = 0U; index < 7U && entry[index] != ' '; ++index)
        body[body_length++] = atom_out_char(entry[index]);
    if (body_length == 0U) body[body_length++] = '_';
    /* Windows drops a trailing dot. */
    if (body[body_length - 1U] == '.') body[body_length - 1U] = '_';
    if (qualifier != ' ') {
        out[at++] = qualifier == '.' ? '_' : atom_out_char(qualifier);
        out[at++] = '.';
    } else if (atom_is_device_stem(body, atom_stem_length(body, body_length))) {
        out[at++] = '_';
    }
    for (index = 0U; index < body_length; ++index) out[at++] = body[index];
    if (suffix) {
        out[at++] = '~';
        if (key >= 10U) out[at++] = (char)('0' + (char)(key / 10U));
        out[at++] = (char)('0' + (char)(key % 10U));
    }
    out[at] = 0;
}

static bool atom_names_equal(const char *a, const char *b) {
    size_t index = 0U;
    for (;;) {
        if (atom_fold(a[index]) != atom_fold(b[index])) return false;
        if (a[index] == 0) return true;
        ++index;
    }
}

static void atom_stream_free(void *opaque) {
    if (opaque) xx_mem_free(opaque);
}

static bool atom_parse(Abstractformat *format, atom_stream **result) {
    uint8_t catalogue[ATOM_CATALOGUE_BYTES];
    atom_stream probe;
    atom_stream *stream;
    size_t index, earlier;
    if (!result || !atom_probe(format, catalogue, &probe)) return false;
    for (index = 0U; index < probe.count; ++index) {
        atom_member *member = &probe.items[index];
        const uint8_t *entry = catalogue + member->index * 8U;
        atom_make_name(entry, false, 0U, member->name);
        for (earlier = 0U; earlier < index; ++earlier) {
            if (atom_names_equal(probe.items[earlier].name, member->name)) {
                atom_make_name(entry, true, member->index, member->name);
                break;
            }
        }
    }
    stream = (atom_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return false;
    xx_rt_memcpy(stream, &probe, sizeof(*stream));
    *result = stream;
    return true;
}

static bool atom_write_all(xx_io_device *destination, const uint8_t *data,
                           size_t size) {
    size_t written = 0U;
    while (written < size) {
        ssize_t amount = xx_io_write(destination, data + written,
                                     size - written);
        if (amount <= 0 || (size_t)amount > size - written) return false;
        written += (size_t)amount;
    }
    return true;
}

/* Copy a file sector by sector into `destination`, or just read it through
 * when that is NULL. */
static bool atom_copy_member(Abstractformat *format, const atom_stream *stream,
                             const atom_member *member,
                             xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t sector[ATOM_SECTOR];
    uint32_t remaining = member->length, number = member->start_sector;
    if (atom_sectors_of(member->length) > stream->disc_sectors ||
        number > stream->disc_sectors - atom_sectors_of(member->length))
        return false;
    while (remaining > 0U) {
        size_t chunk = remaining > ATOM_SECTOR ? (size_t)ATOM_SECTOR
                                               : (size_t)remaining;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!atom_read_at(format->device,
                          format->base_address +
                              (int64_t)number * ATOM_SECTOR,
                          sector, chunk))
            return false;
        if (destination && !atom_write_all(destination, sector, chunk))
            return false;
        remaining -= (uint32_t)chunk;
        ++number;
    }
    return true;
}

/* Re-check a built name before touching the disk: one path component of
 * printable characters with no separators or reserved characters, not
 * ending in '.', and not a device stem. */
static bool atom_safe_output_name(const char *name) {
    size_t length, index;
    if (!name || !name[0]) return false;
    length = xx_str_len(name);
    if (length >= ATOM_NAME_BUFFER) return false;
    for (index = 0U; index < length; ++index) {
        char c = name[index];
        if ((unsigned char)c < 0x21U || (unsigned char)c > 0x7EU ||
            c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' ||
            c == '"' || c == '|' || c == '?' || c == '*')
            return false;
    }
    if (name[length - 1U] == '.') return false;
    if (name[0] == '.' && (length == 1U || name[1] == '.')) return false;
    return !atom_is_device_stem(name, atom_stem_length(name, length));
}

static bool atom_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *atom_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool atom_set_record(Abstractformat *format, xx_archive_record *record,
                            const atom_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address + (int64_t)member->index * 8;
    record->header_size = 8;
    record->data_offset = format->base_address +
                          (int64_t)member->start_sector * ATOM_SECTOR;
    record->compressed_size = member->length;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->length) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->length) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->locked ? 1U : 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_acorn_atom_disk_init(xx_acorn_atom_disk *archive, xx_io_device *device,
                             int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_ACORN_ATOM_DISK_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format,
                            "application/x-acorn-atom-disk-image");
    xx_format_set_extension(&archive->format, "40t");
    archive->format.check_is_valid = xx_acorn_atom_disk_check_is_valid;
    archive->format.handle_base_info = xx_acorn_atom_disk_handle_base_info;
    archive->format.get_format_size = xx_acorn_atom_disk_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_acorn_atom_disk_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_acorn_atom_disk_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_acorn_atom_disk_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_acorn_atom_disk_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_acorn_atom_disk_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_acorn_atom_disk_free_archive_records_reading;
    archive->image_size = -1;
}

xx_acorn_atom_disk *xx_acorn_atom_disk_create(xx_io_device *device,
                                              int64_t base_address) {
    xx_acorn_atom_disk *archive =
        (xx_acorn_atom_disk *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_acorn_atom_disk_init(archive, device, base_address);
    return archive;
}

void xx_acorn_atom_disk_destroy(xx_acorn_atom_disk *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_acorn_atom_disk_free(xx_acorn_atom_disk *archive) {
    if (!archive) return;
    xx_acorn_atom_disk_destroy(archive);
    xx_mem_free(archive);
}

bool xx_acorn_atom_disk_check_is_valid(Abstractformat *format,
                                       xx_pd_struct *pd) {
    uint8_t catalogue[ATOM_CATALOGUE_BYTES];
    atom_stream probe;
    (void)pd;
    return atom_probe(format, catalogue, &probe);
}

bool xx_acorn_atom_disk_handle_base_info(Abstractformat *format,
                                         xx_pd_struct *pd) {
    atom_stream *stream;
    xx_acorn_atom_disk *archive;
    (void)pd;
    if (!format || !atom_parse(format, &stream)) return false;
    archive = (xx_acorn_atom_disk *)format;
    archive->number_of_records = stream->count;
    archive->image_size = ATOM_IMAGE_SIZE;
    archive->disc_sectors = stream->disc_sectors;
    archive->cycle = stream->cycle;
    xx_rt_memcpy(archive->title, stream->title, sizeof(archive->title));
    format->number_of_archive_records = stream->count;
    format->format_size = ATOM_IMAGE_SIZE;
    format->is_valid = true;
    format->base_info_handled = true;
    atom_stream_free(stream);
    return true;
}

int64_t xx_acorn_atom_disk_get_format_size(Abstractformat *format,
                                           xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_acorn_atom_disk_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_acorn_atom_disk_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_acorn_atom_disk_handle_base_info(format, pd))
               ? ((xx_acorn_atom_disk *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_acorn_atom_disk_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    atom_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!atom_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        atom_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = atom_stream_free;
    state->total_records = stream->count;
    if (!atom_copy_options(&state->options, options) ||
        !atom_set_record(format, &state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_acorn_atom_disk_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_acorn_atom_disk_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    atom_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (atom_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = atom_set_record(format, &state->current_record,
                                        &stream->items[stream->index]);
    return state->has_record;
}

bool xx_acorn_atom_disk_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    atom_stream *stream;
    atom_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (atom_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    path_option = atom_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        /* No destination: read the file through, which verifies it. */
        return atom_copy_member(format, stream, member, NULL, pd);
    if (!atom_safe_output_name(member->name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW)
        base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING ||
             path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        created = true;
        result = atom_copy_member(format, stream, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_acorn_atom_disk_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
