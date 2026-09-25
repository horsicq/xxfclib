/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * InstallShield skin (setup.skin).  xx_installshield_skin.h carries the
 * layout: the whole file is nibble-swapped and XORed with an 8-byte key, and
 * the plain text is a headerless run of {name NUL, decimal size NUL, data}.
 * The key and the record rules were read from the file structure of real
 * skins and confirmed against U3's handler for "IS ISN" (key table at VA
 * 0x7CB018, name characters 0x20..0x7F, decimal size, records to EOF).
 *
 * There is no signature, so the probe is the record chain itself: every
 * record needs a printable, NUL-terminated name, a NUL-terminated run of
 * 1..19 decimal digits and a size that fits, and the last record must end
 * exactly at the end of the device.  Each record costs one read of at most
 * 281 bytes, the walk stops after 4096 records, and nothing is allocated
 * until records are actually enumerated.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/installshield_skin/xx_installshield_skin.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as the reader is registered. */
#ifdef INSTALLSHIELD_SKIN
#define XX_INSTALLSHIELD_SKIN_FILE_TYPE XX_FILE_TYPE_INSTALLSHIELD_SKIN
#else
#define XX_INSTALLSHIELD_SKIN_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define ISS_NAME_MAX ((size_t)XX_INSTALLSHIELD_SKIN_NAME_MAX)
#define ISS_DIGITS_MAX ((size_t)XX_INSTALLSHIELD_SKIN_DIGITS_MAX)
#define ISS_MAX_RECORDS ((size_t)XX_INSTALLSHIELD_SKIN_MAX_RECORDS)
/* Longest record header: the name, its NUL, the digits and their NUL. */
#define ISS_WINDOW (ISS_NAME_MAX + 1U + ISS_DIGITS_MAX + 1U)
/* Smallest record: one name byte, NUL, one digit, NUL, no data. */
#define ISS_MIN_RECORD 4
#define ISS_COPY_BUFFER 0x10000U
/* A renamed duplicate gets "_<record number>" and, if that is taken too,
 * "_<record number>_<try>"; this is how many names are tried. */
#define ISS_RENAME_TRIES 8U
#define ISS_SUFFIX_MAX 24U
#define ISS_OUT_NAME_MAX (ISS_NAME_MAX + ISS_SUFFIX_MAX)

static const uint8_t iss_key[8] = {0xA2U, 0x85U, 0x59U, 0xBCU,
                                   0xA3U, 0x9FU, 0x3BU, 0xACU};

typedef struct iss_header_s {
    size_t header_size;
    int64_t data_size;
    char name[ISS_NAME_MAX + 1U];
} iss_header;

typedef struct iss_member_s {
    int64_t header_offset; /**< From the start of the skin. */
    int64_t data_offset;   /**< From the start of the skin. */
    int64_t size;
    bool extractable;
    char original[ISS_NAME_MAX + 1U];  /**< As stored, '\\' made '/'. */
    char name[ISS_OUT_NAME_MAX + 1U];  /**< Unique among the members. */
} iss_member;

typedef struct iss_stream_s {
    iss_member *members;
    size_t count;
    size_t index;
} iss_stream;

void xx_installshield_skin_transform(uint8_t *data, size_t size,
                                     uint64_t position, bool encode) {
    size_t index;
    if (!data) return;
    for (index = 0U; index < size; ++index) {
        uint8_t key = iss_key[(position + index) & 7U];
        uint8_t value = data[index];
        if (encode) {
            value = (uint8_t)(value ^ key);
            data[index] = (uint8_t)((value >> 4U) | (value << 4U));
        } else {
            value = (uint8_t)((value >> 4U) | (value << 4U));
            data[index] = (uint8_t)(value ^ key);
        }
    }
}

static bool iss_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* One record header at @p position (relative to the skin start) of a skin
 * that is @p size bytes long.  The data it announces must fit in the skin. */
static bool iss_read_header(xx_io_device *device, int64_t base,
                            int64_t position, int64_t size,
                            iss_header *out) {
    uint8_t window[ISS_WINDOW];
    int64_t available;
    size_t length, index, digits = 0U;
    uint64_t value = 0U;
    if (!out || position < 0 || position >= size) return false;
    available = size - position;
    if (available < ISS_MIN_RECORD) return false;
    length = available < (int64_t)ISS_WINDOW ? (size_t)available : ISS_WINDOW;
    if (!iss_read_at(device, base + position, window, length)) return false;
    xx_installshield_skin_transform(window, length, (uint64_t)position, false);
    for (index = 0U; index < length && window[index] != 0U; ++index) {
        if (index >= ISS_NAME_MAX || window[index] < 0x20U ||
            window[index] > 0x7FU)
            return false;
        out->name[index] = (char)window[index];
    }
    if (index == 0U || index >= length) return false;
    out->name[index] = 0;
    for (++index; index < length && window[index] != 0U; ++index) {
        if (window[index] < '0' || window[index] > '9' ||
            ++digits > ISS_DIGITS_MAX)
            return false;
        /* value never exceeds available, so this cannot overflow. */
        if (value > (uint64_t)available / 10U) return false;
        value = value * 10U + (uint64_t)(window[index] - '0');
        if (value > (uint64_t)available) return false;
    }
    if (digits == 0U || index >= length) return false;
    out->header_size = index + 1U;
    if (value > (uint64_t)(available - (int64_t)out->header_size))
        return false;
    out->data_size = (int64_t)value;
    return true;
}

/* Walk the whole chain.  With @p members, fills up to @p capacity of them.
 * Returns the number of records, 0 when the device is not a skin. */
static size_t iss_walk(Abstractformat *format, iss_member *members,
                       size_t capacity, int64_t *extent,
                       uint64_t *unpacked) {
    iss_header header;
    int64_t total, size, position = 0;
    uint64_t sum = 0U;
    size_t count = 0U;
    if (!format || !format->device || format->base_address < 0) return 0U;
    total = xx_io_total_size(format->device);
    if (total <= format->base_address) return 0U;
    size = total - format->base_address;
    if (size < ISS_MIN_RECORD) return 0U;
    while (position < size) {
        if (count >= ISS_MAX_RECORDS ||
            !iss_read_header(format->device, format->base_address, position,
                             size, &header))
            return 0U;
        if (members) {
            iss_member *member;
            size_t index;
            if (count >= capacity) return 0U;
            member = &members[count];
            member->header_offset = position;
            member->data_offset = position + (int64_t)header.header_size;
            member->size = header.data_size;
            member->extractable = true;
            for (index = 0U; header.name[index]; ++index)
                member->original[index] =
                    header.name[index] == '\\' ? '/' : header.name[index];
            member->original[index] = 0;
        }
        sum += (uint64_t)header.data_size;
        ++count;
        /* iss_read_header guaranteed the record fits in the skin. */
        position += (int64_t)header.header_size + header.data_size;
    }
    if (position != size) return 0U;
    if (extent) *extent = size;
    if (unpacked) *unpacked = sum;
    return count;
}

/* ---------------------------------------------------------------------- */
/* Names                                                                   */

static char iss_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool iss_same_name(const char *a, const char *b) {
    size_t index;
    for (index = 0U;; ++index) {
        if (iss_upper(a[index]) != iss_upper(b[index])) return false;
        if (!a[index]) return true;
    }
}

/* True when the first @p stem bytes of @p name spell the upper-case
 * @p word exactly, ignoring case. */
static bool iss_stem_is(const char *name, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || iss_upper(name[index]) != word[index])
            return false;
    return word[stem] == 0;
}

/* One path component of @p length bytes.  Refuses empty components, names
 * Windows would resolve to "." / ".." or silently trim (trailing dots and
 * spaces), reserved punctuation, control and non-ASCII bytes, and device
 * names such as CON, lpt1.txt or CONIN$ with or without an extension. */
static bool iss_safe_component(const char *name, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t stem = 0U, index;
    if (length == 0U || name[length - 1U] == '.' || name[length - 1U] == ' ')
        return false;
    for (index = 0U; index < length; ++index) {
        char c = name[index];
        if ((unsigned char)c < 0x20U || (unsigned char)c > 0x7EU ||
            c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' ||
            c == '"' || c == '|' || c == '?' || c == '*')
            return false;
    }
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (iss_stem_is(name, stem, devices[index])) return false;
    if (stem == 4U && name[3] >= '0' && name[3] <= '9' &&
        ((iss_upper(name[0]) == 'C' && iss_upper(name[1]) == 'O' &&
          iss_upper(name[2]) == 'M') ||
         (iss_upper(name[0]) == 'L' && iss_upper(name[1]) == 'P' &&
          iss_upper(name[2]) == 'T')))
        return false;
    return true;
}

/* A relative '/'-separated path whose every component is safe. */
static bool iss_safe_output_name(const char *name) {
    size_t start = 0U, index = 0U;
    if (!name || !name[0]) return false;
    for (;;) {
        if (name[index] == '/' || name[index] == 0) {
            if (!iss_safe_component(name + start, index - start))
                return false;
            if (name[index] == 0) return true;
            start = index + 1U;
        }
        ++index;
    }
}

static bool iss_taken(const iss_member *members, size_t count, size_t before,
                      const char *candidate) {
    size_t index;
    for (index = 0U; index < before; ++index)
        if (iss_same_name(members[index].name, candidate)) return true;
    for (index = 0U; index < count; ++index)
        if (iss_same_name(members[index].original, candidate)) return true;
    return false;
}

static size_t iss_put_number(char *out, uint64_t value) {
    char digits[24];
    size_t count = 0U, index;
    do {
        digits[count++] = (char)('0' + (int)(value % 10U));
        value /= 10U;
    } while (value && count < sizeof(digits));
    for (index = 0U; index < count; ++index)
        out[index] = digits[count - 1U - index];
    return count;
}

/* Build "<stem>_<number>[_<try>]<extension>" from @p original, where the
 * extension is the last '.' of the last path component (if any). */
static bool iss_make_candidate(const char *original, size_t number,
                               unsigned attempt, char *out) {
    size_t length = xx_str_len(original), dot = length, index, at;
    char suffix[ISS_SUFFIX_MAX + 24U];
    size_t suffix_length = 0U;
    for (index = length; index > 0U; --index) {
        if (original[index - 1U] == '/') break;
        if (original[index - 1U] == '.') {
            dot = index - 1U;
            break;
        }
    }
    /* A leading dot (".ini") is a name, not an extension. */
    if (dot < length && (dot == 0U || original[dot - 1U] == '/')) dot = length;
    suffix[suffix_length++] = '_';
    suffix_length += iss_put_number(suffix + suffix_length, (uint64_t)number);
    if (attempt > 0U) {
        suffix[suffix_length++] = '_';
        suffix_length +=
            iss_put_number(suffix + suffix_length, (uint64_t)attempt + 1U);
    }
    if (length + suffix_length > ISS_OUT_NAME_MAX) return false;
    xx_rt_memcpy(out, original, dot);
    at = dot;
    xx_rt_memcpy(out + at, suffix, suffix_length);
    at += suffix_length;
    xx_rt_memcpy(out + at, original + dot, length - dot);
    at += length - dot;
    out[at] = 0;
    return true;
}

/* Names compared ASCII case-insensitively are made unique: every later
 * duplicate gets "_<record number>" before its extension, a name the skin
 * itself carries is never the one given up for a made-up one, and a member
 * that still has no free name is listed but never extracted. */
static void iss_dedupe(iss_member *members, size_t count) {
    char candidate[ISS_OUT_NAME_MAX + 1U];
    size_t index;
    for (index = 0U; index < count; ++index) {
        iss_member *member = &members[index];
        unsigned attempt;
        bool found = false;
        size_t length = xx_str_len(member->original);
        xx_rt_memcpy(member->name, member->original, length + 1U);
        if (!iss_taken(members, 0U, index, member->original)) continue;
        for (attempt = 0U; attempt < ISS_RENAME_TRIES && !found; ++attempt) {
            if (!iss_make_candidate(member->original, index + 1U, attempt,
                                    candidate))
                break;
            if (!iss_taken(members, count, index, candidate)) {
                xx_rt_memcpy(member->name, candidate,
                             xx_str_len(candidate) + 1U);
                found = true;
            }
        }
        if (!found) member->extractable = false;
    }
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

static void iss_stream_free(void *opaque) {
    iss_stream *stream = (iss_stream *)opaque;
    if (!stream) return;
    if (stream->members) xx_mem_free(stream->members);
    xx_mem_free(stream);
}

static bool iss_copy_options(xx_list_s *destination,
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

static const xx_var *iss_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool iss_set_record(xx_archive_record *record,
                           const iss_member *member, int64_t base) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = base + member->header_offset;
    record->header_size = member->data_offset - member->header_offset;
    record->data_offset = base + member->data_offset;
    record->compressed_size = member->size;
    /* Obfuscated with a fixed key, not encrypted: nothing is compressed and
     * no password is involved, so the method is "none". */
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static bool iss_write_all(xx_io_device *destination, const uint8_t *data,
                          size_t size) {
    size_t done = 0U;
    while (done < size) {
        ssize_t amount = xx_io_write(destination, data + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Decode one member into @p destination (NULL only reads it through). */
static bool iss_copy_member(Abstractformat *format, const iss_member *member,
                            xx_io_device *destination, xx_pd_struct *pd) {
    uint8_t *buffer;
    int64_t done = 0;
    bool result = false;
    if (!format || !member || member->size < 0) return false;
    buffer = (uint8_t *)xx_mem_alloc(ISS_COPY_BUFFER);
    if (!buffer) return false;
    while (done < member->size) {
        int64_t left = member->size - done;
        size_t chunk = left < (int64_t)ISS_COPY_BUFFER ? (size_t)left
                                                        : ISS_COPY_BUFFER;
        int64_t position = member->data_offset + done;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (!iss_read_at(format->device, format->base_address + position,
                         buffer, chunk))
            goto done;
        xx_installshield_skin_transform(buffer, chunk, (uint64_t)position,
                                        false);
        if (destination && !iss_write_all(destination, buffer, chunk))
            goto done;
        done += (int64_t)chunk;
    }
    result = true;
done:
    xx_mem_free(buffer);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_installshield_skin_init(xx_installshield_skin *archive,
                                xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_INSTALLSHIELD_SKIN_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format,
                            "application/x-installshield-skin");
    xx_format_set_extension(&archive->format, "skin");
    archive->format.check_is_valid = xx_installshield_skin_check_is_valid;
    archive->format.handle_base_info = xx_installshield_skin_handle_base_info;
    archive->format.get_format_size = xx_installshield_skin_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_installshield_skin_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_installshield_skin_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_installshield_skin_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_installshield_skin_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_installshield_skin_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_installshield_skin_free_archive_records_reading;
}

xx_installshield_skin *xx_installshield_skin_create(xx_io_device *device,
                                                    int64_t base_address) {
    xx_installshield_skin *archive =
        (xx_installshield_skin *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_installshield_skin_init(archive, device, base_address);
    return archive;
}

void xx_installshield_skin_destroy(xx_installshield_skin *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_installshield_skin_free(xx_installshield_skin *archive) {
    if (!archive) return;
    xx_installshield_skin_destroy(archive);
    xx_mem_free(archive);
}

bool xx_installshield_skin_check_is_valid(Abstractformat *format,
                                          xx_pd_struct *pd) {
    (void)pd;
    return iss_walk(format, NULL, 0U, NULL, NULL) != 0U;
}

bool xx_installshield_skin_handle_base_info(Abstractformat *format,
                                            xx_pd_struct *pd) {
    xx_installshield_skin *archive;
    int64_t extent = 0;
    uint64_t unpacked = 0U;
    size_t count;
    (void)pd;
    if (!format) return false;
    count = iss_walk(format, NULL, 0U, &extent, &unpacked);
    if (count == 0U) return false;
    archive = (xx_installshield_skin *)format;
    archive->number_of_records = (uint64_t)count;
    archive->unpacked_size = unpacked;
    format->number_of_archive_records = (uint64_t)count;
    format->format_size = extent;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_installshield_skin_get_format_size(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_installshield_skin_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_installshield_skin_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_installshield_skin_handle_base_info(format, pd))
               ? ((xx_installshield_skin *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_installshield_skin_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    iss_stream *stream;
    xx_archive_record_state *state;
    size_t count;
    (void)pd;
    count = iss_walk(format, NULL, 0U, NULL, NULL);
    if (count == 0U) return NULL;
    stream = (iss_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->members = (iss_member *)xx_mem_calloc(count, sizeof(iss_member));
    if (!stream->members ||
        iss_walk(format, stream->members, count, NULL, NULL) != count) {
        iss_stream_free(stream);
        return NULL;
    }
    stream->count = count;
    iss_dedupe(stream->members, count);
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        iss_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = iss_stream_free;
    state->total_records = (uint64_t)count;
    if (!iss_copy_options(&state->options, options) ||
        !iss_set_record(&state->current_record, &stream->members[0],
                        format->base_address)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_installshield_skin_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_installshield_skin_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    iss_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (iss_stream *)state->internal_state) ||
        stream->index + 1U >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    if (!iss_set_record(&state->current_record,
                        &stream->members[stream->index],
                        format->base_address)) {
        state->has_record = false;
        return false;
    }
    state->has_record = true;
    return true;
}

bool xx_installshield_skin_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    iss_stream *stream;
    const iss_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (iss_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->members[stream->index];
    path_option = iss_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return iss_copy_member(format, member, NULL, pd);
    if (!member->extractable || !iss_safe_output_name(member->name))
        return false;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
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
        result = iss_copy_member(format, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_installshield_skin_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
