/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * TRS-80 JV1 disk image: a headerless dump of a single-sided, single density
 * TRS-80 Model I disk (10 sectors of 256 bytes per track) carrying a
 * TRSDOS 2.3 / LDOS 5 style filesystem.  The layout and every rule the
 * parser enforces are listed in xx_trs_80_jv1.h.
 *
 * The directory structures (GAT/HIT/FPDE/FXDE, the HIT name hash, extent
 * encoding and the file size rule) follow trs80-base src/Trsdos.ts by
 * Lawrence Kesteloot (MIT License, https://github.com/lkesteloot/trs80);
 * this is an independent C implementation of that structure for Model I
 * single density disks.
 *
 * The parse is also the late detection probe, so it is ordered cheapest
 * first: the image size, three boot sector bytes, one 2560-byte directory
 * track read validated entry by entry, and only then any allocation.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/trs_80_jv1/xx_trs_80_jv1.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Self-healing shim: the file compiles before the enum exists. */
#ifdef TRS_80_JV1
#define XX_TRS_80_JV1_FILE_TYPE XX_FILE_TYPE_TRS_80_JV1
#else
#define XX_TRS_80_JV1_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define JV1_SECTOR 256U
#define JV1_SPT 10U
#define JV1_TRACK (JV1_SECTOR * JV1_SPT)
#define JV1_MIN_TRACKS 2U
#define JV1_MAX_TRACKS 96U
#define JV1_GRANULES_PER_TRACK 2U
#define JV1_SECTORS_PER_GRANULE 5U
#define JV1_ENTRY 32U
#define JV1_ENTRIES_PER_SECTOR 8U
#define JV1_FIRST_DIR_SECTOR 2U
#define JV1_DIR_SECTORS (JV1_SPT - JV1_FIRST_DIR_SECTOR)
#define JV1_MAX_FILES (JV1_DIR_SECTORS * JV1_ENTRIES_PER_SECTOR)
#define JV1_MAX_CHAIN JV1_MAX_FILES
#define JV1_EXTENTS 5U
#define JV1_FLAG_EXTENDED 0x80U
#define JV1_FLAG_ACTIVE 0x10U
/* 8 + '~' + 3 digits + '.' + 3 + leading '_' + NUL = 18. */
#define JV1_NAME_BUFFER 24

typedef struct jv1_member_s {
    char name[JV1_NAME_BUFFER];
    int64_t size;
    uint32_t needed; /**< Sectors holding `size` bytes. */
    uint32_t first;  /**< Absolute sector of the file's first sector. */
    uint8_t dec;
    uint8_t flags;
} jv1_member;

typedef struct jv1_stream_s {
    uint8_t directory[JV1_TRACK];
    int64_t image_size;
    int64_t directory_offset; /**< Absolute. */
    uint32_t tracks;
    uint32_t directory_track;
    jv1_member *items;
    size_t count;
    size_t index;
} jv1_stream;

static bool jv1_read_at(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 || xx_io_seek64(device, offset, SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t amount = xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* The HIT hash of an 11-byte name: XOR each byte in, rotate left. */
static uint8_t jv1_hash(const uint8_t *name)
{
    uint32_t hash = 0U, index;
    for (index = 0U; index < 11U; ++index) {
        hash ^= name[index];
        hash = ((hash << 1) | (hash >> 7)) & 0xFFU;
    }
    return (uint8_t)(hash == 0U ? 1U : hash);
}

/* Printable ASCII up to a 0x00 / 0x0D terminator or the end. */
static bool jv1_text_ok(const uint8_t *text, size_t length)
{
    size_t index;
    for (index = 0U; index < length; ++index) {
        uint8_t c = text[index];
        if (c == 0x00U || c == 0x0DU) return true;
        if (c < 0x20U || c > 0x7EU) return false;
    }
    return true;
}

static bool jv1_dec_ok(uint32_t dec)
{
    return (dec & 0x1FU) < JV1_DIR_SECTORS;
}

static const uint8_t *jv1_entry(const uint8_t *directory, uint32_t dec)
{
    uint32_t sector = (dec & 0x1FU) + JV1_FIRST_DIR_SECTOR;
    uint32_t slot = dec >> 5;
    return directory + (size_t)sector * JV1_SECTOR + (size_t)slot * JV1_ENTRY;
}

/* Size of the file whose primary entry is `entry`; false when inconsistent. */
static bool jv1_file_size(const uint8_t *entry, uint32_t tracks, int64_t *size, uint32_t *needed)
{
    uint32_t sectors = (uint32_t)entry[20] | ((uint32_t)entry[21] << 8);
    uint32_t eof = entry[3];
    int64_t value;
    if (eof != 0U && sectors == 0U) return false;
    value = (int64_t)sectors * JV1_SECTOR + eof - (eof != 0U ? JV1_SECTOR : 0);
    *size = value;
    *needed = (uint32_t)((value + JV1_SECTOR - 1) / JV1_SECTOR);
    return *needed <= tracks * JV1_SPT;
}

/* Walk the extent chain of the file at `dec`.  The first `needed` sectors'
 * absolute numbers go to `out` when it is not NULL.  Every entry and extent
 * of the chain is validated; the extents must cover `needed` sectors. */
static bool jv1_walk(const uint8_t *directory, uint32_t tracks, uint32_t dec, uint32_t needed, uint16_t *out, uint32_t *first)
{
    uint8_t visited[32];
    uint32_t covered = 0U, chain = 0U, previous = 0U, total = tracks * JV1_SPT;
    bool have_first = false;
    xx_mem_zero(visited, sizeof(visited));
    for (;;) {
        const uint8_t *entry;
        uint32_t index;
        bool link = false;
        if (!jv1_dec_ok(dec) || chain >= JV1_MAX_CHAIN || (visited[dec >> 3] & (uint8_t)(1U << (dec & 7U))) != 0U) return false;
        visited[dec >> 3] |= (uint8_t)(1U << (dec & 7U));
        entry = jv1_entry(directory, dec);
        if (chain != 0U && ((entry[0] & (JV1_FLAG_EXTENDED | JV1_FLAG_ACTIVE)) != (JV1_FLAG_EXTENDED | JV1_FLAG_ACTIVE) || entry[1] != previous)) return false;
        for (index = 0U; index < JV1_EXTENTS; ++index) {
            uint32_t track = entry[22U + index * 2U];
            uint32_t granule = entry[23U + index * 2U];
            uint32_t offset, count, start, sector;
            if (track == 0xFFU) break;
            if (track == 0xFEU) {
                if (index != JV1_EXTENTS - 1U) return false;
                link = true;
                break;
            }
            offset = granule >> 5;
            count = ((granule & 0x1FU) + 1U) * JV1_SECTORS_PER_GRANULE;
            if (track >= tracks || offset >= JV1_GRANULES_PER_TRACK) return false;
            start = track * JV1_SPT + offset * JV1_SECTORS_PER_GRANULE;
            if (start + count > total) return false;
            if (!have_first) {
                *first = start;
                have_first = true;
            }
            for (sector = 0U; sector < count && covered < needed; ++sector) {
                if (out) out[covered] = (uint16_t)(start + sector);
                ++covered;
            }
        }
        ++chain;
        if (!link) break;
        previous = dec;
        dec = entry[31];
    }
    if (!have_first) *first = 0U;
    return covered >= needed;
}

/* -1 invalid, 0 not a primary file entry, 1 a valid file. */
static int jv1_check_primary(const uint8_t *directory, uint32_t tracks, uint32_t dec, jv1_member *member)
{
    const uint8_t *entry = jv1_entry(directory, dec);
    uint32_t index, needed, first = 0U;
    int64_t size;
    if ((entry[0] & JV1_FLAG_ACTIVE) == 0U || (entry[0] & JV1_FLAG_EXTENDED) != 0U) return 0;
    if (entry[5] == ' ') return -1;
    for (index = 5U; index < 16U; ++index)
        if (entry[index] < 0x20U || entry[index] > 0x7EU) return -1;
    if (directory[JV1_SECTOR + dec] != jv1_hash(entry + 5)) return -1;
    if (!jv1_file_size(entry, tracks, &size, &needed) || !jv1_walk(directory, tracks, dec, needed, NULL, &first)) return -1;
    if (member) {
        member->size = size;
        member->needed = needed;
        member->first = first;
        member->dec = (uint8_t)dec;
        member->flags = entry[0];
    }
    return 1;
}

static bool jv1_geometry(Abstractformat *format, int64_t *size, uint32_t *tracks)
{
    int64_t total;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    *size = total - format->base_address;
    if (*size <= 0 || *size % JV1_TRACK != 0 || *size / JV1_TRACK < JV1_MIN_TRACKS || *size / JV1_TRACK > JV1_MAX_TRACKS) return false;
    *tracks = (uint32_t)(*size / JV1_TRACK);
    return true;
}

/* Size, boot sector, directory read, entry validation.  No allocation. */
static bool jv1_probe(Abstractformat *format, jv1_stream *out, uint32_t *files)
{
    uint8_t boot[3];
    uint32_t dec, count = 0U, sector, slot;
    if (!jv1_geometry(format, &out->image_size, &out->tracks) || !jv1_read_at(format->device, format->base_address, boot, sizeof(boot))) return false;
    out->directory_track = boot[2] & 0x7FU;
    if (out->directory_track == 0U || out->directory_track >= out->tracks) return false;
    out->directory_offset = format->base_address + (int64_t)out->directory_track * JV1_TRACK;
    if (!jv1_read_at(format->device, out->directory_offset, out->directory, JV1_TRACK)) return false;
    if (!jv1_text_ok(out->directory + 0xD0, 8U) || !jv1_text_ok(out->directory + 0xD8, 8U) || !jv1_text_ok(out->directory + 0xE0, 0x20U)) return false;
    for (sector = 0U; sector < JV1_DIR_SECTORS; ++sector) {
        for (slot = 0U; slot < JV1_ENTRIES_PER_SECTOR; ++slot) {
            int kind;
            dec = (slot << 5) | sector;
            kind = jv1_check_primary(out->directory, out->tracks, dec, NULL);
            if (kind < 0) return false;
            if (kind > 0) ++count;
        }
    }
    *files = count;
    return count != 0U;
}

static char jv1_out_char(uint8_t c)
{
    if (c < 0x21U || c > 0x7EU) return '_';
    switch (c) {
        case '/':
        case '\\':
        case ':':
        case '*':
        case '?':
        case '"':
        case '<':
        case '>':
        case '|':
        case '~':
        case '.': return '_';
        default: return (char)c;
    }
}

static char jv1_fold(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool jv1_stem_is(const char *stem, size_t length, const char *word)
{
    size_t index;
    for (index = 0U; index < length; ++index)
        if (word[index] == 0 || jv1_fold(stem[index]) != word[index]) return false;
    return word[length] == 0;
}

static bool jv1_is_device_stem(const char *stem, size_t length)
{
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"};
    size_t index;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (jv1_stem_is(stem, length, devices[index])) return true;
    if (length == 4U && stem[3] >= '0' && stem[3] <= '9' && (jv1_stem_is(stem, 3U, "COM") || jv1_stem_is(stem, 3U, "LPT"))) return true;
    return false;
}

/* Trailing spaces are padding; interior ones are kept (as '_'). */
static size_t jv1_field_length(const uint8_t *field, size_t length)
{
    while (length > 0U && field[length - 1U] == ' ') --length;
    return length;
}

/* "[_]STEM[~DEC][.EXT]" into `out` (JV1_NAME_BUFFER bytes). */
static void jv1_make_name(const uint8_t *entry, bool suffix, uint32_t dec, char *out)
{
    char stem[8];
    size_t stem_length = jv1_field_length(entry + 5, 8U);
    size_t type_length = jv1_field_length(entry + 13, 3U);
    size_t at = 0U, index;
    for (index = 0U; index < stem_length; ++index) stem[index] = jv1_out_char(entry[5U + index]);
    if (stem_length == 0U) stem[stem_length++] = '_';
    if (jv1_is_device_stem(stem, stem_length)) out[at++] = '_';
    for (index = 0U; index < stem_length; ++index) out[at++] = stem[index];
    if (suffix) {
        char digits[3];
        size_t count = 0U;
        do {
            digits[count++] = (char)('0' + (char)(dec % 10U));
            dec /= 10U;
        } while (dec != 0U && count < sizeof(digits));
        out[at++] = '~';
        while (count > 0U) out[at++] = digits[--count];
    }
    if (type_length != 0U) {
        out[at++] = '.';
        for (index = 0U; index < type_length; ++index) out[at++] = jv1_out_char(entry[13U + index]);
    }
    out[at] = 0;
}

static bool jv1_names_equal(const char *a, const char *b)
{
    size_t index = 0U;
    for (;;) {
        if (jv1_fold(a[index]) != jv1_fold(b[index])) return false;
        if (a[index] == 0) return true;
        ++index;
    }
}

static void jv1_stream_free(void *opaque)
{
    jv1_stream *stream = (jv1_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool jv1_parse(Abstractformat *format, jv1_stream **result)
{
    jv1_stream *stream;
    uint32_t files = 0U, sector, slot;
    size_t count = 0U, member_index;
    if (!result) return false;
    stream = (jv1_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    if (!jv1_probe(format, stream, &files) || files > JV1_MAX_FILES) goto fail;
    stream->items = (jv1_member *)xx_mem_calloc(files, sizeof(jv1_member));
    if (!stream->items) goto fail;
    for (sector = 0U; sector < JV1_DIR_SECTORS; ++sector) {
        for (slot = 0U; slot < JV1_ENTRIES_PER_SECTOR; ++slot) {
            uint32_t dec = (slot << 5) | sector;
            jv1_member member;
            int kind;
            xx_mem_zero(&member, sizeof(member));
            kind = jv1_check_primary(stream->directory, stream->tracks, dec, &member);
            if (kind < 0) goto fail;
            if (kind == 0) continue;
            if (count >= files) goto fail;
            stream->items[count++] = member;
        }
    }
    if (count != files) goto fail;
    for (member_index = 0U; member_index < count; ++member_index) {
        jv1_member *member = &stream->items[member_index];
        const uint8_t *entry = jv1_entry(stream->directory, member->dec);
        size_t earlier;
        jv1_make_name(entry, false, 0U, member->name);
        for (earlier = 0U; earlier < member_index; ++earlier) {
            if (jv1_names_equal(stream->items[earlier].name, member->name)) {
                jv1_make_name(entry, true, member->dec, member->name);
                break;
            }
        }
    }
    stream->count = count;
    *result = stream;
    return true;
fail:
    jv1_stream_free(stream);
    return false;
}

static bool jv1_write_all(xx_io_device *destination, const uint8_t *data, size_t size)
{
    size_t written = 0U;
    while (written < size) {
        ssize_t amount = xx_io_write(destination, data + written, size - written);
        if (amount <= 0 || (size_t)amount > size - written) return false;
        written += (size_t)amount;
    }
    return true;
}

/* Stream a file sector by sector into `destination`, or just read it through
 * when that is NULL. */
static bool jv1_copy_member(Abstractformat *format, const jv1_stream *stream, const jv1_member *member, xx_io_device *destination, xx_pd_struct *pd)
{
    uint8_t buffer[JV1_SECTOR];
    uint16_t *sectors;
    uint32_t index, first = 0U;
    int64_t remaining = member->size;
    bool ok = true;
    if (member->size < 0 || member->needed > stream->tracks * JV1_SPT) return false;
    if (member->size == 0) return true;
    sectors = (uint16_t *)xx_mem_alloc((size_t)member->needed * sizeof(uint16_t));
    if (!sectors) return false;
    if (!jv1_walk(stream->directory, stream->tracks, member->dec, member->needed, sectors, &first)) {
        xx_mem_free(sectors);
        return false;
    }
    for (index = 0U; index < member->needed && remaining > 0; ++index) {
        size_t chunk = remaining > (int64_t)JV1_SECTOR ? (size_t)JV1_SECTOR : (size_t)remaining;
        if ((pd && xx_pd_is_stopped(pd)) || sectors[index] >= stream->tracks * JV1_SPT ||
            !jv1_read_at(format->device, format->base_address + (int64_t)sectors[index] * JV1_SECTOR, buffer, chunk) ||
            (destination && !jv1_write_all(destination, buffer, chunk))) {
            ok = false;
            break;
        }
        remaining -= (int64_t)chunk;
    }
    xx_mem_free(sectors);
    return ok && remaining == 0;
}

/* Names come from jv1_make_name; re-check before touching the disk: one
 * component of safe characters with at most one dot, not a device name. */
static bool jv1_safe_output_name(const char *name)
{
    size_t length, index, dot;
    if (!name || !name[0]) return false;
    length = xx_str_len(name);
    if (length >= JV1_NAME_BUFFER) return false;
    dot = length;
    for (index = 0U; index < length; ++index) {
        char c = name[index];
        if ((unsigned char)c < 0x21U || (unsigned char)c > 0x7EU || c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' ||
            c == '*')
            return false;
        if (c == '.') {
            if (dot != length) return false;
            dot = index;
        }
    }
    if (dot == 0U || dot + 1U == length) return false;
    return !jv1_is_device_stem(name, dot);
}

static bool jv1_copy_options(xx_list_s *destination, const xx_list_s *source)
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

static const xx_var *jv1_option(const xx_list_s *options, uint32_t id)
{
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool jv1_set_record(const jv1_stream *stream, xx_archive_record *record, const jv1_member *member)
{
    uint32_t sector = (member->dec & 0x1FU) + JV1_FIRST_DIR_SECTOR;
    uint32_t slot = (uint32_t)member->dec >> 5;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = stream->directory_offset + (int64_t)sector * JV1_SECTOR + (int64_t)slot * JV1_ENTRY;
    record->header_size = JV1_ENTRY;
    record->data_offset = stream->directory_offset - (int64_t)stream->directory_track * JV1_TRACK + (int64_t)member->first * JV1_SECTOR;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) && xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) && xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES, member->flags) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_trs_80_jv1_init(xx_trs_80_jv1 *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_TRS_80_JV1_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-trs80-jv1");
    xx_format_set_extension(&archive->format, "jv1");
    archive->format.check_is_valid = xx_trs_80_jv1_check_is_valid;
    archive->format.handle_base_info = xx_trs_80_jv1_handle_base_info;
    archive->format.get_format_size = xx_trs_80_jv1_get_format_size;
    archive->format.get_number_of_archive_records = xx_trs_80_jv1_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_trs_80_jv1_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_trs_80_jv1_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_trs_80_jv1_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_trs_80_jv1_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_trs_80_jv1_free_archive_records_reading;
    archive->image_size = -1;
}

xx_trs_80_jv1 *xx_trs_80_jv1_create(xx_io_device *device, int64_t base_address)
{
    xx_trs_80_jv1 *archive = (xx_trs_80_jv1 *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_trs_80_jv1_init(archive, device, base_address);
    return archive;
}

void xx_trs_80_jv1_destroy(xx_trs_80_jv1 *archive)
{
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_trs_80_jv1_free(xx_trs_80_jv1 *archive)
{
    if (!archive) return;
    xx_trs_80_jv1_destroy(archive);
    xx_mem_free(archive);
}

bool xx_trs_80_jv1_check_is_valid(Abstractformat *format, xx_pd_struct *pd)
{
    jv1_stream *stream;
    (void)pd;
    if (!jv1_parse(format, &stream)) return false;
    jv1_stream_free(stream);
    return true;
}

bool xx_trs_80_jv1_handle_base_info(Abstractformat *format, xx_pd_struct *pd)
{
    jv1_stream *stream;
    xx_trs_80_jv1 *archive;
    (void)pd;
    if (!format || !jv1_parse(format, &stream)) return false;
    archive = (xx_trs_80_jv1 *)format;
    archive->number_of_records = stream->count;
    archive->image_size = stream->image_size;
    archive->tracks = stream->tracks;
    archive->directory_track = stream->directory_track;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->image_size;
    format->is_valid = true;
    format->base_info_handled = true;
    jv1_stream_free(stream);
    return true;
}

int64_t xx_trs_80_jv1_get_format_size(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_trs_80_jv1_handle_base_info(format, pd)) ? format->format_size : -1;
}

uint64_t xx_trs_80_jv1_get_number_of_archive_records(Abstractformat *format, xx_pd_struct *pd)
{
    return format && (format->base_info_handled || xx_trs_80_jv1_handle_base_info(format, pd)) ? ((xx_trs_80_jv1 *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_trs_80_jv1_create_archive_records_reading(Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd)
{
    jv1_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!jv1_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        jv1_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = jv1_stream_free;
    state->total_records = stream->count;
    if (!jv1_copy_options(&state->options, options) || !jv1_set_record(stream, &state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_trs_80_jv1_get_current_archive_record(Abstractformat *format, xx_archive_record_state *state)
{
    return format && state && state->format == format && state->has_record ? &state->current_record : NULL;
}

bool xx_trs_80_jv1_archive_record_move_to_next(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    jv1_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format || !(stream = (jv1_stream *)state->internal_state) || ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = jv1_set_record(stream, &state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_trs_80_jv1_unpack_current_archive_record(Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd)
{
    jv1_stream *stream;
    jv1_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record || !(stream = (jv1_stream *)state->internal_state) || stream->index >= stream->count ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    path_option = jv1_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) /* No destination: read the file through, which verifies it. */
        return jv1_copy_member(format, stream, member, NULL, pd);
    if (!jv1_safe_output_name(member->name)) return false;
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(path_option);
    else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base = owned_base;
    }
    if (!base) goto done;
    path = (base[0] && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') ? xx_str_concat3(base, "/", member->name)
                                                                                                  : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = jv1_copy_member(format, stream, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_trs_80_jv1_free_archive_records_reading(Abstractformat *format, xx_archive_record_state *state)
{
    (void)format;
    xx_archive_record_state_free(state);
}
