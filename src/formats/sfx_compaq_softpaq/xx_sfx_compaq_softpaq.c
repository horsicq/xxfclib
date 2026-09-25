/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Compaq SoftPaq v1 self-extractors.  xx_sfx_compaq_softpaq.h carries the
 * layout.  The code is written from the file structure; the stub signatures
 * and the way the closing directory record is searched for were taken from
 * observing the behaviour of the U3 extractor on the corpus files.
 *
 * The DOS extractor stub is only compared against fixed header bytes; it is
 * never decompressed, executed or emulated.  The payload is a flat table of
 * stored members, so extraction is a bounded copy.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sfx_compaq_softpaq/xx_sfx_compaq_softpaq.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

/* xxfc_defs.h is shared and is not edited from here, so the file-type
 * constant is resolved through the alias macro that the enumerator defines. */
#ifdef SFX_COMPAQ_SOFTPAQ
#define XX_SFX_COMPAQ_SOFTPAQ_FILE_TYPE XX_FILE_TYPE_SFX_COMPAQ_SOFTPAQ
#else
#define XX_SFX_COMPAQ_SOFTPAQ_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* --- constants ------------------------------------------------------------ */

#define SPQ1_HEADER_SIZE 0xD8U /* covers the build-6 name at 0xD0..0xD7 */
#define SPQ1_NAME_FIELD 12U
#define SPQ1_SHORT XX_SFX_COMPAQ_SOFTPAQ_RECORD_SHORT
#define SPQ1_LONG XX_SFX_COMPAQ_SOFTPAQ_RECORD_LONG
#define SPQ1_TAIL XX_SFX_COMPAQ_SOFTPAQ_TAIL
/* The closing record ends between 25 and 256 bytes into the tail window:
 * candidate end positions, measured from the window start, run from 0x100
 * down to 0x19. */
#define SPQ1_END_HIGH 0x100U
#define SPQ1_END_LOW 0x19U
#define SPQ1_MAX_RECORDS XX_SFX_COMPAQ_SOFTPAQ_MAX_RECORDS
#define SPQ1_RENAME_TRIES 8U
/* Worst case every byte of the name escapes to three characters, then
 * "_" and a 20-digit number. */
#define SPQ1_NAME_CAPACITY (SPQ1_NAME_FIELD * 3U + 48U)

typedef struct spq1_stub_s {
    uint32_t at10;
    uint32_t at20;
    uint32_t at24;
    int64_t stub_size;
} spq1_stub;

/* Builds 1..5; build 6 is the program name at 0xD0. */
static const spq1_stub spq1_stubs[5] = {
    {0xB03F6B6CU, 0x22BF02E6U, 0x23490000U, 0x3CBD},
    {0x9D6C6B6CU, 0x236302F0U, 0x23ED0000U, 0x3D5D},
    {0x0E936B6CU, 0x236302F0U, 0x23ED0000U, 0x3E01},
    {0xBB4B6B6CU, 0x236302F0U, 0x23ED0000U, 0x3E0D},
    {0x1FD96B6CU, 0x23AD03E4U, 0x24370000U, 0x4EA5}};
static const uint8_t spq1_program[8] = {0x06U, 'U', 'S', '_', 'P', 'C', 'U', 0U};
#define SPQ1_PROGRAM_STUB_SIZE 0x3D8B

/* --- small helpers --------------------------------------------------------- */

static uint16_t spq1_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t spq1_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static int64_t spq1_le32s(const uint8_t *bytes) {
    return (int64_t)(int32_t)spq1_le32(bytes);
}

static bool spq1_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* --- locating the payload -------------------------------------------------- */

typedef struct spq1_layout_s {
    int64_t size;             /* bytes from the base to the end of the device */
    int64_t directory;        /* relative to the base */
    int64_t trailer;          /* relative to the base */
    uint32_t record_size;
    uint32_t stub_build;
    size_t records;           /* directory entries, the stub's included */
} spq1_layout;

static uint32_t spq1_match_stub(const uint8_t *header, int64_t size) {
    uint32_t build;
    if (header[0] != 'M' || header[1] != 'Z') return 0U;
    for (build = 0U; build < 5U; ++build) {
        const spq1_stub *stub = &spq1_stubs[build];
        if (spq1_le32(header + 0x10) == stub->at10 &&
            spq1_le32(header + 0x20) == stub->at20 &&
            spq1_le32(header + 0x24) == stub->at24 && size > stub->stub_size)
            return build + 1U;
    }
    if (xx_rt_memcmp(header + XX_SFX_COMPAQ_SOFTPAQ_NAME_AT, spq1_program,
                     sizeof(spq1_program)) == 0 &&
        size > SPQ1_PROGRAM_STUB_SIZE)
        return 6U;
    return 0U;
}

/* The closing record: "FIT00" and '1' or '2', a zero byte, size 0 and a
 * positive directory offset. */
static bool spq1_is_trailer(const uint8_t *record) {
    return record[0] == 'F' && record[1] == 'I' && record[2] == 'T' &&
           record[3] == '0' && record[4] == '0' &&
           (record[5] == '1' || record[5] == '2') && record[12] == 0U &&
           spq1_le32(record + 13) == 0U && spq1_le32s(record + 17) > 0;
}

/* One directory entry: the zero byte after the name, a non-negative size and
 * offset, and bytes that end before the directory begins. */
static bool spq1_entry_ok(const uint8_t *entry, int64_t directory) {
    int64_t size, offset;
    if (entry[12] != 0U) return false;
    size = spq1_le32s(entry + 13);
    offset = spq1_le32s(entry + 17);
    return size >= 0 && offset >= 0 && offset <= directory &&
           size <= directory - offset;
}

/* Validates the stub, finds the closing record and checks the whole
 * directory.  Every read is bounded: 0xD8 header bytes, 256 tail bytes and a
 * directory of at most SPQ1_MAX_RECORDS * 25 bytes. */
static bool spq1_locate(Abstractformat *format, spq1_layout *layout,
                        uint8_t **directory_out, xx_pd_struct *pd) {
    uint8_t header[SPQ1_HEADER_SIZE];
    uint8_t tail[SPQ1_TAIL];
    uint8_t *table = NULL;
    int64_t total, size, window, trailer = -1, directory = 0, span;
    uint32_t end, record_size = 0U, build;
    size_t records, index;
    if (directory_out) *directory_out = NULL;
    if (!format || !format->device || !layout || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    /* The smallest stub is 0x3CBD bytes and the file must be larger. */
    if (size <= spq1_stubs[0].stub_size) return false;
    if (!spq1_read_at(format->device, format->base_address, header,
                      sizeof(header)))
        return false;
    build = spq1_match_stub(header, size);
    if (build == 0U) return false;

    /* size > 0x3CBD, so the whole window is inside the file. */
    window = size - (int64_t)SPQ1_TAIL;
    if (!spq1_read_at(format->device, format->base_address + window, tail,
                      sizeof(tail)))
        return false;
    for (end = SPQ1_END_HIGH; end >= SPQ1_END_LOW && trailer < 0; --end) {
        if (spq1_is_trailer(tail + end - SPQ1_LONG)) {
            trailer = window + (int64_t)(end - SPQ1_LONG);
            record_size = SPQ1_LONG;
        } else if (spq1_is_trailer(tail + end - SPQ1_SHORT)) {
            trailer = window + (int64_t)(end - SPQ1_SHORT);
            record_size = SPQ1_SHORT;
        }
    }
    if (trailer < 0) return false;
    directory = spq1_le32s(tail + (size_t)(trailer - window) + 17U);
    if (directory <= 0 || directory >= trailer) return false;
    span = trailer - directory;
    if (span % (int64_t)record_size != 0) return false;
    if (span / (int64_t)record_size > (int64_t)SPQ1_MAX_RECORDS) return false;
    records = (size_t)(span / (int64_t)record_size);
    /* The extractor's own entry plus at least one member. */
    if (records < 2U) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;

    table = (uint8_t *)xx_mem_alloc((size_t)span);
    if (!table) return false;
    if (!spq1_read_at(format->device, format->base_address + directory, table,
                      (size_t)span))
        goto fail;
    for (index = 0U; index < records; ++index)
        if (!spq1_entry_ok(table + index * record_size, directory)) goto fail;

    layout->size = size;
    layout->directory = directory;
    layout->trailer = trailer;
    layout->record_size = record_size;
    layout->stub_build = build;
    layout->records = records;
    if (directory_out)
        *directory_out = table;
    else
        xx_mem_free(table);
    return true;
fail:
    xx_mem_free(table);
    return false;
}

/* --- member names ------------------------------------------------------------ */

static char spq1_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool spq1_stem_is(const char *name, size_t stem, const char *word) {
    size_t i;
    for (i = 0U; i < stem && word[i]; ++i)
        if (spq1_upper(name[i]) != word[i]) return false;
    return i == stem && word[i] == 0;
}

/* Windows opens a device, not a file, for these stems whatever the
 * extension. */
static bool spq1_is_device_name(const char *name) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t stem = 0U, k;
    while (name[stem] && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (k = 0U; k < sizeof(devices) / sizeof(devices[0]); ++k)
        if (spq1_stem_is(name, stem, devices[k])) return true;
    if (stem == 4U && name[3] >= '0' && name[3] <= '9' &&
        (spq1_stem_is(name, 3U, "COM") || spq1_stem_is(name, 3U, "LPT")))
        return true;
    return false;
}

static void spq1_put_number(char *out, size_t *used, uint64_t value) {
    char digits[24];
    size_t count = 0U;
    do {
        digits[count++] = (char)('0' + (int)(value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count != 0U) out[(*used)++] = digits[--count];
}

/* Builds the published name into @p out (SPQ1_NAME_CAPACITY bytes). */
static void spq1_make_name(const uint8_t *field, size_t member, char *out) {
    static const char hex[] = "0123456789ABCDEF";
    size_t length = 0U, i, used = 0U;
    while (length < SPQ1_NAME_FIELD && field[length] != 0U) ++length;
    while (length > 0U &&
           (field[length - 1U] == ' ' || field[length - 1U] == '.'))
        --length;
    for (i = 0U; i < length; ++i) {
        uint8_t c = field[i];
        bool keep = (c > 0x20U && c < 0x7fU && c != '%' && c != '/' &&
                     c != '\\' && c != ':' && c != '*' && c != '?' &&
                     c != '"' && c != '<' && c != '>' && c != '|') ||
                    (c == ' ' && used != 0U);
        if (keep) {
            out[used++] = (char)c;
        } else {
            out[used++] = '%';
            out[used++] = hex[(c >> 4U) & 0x0fU];
            out[used++] = hex[c & 0x0fU];
        }
    }
    if (used == 0U) {
        const char *prefix = "record";
        while (*prefix) out[used++] = *prefix++;
        spq1_put_number(out, &used, (uint64_t)member);
    }
    out[used] = 0;
}

/* "<stem>_<number><extension>", the number going before the last dot. */
static void spq1_with_suffix(const char *name, uint64_t number, char *out) {
    size_t length = xx_str_len(name), dot = length, i, used;
    for (i = length; i > 0U; --i) {
        if (name[i - 1U] == '.') {
            dot = i - 1U;
            break;
        }
    }
    if (dot == 0U) dot = length;
    xx_rt_memcpy(out, name, dot);
    used = dot;
    out[used++] = '_';
    spq1_put_number(out, &used, number);
    xx_rt_memcpy(out + used, name + dot, length - dot);
    used += length - dot;
    out[used] = 0;
}

/* --- member table ---------------------------------------------------------- */

typedef struct spq1_member_s {
    char *name;
    int64_t header_offset; /* absolute */
    int64_t data_offset;   /* absolute */
    int64_t size;
    uint16_t dos_date;
    uint16_t dos_time;
    bool has_time;
    bool safe;
} spq1_member;

typedef struct spq1_stream_s {
    spq1_member *items;
    size_t count;
    size_t index;
    spq1_layout layout;
} spq1_stream;

static void spq1_stream_free(void *opaque) {
    spq1_stream *stream = (spq1_stream *)opaque;
    size_t index;
    if (!stream) return;
    if (stream->items) {
        for (index = 0U; index < stream->count; ++index)
            if (stream->items[index].name)
                xx_mem_free(stream->items[index].name);
        xx_mem_free(stream->items);
    }
    xx_mem_free(stream);
}

static bool spq1_name_taken(const spq1_stream *stream, size_t before,
                            const char *name) {
    size_t j;
    for (j = 0U; j < before; ++j)
        if (xx_str_icmp(stream->items[j].name, name) == 0) return true;
    return false;
}

static bool spq1_parse(Abstractformat *format, spq1_stream **result,
                       xx_pd_struct *pd) {
    spq1_layout layout;
    uint8_t *table = NULL;
    spq1_stream *stream = NULL;
    size_t index, members;
    if (!result) return false;
    *result = NULL;
    xx_rt_memset(&layout, 0, sizeof(layout));
    if (!spq1_locate(format, &layout, &table, pd)) return false;
    members = layout.records - 1U;
    stream = (spq1_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) goto fail;
    xx_rt_memset(stream, 0, sizeof(*stream));
    stream->layout = layout;
    stream->items = (spq1_member *)xx_mem_alloc(members * sizeof(spq1_member));
    if (!stream->items) goto fail;
    xx_rt_memset(stream->items, 0, members * sizeof(spq1_member));

    for (index = 0U; index < members; ++index) {
        /* Entry 0 is the extractor itself. */
        const uint8_t *entry = table + (index + 1U) * layout.record_size;
        spq1_member *member = &stream->items[index];
        char original[SPQ1_NAME_CAPACITY];
        uint32_t tries;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        member->header_offset = format->base_address + layout.directory +
                                (int64_t)((index + 1U) * layout.record_size);
        member->size = spq1_le32s(entry + 13);
        member->data_offset = format->base_address + spq1_le32s(entry + 17);
        if (layout.record_size == SPQ1_LONG) {
            member->dos_date = spq1_le16(entry + 21);
            member->dos_time = spq1_le16(entry + 23);
            member->has_time = member->dos_date != 0U;
        }
        member->name = (char *)xx_mem_alloc(SPQ1_NAME_CAPACITY);
        if (!member->name) goto fail;
        stream->count = index + 1U;
        spq1_make_name(entry, index, original);
        xx_rt_memcpy(member->name, original, xx_str_len(original) + 1U);
        member->safe = true;
        /* Duplicates are renamed against every name already published, so
         * the final names are pairwise distinct.  Each try suffixes the
         * original name, never a previous candidate. */
        for (tries = 0U; spq1_name_taken(stream, index, member->name);
             ++tries) {
            if (tries >= SPQ1_RENAME_TRIES) {
                member->safe = false;
                break;
            }
            spq1_with_suffix(original,
                             (uint64_t)index + (uint64_t)tries * members,
                             member->name);
        }
        if (spq1_is_device_name(member->name)) member->safe = false;
    }
    xx_mem_free(table);
    *result = stream;
    return true;
fail:
    if (table) xx_mem_free(table);
    spq1_stream_free(stream);
    return false;
}

/* --- record plumbing ------------------------------------------------------- */

static bool spq1_copy_options(xx_list_s *destination,
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

static const xx_var *spq1_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool spq1_set_record(xx_archive_record *record,
                            const spq1_member *member,
                            uint32_t record_size) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = (int64_t)record_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    if (!xx_archive_record_set_original_name(record, member->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        (uint64_t)member->size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        (uint64_t)member->size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        0U) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false))
        return false;
    if (member->has_time &&
        (!xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                         member->dos_date) ||
         !xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                         member->dos_time)))
        return false;
    return true;
}

/* --- lifecycle ------------------------------------------------------------- */

void xx_sfx_compaq_softpaq_init(xx_sfx_compaq_softpaq *archive,
                                xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_rt_memset(archive, 0, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SFX_COMPAQ_SOFTPAQ_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-dosexec");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_sfx_compaq_softpaq_check_is_valid;
    archive->format.handle_base_info = xx_sfx_compaq_softpaq_handle_base_info;
    archive->format.get_format_size = xx_sfx_compaq_softpaq_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sfx_compaq_softpaq_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sfx_compaq_softpaq_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sfx_compaq_softpaq_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sfx_compaq_softpaq_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sfx_compaq_softpaq_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sfx_compaq_softpaq_free_archive_records_reading;
    archive->directory_offset = -1;
    archive->trailer_offset = -1;
}

xx_sfx_compaq_softpaq *xx_sfx_compaq_softpaq_create(xx_io_device *device,
                                                    int64_t base_address) {
    xx_sfx_compaq_softpaq *archive =
        (xx_sfx_compaq_softpaq *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_sfx_compaq_softpaq_init(archive, device, base_address);
    return archive;
}

void xx_sfx_compaq_softpaq_destroy(xx_sfx_compaq_softpaq *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_sfx_compaq_softpaq_free(xx_sfx_compaq_softpaq *archive) {
    if (!archive) return;
    xx_sfx_compaq_softpaq_destroy(archive);
    xx_mem_free(archive);
}

bool xx_sfx_compaq_softpaq_check_is_valid(Abstractformat *format,
                                          xx_pd_struct *pd) {
    spq1_layout layout;
    return spq1_locate(format, &layout, NULL, pd);
}

bool xx_sfx_compaq_softpaq_handle_base_info(Abstractformat *format,
                                            xx_pd_struct *pd) {
    spq1_layout layout;
    xx_sfx_compaq_softpaq *archive;
    if (!format || !spq1_locate(format, &layout, NULL, pd)) return false;
    archive = (xx_sfx_compaq_softpaq *)format;
    archive->number_of_records = (uint64_t)(layout.records - 1U);
    archive->directory_offset = layout.directory;
    archive->trailer_offset = layout.trailer;
    archive->record_size = layout.record_size;
    archive->stub_build = layout.stub_build;
    format->number_of_archive_records = archive->number_of_records;
    format->format_size = layout.trailer + (int64_t)layout.record_size;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_sfx_compaq_softpaq_get_format_size(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_compaq_softpaq_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_sfx_compaq_softpaq_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_compaq_softpaq_handle_base_info(format, pd))
               ? ((xx_sfx_compaq_softpaq *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_sfx_compaq_softpaq_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    spq1_stream *stream;
    xx_archive_record_state *state;
    if (!spq1_parse(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        spq1_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = spq1_stream_free;
    state->total_records = stream->count;
    if (!spq1_copy_options(&state->options, options) ||
        !spq1_set_record(&state->current_record, &stream->items[0],
                         stream->layout.record_size)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_sfx_compaq_softpaq_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_sfx_compaq_softpaq_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    spq1_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (spq1_stream *)state->internal_state) ||
        stream->index + 1U >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        spq1_set_record(&state->current_record, &stream->items[stream->index],
                        stream->layout.record_size);
    return state->has_record;
}

bool xx_sfx_compaq_softpaq_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    spq1_stream *stream;
    const spq1_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    size_t base_length;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (spq1_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!member->safe) return false;
    path_option = spq1_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    /* Nothing to write: the member's range was checked against the
     * directory when the table was parsed. */
    if (!path_option) return true;
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    base_length = xx_str_len(base);
    path = (base_length != 0U && base[base_length - 1U] != '/' &&
            base[base_length - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    /* The helper removes its output itself on failure, and only when it
     * opened that file, so nothing is deleted here. */
    result = xx_store_unpack_device_to_file(format->device, member->data_offset,
                                            member->size, path, pd);
    if (result && member->has_time)
        (void)xx_store_apply_dos_time_and_attrs_a(path, member->dos_date,
                                                  member->dos_time, 0U);
done:
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_sfx_compaq_softpaq_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
