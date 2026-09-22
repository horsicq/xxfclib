/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * "EMT" compressed diskette image.  The four header tests are U3's own
 * recognition predicate (FUN_006716d0) reproduced exactly; the EBCDIC banner
 * at 0x40 is decoded for display.  xx_emt.h records the field table and
 * explains why this reader publishes no archive records.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/emt/xx_emt.h"

#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

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

/* ----------------------------------------------------------- lifecycle -- */

void xx_emt_init(xx_emt *image, xx_io_device *device, int64_t base_address) {
    if (!image) return;
    xx_mem_zero(image, sizeof(*image));
    xx_format_init(&image->format, device, base_address);
    /* The 0x346e026c marker is stored little endian. */
    image->format.endian = XX_ENDIAN_LITTLE;
    image->format.file_type = XX_EMT_FILE_TYPE;
    /* Not an archive: the container names nothing and bounds nothing. */
    image->format.format_type = XX_TYPE_RAW;
    image->format.is_archive = false;
    xx_format_set_mime_type(&image->format, "application/x-emt-diskimage");
    xx_format_set_extension(&image->format, "emt");
    image->format.check_is_valid = xx_emt_check_is_valid;
    image->format.handle_base_info = xx_emt_handle_base_info;
    image->format.get_format_size = xx_emt_get_format_size;
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
    emt_parsed parsed;
    return emt_parse(format, &parsed, pd);
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
    /* The image is everything that was handed to the reader; the container
     * states no length, so this is a statement about the input, not a claim
     * recovered from the file - which is also why there is no overlay. */
    format->format_size = parsed.total_size - format->base_address;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->number_of_archive_records = 0U;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
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
