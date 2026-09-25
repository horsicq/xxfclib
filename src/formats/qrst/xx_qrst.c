/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the Compaq QRST diskette-set container (the ._01 / ._02
 * files shipped with Compaq SoftPaq driver sets).  XArchive has no module
 * for it and the format is undocumented; everything below was derived from
 * the 13 corpus samples and confirmed against F:\ARC\U3.exe, whose
 * Image1.img / Image2.img output this reader reproduces byte for byte.
 *
 * A file is one or more concatenated sections, each holding one floppy
 * image.  A section is a fixed 796-byte header:
 *
 *   0x00  4    "QRST"
 *   0x04  4    version as an IEEE-754 float: 1.0 in twelve samples, 5.0 in
 *              one.  Only 1.0 is implemented - see the note at the end.
 *   0x08  4    set identifier (varies per product, not interpreted)
 *   0x0C  1    diskette type: 3 = 720 KiB, 4 = 1.44 MiB.  This is what gives
 *              the geometry, and so the track size and the image size.
 *   0x0D  1    disk number within the set, 1 based
 *   0x0E  1    number of disks in the set
 *   0x0F  ...  product description, then the on-screen banner text, NUL
 *              padded to the full 796 bytes
 *
 * followed by one record per track, in cylinder-then-head order, until the
 * whole image has been produced:
 *
 *   [cylinder][head][0]              + one raw track, stored verbatim
 *   [cylinder][head][1][fill]        - the whole track is that one byte
 *   [cylinder][head][2][u16 size]    + `size` bytes of run-length data
 *
 * The run-length coding alternates, starting with a literal run: a count
 * byte then that many literal bytes, then a count byte and one byte to
 * repeat, and so on to the end of the record.  A track must produce exactly
 * the geometry's track size; anything else invalidates the archive.
 *
 * Version 5.0 keeps the same 796-byte header but replaces the track chain
 * with a 25-byte directory and one or two PKWARE DCL ("implode") streams:
 *
 *   0x00  1    unused
 *   0x01  4    offset of image 1, from the section start; always 0x335,
 *              which is the header plus this directory
 *   0x05  4    packed length of image 1
 *   0x09  4    CRC-32 of image 1's *packed* bytes (ISO-HDLC, the ZIP one)
 *   0x0D  4    offset of image 2, or zero when the section holds only one
 *   0x11  4    packed length of image 2
 *   0x15  4    CRC-32 of image 2's packed bytes
 *
 * and the section ends at max(off1 + size1, off2 + size2).  A stream whose
 * packed length equals the geometry's image size is stored rather than
 * imploded.  The DCL output is usually shorter than the geometry - the
 * producer stops after the last written sector - so the decoded length is
 * whatever the stream yields, not the nominal disk size.
 *
 * This was read out of U3's own handler (F:\utils\U3\src, format 543
 * "QRST": FUN_0064e990 recognition, FUN_0064eea0 walk, FUN_0064ed90 image,
 * FUN_0043e3d0 the codec, whose bit grammar - one flag bit, a 7-bit length
 * prefix code with a 0x207 end symbol, a 14-bit distance prefix code and a
 * 15-bit literal code over a 1/2/4 KiB window - is PKWARE DCL exactly).
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/qrst/xx_qrst.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

#ifdef QRST
#define XX_QRST_FILE_TYPE XX_FILE_TYPE_QRST
#else
#define XX_QRST_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define QRST_HEADER_SIZE 796
/* 1.0f as little endian IEEE-754. */
#define QRST_VERSION_1_0 UINT32_C(0x3F800000)
/* 5.0f as little endian IEEE-754. */
#define QRST_VERSION_5_0 UINT32_C(0x40A00000)
#define QRST_MAX_SECTIONS 64U
#define QRST_SECTOR_SIZE 512
/* The 5.0 image directory that follows the header, and the offset it always
 * carries for the first image: the two run back to back. */
#define QRST_V5_DIR_SIZE 25
#define QRST_V5_FIRST_OFFSET (QRST_HEADER_SIZE + QRST_V5_DIR_SIZE)

typedef struct qrst_section_s {
    char *name;
    int64_t header_offset;
    int64_t data_offset;
    int64_t data_size;    /* packed extent of the track chain */
    int64_t track_size;
    int64_t image_size;
    uint32_t version;     /* QRST_VERSION_1_0 or QRST_VERSION_5_0 */
    uint32_t packed_crc;  /* 5.0 only: CRC-32 of the packed extent */
    uint8_t disk_type;
    uint8_t disk_number;
    uint8_t disk_count;
} qrst_section;

typedef struct qrst_stream_s {
    qrst_section *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} qrst_stream;

static uint16_t qrst_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t qrst_le32(const uint8_t *bytes) {
    return (uint32_t)qrst_le16(bytes) |
           ((uint32_t)qrst_le16(bytes + 2U) << 16U);
}

static bool qrst_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* The standard DOS diskette geometries.  Only types 3 and 4 appear in the
 * corpus; 1 and 2 are the other two the same table covers, and an unknown
 * type makes the section invalid rather than defaulting to anything. */
static bool qrst_geometry(uint8_t disk_type, int64_t *track_size,
                          int64_t *image_size) {
    int cylinders, heads, sectors;
    switch (disk_type) {
        case 1U: cylinders = 40; heads = 2; sectors = 9; break;   /* 360 K */
        case 2U: cylinders = 80; heads = 2; sectors = 15; break;  /* 1.2 M */
        case 3U: cylinders = 80; heads = 2; sectors = 9; break;   /* 720 K */
        case 4U: cylinders = 80; heads = 2; sectors = 18; break;  /* 1.44M */
        default: return false;
    }
    *track_size = (int64_t)sectors * QRST_SECTOR_SIZE;
    *image_size = *track_size * cylinders * heads;
    return true;
}

static char *qrst_image_name(size_t index) {
    static const char prefix[] = "Image";
    static const char suffix[] = ".img";
    char digits[24];
    char *name;
    size_t count = 0U, at, value = index;
    do {
        digits[count++] = (char)('0' + (int)(value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    name = (char *)xx_mem_alloc(sizeof(prefix) - 1U + count +
                                sizeof(suffix));
    if (!name) return NULL;
    xx_rt_memcpy(name, prefix, sizeof(prefix) - 1U);
    for (at = 0U; at < count; ++at)
        name[sizeof(prefix) - 1U + at] = digits[count - 1U - at];
    xx_rt_memcpy(name + sizeof(prefix) - 1U + count, suffix, sizeof(suffix));
    return name;
}

static bool qrst_safe_output_name(const char *name) {
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':') return false;
    for (at = name; *at; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || c == '/' || c == '\\' || c < 0x20U)
            return false;
    }
    return true;
}

static void qrst_stream_free(void *opaque) {
    qrst_stream *stream = (qrst_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool qrst_add_section(qrst_stream *stream,
                             const qrst_section *section) {
    qrst_section *grown;
    if (!stream || !section || stream->count >= QRST_MAX_SECTIONS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (qrst_section *)xx_mem_realloc(stream->items,
                                           (stream->count + 1U) *
                                               sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *section;
    return true;
}

/* Measure the track chain without decoding it: parse() only needs to know
 * where the section ends. */
static bool qrst_measure_tracks(xx_io_device *device, int64_t base,
                                int64_t limit, int64_t track_size,
                                int64_t image_size, int64_t *packed_size) {
    int64_t cursor = 0, produced = 0;
    while (produced < image_size) {
        uint8_t record[5];
        uint8_t flag;
        if (limit - cursor < 3 ||
            !qrst_read_at(device, base + cursor, record, 3U)) return false;
        flag = record[2];
        if (flag == 0U) {
            if (limit - cursor - 3 < track_size) return false;
            cursor += 3 + track_size;
        } else if (flag == 1U) {
            if (limit - cursor < 4) return false;
            cursor += 4;
        } else if (flag == 2U) {
            int64_t size;
            if (limit - cursor < 5 ||
                !qrst_read_at(device, base + cursor, record, 5U))
                return false;
            size = (int64_t)qrst_le16(record + 3U);
            /* Bound the declared record against what is actually there
             * before it is used for anything. */
            if (limit - cursor - 5 < size) return false;
            cursor += 5 + size;
        } else {
            return false;
        }
        produced += track_size;
    }
    if (produced != image_size) return false;
    *packed_size = cursor;
    return true;
}

static bool qrst_parse(Abstractformat *format, qrst_stream **result) {
    qrst_stream *stream = NULL;
    int64_t total, size, cursor;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < QRST_HEADER_SIZE) return false;
    stream = (qrst_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    cursor = 0;
    while (cursor < size) {
        uint8_t header[16];
        qrst_section section;
        int64_t track_size, image_size, packed_size = 0;
        uint32_t version;
        if (size - cursor < QRST_HEADER_SIZE ||
            !qrst_read_at(format->device, format->base_address + cursor,
                          header, sizeof(header)) ||
            xx_rt_memcmp(header, "QRST", 4U) != 0) goto fail;
        version = qrst_le32(header + 4U);
        if (version != QRST_VERSION_1_0 && version != QRST_VERSION_5_0)
            goto fail;
        if (!qrst_geometry(header[12], &track_size, &image_size)) goto fail;
        if (version == QRST_VERSION_5_0) {
            uint8_t directory[QRST_V5_DIR_SIZE];
            int64_t remaining = size - cursor, extent = 0;
            size_t which;
            if (remaining < QRST_V5_FIRST_OFFSET ||
                !qrst_read_at(format->device,
                              format->base_address + cursor +
                                  QRST_HEADER_SIZE,
                              directory, sizeof(directory))) goto fail;
            /* Two directory slots of twelve bytes each; the second is
             * present only when its offset is non-zero. */
            for (which = 0U; which < 2U; ++which) {
                int64_t offset =
                    (int64_t)qrst_le32(directory + 1U + which * 12U);
                int64_t packed =
                    (int64_t)qrst_le32(directory + 5U + which * 12U);
                uint32_t crc = qrst_le32(directory + 9U + which * 12U);
                if (which != 0U && offset == 0) break;
                /* Both halves of the declared extent are bounded against
                 * what the file really holds before either is recorded. */
                if (offset < QRST_V5_FIRST_OFFSET || offset > remaining ||
                    packed > remaining - offset) goto fail;
                if (offset + packed > extent) extent = offset + packed;
                xx_mem_zero(&section, sizeof(section));
                section.name = qrst_image_name(stream->count + 1U);
                if (!section.name) goto fail;
                section.header_offset = format->base_address + cursor;
                section.data_offset = format->base_address + cursor + offset;
                section.data_size = packed;
                section.track_size = track_size;
                section.image_size = image_size;
                section.version = version;
                section.packed_crc = crc;
                section.disk_type = header[12];
                section.disk_number = header[13];
                section.disk_count = header[14];
                if (!qrst_add_section(stream, &section)) {
                    xx_str_free(section.name);
                    goto fail;
                }
            }
            /* A section can never be shorter than its own header plus this
             * directory, so the walk always makes progress. */
            if (extent < QRST_V5_FIRST_OFFSET) goto fail;
            cursor += extent;
            continue;
        }
        if (!qrst_measure_tracks(format->device,
                                 format->base_address + cursor +
                                     QRST_HEADER_SIZE,
                                 size - cursor - QRST_HEADER_SIZE,
                                 track_size, image_size, &packed_size))
            goto fail;
        xx_mem_zero(&section, sizeof(section));
        section.name = qrst_image_name(stream->count + 1U);
        if (!section.name) goto fail;
        section.header_offset = format->base_address + cursor;
        section.data_offset = format->base_address + cursor +
                              QRST_HEADER_SIZE;
        section.data_size = packed_size;
        section.track_size = track_size;
        section.image_size = image_size;
        section.version = version;
        section.disk_type = header[12];
        section.disk_number = header[13];
        section.disk_count = header[14];
        if (!qrst_add_section(stream, &section)) {
            xx_str_free(section.name);
            goto fail;
        }
        cursor += QRST_HEADER_SIZE + packed_size;
    }
    /* Sections must tile the file exactly. */
    if (stream->count == 0U || cursor != size) goto fail;
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    qrst_stream_free(stream);
    return false;
}

static bool qrst_copy_options(xx_list_s *destination,
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

static const xx_var *qrst_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool qrst_set_record(xx_archive_record *record,
                            const qrst_section *section) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = section->header_offset;
    record->header_size = QRST_HEADER_SIZE;
    record->data_offset = section->data_offset;
    record->compressed_size = section->data_size;
    return xx_archive_record_set_original_name(record, section->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)section->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)section->image_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          section->disk_type) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false);
}

/* A 5.0 image is a PKWARE DCL stream, or - when its packed length is exactly
 * the geometry's length - the image stored verbatim.  The directory's CRC-32
 * covers the *packed* bytes, so it is the one thing the file asserts about
 * this extent and it is checked before anything is decoded; the stream then
 * has to end on its own end symbol having consumed the whole extent, or
 * nothing is emitted at all. */
static bool qrst_decode_dcl(Abstractformat *format,
                            const qrst_section *section, uint8_t **plain,
                            size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t packed_size, produced = 0U, consumed = 0U, written = 0U;
    if (section->data_size <= 0 || section->image_size <= 0 ||
        (uint64_t)section->data_size > (uint64_t)SIZE_MAX ||
        (uint64_t)section->image_size > (uint64_t)SIZE_MAX) return false;
    packed_size = (size_t)section->data_size;
    packed = (uint8_t *)xx_mem_alloc(packed_size);
    if (!packed ||
        !qrst_read_at(format->device, section->data_offset, packed,
                      packed_size) ||
        xx_crc32(XX_CRC_TYPE_CRC32, packed, packed_size) !=
            section->packed_crc) goto fail;
    if (section->data_size == section->image_size) {
        *plain = packed;
        *plain_size = packed_size;
        return true;
    }
    /* The stream carries no plaintext length, so it is measured first and
     * the buffer sized to what it will really produce: the image stops at
     * the last written sector and so is normally shorter than the nominal
     * disk. */
    if (!xx_dcl_scan_memory(packed, packed_size, (size_t)section->image_size,
                            &consumed, &produced) ||
        consumed != packed_size || produced == 0U ||
        produced > (size_t)section->image_size) goto fail;
    output = (uint8_t *)xx_mem_alloc(produced);
    if (!output ||
        !xx_dcl_decode_memory(packed, packed_size, output, produced,
                              &written) ||
        written != produced) goto fail;
    xx_mem_free(packed);
    *plain = output;
    *plain_size = produced;
    return true;
fail:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return false;
}

/* Decode one section's track chain into a whole image. */
static bool qrst_decode(Abstractformat *format, const qrst_section *section,
                        uint8_t **plain, size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    int64_t produced = 0, cursor = 0;
    if (!format || !section || !plain || !plain_size ||
        section->data_size <= 0 ||
        (uint64_t)section->image_size > (uint64_t)SIZE_MAX ||
        (uint64_t)section->data_size > (uint64_t)SIZE_MAX) return false;
    if (section->version == QRST_VERSION_5_0)
        return qrst_decode_dcl(format, section, plain, plain_size);
    packed = (uint8_t *)xx_mem_alloc((size_t)section->data_size);
    output = (uint8_t *)xx_mem_alloc((size_t)section->image_size);
    if (!packed || !output ||
        !qrst_read_at(format->device, section->data_offset, packed,
                      (size_t)section->data_size)) goto fail;
    while (produced < section->image_size) {
        uint8_t flag;
        if (section->data_size - cursor < 3) goto fail;
        flag = packed[cursor + 2];
        if (flag == 0U) {
            if (section->data_size - cursor - 3 < section->track_size)
                goto fail;
            xx_rt_memcpy(output + produced, packed + cursor + 3,
                         (size_t)section->track_size);
            cursor += 3 + section->track_size;
        } else if (flag == 1U) {
            if (section->data_size - cursor < 4) goto fail;
            xx_rt_memset(output + produced, packed[cursor + 3],
                         (size_t)section->track_size);
            cursor += 4;
        } else if (flag == 2U) {
            int64_t size, at, out = 0;
            bool literal = true;
            if (section->data_size - cursor < 5) goto fail;
            size = (int64_t)qrst_le16(packed + cursor + 3);
            if (section->data_size - cursor - 5 < size) goto fail;
            at = cursor + 5;
            while (at < cursor + 5 + size) {
                int64_t count = (int64_t)packed[at++];
                if (out + count > section->track_size) goto fail;
                if (literal) {
                    if (at + count > cursor + 5 + size) goto fail;
                    if (count != 0)
                        xx_rt_memcpy(output + produced + out, packed + at,
                                     (size_t)count);
                    at += count;
                } else {
                    if (at >= cursor + 5 + size) goto fail;
                    if (count != 0)
                        xx_rt_memset(output + produced + out, packed[at],
                                     (size_t)count);
                    ++at;
                }
                out += count;
                literal = !literal;
            }
            /* A track that does not fill exactly is a decode failure, not
             * something to pad. */
            if (out != section->track_size) goto fail;
            cursor += 5 + size;
        } else {
            goto fail;
        }
        produced += section->track_size;
    }
    if (produced != section->image_size || cursor != section->data_size)
        goto fail;
    xx_mem_free(packed);
    *plain = output;
    *plain_size = (size_t)section->image_size;
    return true;
fail:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return false;
}

void xx_qrst_init(xx_qrst *archive, xx_io_device *device,
                  int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_QRST_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-compaq-qrst");
    xx_format_set_extension(&archive->format, "_01");
    archive->format.check_is_valid = xx_qrst_check_is_valid;
    archive->format.handle_base_info = xx_qrst_handle_base_info;
    archive->format.get_format_size = xx_qrst_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_qrst_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_qrst_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_qrst_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_qrst_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_qrst_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_qrst_free_archive_records_reading;
}

xx_qrst *xx_qrst_create(xx_io_device *device, int64_t base_address) {
    xx_qrst *archive = (xx_qrst *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_qrst_init(archive, device, base_address);
    return archive;
}

void xx_qrst_destroy(xx_qrst *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_qrst_free(xx_qrst *archive) {
    if (!archive) return;
    xx_qrst_destroy(archive);
    xx_mem_free(archive);
}

bool xx_qrst_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    qrst_stream *stream;
    (void)pd;
    if (!qrst_parse(format, &stream)) return false;
    qrst_stream_free(stream);
    return true;
}

bool xx_qrst_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    qrst_stream *stream;
    xx_qrst *archive;
    (void)pd;
    if (!format || !qrst_parse(format, &stream)) return false;
    archive = (xx_qrst *)format;
    archive->number_of_records = stream->count;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    qrst_stream_free(stream);
    return true;
}

int64_t xx_qrst_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_qrst_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_qrst_get_number_of_archive_records(Abstractformat *format,
                                               xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_qrst_handle_base_info(format, pd))
               ? ((xx_qrst *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_qrst_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    qrst_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!qrst_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        qrst_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = qrst_stream_free;
    state->total_records = stream->count;
    if (!qrst_copy_options(&state->options, options) ||
        !qrst_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_qrst_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_qrst_archive_record_move_to_next(Abstractformat *format,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    qrst_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (qrst_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = qrst_set_record(&state->current_record,
                                        &stream->items[stream->index]);
    return state->has_record;
}

bool xx_qrst_unpack_current_archive_record(Abstractformat *format,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    qrst_stream *stream;
    qrst_section *section;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (qrst_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    section = &stream->items[stream->index];
    if (!qrst_safe_output_name(section->name) ||
        !qrst_decode(format, section, &plain, &plain_size)) goto done;
    path_option = qrst_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
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
               ? xx_str_concat3(base, "/", section->name)
               : xx_str_concat(base, section->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = true;
        while (written < plain_size) {
            ssize_t amount = xx_io_write(destination, plain + written,
                                         plain_size - written);
            if (amount <= 0 || (size_t)amount > plain_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_qrst_free_archive_records_reading(Abstractformat *format,
                                          xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
