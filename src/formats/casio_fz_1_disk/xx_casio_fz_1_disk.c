/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Casio FZ-1 floppy image - the raw sector dump of a disk written by the
 * FZ-1 / FZ-10M / FZ-20M sampler.  xx_casio_fz_1_disk.h carries the field
 * table.  Written from Casio's "Model FZ-1 Data Structures" (1987), section
 * 1 "Disk"; the only change from that text is the order of the three counts
 * at the end of a file head, which the machine writes as bank, voice, wave.
 *
 * THERE IS NO MAGIC.  Sector 0 carries four fixed bytes after the disk name
 * (00 00 02 00) and four zero bytes after the password, and its allocation
 * table always marks sectors 0 and 1 as used.  With the exact image size those
 * make the gate; the directory then has to hold together as well: every entry
 * up to the first blank one must name a legal content type and a head sector
 * whose data block pointers are well-formed ranges inside the disk that never
 * visit a sector twice.  An entry that fails this after a blank entry is
 * stale directory space and is skipped instead.  A formatted disk with no
 * files is still an FZ-1 disk and is accepted with no members.
 *
 * The allocation table is deliberately not required to cover every data
 * sector: images written by third-party tools are not trusted to keep it, and
 * the machine itself only follows the pointers.
 *
 * Everything read from the image is bounded by the geometry: one pass reads
 * sectors 0 and 1, then at most 64 file heads; a file is at most 1278 sectors
 * and is streamed one sector at a time, so nothing is allocated from a
 * length field.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/casio_fz_1_disk/xx_casio_fz_1_disk.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Self-healing shim: the file compiles before the enum exists. */
#ifdef CASIO_FZ_1_DISK
#define XX_CASIO_FZ_1_DISK_FILE_TYPE XX_FILE_TYPE_CASIO_FZ_1_DISK
#else
#define XX_CASIO_FZ_1_DISK_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define FZ1_SECTOR_SIZE XX_CASIO_FZ_1_DISK_SECTOR_SIZE
#define FZ1_SECTOR_COUNT XX_CASIO_FZ_1_DISK_SECTOR_COUNT
#define FZ1_IMAGE_SIZE ((int64_t)XX_CASIO_FZ_1_DISK_IMAGE_SIZE)
#define FZ1_FIRST_DATA_SECTOR 2U
#define FZ1_CAT_OFFSET 0x80U
#define FZ1_DIR_ENTRIES 64U
#define FZ1_ENTRY_SIZE 16U
#define FZ1_NAME_SIZE 12U
#define FZ1_DBP_COUNT 64U
#define FZ1_COUNTS_OFFSET 0x3FAU
#define FZ1_MAX_TYPE 5U
#define FZ1_MAX_PART 1U
#define FZ1_MAX_BANKS 8U
#define FZ1_MAX_VOICES 64U
#define FZ1_TYPE_SEQUENCE 4U
/* Room for "_" + 12 name bytes + "~64" + ".disk2" + ".fzf" + NUL. */
#define FZ1_OUT_NAME_SIZE 40U

typedef struct fz1_member_s {
    char *name;
    int64_t head_offset;
    int64_t data_offset;
    int64_t size;
    uint16_t head_sector;
    uint16_t extent_count;
    uint16_t starts[FZ1_DBP_COUNT];
    uint16_t ends[FZ1_DBP_COUNT];
    bool skip_head; /**< The first range starts with the head sector. */
    uint8_t type;
    uint8_t part;
    uint16_t banks;
    uint16_t voices;
    uint16_t waves;
} fz1_member;

typedef struct fz1_stream_s {
    fz1_member *items;
    size_t count;
    size_t index;
    char label[13];
} fz1_stream;

static void fz1_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static uint16_t fz1_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static bool fz1_read_at(Abstractformat *self, int64_t offset, uint8_t *buffer,
                        size_t size) {
    size_t done = 0U;
    if (!self || !self->device || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(self->device, buffer + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static void fz1_stream_free(void *pointer) {
    fz1_stream *stream = (fz1_stream *)pointer;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static char fz1_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool fz1_same_name(const char *left, const char *right) {
    while (*left && *right) {
        if (fz1_upper(*left) != fz1_upper(*right)) return false;
        ++left;
        ++right;
    }
    return *left == *right;
}

/* True when name[0..stem) spells @p word, ignoring case. */
static bool fz1_stem_is(const char *name, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index) {
        if (!word[index] || fz1_upper(name[index]) != word[index]) return false;
    }
    return word[stem] == 0;
}

/* Windows resolves these to devices with or without an extension. */
static bool fz1_is_device_name(const char *name) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t stem = 0U, index;
    while (name[stem] && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index) {
        if (fz1_stem_is(name, stem, devices[index])) return true;
    }
    return stem == 4U && name[3] >= '0' && name[3] <= '9' &&
           ((fz1_upper(name[0]) == 'C' && fz1_upper(name[1]) == 'O' &&
             fz1_upper(name[2]) == 'M') ||
            (fz1_upper(name[0]) == 'L' && fz1_upper(name[1]) == 'P' &&
             fz1_upper(name[2]) == 'T'));
}

/* One output character for a name byte: separators, reserved punctuation,
 * control and non-ASCII bytes all become '_'. */
static char fz1_safe_char(uint8_t value) {
    if (value < 0x20U || value > 0x7EU) return '_';
    if (value == '/' || value == '\\' || value == ':' || value == '*' ||
        value == '?' || value == '"' || value == '<' || value == '>' ||
        value == '|') {
        return '_';
    }
    return (char)value;
}

static void fz1_append(char *out, size_t *length, const char *text) {
    while (*text && *length + 1U < FZ1_OUT_NAME_SIZE) out[(*length)++] = *text++;
    out[*length] = 0;
}

/* "<name>[~N][.disk2].<ext>", unique (case-insensitively) among the members
 * already published.  The stem never holds a separator, so the result is a
 * single path component; it always ends in an extension, so it can never be
 * "." or "..". */
static char *fz1_member_name(const fz1_stream *stream, const uint8_t *entry,
                             unsigned slot) {
    static const char *const extensions[] = {"fzf", "fzv", "fzb",
                                             "fze", "fzs", "fzp"};
    char stem[FZ1_OUT_NAME_SIZE];
    char candidate[FZ1_OUT_NAME_SIZE];
    size_t begin = 0U, end = 0U, length = 0U, index;
    unsigned attempt;

    while (end < FZ1_NAME_SIZE && entry[end] != 0U) ++end;
    while (end > 0U && entry[end - 1U] == ' ') --end;
    while (begin < end && entry[begin] == ' ') ++begin;
    if (begin == end) {
        (void)xx_rt_snprintf(stem, sizeof(stem), "FILE%02u", slot);
        length = xx_str_len(stem);
    } else {
        for (index = begin; index < end; ++index) {
            stem[length++] = fz1_safe_char(entry[index]);
        }
        stem[length] = 0;
    }
    if (fz1_is_device_name(stem)) {
        /* Shift right by one, terminator included; the buffers overlap. */
        for (index = length + 1U; index > 0U; --index) {
            stem[index] = stem[index - 1U];
        }
        stem[0] = '_';
        ++length;
    }
    for (attempt = 0U; attempt <= FZ1_DIR_ENTRIES; ++attempt) {
        char number[8];
        size_t used = 0U;
        bool clash = false;
        candidate[0] = 0;
        fz1_append(candidate, &used, stem);
        if (attempt != 0U) {
            (void)xx_rt_snprintf(number, sizeof(number), "~%u", attempt);
            fz1_append(candidate, &used, number);
        }
        if (entry[13] != 0U) fz1_append(candidate, &used, ".disk2");
        fz1_append(candidate, &used, ".");
        fz1_append(candidate, &used, extensions[entry[12]]);
        for (index = 0U; index < stream->count; ++index) {
            if (fz1_same_name(stream->items[index].name, candidate)) {
                clash = true;
                break;
            }
        }
        if (!clash) return xx_str_dup(candidate);
    }
    return NULL;
}

/* ---------------------------------------------------------------- parse -- */

/* Decode one live directory entry.  False means the entry does not describe
 * an FZ-1 file: an unknown type or part, a head outside the data area, a
 * control byte in the name, counts larger than the machine's memory holds,
 * or a pointer list that is empty, runs backwards, leaves the disk or visits
 * a sector twice. */
static bool fz1_read_entry(Abstractformat *self, const uint8_t *entry,
                           fz1_member *member) {
    uint8_t head[FZ1_SECTOR_SIZE];
    uint8_t seen[FZ1_SECTOR_COUNT / 8U];
    uint32_t sectors = 0U;
    unsigned index;
    uint16_t sloc;

    xx_mem_zero(member, sizeof(*member));
    member->type = entry[12];
    member->part = entry[13];
    sloc = fz1_le16(entry + 14);
    if (member->type > FZ1_MAX_TYPE || member->part > FZ1_MAX_PART ||
        sloc < FZ1_FIRST_DATA_SECTOR || sloc >= FZ1_SECTOR_COUNT) {
        return false;
    }
    for (index = 0U; index < FZ1_NAME_SIZE && entry[index] != 0U; ++index) {
        if (entry[index] < 0x20U || entry[index] == 0x7FU) return false;
    }
    if (!fz1_read_at(self,
                     self->base_address + (int64_t)sloc * FZ1_SECTOR_SIZE,
                     head, sizeof(head))) {
        return false;
    }
    member->head_sector = sloc;
    member->banks = fz1_le16(head + FZ1_COUNTS_OFFSET);
    member->voices = fz1_le16(head + FZ1_COUNTS_OFFSET + 2U);
    member->waves = fz1_le16(head + FZ1_COUNTS_OFFSET + 4U);
    if (member->type != FZ1_TYPE_SEQUENCE &&
        (member->banks > FZ1_MAX_BANKS || member->voices > FZ1_MAX_VOICES)) {
        return false;
    }

    xx_mem_zero(seen, sizeof(seen));
    for (index = 0U; index < FZ1_DBP_COUNT; ++index) {
        uint16_t start = fz1_le16(head + index * 4U);
        uint16_t stop = fz1_le16(head + index * 4U + 2U);
        uint32_t sector;
        if (start == 0U && stop == 0U) break;
        if (start < FZ1_FIRST_DATA_SECTOR || stop < start ||
            stop >= FZ1_SECTOR_COUNT) {
            return false;
        }
        /* Each step marks a new sector or fails, so this runs at most
         * 1278 times per file whatever the pointers say. */
        for (sector = start; sector <= stop; ++sector) {
            uint8_t bit = (uint8_t)(1U << (sector & 7U));
            if (seen[sector >> 3U] & bit) return false;
            seen[sector >> 3U] |= bit;
        }
        member->starts[member->extent_count] = start;
        member->ends[member->extent_count] = stop;
        ++member->extent_count;
        sectors += (uint32_t)(stop - start) + 1U;
    }
    if (member->extent_count == 0U) return false;

    member->skip_head = member->starts[0] == sloc;
    /* A head that turns up in the middle of its own data is not a file. */
    if (!member->skip_head && (seen[sloc >> 3U] & (1U << (sloc & 7U)))) {
        return false;
    }
    if (member->skip_head) --sectors;
    member->size = (int64_t)sectors * FZ1_SECTOR_SIZE;
    member->head_offset = self->base_address + (int64_t)sloc * FZ1_SECTOR_SIZE;
    if (!member->skip_head) {
        member->data_offset =
            self->base_address + (int64_t)member->starts[0] * FZ1_SECTOR_SIZE;
    } else if (member->ends[0] > member->starts[0]) {
        member->data_offset = self->base_address +
                              ((int64_t)member->starts[0] + 1) * FZ1_SECTOR_SIZE;
    } else if (member->extent_count > 1U) {
        member->data_offset =
            self->base_address + (int64_t)member->starts[1] * FZ1_SECTOR_SIZE;
    } else {
        member->data_offset = member->head_offset + FZ1_SECTOR_SIZE;
    }
    return true;
}

/* The disk ID and allocation-table facts every FZ-1 head sector holds. */
static bool fz1_head_ok(const uint8_t *sector0) {
    unsigned index;
    for (index = 0U; index < FZ1_NAME_SIZE; ++index) {
        uint8_t c = sector0[index];
        if (c != 0U && (c < 0x20U || c > 0x7EU) && (c < 0xA1U || c > 0xDFU)) {
            return false;
        }
    }
    return sector0[12] == 0U && sector0[13] == 0U && sector0[14] == 2U &&
           sector0[15] == 0U && sector0[28] == 0U && sector0[29] == 0U &&
           sector0[30] == 0U && sector0[31] == 0U &&
           (sector0[FZ1_CAT_OFFSET] & 0x03U) == 0x03U;
}

static fz1_stream *fz1_parse(Abstractformat *self, xx_pd_struct *pd) {
    uint8_t system[2U * FZ1_SECTOR_SIZE];
    fz1_stream *stream;
    bool after_blank = false;
    unsigned slot;
    size_t length;

    if (!self || !self->device || self->base_address < 0) return NULL;
    {
        int64_t total = xx_io_total_size(self->device);
        /* A raw dump has exactly one size; anything else is not this. */
        if (total < self->base_address ||
            total - self->base_address != FZ1_IMAGE_SIZE) {
            return NULL;
        }
    }
    if (!fz1_read_at(self, self->base_address, system, sizeof(system)) ||
        !fz1_head_ok(system)) {
        return NULL;
    }
    stream = (fz1_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));
    stream->items =
        (fz1_member *)xx_mem_alloc(sizeof(fz1_member) * FZ1_DIR_ENTRIES);
    if (!stream->items) goto fail;

    length = FZ1_NAME_SIZE;
    while (length > 0U &&
           (system[length - 1U] == ' ' || system[length - 1U] == 0U)) {
        --length;
    }
    for (slot = 0U; slot < length; ++slot) {
        stream->label[slot] = system[slot] == 0U ? ' ' : (char)system[slot];
    }
    stream->label[length] = 0;

    for (slot = 0U; slot < FZ1_DIR_ENTRIES; ++slot) {
        const uint8_t *entry =
            system + FZ1_SECTOR_SIZE + (size_t)slot * FZ1_ENTRY_SIZE;
        fz1_member member;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (entry[0] == 0U) {
            after_blank = true;
            continue;
        }
        if (!fz1_read_entry(self, entry, &member)) {
            if (after_blank) continue;
            goto fail;
        }
        member.name = fz1_member_name(stream, entry, slot);
        if (!member.name) goto fail;
        stream->items[stream->count++] = member;
    }
    return stream;

fail:
    fz1_stream_free(stream);
    return NULL;
}

/* -------------------------------------------------------------- extract -- */

/* Copy the member's sectors, in pointer order and without the head, to
 * @p output; with no output the sectors are only read. */
static bool fz1_copy(Abstractformat *self, const fz1_member *member,
                     xx_io_device *output, xx_pd_struct *pd) {
    uint8_t sector_data[FZ1_SECTOR_SIZE];
    int64_t written = 0;
    unsigned extent;

    for (extent = 0U; extent < member->extent_count; ++extent) {
        uint32_t sector = member->starts[extent];
        if (extent == 0U && member->skip_head) ++sector;
        for (; sector <= member->ends[extent]; ++sector) {
            size_t done = 0U;
            if (pd && xx_pd_is_stopped(pd)) return false;
            if (sector >= FZ1_SECTOR_COUNT ||
                !fz1_read_at(self,
                             self->base_address +
                                 (int64_t)sector * FZ1_SECTOR_SIZE,
                             sector_data, sizeof(sector_data))) {
                return false;
            }
            while (output && done < sizeof(sector_data)) {
                ssize_t sent = xx_io_write(output, sector_data + done,
                                           sizeof(sector_data) - done);
                if (sent <= 0 || (size_t)sent > sizeof(sector_data) - done) {
                    return false;
                }
                done += (size_t)sent;
            }
            written += FZ1_SECTOR_SIZE;
        }
    }
    return written == member->size;
}

/* ------------------------------------------------------------ lifecycle -- */

void xx_casio_fz_1_disk_init(xx_casio_fz_1_disk *archive, xx_io_device *device,
                             int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_CASIO_FZ_1_DISK_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-casio-fz1-disk");
    xx_format_set_extension(&archive->format, "img");
    archive->format.check_is_valid = xx_casio_fz_1_disk_check_is_valid;
    archive->format.handle_base_info = xx_casio_fz_1_disk_handle_base_info;
    archive->format.get_format_size = xx_casio_fz_1_disk_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_casio_fz_1_disk_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_casio_fz_1_disk_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_casio_fz_1_disk_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_casio_fz_1_disk_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_casio_fz_1_disk_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_casio_fz_1_disk_free_archive_records_reading;
    archive->format.destroy = fz1_vtable_destroy;
}

xx_casio_fz_1_disk *xx_casio_fz_1_disk_create(xx_io_device *device,
                                              int64_t base_address) {
    xx_casio_fz_1_disk *archive =
        (xx_casio_fz_1_disk *)xx_mem_alloc(sizeof(*archive));
    if (!archive) return NULL;
    xx_casio_fz_1_disk_init(archive, device, base_address);
    return archive;
}

void xx_casio_fz_1_disk_destroy(xx_casio_fz_1_disk *archive) {
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_casio_fz_1_disk_free(xx_casio_fz_1_disk *archive) {
    if (!archive) return;
    xx_casio_fz_1_disk_destroy(archive);
    xx_mem_free(archive);
}

static void fz1_vtable_destroy(Abstractformat *self) {
    xx_casio_fz_1_disk_destroy((xx_casio_fz_1_disk *)self);
}

/* --------------------------------------------------------------- format -- */

bool xx_casio_fz_1_disk_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd) {
    fz1_stream *stream;
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = fz1_parse(self, pd);
    if (!stream) return false;
    fz1_stream_free(stream);
    return true;
}

bool xx_casio_fz_1_disk_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_casio_fz_1_disk *archive = (xx_casio_fz_1_disk *)self;
    fz1_stream *stream;
    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    self->base_info_handled = true;
    stream = fz1_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = FZ1_IMAGE_SIZE;
    self->number_of_archive_records = stream->count;
    self->file_type = XX_CASIO_FZ_1_DISK_FILE_TYPE;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    archive->number_of_records = stream->count;
    xx_rt_memcpy(archive->label, stream->label, sizeof(archive->label));
    fz1_stream_free(stream);
    return true;
}

int64_t xx_casio_fz_1_disk_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_casio_fz_1_disk_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_casio_fz_1_disk *)self)->number_of_records
                          : 0U;
}

/* -------------------------------------------------------------- records -- */

static bool fz1_set_record(xx_archive_record *record,
                           const fz1_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->head_offset;
    record->header_size = FZ1_SECTOR_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD, 0U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool fz1_copy_options(xx_list_s *target, const xx_list_s *options) {
    size_t index;
    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) ||
            !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *fz1_get_option(const xx_list_s *options,
                                    uint32_t meta_id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_casio_fz_1_disk_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    fz1_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = fz1_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        fz1_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = fz1_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!fz1_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !fz1_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_casio_fz_1_disk_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_casio_fz_1_disk_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    fz1_stream *stream;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (fz1_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record =
        fz1_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_casio_fz_1_disk_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd) {
    fz1_stream *stream;
    const fz1_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    xx_io_device *output;
    size_t base_length;
    bool result;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (fz1_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!member->name || !member->name[0]) return false;

    path_option = fz1_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return fz1_copy(self, member, NULL, pd);
    if (path_option->type == XX_VAR_TYPE_STRING ||
        path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING ||
               path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    base_length = xx_str_len(base_path);
    if (base_length != 0U && base_path[base_length - 1U] != '/' &&
        base_path[base_length - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;
    if (!xx_store_create_dirs_a(target_path, false)) {
        xx_str_free(target_path);
        return false;
    }
    output = xx_io_file_open(target_path, "wb");
    created = output != NULL;
    if (!output) {
        xx_str_free(target_path);
        return false;
    }
    result = fz1_copy(self, member, output, pd);
    if (xx_io_close(output) != 0) result = false;
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_casio_fz_1_disk_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
