/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * SMD's string policy is format metadata in Radare2: minimum length 10 and
 * the uppercase ('U') filter. Keep that compatibility scanner independent of
 * the strict cartridge/checksum reader because the upstream regression builds
 * a signature-bearing sparse image solely to exercise string selection.
 */

#include "xxfclib/formats/game/sega_megadrive_rom/xx_sega_megadrive_rom.h"
#include "xxfclib/data/xx_pd.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

#define SMD_MAX_IMAGE_SIZE (16U * 1024U * 1024U)
#define SMD_DISPLAY_CAPACITY (XX_SEGA_MEGADRIVE_ROM_MAX_STRING_LENGTH * 4U + 1U)

static bool smd_span(xx_io_device *device, int64_t base, uint64_t size)
{
    int64_t total;
    return device && base >= 0 && size <= SMD_MAX_IMAGE_SIZE && (total = xx_io_size(device)) >= base && size <= (uint64_t)(total - base);
}

static bool smd_read_exact(xx_io_device *device, int64_t offset, void *buffer, size_t size)
{
    size_t done = 0U;
    if (xx_io_seek64(device, offset, XX_RT_SEEK_SET) != 0) return false;
    while (done < size) {
        ssize_t got = xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (got <= 0 || (size_t)got > size - done) return false;
        done += (size_t)got;
    }
    return true;
}

static bool smd_signature(const uint8_t *bytes, uint64_t size)
{
    return size > 0x190U && xx_rt_memcmp(bytes + 0x100U, "SEGA", 4U) == 0;
}

bool xx_sega_megadrive_rom_has_compatible_header(xx_io_device *device, int64_t base_address, uint64_t image_size)
{
    uint8_t signature[4];
    int64_t saved;
    bool result;
    if (!smd_span(device, base_address, image_size) || image_size <= 0x190U) return false;
    saved = xx_io_tell(device);
    result = smd_read_exact(device, base_address + 0x100, signature, sizeof(signature)) && xx_rt_memcmp(signature, "SEGA", sizeof(signature)) == 0;
    if (saved >= 0 && xx_io_seek64(device, saved, XX_RT_SEEK_SET) != 0) result = false;
    return result;
}

/* Return the byte length of one canonical UTF-8 scalar, zero for malformed
 * input. The scanner treats every well-formed non-ASCII scalar as printable;
 * the uppercase SMD filter subsequently rejects it, matching its ASCII-only
 * policy while retaining the unfiltered ordinal. */
static size_t smd_utf8_scalar(const uint8_t *text, size_t left)
{
    uint8_t a, b, c, d;
    if (!left) return 0U;
    a = text[0];
    if (a < 0x80U) return 1U;
    if (a >= 0xc2U && a <= 0xdfU) {
        return left >= 2U && (text[1] & 0xc0U) == 0x80U ? 2U : 0U;
    }
    if (a >= 0xe0U && a <= 0xefU) {
        if (left < 3U) return 0U;
        b = text[1];
        c = text[2];
        if ((c & 0xc0U) != 0x80U || (b & 0xc0U) != 0x80U) return 0U;
        if ((a == 0xe0U && b < 0xa0U) || (a == 0xedU && b >= 0xa0U)) return 0U;
        return 3U;
    }
    if (a >= 0xf0U && a <= 0xf4U) {
        if (left < 4U) return 0U;
        b = text[1];
        c = text[2];
        d = text[3];
        if ((b & 0xc0U) != 0x80U || (c & 0xc0U) != 0x80U || (d & 0xc0U) != 0x80U) return 0U;
        if ((a == 0xf0U && b < 0x90U) || (a == 0xf4U && b >= 0x90U)) return 0U;
        return 4U;
    }
    return 0U;
}

static char smd_escape(uint8_t value)
{
    switch (value) {
        case '\a': return 'a';
        case '\b': return 'b';
        case '\t': return 't';
        case '\n': return 'n';
        case '\v': return 'v';
        case '\f': return 'f';
        case '\r': return 'r';
        case 0x1bU: return 'e';
        default: return 0;
    }
}

static unsigned smd_ratio(unsigned char value, const char *text, size_t length)
{
    size_t index, count = 0U;
    if (!length) return 0U;
    for (index = 0U; index < length; ++index)
        if ((unsigned char)text[index] == value) ++count;
    return (unsigned)(count * 100U / length);
}

static bool smd_uppercase_filter(const char *text, size_t length)
{
    size_t index;
    bool uppercase = false, escape = false;
    if (!length) return false;
    for (index = 0U; index < length; ++index) {
        unsigned char value = (unsigned char)text[index];
        if (value == ' ' || (escape && (value == 't' || value == 'n' || value == 'r'))) {
            escape = false;
            continue;
        }
        if (value < 0x20U || value > 0x7eU || (value >= 'a' && value <= 'z')) return false;
        if (value >= 'A' && value <= 'Z') uppercase = true;
        escape = !escape && value == '\\';
    }
    if (smd_ratio((unsigned char)text[0], text, length) >= 60U) return false;
    if (length > 1U && smd_ratio((unsigned char)text[1], text, length) >= 60U) return false;
    return uppercase;
}

bool xx_sega_megadrive_rom_visit_uppercase_strings(xx_io_device *device, int64_t base_address, uint64_t image_size, uint32_t minimum_length,
                                                   xx_sega_megadrive_rom_string_callback callback, void *user, xx_pd_struct *pd)
{
    uint8_t *bytes = NULL;
    char *display = NULL;
    uint64_t at = 0U;
    uint32_t ordinal = 0U;
    int64_t saved;
    bool result = false;
    if (!callback || !smd_span(device, base_address, image_size) || image_size > (uint64_t)SIZE_MAX || (pd && xx_pd_is_stopped(pd))) return false;
    if (!minimum_length) minimum_length = XX_SEGA_MEGADRIVE_ROM_DEFAULT_MIN_STRING_LENGTH;
    if (minimum_length > XX_SEGA_MEGADRIVE_ROM_MAX_STRING_LENGTH) return false;
    saved = xx_io_tell(device);
    bytes = (uint8_t *)xx_mem_alloc((size_t)image_size);
    display = (char *)xx_mem_alloc(SMD_DISPLAY_CAPACITY);
    if (!bytes || !display || !smd_read_exact(device, base_address, bytes, (size_t)image_size) || !smd_signature(bytes, image_size)) goto done;

    while (at < image_size) {
        uint64_t start = at;
        uint32_t scalars = 0U;
        size_t display_length = 0U;
        bool terminated = false;
        if ((at & 0xffffU) == 0U && pd && xx_pd_is_stopped(pd)) goto done;

        while (at < image_size && scalars < XX_SEGA_MEGADRIVE_ROM_MAX_STRING_LENGTH) {
            uint8_t value = bytes[at];
            size_t encoded;
            char escaped;
            if (value >= 0x20U && value <= 0x7eU) {
                display[display_length++] = (char)value;
                ++at;
                ++scalars;
                continue;
            }
            escaped = smd_escape(value);
            if (escaped) {
                display[display_length++] = '\\';
                display[display_length++] = escaped;
                ++at;
                ++scalars;
                continue;
            }
            encoded = smd_utf8_scalar(bytes + at, (size_t)(image_size - at));
            if (encoded > 1U) {
                xx_mem_copy(display + display_length, bytes + at, encoded);
                display_length += encoded;
                at += encoded;
                ++scalars;
                continue;
            }
            ++at; /* NUL, malformed UTF-8 or an unsupported control. */
            terminated = true;
            break;
        }
        display[display_length] = '\0';

        if (scalars >= minimum_length) {
            xx_sega_megadrive_rom_string record;
            /* Pinned Radare2 trims only the presentation length; storage_size
             * continues to describe the original C string and terminator. */
            while (display_length && display[display_length - 1U] == ' ') {
                display[--display_length] = '\0';
                --scalars;
            }
            if (smd_uppercase_filter(display, display_length)) {
                record.offset = (uint64_t)base_address + start;
                record.ordinal = ordinal;
                record.length = scalars;
                record.storage_size = (uint32_t)(at - start);
                record.text = display;
                if (!callback(&record, user)) {
                    result = true;
                    goto done;
                }
            }
            ++ordinal;
        }

        /* A maximum-length candidate consumes no terminator. Progress is
         * already guaranteed; the next byte starts a fresh bounded record. */
        (void)terminated;
    }
    result = !(pd && xx_pd_is_stopped(pd));

done:
    xx_mem_free(display);
    xx_mem_free(bytes);
    if (saved >= 0 && xx_io_seek64(device, saved, XX_RT_SEEK_SET) != 0) result = false;
    return result;
}
