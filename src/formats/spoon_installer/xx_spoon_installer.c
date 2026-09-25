/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Spoon Installer: a PE stub whose overlay carries one bzip2 stream per
 * installed file, a file directory behind the last stream and a 24-byte
 * footer that ends the file.  xx_spoon_installer.h has the layout.
 *
 * No reference implementation exists; the layout was measured on the four
 * installers of the reference corpus (every directory field, every stream
 * boundary and the per-file byte sum agree on all of them).  The executable
 * is not parsed beyond its "MZ" signature: the footer locates the directory
 * and the directory locates every stream.  No code is run or emulated.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/spoon_installer/xx_spoon_installer.h"

#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder: xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * the real file type is picked up as soon as the format is registered. */
#ifdef SPOON_INSTALLER
#define XX_SPOON_INSTALLER_FILE_TYPE XX_FILE_TYPE_SPOON_INSTALLER
#else
#define XX_SPOON_INSTALLER_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define SPOON_FOOTER_SIZE ((int64_t)XX_SPOON_INSTALLER_FOOTER_SIZE)
#define SPOON_RECORD_FIXED 17U
/* One name character and its terminator. */
#define SPOON_MIN_NAME 2U
#define SPOON_MIN_RECORD (SPOON_RECORD_FIXED + SPOON_MIN_NAME)
#define SPOON_MAX_FILES UINT32_C(65536)
/* A directory record is at most 17 + 255 bytes, so this also bounds the
 * directory buffer: 65536 records of average name length ~47. */
#define SPOON_MAX_DIRECTORY (4U << 20)
/* "BZh9", the end-of-stream magic and its CRC: the shortest bzip2 stream. */
#define SPOON_MIN_STREAM 14U
#define SPOON_STREAM_HEAD 10U
/* No stream can start inside the MZ header. */
#define SPOON_MIN_PAYLOAD 0x40U
/* Published names: at most 254 ANSI bytes, three UTF-8 bytes each. */
#define SPOON_NAME_BUFFER (255U * 3U + 1U)
#define SPOON_DEDUP_TRIES 32U
#define SPOON_COPY_BUFFER 0x10000U
#define SPOON_SSIZE_LIMIT (((size_t)-1) >> 1U)
/* ZIP's method number for bzip2. */
#define SPOON_METHOD_BZIP2 12U

static const uint8_t spoon_signature[XX_SPOON_INSTALLER_SIGNATURE_SIZE] = {
    0x83U, 0x52U, 0x34U, 0x03U, 0x43U, 0xF3U, 0xFFU, 0x0EU};

typedef struct spoon_layout_s {
    int64_t base;           /* device offset of the executable */
    int64_t total;          /* bytes from base to the device end */
    int64_t directory;      /* directory offset, relative to base */
    int64_t directory_size;
    int64_t payload;        /* first stream, relative to base */
    uint64_t unpacked_total;
    uint32_t count;
    uint32_t value0;
    uint32_t value1;
} spoon_layout;

typedef struct spoon_entry_s {
    char *name;             /* published UTF-8 name */
    int64_t header_offset;  /* device offset of the directory record */
    int64_t header_size;
    int64_t data_offset;    /* device offset of the stream */
    int64_t packed_size;
    uint32_t unpacked_size;
    uint32_t checksum;
    bool extractable;
} spoon_entry;

typedef struct spoon_table_s {
    spoon_entry *entries;
    size_t count;
    /* Case-insensitive set of published names: slot = entry index + 1. */
    uint32_t *slots;
    size_t mask;
} spoon_table;

typedef struct spoon_stream_s {
    spoon_layout layout;
    spoon_table table;
    size_t index;
    uint64_t max_member;    /* UINT64_MAX when unset */
} spoon_stream;

/* ---------------------------------------------------------------------- */
/* Helpers                                                                 */

static uint32_t spoon_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static bool spoon_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static char spoon_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* ---------------------------------------------------------------------- */
/* Names                                                                   */

/* Windows-1252 0x80..0x9F; zero marks the five unassigned bytes. */
static const uint16_t spoon_cp1252_high[32] = {
    0x20ACU, 0x0000U, 0x201AU, 0x0192U, 0x201EU, 0x2026U, 0x2020U, 0x2021U,
    0x02C6U, 0x2030U, 0x0160U, 0x2039U, 0x0152U, 0x0000U, 0x017DU, 0x0000U,
    0x0000U, 0x2018U, 0x2019U, 0x201CU, 0x201DU, 0x2022U, 0x2013U, 0x2014U,
    0x02DCU, 0x2122U, 0x0161U, 0x203AU, 0x0153U, 0x0000U, 0x017EU, 0x0178U};

static bool spoon_word_is(const uint8_t *name, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || spoon_upper((char)name[index]) != word[index])
            return false;
    return word[stem] == 0;
}

/* The raw ANSI name (without its terminator) is usable as a file name on
 * every host: no separators, drive colons or other reserved punctuation, no
 * DEL or unassigned code-page byte, not only dots and spaces, no trailing
 * dot or space (Windows drops those, so two names could alias), and no
 * device name such as CON, NUL.TXT, COM1, LPT9.LOG or CONIN$. */
static bool spoon_safe_raw_name(const uint8_t *name, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t index, stem = 0U;
    bool meaningful = false;
    if (length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        uint8_t c = name[index];
        if (c < 0x20U || c == 0x7FU || c == '/' || c == '\\' || c == ':' ||
            c == '<' || c == '>' || c == '"' || c == '|' || c == '?' ||
            c == '*')
            return false;
        if (c >= 0x80U && c <= 0x9FU && spoon_cp1252_high[c - 0x80U] == 0U)
            return false;
        if (c != '.' && c != ' ') meaningful = true;
    }
    if (!meaningful || name[length - 1U] == '.' || name[length - 1U] == ' ')
        return false;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (spoon_word_is(name, stem, devices[index])) return false;
    /* COM0..COM9 and LPT0..LPT9, also with the superscript digits 1, 2, 3
     * (0xB9, 0xB2, 0xB3 in Windows-1252). */
    if (stem == 4U &&
        (spoon_word_is(name, 3U, "COM") || spoon_word_is(name, 3U, "LPT")) &&
        ((name[3] >= '0' && name[3] <= '9') || name[3] == 0xB9U ||
         name[3] == 0xB2U || name[3] == 0xB3U))
        return false;
    return true;
}

/* Windows-1252 to UTF-8; @p out holds SPOON_NAME_BUFFER bytes. */
static void spoon_decode_name(const uint8_t *name, size_t length, char *out) {
    size_t index, used = 0U;
    for (index = 0U; index < length; ++index) {
        uint32_t code = name[index];
        if (code >= 0x80U && code <= 0x9FU) code = spoon_cp1252_high[code - 0x80U];
        if (code < 0x80U) {
            out[used++] = (char)code;
        } else if (code < 0x800U) {
            out[used++] = (char)(0xC0U | (code >> 6U));
            out[used++] = (char)(0x80U | (code & 0x3FU));
        } else {
            out[used++] = (char)(0xE0U | (code >> 12U));
            out[used++] = (char)(0x80U | ((code >> 6U) & 0x3FU));
            out[used++] = (char)(0x80U | (code & 0x3FU));
        }
    }
    out[used] = 0;
}

static uint32_t spoon_name_hash(const char *name) {
    uint32_t hash = UINT32_C(2166136261);
    for (; *name; ++name) {
        hash ^= (uint8_t)spoon_upper(*name);
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static bool spoon_same_name(const char *left, const char *right) {
    for (; *left && *right; ++left, ++right)
        if (spoon_upper(*left) != spoon_upper(*right)) return false;
    return *left == *right;
}

static bool spoon_name_taken(const spoon_table *table, const char *name) {
    size_t slot = spoon_name_hash(name) & table->mask, probes;
    for (probes = 0U; probes <= table->mask; ++probes) {
        uint32_t value = table->slots[slot];
        if (value == 0U) return false;
        if (spoon_same_name(table->entries[value - 1U].name, name)) return true;
        slot = (slot + 1U) & table->mask;
    }
    return true;
}

static void spoon_name_insert(spoon_table *table, size_t index) {
    size_t slot = spoon_name_hash(table->entries[index].name) & table->mask,
           probes;
    for (probes = 0U; probes <= table->mask; ++probes) {
        if (table->slots[slot] == 0U) {
            table->slots[slot] = (uint32_t)(index + 1U);
            return;
        }
        slot = (slot + 1U) & table->mask;
    }
}

static char *spoon_strdup(const char *text) {
    size_t length = xx_str_len(text);
    char *copy = (char *)xx_mem_alloc(length + 1U);
    if (copy) xx_rt_memcpy(copy, text, length + 1U);
    return copy;
}

/* Name entry @p index from its raw ANSI bytes.  An unusable name becomes
 * "file_NNNN"; a name that is already published (ignoring ASCII case) gets
 * "_NNNN" (and a further counter if needed) inserted before its extension,
 * so no member overwrites another.  A member that cannot be given a unique
 * name is listed under its colliding name but never written. */
static bool spoon_publish_name(spoon_table *table, size_t index,
                               const uint8_t *raw, size_t length) {
    spoon_entry *entry = &table->entries[index];
    char base[SPOON_NAME_BUFFER];
    char candidate[SPOON_NAME_BUFFER + 32U];
    size_t base_length, stem, attempt;
    if (spoon_safe_raw_name(raw, length))
        spoon_decode_name(raw, length, base);
    else
        (void)xx_rt_snprintf(base, sizeof(base), "file_%04u", (unsigned)index);
    entry->extractable = false;
    if (!spoon_name_taken(table, base)) {
        entry->name = spoon_strdup(base);
        if (!entry->name) return false;
        entry->extractable = true;
        spoon_name_insert(table, index);
        return true;
    }
    base_length = xx_str_len(base);
    stem = base_length;
    while (stem > 0U && base[stem - 1U] != '.') --stem;
    stem = stem > 1U ? stem - 1U : base_length;
    for (attempt = 0U; attempt < SPOON_DEDUP_TRIES; ++attempt) {
        char suffix[32];
        size_t suffix_length;
        if (attempt == 0U)
            (void)xx_rt_snprintf(suffix, sizeof(suffix), "_%04u",
                                 (unsigned)index);
        else
            (void)xx_rt_snprintf(suffix, sizeof(suffix), "_%04u_%u",
                                 (unsigned)index, (unsigned)attempt);
        suffix_length = xx_str_len(suffix);
        xx_rt_memcpy(candidate, base, stem);
        xx_rt_memcpy(candidate + stem, suffix, suffix_length);
        xx_rt_memcpy(candidate + stem + suffix_length, base + stem,
                     base_length - stem + 1U);
        if (!spoon_name_taken(table, candidate)) {
            entry->extractable = true;
            break;
        }
    }
    entry->name = spoon_strdup(entry->extractable ? candidate : base);
    if (!entry->name) return false;
    if (entry->extractable) spoon_name_insert(table, index);
    return true;
}

/* ---------------------------------------------------------------------- */
/* Footer and directory                                                    */

static bool spoon_locate(Abstractformat *format, spoon_layout *layout) {
    uint8_t footer[XX_SPOON_INSTALLER_FOOTER_SIZE];
    uint8_t mz[2];
    int64_t device_size, directory_end;
    if (!format || !format->device || !layout || format->base_address < 0)
        return false;
    xx_mem_zero(layout, sizeof(*layout));
    device_size = xx_io_total_size(format->device);
    if (device_size < format->base_address) return false;
    layout->base = format->base_address;
    layout->total = device_size - format->base_address;
    if (layout->total < (int64_t)(SPOON_MIN_PAYLOAD + SPOON_MIN_STREAM +
                                  SPOON_MIN_RECORD) + SPOON_FOOTER_SIZE ||
        !spoon_read_at(format->device,
                       layout->base + layout->total - SPOON_FOOTER_SIZE,
                       footer, sizeof(footer)) ||
        xx_rt_memcmp(footer + 16U, spoon_signature,
                     sizeof(spoon_signature)) != 0 ||
        !spoon_read_at(format->device, layout->base, mz, sizeof(mz)) ||
        mz[0] != 'M' || mz[1] != 'Z')
        return false;
    layout->value0 = spoon_le32(footer);
    layout->value1 = spoon_le32(footer + 4U);
    layout->directory = (int64_t)spoon_le32(footer + 8U);
    layout->count = spoon_le32(footer + 12U);
    directory_end = layout->total - SPOON_FOOTER_SIZE;
    if (layout->count == 0U || layout->count > SPOON_MAX_FILES ||
        layout->directory < (int64_t)(SPOON_MIN_PAYLOAD + SPOON_MIN_STREAM) ||
        layout->directory >= directory_end)
        return false;
    layout->directory_size = directory_end - layout->directory;
    if (layout->directory_size > (int64_t)SPOON_MAX_DIRECTORY ||
        layout->directory_size <
            (int64_t)layout->count * (int64_t)SPOON_MIN_RECORD)
        return false;
    return true;
}

/* A bzip2 stream header: "BZh", a block size digit, then the magic of a
 * first block or of the end of an empty stream. */
static bool spoon_stream_head_ok(const uint8_t *head) {
    static const uint8_t block[6] = {0x31U, 0x41U, 0x59U, 0x26U, 0x53U, 0x59U};
    static const uint8_t end[6] = {0x17U, 0x72U, 0x45U, 0x38U, 0x50U, 0x90U};
    return head[0] == 'B' && head[1] == 'Z' && head[2] == 'h' &&
           head[3] >= '1' && head[3] <= '9' &&
           (xx_rt_memcmp(head + 4U, block, 6U) == 0 ||
            xx_rt_memcmp(head + 4U, end, 6U) == 0);
}

static void spoon_table_free(spoon_table *table) {
    size_t index;
    if (!table) return;
    if (table->entries) {
        for (index = 0U; index < table->count; ++index)
            if (table->entries[index].name)
                xx_mem_free(table->entries[index].name);
        xx_mem_free(table->entries);
    }
    if (table->slots) xx_mem_free(table->slots);
    xx_mem_zero(table, sizeof(*table));
}

/* Read and check the whole directory.  Every record must lie inside it, the
 * directory must end exactly at the footer, the streams must follow each
 * other without gaps from the first one up to the directory, and each must
 * open with a bzip2 header.  With @p table set the entries are also built. */
static bool spoon_walk(Abstractformat *format, spoon_layout *layout,
                       spoon_table *table, xx_pd_struct *pd) {
    uint8_t *directory;
    uint8_t head[SPOON_STREAM_HEAD];
    size_t position = 0U, size;
    int64_t expected = -1;
    uint32_t index;
    bool result = false;
    if (table) {
        size_t slots = 16U;
        xx_mem_zero(table, sizeof(*table));
        while (slots < (size_t)layout->count * 2U) slots <<= 1U;
        table->entries = (spoon_entry *)xx_mem_calloc(layout->count,
                                                      sizeof(spoon_entry));
        table->slots = (uint32_t *)xx_mem_calloc(slots, sizeof(uint32_t));
        table->mask = slots - 1U;
        if (!table->entries || !table->slots) {
            spoon_table_free(table);
            return false;
        }
    }
    size = (size_t)layout->directory_size;
    directory = (uint8_t *)xx_mem_alloc(size);
    if (!directory ||
        !spoon_read_at(format->device, layout->base + layout->directory,
                       directory, size))
        goto done;
    layout->unpacked_total = 0U;
    for (index = 0U; index < layout->count; ++index) {
        const uint8_t *record;
        uint32_t offset, packed, unpacked, checksum;
        size_t length, name_index;
        if ((index & 0xFFU) == 0U && pd && xx_pd_is_stopped(pd)) goto done;
        if (size - position < SPOON_MIN_RECORD) goto done;
        record = directory + position;
        offset = spoon_le32(record);
        packed = spoon_le32(record + 4U);
        unpacked = spoon_le32(record + 8U);
        checksum = spoon_le32(record + 12U);
        length = record[16];
        if (length < SPOON_MIN_NAME ||
            size - position - SPOON_RECORD_FIXED < length ||
            record[SPOON_RECORD_FIXED + length - 1U] != 0U)
            goto done;
        for (name_index = 0U; name_index + 1U < length; ++name_index)
            if (record[SPOON_RECORD_FIXED + name_index] < 0x20U) goto done;
        if (index == 0U) {
            if (offset < SPOON_MIN_PAYLOAD) goto done;
            layout->payload = (int64_t)offset;
        } else if ((int64_t)offset != expected) {
            goto done;
        }
        if (packed < SPOON_MIN_STREAM ||
            (int64_t)packed > layout->directory - (int64_t)offset)
            goto done;
        expected = (int64_t)offset + (int64_t)packed;
        if (!spoon_read_at(format->device, layout->base + (int64_t)offset,
                           head, sizeof(head)) ||
            !spoon_stream_head_ok(head))
            goto done;
        layout->unpacked_total += unpacked;
        if (table) {
            spoon_entry *entry = &table->entries[index];
            entry->header_offset = layout->base + layout->directory +
                                   (int64_t)position;
            entry->header_size = (int64_t)(SPOON_RECORD_FIXED + length);
            entry->data_offset = layout->base + (int64_t)offset;
            entry->packed_size = (int64_t)packed;
            entry->unpacked_size = unpacked;
            entry->checksum = checksum;
            if (!spoon_publish_name(table, index, record + SPOON_RECORD_FIXED,
                                    length - 1U))
                goto done;
            table->count = (size_t)index + 1U;
        }
        position += SPOON_RECORD_FIXED + length;
    }
    result = position == size && expected == layout->directory;
done:
    if (directory) xx_mem_free(directory);
    if (!result && table) spoon_table_free(table);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Extraction                                                              */

/* Output sink: forwards to the destination (none when only verifying) and
 * refuses to grow past the declared unpacked size. */
typedef struct spoon_sink_s {
    xx_io_device device;
    xx_io_device *target;
    uint64_t written;
    uint64_t limit;
} spoon_sink;

static ssize_t spoon_sink_write(xx_io_device *self, const void *buffer,
                                size_t size) {
    spoon_sink *sink = self ? (spoon_sink *)self->priv : NULL;
    const uint8_t *bytes = (const uint8_t *)buffer;
    size_t done = 0U;
    if (!sink || (!bytes && size != 0U) || size > SPOON_SSIZE_LIMIT ||
        (uint64_t)size > sink->limit - sink->written)
        return -1;
    while (sink->target && done < size) {
        ssize_t wrote = xx_io_write(sink->target, bytes + done, size - done);
        if (wrote <= 0 || (size_t)wrote > size - done) return -1;
        done += (size_t)wrote;
    }
    sink->written += size;
    return (ssize_t)size;
}

/* The directory's check value: the packed stream's bytes summed. */
static bool spoon_checksum_ok(xx_io_device *device, const spoon_entry *entry,
                              xx_pd_struct *pd) {
    uint8_t *buffer;
    int64_t offset = entry->data_offset, left = entry->packed_size;
    uint32_t sum = 0U;
    bool result = true;
    buffer = (uint8_t *)xx_mem_alloc(SPOON_COPY_BUFFER);
    if (!buffer) return false;
    while (left > 0) {
        size_t chunk = left < (int64_t)SPOON_COPY_BUFFER ? (size_t)left
                                                         : SPOON_COPY_BUFFER;
        size_t index;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !spoon_read_at(device, offset, buffer, chunk)) {
            result = false;
            break;
        }
        for (index = 0U; index < chunk; ++index) sum += buffer[index];
        offset += (int64_t)chunk;
        left -= (int64_t)chunk;
    }
    xx_mem_free(buffer);
    return result && sum == entry->checksum;
}

static bool spoon_unpack_entry(Abstractformat *format,
                               const spoon_stream *stream,
                               const spoon_entry *entry, xx_io_device *target,
                               xx_pd_struct *pd) {
    spoon_sink sink;
    if ((uint64_t)entry->unpacked_size > stream->max_member ||
        !spoon_checksum_ok(format->device, entry, pd))
        return false;
    xx_mem_zero(&sink, sizeof(sink));
    sink.device.write = spoon_sink_write;
    sink.device.priv = &sink;
    sink.target = target;
    sink.limit = entry->unpacked_size;
    /* The decoder checks every block CRC and the stream CRC. */
    return xx_bzip2_unpack_device(format->device, entry->data_offset,
                                  entry->packed_size, &sink.device, pd) &&
           sink.written == (uint64_t)entry->unpacked_size;
}

/* ---------------------------------------------------------------------- */
/* Options and records                                                     */

static bool spoon_copy_options(xx_list_s *destination,
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

/* XX_META_ID_OPT_MAX_MEMBER_SIZE, when set to a non-negative integer. */
static uint64_t spoon_max_member(const Abstractformat *format,
                                 const xx_list_s *options) {
    const xx_var *limit = xx_format_resolve_extra_parameter(
        format, options, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (!limit) return UINT64_MAX;
    switch (limit->type) {
        case XX_VAR_TYPE_UINT8:
        case XX_VAR_TYPE_UINT16:
        case XX_VAR_TYPE_UINT32:
        case XX_VAR_TYPE_UINT64: return xx_var_get_u64(limit);
        case XX_VAR_TYPE_INT8:
        case XX_VAR_TYPE_INT16:
        case XX_VAR_TYPE_INT32:
        case XX_VAR_TYPE_INT64: {
            int64_t value = xx_var_get_i64(limit);
            return value >= 0 ? (uint64_t)value : UINT64_MAX;
        }
        default: return UINT64_MAX;
    }
}

static bool spoon_set_record(xx_archive_record *record,
                             const spoon_entry *entry) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = entry->header_offset;
    record->header_size = entry->header_size;
    record->data_offset = entry->data_offset;
    record->compressed_size = entry->packed_size;
    return xx_archive_record_set_original_name(record, entry->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)entry->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)entry->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          SPOON_METHOD_BZIP2) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

static void spoon_stream_free(void *opaque) {
    spoon_stream *stream = (spoon_stream *)opaque;
    if (!stream) return;
    spoon_table_free(&stream->table);
    xx_mem_free(stream);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_spoon_installer_init(xx_spoon_installer *archive, xx_io_device *device,
                             int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SPOON_INSTALLER_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.os = XX_OS_WINDOWS;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-dosexec");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_spoon_installer_check_is_valid;
    archive->format.handle_base_info = xx_spoon_installer_handle_base_info;
    archive->format.get_format_size = xx_spoon_installer_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_spoon_installer_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_spoon_installer_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_spoon_installer_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_spoon_installer_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_spoon_installer_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_spoon_installer_free_archive_records_reading;
    archive->payload_offset = -1;
    archive->directory_offset = -1;
}

xx_spoon_installer *xx_spoon_installer_create(xx_io_device *device,
                                              int64_t base_address) {
    xx_spoon_installer *archive =
        (xx_spoon_installer *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_spoon_installer_init(archive, device, base_address);
    return archive;
}

void xx_spoon_installer_destroy(xx_spoon_installer *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_spoon_installer_free(xx_spoon_installer *archive) {
    if (!archive) return;
    xx_spoon_installer_destroy(archive);
    xx_mem_free(archive);
}

bool xx_spoon_installer_check_is_valid(Abstractformat *format,
                                       xx_pd_struct *pd) {
    spoon_layout layout;
    return spoon_locate(format, &layout) &&
           spoon_walk(format, &layout, NULL, pd);
}

bool xx_spoon_installer_handle_base_info(Abstractformat *format,
                                         xx_pd_struct *pd) {
    xx_spoon_installer *archive;
    spoon_layout layout;
    if (!format || !spoon_locate(format, &layout) ||
        !spoon_walk(format, &layout, NULL, pd))
        return false;
    archive = (xx_spoon_installer *)format;
    archive->number_of_records = layout.count;
    archive->payload_offset = layout.base + layout.payload;
    archive->directory_offset = layout.base + layout.directory;
    archive->directory_size = layout.directory_size;
    archive->unpacked_total = layout.unpacked_total;
    archive->footer_value0 = layout.value0;
    archive->footer_value1 = layout.value1;
    format->number_of_archive_records = layout.count;
    format->format_size = layout.total;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_spoon_installer_get_format_size(Abstractformat *format,
                                           xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_spoon_installer_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_spoon_installer_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_spoon_installer_handle_base_info(format, pd))
               ? ((xx_spoon_installer *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_spoon_installer_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    spoon_stream *stream;
    xx_archive_record_state *state;
    if (!format) return NULL;
    stream = (spoon_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!spoon_locate(format, &stream->layout) ||
        !spoon_walk(format, &stream->layout, &stream->table, pd) ||
        stream->table.count == 0U) {
        spoon_stream_free(stream);
        return NULL;
    }
    stream->max_member = spoon_max_member(format, options);
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        spoon_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = spoon_stream_free;
    state->total_records = (int64_t)stream->table.count;
    if (!spoon_copy_options(&state->options, options) ||
        !spoon_set_record(&state->current_record, &stream->table.entries[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->current_index = 0;
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_spoon_installer_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_spoon_installer_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    spoon_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (spoon_stream *)state->internal_state) ||
        stream->index + 1U >= stream->table.count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    if (!spoon_set_record(&state->current_record,
                          &stream->table.entries[stream->index])) {
        state->has_record = false;
        return false;
    }
    state->current_index = (int64_t)stream->index;
    state->has_record = true;
    return true;
}

bool xx_spoon_installer_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    spoon_stream *stream;
    const spoon_entry *entry;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (spoon_stream *)state->internal_state) ||
        stream->index >= stream->table.count || (pd && xx_pd_is_stopped(pd)))
        return false;
    entry = &stream->table.entries[stream->index];
    path_option = xx_format_resolve_extra_parameter(
        format, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return spoon_unpack_entry(format, stream, entry, NULL, pd);
    if (!entry->extractable) return false;
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
               ? xx_str_concat3(base, "/", entry->name)
               : xx_str_concat(base, entry->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = spoon_unpack_entry(format, stream, entry, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_spoon_installer_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
