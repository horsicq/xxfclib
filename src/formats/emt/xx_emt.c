/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * "EMT" compressed diskette image.  The four header tests are U3's own
 * recognition predicate (FUN_006716d0) reproduced exactly; the EBCDIC banner
 * at 0x40 is decoded for display. Track RLE and floppy geometry are checked
 * before publishing one disk-image member.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/emt/xx_emt.h"

#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

#define EMT_MAX_PACKED (64U * 1024U * 1024U)
#define EMT_MAX_IMAGE (32U * 1024U * 1024U)
#define EMT_TRACK_BYTES 9216U
#define EMT_RECORD_BYTES (126U + EMT_TRACK_BYTES + 2U)

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as EMT is registered there. */
#ifdef EMT
#define XX_EMT_FILE_TYPE XX_FILE_TYPE_EMT
#else
#define XX_EMT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* The u32 U3 requires at 0x5c, written out as bytes so the test does not
 * depend on the host's byte order. */
static const uint8_t xx_emt_marker[4] = {0x6cU, 0x02U, 0x6eU, 0x34U};

typedef struct emt_parsed_s {
    int64_t total_size;
    char banner[XX_EMT_BANNER_SIZE + 1];
} emt_parsed;

static void xx_emt_vtable_destroy(Abstractformat *self);

/* ----------------------------------------------------------- EBCDIC ---- */

/* IBM code page 037, the only part of it these headers use: letters, digits,
 * space and the few punctuation marks that appear in the banners.  Anything
 * outside that becomes '?' rather than being dropped, so the length of the
 * decoded banner always matches the field. */
static char emt_from_ebcdic(uint8_t value) {
    if (value >= 0xc1U && value <= 0xc9U) return (char)('A' + (value - 0xc1U));
    if (value >= 0xd1U && value <= 0xd9U) return (char)('J' + (value - 0xd1U));
    if (value >= 0xe2U && value <= 0xe9U) return (char)('S' + (value - 0xe2U));
    if (value >= 0x81U && value <= 0x89U) return (char)('a' + (value - 0x81U));
    if (value >= 0x91U && value <= 0x99U) return (char)('j' + (value - 0x91U));
    if (value >= 0xa2U && value <= 0xa9U) return (char)('s' + (value - 0xa2U));
    if (value >= 0xf0U && value <= 0xf9U) return (char)('0' + (value - 0xf0U));
    switch (value) {
        case 0x40U: return ' ';
        case 0x4bU: return '.';
        case 0x4cU: return '<';
        case 0x4dU: return '(';
        case 0x4eU: return '+';
        case 0x50U: return '&';
        case 0x5cU: return '*';
        case 0x5dU: return ')';
        case 0x60U: return '-';
        case 0x61U: return '/';
        case 0x6bU: return ',';
        case 0x6eU: return '>';
        case 0x6fU: return '?';
        case 0x7aU: return ':';
        case 0x7dU: return '\'';
        case 0x7eU: return '=';
        case 0x7fU: return '"';
        case 0x00U: return ' ';
        default: return '?';
    }
}

/* ------------------------------------------------------------- helpers -- */

/* All positioning goes through seek64: an image can sit at an arbitrary
 * offset inside a larger dump, and long is 32-bit on Win64. */
static bool emt_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount =
            xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* --------------------------------------------------------------- parse -- */

static bool emt_parse(Abstractformat *format, emt_parsed *parsed,
                      xx_pd_struct *pd) {
    uint8_t header[XX_EMT_HEADER_SIZE];
    int64_t span;
    size_t index, length;

    if (parsed) {
        xx_mem_zero(parsed, sizeof(*parsed));
        parsed->total_size = -1;
    }
    if (!format || !format->device || !parsed || format->base_address < 0 ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    parsed->total_size = xx_io_total_size(format->device);
    if (parsed->total_size < format->base_address) return false;
    span = parsed->total_size - format->base_address;
    /* The header itself plus at least one payload byte. */
    if (span <= (int64_t)XX_EMT_HEADER_SIZE) return false;
    if (!emt_read_at(format->device, format->base_address, header,
                     sizeof(header)))
        return false;

    /* U3's four tests, in its own order. */
    if (header[0x00] != '\\') return false;
    if (header[0x02] != 'z') return false;
    if (header[0x58] != '1') return false;
    if (xx_rt_memcmp(header + 0x5c, xx_emt_marker, sizeof(xx_emt_marker)) != 0)
        return false;

    for (index = 0U; index < XX_EMT_BANNER_SIZE; ++index) {
        parsed->banner[index] =
            emt_from_ebcdic(header[XX_EMT_BANNER_OFFSET + index]);
    }
    parsed->banner[XX_EMT_BANNER_SIZE] = 0;
    /* Trim the EBCDIC space padding at both ends. */
    length = XX_EMT_BANNER_SIZE;
    while (length > 0U && parsed->banner[length - 1U] == ' ') --length;
    parsed->banner[length] = 0;
    for (index = 0U; index < length && parsed->banner[index] == ' '; ++index) {
    }
    if (index != 0U) {
        size_t move;
        for (move = 0U; move + index <= length; ++move)
            parsed->banner[move] = parsed->banner[move + index];
    }
    return true;
}

/* Each physical track begins with a 126-byte descriptor, followed by 9216
 * disk bytes. Non-final records also carry the next record's two CRC bytes.
 * F1 is an RLE escape: [F1][value][count]. This decoding and the BPB geometry
 * checks are independently byte-verified against the TSENG_D1.EMT corpus
 * reference image. */
static bool emt_decode_record(const uint8_t *packed, size_t packed_size,
                              size_t *position, uint8_t *record,
                              size_t expected) {
    size_t out = 0U, at;
    if (!packed || !position || !record || *position > packed_size) return false;
    at = *position;
    while (out < expected) {
        uint8_t value;
        if (at >= packed_size) return false;
        value = packed[at++];
        if (value != 0xf1U) {
            record[out++] = value;
        } else {
            uint8_t repeated, count;
            if (packed_size - at < 2U) return false;
            repeated = packed[at++];
            count = packed[at++];
            if (count > expected - out) return false;
            xx_rt_memset(record + out, repeated, count);
            out += count;
        }
    }
    *position = at;
    return true;
}

static bool emt_track_header(const uint8_t *record, size_t track) {
    return record[1] == 0U && record[2] == 0U &&
           record[3] == 0x24U && record[4] == 0x80U &&
           record[5] == 0U && record[6] == 0x31U &&
           record[7] == (uint8_t)(track / 2U) &&
           record[8] == (uint8_t)(track & 1U);
}

static bool pm_parse(Abstractformat *format, pm_stream *stream,
                     xx_pd_struct *pd) {
    static const uint8_t signature[] =
        {0xf1U, 0x00U, 0x03U, 0x24U, 0x80U, 0x00U, 0x31U};
    uint8_t record[EMT_RECORD_BYTES];
    uint8_t *packed = NULL, *image = NULL;
    emt_parsed parsed;
    int64_t span = pm_available(format);
    size_t position = 0U, track, track_count, raw_size, i;
    uint32_t bytes_per_sector, total_sectors, sectors_per_track;
    bool found = false, ok = false;

    if (!emt_parse(format, &parsed, pd) || span <= 0 ||
        (uint64_t)span > EMT_MAX_PACKED) return false;
    packed = (uint8_t *)xx_mem_alloc((size_t)span);
    if (!packed || !pm_read(format, 0, packed, (size_t)span)) goto done;
    for (i = 0U; i + sizeof(signature) <= (size_t)span && i <= 4096U; ++i) {
        if (xx_rt_memcmp(packed + i, signature, sizeof(signature)) == 0) {
            position = i;
            found = true;
            break;
        }
    }
    if (!found || !emt_decode_record(packed, (size_t)span, &position,
                                      record, sizeof(record)) ||
        !emt_track_header(record, 0U)) goto done;

    bytes_per_sector = xx_data_get_u16(record + 126U + 11U, 2, 0, false);
    sectors_per_track = xx_data_get_u16(record + 126U + 24U, 2, 0, false);
    total_sectors = xx_data_get_u16(record + 126U + 19U, 2, 0, false);
    if (total_sectors == 0U)
        total_sectors = xx_data_get_u32(record + 126U + 32U, 4, 0, false);
    if (bytes_per_sector < 128U || bytes_per_sector > 4096U ||
        (bytes_per_sector & (bytes_per_sector - 1U)) != 0U ||
        sectors_per_track < 1U || sectors_per_track > 63U ||
        bytes_per_sector * sectors_per_track != EMT_TRACK_BYTES ||
        total_sectors == 0U ||
        (uint64_t)bytes_per_sector * total_sectors > EMT_MAX_IMAGE ||
        ((uint64_t)bytes_per_sector * total_sectors) % EMT_TRACK_BYTES)
        goto done;
    raw_size = (size_t)bytes_per_sector * total_sectors;
    track_count = raw_size / EMT_TRACK_BYTES;
    image = (uint8_t *)xx_mem_alloc(raw_size);
    if (!image) goto done;
    xx_rt_memcpy(image, record + 126U, EMT_TRACK_BYTES);
    for (track = 1U; track < track_count; ++track) {
        size_t record_size = track + 1U == track_count ?
                             EMT_RECORD_BYTES - 2U : EMT_RECORD_BYTES;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !emt_decode_record(packed, (size_t)span, &position,
                               record, record_size) ||
            !emt_track_header(record, track)) goto done;
        xx_rt_memcpy(image + track * EMT_TRACK_BYTES,
                     record + 126U, EMT_TRACK_BYTES);
    }
    if (!pm_add(format, stream, "disk.img", 0, span)) goto done;
    xx_rt_strncpy(stream->items[0].name, "disk.img",
                  sizeof(stream->items[0].name) - 1U);
    stream->items[0].name[sizeof(stream->items[0].name) - 1U] = 0;
    stream->items[0].size = (int64_t)raw_size;
    stream->items[0].memory = image;
    image = NULL;
    stream->size = span;
    ok = true;
done:
    xx_mem_free(image);
    xx_mem_free(packed);
    return ok;
}

static bool emt_record(xx_archive_record_state *state) {
    if (!pm_record(state)) return false;
    return xx_archive_record_set_meta_u64(&state->current_record,
                                           XX_META_ID_COMPRESSION_METHOD, 1U);
}

static xx_archive_record_state *emt_create_records(Abstractformat *format,
                                                    const xx_list_s *options,
                                                    xx_pd_struct *pd) {
    xx_archive_record_state *state = pm_create_records(format, options, pd);
    if (state && state->has_record && !emt_record(state)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    return state;
}

/* ----------------------------------------------------------- lifecycle -- */

void xx_emt_init(xx_emt *image, xx_io_device *device, int64_t base_address) {
    if (!image) return;
    xx_mem_zero(image, sizeof(*image));
    pm_init(&image->format, device, base_address, XX_EMT_FILE_TYPE, "emt");
    /* The 0x346e026c marker is stored little endian. */
    image->format.endian = XX_ENDIAN_LITTLE;
    image->format.file_type = XX_EMT_FILE_TYPE;
    image->format.format_type = XX_TYPE_ARCHIVE;
    image->format.is_archive = true;
    xx_format_set_mime_type(&image->format, "application/x-emt-diskimage");
    xx_format_set_extension(&image->format, "emt");
    image->format.check_is_valid = xx_emt_check_is_valid;
    image->format.handle_base_info = xx_emt_handle_base_info;
    image->format.get_format_size = xx_emt_get_format_size;
    image->format.create_archive_records_reading = emt_create_records;
    image->format.destroy = xx_emt_vtable_destroy;
}

xx_emt *xx_emt_create(xx_io_device *device, int64_t base_address) {
    xx_emt *image = (xx_emt *)xx_mem_alloc(sizeof(*image));
    if (image) xx_emt_init(image, device, base_address);
    return image;
}

void xx_emt_destroy(xx_emt *image) {
    if (!image) return;
    xx_format_cleanup_extra_parameters(&image->format);
}

static void xx_emt_vtable_destroy(Abstractformat *self) {
    xx_emt_destroy((xx_emt *)self);
}

void xx_emt_free(xx_emt *image) {
    if (!image) return;
    xx_emt_destroy(image);
    xx_mem_free(image);
}

/* -------------------------------------------------------------- format -- */

bool xx_emt_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    return pm_valid(format, pd);
}

bool xx_emt_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    xx_emt *image = (xx_emt *)format;
    emt_parsed parsed;
    size_t index;

    if (!format) return false;
    if (!emt_parse(format, &parsed, pd)) {
        format->is_valid = false;
        format->base_info_handled = false;
        return false;
    }
    for (index = 0U; index <= XX_EMT_BANNER_SIZE; ++index)
        image->banner[index] = parsed.banner[index];
    xx_format_set_version(format, image->banner);
    return pm_handle(format, pd);
}

int64_t xx_emt_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    if (!format || (!format->base_info_handled &&
                    !xx_format_handle_base_info(format, pd)))
        return -1;
    return format->format_size;
}

/* ----------------------------------------------------------- accessors -- */

const char *xx_emt_get_banner(const xx_emt *image) {
    return image ? image->banner : "";
}
