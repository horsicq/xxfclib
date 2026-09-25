/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * "F Install 2" installer disk data (DISK1, DISK2, ...): a signature, a
 * directory of 0x18-byte entries and the stored files behind it.
 * xx_finstall.h carries the field table.
 *
 * Written from the structure of the known disk; the acceptance rules of
 * U3's handler (first entry right behind the directory, entries in order)
 * are kept, the version digit is not pinned to '2'.  Names are DOS names in
 * code page 437, converted to UTF-8.  A backslash or slash inside a name is
 * taken as a directory separator.  Names that would escape the output
 * directory or alias something on Windows ("..", "C:X", "CON.TXT", a
 * trailing dot) are listed but refused on extraction, and later duplicates
 * (compared the way Windows compares names) get a "_<member number>" suffix
 * before the extension.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/finstall/xx_finstall.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder: xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested; this
 * picks up the real file type as soon as the format is registered. */
#ifdef FINSTALL
#define XX_FINSTALL_FILE_TYPE XX_FILE_TYPE_FINSTALL
#else
#define XX_FINSTALL_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* ---- layout ------------------------------------------------------------ */

#define FI_HEADER ((uint32_t)XX_FINSTALL_HEADER_SIZE)
#define FI_ENTRY ((uint32_t)XX_FINSTALL_ENTRY_SIZE)
#define FI_NAME_FIELD ((uint32_t)XX_FINSTALL_NAME_FIELD)
#define FI_VERSION_AT 0x0BU
#define FI_CAPACITY_AT 0x0CU
#define FI_COUNT_AT 0x10U
#define FI_NAME_AT 0x08U

/* Directory entries fetched per read. */
#define FI_BATCH 256U

/* A converted name: 16 code-page-437 bytes of up to 3 UTF-8 bytes each,
 * plus room for the duplicate suffixes of every rename round. */
#define FI_RENAME_ROUNDS 4U
#define FI_NAME_MAX (FI_NAME_FIELD * 3U + FI_RENAME_ROUNDS * 7U + 1U)

static const uint8_t fi_signature[11] = {0x01U, 'F', ' ', 'I', 'n', 's',
                                         't',   'a', 'l', 'l', ' '};

/* Code page 437, 0x80..0xFF. */
static const uint16_t fi_cp437[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
    0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
    0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
    0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
    0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
    0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
    0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
    0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
    0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
    0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
    0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0};

typedef struct fi_layout {
    int64_t available;     /**< Device bytes from the base on. */
    int64_t directory_end; /**< From the base. */
    int64_t data_end;      /**< From the base; may exceed available. */
    uint32_t count;
    uint32_t capacity;
    char version;
    bool truncated;
} fi_layout;

typedef struct fi_member {
    int64_t offset; /**< From the base. */
    uint32_t size;
    bool unsafe;    /**< Listed, but refused on extraction. */
    bool pending;   /**< Scratch flag of fi_resolve_duplicates. */
    char name[FI_NAME_MAX];
} fi_member;

typedef struct fi_stream {
    fi_member *members;
    fi_layout layout;
    uint32_t count;
    uint32_t index;
} fi_stream;

static uint32_t fi_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static uint32_t fi_le16(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static bool fi_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* ---- names ------------------------------------------------------------- */

static size_t fi_put_utf8(char *out, uint32_t code) {
    if (code < 0x80U) {
        out[0] = (char)code;
        return 1U;
    }
    if (code < 0x800U) {
        out[0] = (char)(0xC0U | (code >> 6U));
        out[1] = (char)(0x80U | (code & 0x3FU));
        return 2U;
    }
    out[0] = (char)(0xE0U | (code >> 12U));
    out[1] = (char)(0x80U | ((code >> 6U) & 0x3FU));
    out[2] = (char)(0x80U | (code & 0x3FU));
    return 3U;
}

/* Next code point of a UTF-8 string this file built (1..3 byte forms
 * only), folded the way Windows folds file names in these ranges. */
static uint32_t fi_fold_next(const char **cursor) {
    const uint8_t *p = (const uint8_t *)*cursor;
    uint32_t code;
    if (p[0] < 0x80U) {
        code = p[0];
        *cursor += p[0] ? 1 : 0;
    } else if ((p[0] & 0xE0U) == 0xC0U && p[1]) {
        code = ((uint32_t)(p[0] & 0x1FU) << 6U) | (p[1] & 0x3FU);
        *cursor += 2;
    } else if ((p[0] & 0xF0U) == 0xE0U && p[1] && p[2]) {
        code = ((uint32_t)(p[0] & 0x0FU) << 12U) |
               ((uint32_t)(p[1] & 0x3FU) << 6U) | (p[2] & 0x3FU);
        *cursor += 3;
    } else {
        code = p[0];
        *cursor += 1;
    }
    if (code >= 'a' && code <= 'z') return code - 0x20U;
    if (code >= 0xE0U && code <= 0xFEU && code != 0xF7U) return code - 0x20U;
    if (code == 0xFFU) return 0x178U;
    if (code >= 0x3B1U && code <= 0x3C9U && code != 0x3C2U)
        return code - 0x20U;
    return code;
}

static int fi_compare_folded(const char *a, const char *b) {
    for (;;) {
        uint32_t x = fi_fold_next(&a), y = fi_fold_next(&b);
        if (x != y) return x < y ? -1 : 1;
        if (x == 0U) return 0;
    }
}

static char fi_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* One path component (UTF-8, @p length bytes) that is safe to create on
 * Windows and POSIX: not empty, not only dots and spaces (".", ".."), no
 * trailing dot or space (Windows drops them and would alias another name),
 * none of the reserved punctuation (':' also rules out drive letters), and
 * no device name such as CON, NUL.TXT, COM1, LPT9 or CONIN$. */
static bool fi_component_safe(const char *component, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t index, stem = 0U, device;
    bool meaningful = false;
    if (length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        char c = component[index];
        if ((unsigned char)c < 0x20U || c == 0x7F || c == ':' || c == '<' ||
            c == '>' || c == '"' || c == '|' || c == '?' || c == '*' ||
            c == '/' || c == '\\')
            return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful || component[length - 1U] == '.' ||
        component[length - 1U] == ' ')
        return false;
    while (stem < length && component[stem] != '.') ++stem;
    while (stem > 0U && component[stem - 1U] == ' ') --stem;
    for (device = 0U; device < sizeof(devices) / sizeof(devices[0]);
         ++device) {
        const char *word = devices[device];
        for (index = 0U; index < stem; ++index)
            if (!word[index] || fi_upper(component[index]) != word[index])
                break;
        if (index == stem && word[stem] == 0) return false;
    }
    if (stem >= 4U &&
        ((fi_upper(component[0]) == 'C' && fi_upper(component[1]) == 'O' &&
          fi_upper(component[2]) == 'M') ||
         (fi_upper(component[0]) == 'L' && fi_upper(component[1]) == 'P' &&
          fi_upper(component[2]) == 'T'))) {
        /* COM1..COM9 / LPT0..LPT9, and the superscript-digit forms that
         * Windows also reserves (U+00B9, U+00B2, U+00B3). */
        if (stem == 4U && component[3] >= '0' && component[3] <= '9')
            return false;
        if (stem == 5U && (uint8_t)component[3] == 0xC2U &&
            ((uint8_t)component[4] == 0xB9U ||
             (uint8_t)component[4] == 0xB2U ||
             (uint8_t)component[4] == 0xB3U))
            return false;
    }
    return true;
}

/* The name field was already checked by fi_name_valid.  Converts it to
 * UTF-8 with '/' separators and tells whether extraction may use it. */
static void fi_convert_name(const uint8_t *field, char *out, bool *unsafe) {
    size_t position = 0U, component = 0U, index;
    *unsafe = false;
    for (index = 0U; index < FI_NAME_FIELD && field[index] != 0U; ++index) {
        uint8_t byte = field[index];
        if (byte == '\\' || byte == '/') {
            if (!fi_component_safe(out + component, position - component))
                *unsafe = true;
            out[position++] = '/';
            component = position;
            continue;
        }
        position += fi_put_utf8(out + position,
                                byte < 0x80U ? byte : fi_cp437[byte - 0x80U]);
    }
    if (!fi_component_safe(out + component, position - component))
        *unsafe = true;
    out[position] = 0;
}

/* A name has at least one byte, is NUL terminated inside the field (or
 * fills it), and holds no control bytes.  Anything else is not a directory
 * entry of this format. */
static bool fi_name_valid(const uint8_t *field) {
    uint32_t index;
    if (field[0] == 0U) return false;
    for (index = 0U; index < FI_NAME_FIELD && field[index] != 0U; ++index)
        if (field[index] < 0x20U || field[index] == 0x7FU) return false;
    return true;
}

typedef struct fi_key {
    const char *name;
    uint32_t index;
} fi_key;

static int fi_compare_keys(const void *left, const void *right) {
    const fi_key *a = (const fi_key *)left, *b = (const fi_key *)right;
    int order = fi_compare_folded(a->name, b->name);
    if (order != 0) return order;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

/* "<stem>_<number><extension>" in place; the suffix goes in front of the
 * last component's extension.  False when it does not fit. */
static bool fi_suffix(char *name, uint32_t number) {
    char suffix[16];
    size_t length = xx_str_len(name), cut = length, suffix_length, at;
    for (at = length; at > 0U; --at) {
        if (name[at - 1U] == '/') break;
        if (name[at - 1U] == '.' && at - 1U > 0U && name[at - 2U] != '/') {
            cut = at - 1U;
            break;
        }
    }
    if (xx_rt_snprintf(suffix, sizeof(suffix), "_%u", (unsigned)number) <= 0)
        return false;
    suffix_length = xx_str_len(suffix);
    if (length + suffix_length + 1U > FI_NAME_MAX) return false;
    for (at = length + 1U; at > cut; --at)
        name[at - 1U + suffix_length] = name[at - 1U];
    xx_rt_memcpy(name + cut, suffix, suffix_length);
    return true;
}

/* No two members may be written to the same file.  The first of a group of
 * equal names keeps it; the others get "_<member number>".  A renamed name
 * can meet another one, so this repeats; whatever still collides after the
 * last round is refused for extraction. */
static bool fi_resolve_duplicates(fi_member *members, uint32_t count) {
    fi_key *keys;
    uint32_t round, index;
    if (count < 2U) return true;
    keys = (fi_key *)xx_mem_alloc((size_t)count * sizeof(*keys));
    if (!keys) return false;
    for (round = 0U; round <= FI_RENAME_ROUNDS; ++round) {
        bool renamed = false;
        for (index = 0U; index < count; ++index) {
            keys[index].name = members[index].name;
            keys[index].index = index;
            members[index].pending = false;
        }
        xx_rt_qsort(keys, (size_t)count, sizeof(*keys), fi_compare_keys);
        for (index = 1U; index < count; ++index)
            if (fi_compare_folded(keys[index].name, keys[index - 1U].name) ==
                0)
                members[keys[index].index].pending = true;
        for (index = 0U; index < count; ++index) {
            fi_member *member = &members[index];
            if (!member->pending) continue;
            if (round == FI_RENAME_ROUNDS ||
                !fi_suffix(member->name, index + 1U)) {
                member->unsafe = true;
                continue;
            }
            renamed = true;
        }
        if (!renamed) break;
    }
    xx_mem_free(keys);
    return true;
}

/* ---- walk -------------------------------------------------------------- */

/* Checks the header and every directory entry; fills @p members (count
 * entries) when it is not NULL.  Reads at most the header and the
 * directory, which must lie inside the device.  Data that runs past the
 * device end only marks the layout truncated. */
static bool fi_scan(Abstractformat *format, fi_layout *layout,
                    fi_member *members) {
    uint8_t header[XX_FINSTALL_HEADER_SIZE];
    uint8_t batch[FI_BATCH * XX_FINSTALL_ENTRY_SIZE];
    fi_layout result;
    int64_t total, previous_end;
    uint32_t index = 0U;
    if (!format || !format->device || !layout || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    xx_mem_zero(&result, sizeof(result));
    result.available = total - format->base_address;
    if (result.available < (int64_t)(FI_HEADER + FI_ENTRY) ||
        !fi_read_at(format->device, format->base_address, header,
                    sizeof(header)) ||
        xx_rt_memcmp(header, fi_signature, sizeof(fi_signature)) != 0 ||
        header[FI_VERSION_AT] < '0' || header[FI_VERSION_AT] > '9')
        return false;
    result.version = (char)header[FI_VERSION_AT];
    result.capacity = fi_le32(header + FI_CAPACITY_AT);
    result.count = fi_le16(header + FI_COUNT_AT);
    if (result.count == 0U) return false;
    result.directory_end =
        (int64_t)FI_HEADER + (int64_t)result.count * (int64_t)FI_ENTRY;
    if (result.directory_end > result.available) return false;
    previous_end = result.directory_end;
    while (index < result.count) {
        uint32_t chunk = result.count - index, slot;
        if (chunk > FI_BATCH) chunk = FI_BATCH;
        if (!fi_read_at(format->device,
                        format->base_address + (int64_t)FI_HEADER +
                            (int64_t)index * (int64_t)FI_ENTRY,
                        batch, (size_t)chunk * FI_ENTRY))
            return false;
        for (slot = 0U; slot < chunk; ++slot, ++index) {
            const uint8_t *entry = batch + (size_t)slot * FI_ENTRY;
            int64_t offset = (int64_t)fi_le32(entry);
            uint32_t size = fi_le32(entry + 4U);
            /* The first entry's data follows the directory; later entries
             * start at or after the end of the previous one. */
            if (index == 0U ? offset != result.directory_end
                            : offset < previous_end)
                return false;
            if (!fi_name_valid(entry + FI_NAME_AT)) return false;
            previous_end = offset + (int64_t)size;
            if (previous_end > result.available) result.truncated = true;
            if (members) {
                fi_member *member = &members[index];
                member->offset = offset;
                member->size = size;
                member->pending = false;
                fi_convert_name(entry + FI_NAME_AT, member->name,
                                &member->unsafe);
            }
        }
    }
    result.data_end = previous_end;
    *layout = result;
    return true;
}

static void fi_stream_free(void *opaque) {
    fi_stream *stream = (fi_stream *)opaque;
    if (!stream) return;
    if (stream->members) xx_mem_free(stream->members);
    xx_mem_free(stream);
}

/* One header pass for the count, then the directory walk that names every
 * member, then the duplicate pass.  The member table is bounded by the
 * directory, which fi_scan has already found inside the device. */
static fi_stream *fi_stream_load(Abstractformat *format) {
    fi_layout layout;
    fi_stream *stream;
    if (!fi_scan(format, &layout, NULL)) return NULL;
    stream = (fi_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    stream->members =
        (fi_member *)xx_mem_calloc((size_t)layout.count, sizeof(fi_member));
    if (!stream->members || !fi_scan(format, &stream->layout,
                                     stream->members) ||
        stream->layout.count != layout.count ||
        !fi_resolve_duplicates(stream->members, stream->layout.count)) {
        fi_stream_free(stream);
        return NULL;
    }
    stream->count = stream->layout.count;
    return stream;
}

/* ---- records ----------------------------------------------------------- */

static bool fi_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static bool fi_set_record(xx_archive_record *record, Abstractformat *format,
                          const fi_member *member, uint32_t index) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address + (int64_t)FI_HEADER +
                            (int64_t)index * (int64_t)FI_ENTRY;
    record->header_size = FI_ENTRY;
    record->data_offset = format->base_address + member->offset;
    record->compressed_size = (int64_t)member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static const fi_member *fi_current(Abstractformat *format,
                                   xx_archive_record_state *state,
                                   const fi_stream **stream_out) {
    fi_stream *stream;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (fi_stream *)state->internal_state) ||
        stream->index >= stream->count)
        return NULL;
    if (stream_out) *stream_out = stream;
    return &stream->members[stream->index];
}

/* ---- public API -------------------------------------------------------- */

void xx_finstall_init(xx_finstall *archive, xx_io_device *device,
                      int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FINSTALL_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-finstall");
    xx_format_set_extension(&archive->format, "");
    archive->format.check_is_valid = xx_finstall_check_is_valid;
    archive->format.handle_base_info = xx_finstall_handle_base_info;
    archive->format.get_format_size = xx_finstall_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_finstall_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_finstall_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_finstall_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_finstall_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_finstall_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_finstall_free_archive_records_reading;
    archive->data_end = -1;
}

xx_finstall *xx_finstall_create(xx_io_device *device, int64_t base_address) {
    xx_finstall *archive = (xx_finstall *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_finstall_init(archive, device, base_address);
    return archive;
}

void xx_finstall_destroy(xx_finstall *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_finstall_free(xx_finstall *archive) {
    if (!archive) return;
    xx_finstall_destroy(archive);
    xx_mem_free(archive);
}

bool xx_finstall_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    fi_layout layout;
    (void)pd;
    return fi_scan(format, &layout, NULL);
}

bool xx_finstall_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    fi_layout layout;
    xx_finstall *archive;
    char version[2];
    int64_t end;
    (void)pd;
    if (!format || !fi_scan(format, &layout, NULL)) return false;
    archive = (xx_finstall *)format;
    archive->number_of_records = layout.count;
    archive->capacity = layout.capacity;
    archive->version = layout.version;
    archive->data_end = layout.data_end;
    archive->truncated = layout.truncated;
    version[0] = layout.version;
    version[1] = 0;
    xx_format_set_version(format, version);
    end = layout.truncated ? layout.available : layout.data_end;
    format->number_of_archive_records = layout.count;
    format->format_size = end;
    if (end < layout.available) {
        format->overlay_offset = format->base_address + end;
        format->overlay_size = layout.available - end;
    } else {
        format->overlay_offset = -1;
        format->overlay_size = 0;
    }
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_finstall_get_format_size(Abstractformat *format,
                                    xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_finstall_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_finstall_get_number_of_archive_records(Abstractformat *format,
                                                   xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_finstall_handle_base_info(format, pd))
               ? ((xx_finstall *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_finstall_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    fi_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!format) return NULL;
    stream = fi_stream_load(format);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        fi_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = fi_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!fi_copy_options(&state->options, options) ||
        !fi_set_record(&state->current_record, format, &stream->members[0],
                       0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_finstall_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_finstall_archive_record_move_to_next(Abstractformat *format,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    fi_stream *stream;
    (void)pd;
    if (!fi_current(format, state, NULL)) {
        if (state) state->has_record = false;
        return false;
    }
    stream = (fi_stream *)state->internal_state;
    if (++stream->index >= stream->count ||
        !fi_set_record(&state->current_record, format,
                       &stream->members[stream->index], stream->index)) {
        state->has_record = false;
        return false;
    }
    ++state->current_index;
    return true;
}

bool xx_finstall_unpack_current_archive_record(Abstractformat *format,
                                               xx_archive_record_state *state,
                                               xx_pd_struct *pd) {
    const fi_stream *stream = NULL;
    const fi_member *member = fi_current(format, state, &stream);
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool present;
    if (!member || (pd && xx_pd_is_stopped(pd))) return false;
    /* The data must be on the device; nothing is created otherwise. */
    present = member->offset >= 0 &&
              member->offset <= stream->layout.available &&
              (int64_t)member->size <=
                  stream->layout.available - member->offset;
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (option && (uint64_t)member->size > xx_var_get_u64(option))
        return false;
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_UNPACK_PATH);
    if (!option) return present;
    if (member->unsafe || !present) return false;
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    /* The store helper opens the output itself and deletes it on failure,
     * so nothing is removed here. */
    result = xx_store_unpack_device_to_file(
        format->device, format->base_address + member->offset,
        (int64_t)member->size, path, pd);
done:
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_finstall_free_archive_records_reading(Abstractformat *format,
                                              xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
