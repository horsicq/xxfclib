/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * InstallShield "ISSetupStream" container: the member payload of an
 * InstallShield 2009 (and later) Setup.exe and of the Binary.ISSetup.dll
 * stream inside InstallShield-authored MSI packages.  The field table is in
 * xx_installshield_issetupstream.h.
 *
 * Ported in part from XArchive installers/xissetupstream.cpp and the
 * decISSetupStream() codec in core/xdecompress.cpp (MIT, hors): the record
 * acceptance rules, the cipher selectors and the salted name key.  The
 * selector semantics (0 / -1 / -3 unfiltered, 2 and 6 filtered, the rest
 * unsupported) and the UTF-8 form of the key were checked against the
 * behaviour of U3's "SFX IS19" handler, and the whole codec against the five
 * samples of F:\ARC\ARC\SFX IS19.
 *
 * This is a self-extractor reader: the carrier executable is parsed only as
 * far as its PE section table, to find where the overlay starts.  No code of
 * the carrier is looked at, let alone run.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/installshield_issetupstream/xx_installshield_issetupstream.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as the reader is registered. */
#ifdef INSTALLSHIELD_ISSETUPSTREAM
#define XX_INSTALLSHIELD_ISSETUPSTREAM_FILE_TYPE \
    XX_FILE_TYPE_INSTALLSHIELD_ISSETUPSTREAM
#else
#define XX_INSTALLSHIELD_ISSETUPSTREAM_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define ISS_TAG_SIZE 14U
#define ISS_HEADER_SIZE 46
#define ISS_RECORD_SIZE 24
#define ISS_MAX_NAME_BYTES 0x10000U
/* UTF-16 -> UTF-8 needs at most three bytes per code unit (a surrogate pair
 * is four bytes for two units). */
#define ISS_KEY_BUFFER (3U * (ISS_MAX_NAME_BYTES / 2U) + 1U)
/* Room for "_<index>" inserted into a duplicate's name. */
#define ISS_NAME_BUFFER (ISS_KEY_BUFFER + 16U)
#define ISS_MAX_SECTIONS 96U
#define ISS_SECTION_SIZE 40U
#define ISS_MAX_STREAM 0x7FFFFFFFU
#define ISS_CHUNK 65536U
/* No member of a 32-bit installer container inflates past 4 GiB; the cap
 * bounds what a hostile stream can write. */
#define ISS_OUTPUT_CAP (UINT64_C(1) << 32)
#define ISS_POLL_MASK 0xFFU
#define ISS_KEY_PERIOD 1024U

#define ISS_SELECTOR_NONE 0U
#define ISS_SELECTOR_BAD 0xFFFFFFFEU

static const uint8_t iss_tag[ISS_TAG_SIZE] = {'I', 'S', 'S', 'e', 't',
                                              'u', 'p', 'S', 't', 'r',
                                              'e', 'a', 'm', 0};
static const uint8_t iss_salt[4] = {0xECU, 0xCAU, 0x79U, 0xF8U};

typedef struct iss_layout_s {
    int64_t total;     /**< Device size. */
    int64_t tag;       /**< Device offset of the tag. */
    int64_t end;       /**< Device offset behind the last complete record. */
    uint32_t declared; /**< Header record count. */
    uint32_t complete; /**< Records wholly inside the device. */
    uint32_t version;
    bool in_pe;
    bool truncated;
} iss_layout;

typedef struct iss_member_s {
    int64_t header_offset;
    int64_t data_offset;
    int64_t size;
    uint32_t name_bytes;
    uint32_t selector; /**< Normalised: 0, 2, 6 or ISS_SELECTOR_BAD. */
    uint32_t raw_selector;
    uint16_t storage;
    bool renamed;
} iss_member;

typedef struct iss_key_s {
    uint64_t hash;
    uint32_t index;
} iss_key;

typedef struct iss_stream_s {
    iss_member *items;
    size_t count;
    size_t index;
    uint8_t *raw;      /**< ISS_MAX_NAME_BYTES: the current raw name. */
    uint8_t *key;      /**< ISS_KEY_BUFFER: the name as UTF-8, unsalted. */
    size_t key_length;
    char *name;        /**< ISS_NAME_BUFFER: the display / output name. */
    bool name_ok;      /**< Non-empty and without an embedded NUL. */
} iss_stream;

/* ---- helpers ------------------------------------------------------------ */

static uint32_t iss_le16(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static uint32_t iss_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
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

static uint32_t iss_normalise_selector(uint32_t selector) {
    if (selector == 0U || selector == 0xFFFFFFFFU || selector == 0xFFFFFFFDU)
        return ISS_SELECTOR_NONE;
    if (selector == 2U || selector == 6U) return selector;
    return ISS_SELECTOR_BAD;
}

/* ---- locating the container --------------------------------------------- */

/* Overlay start of the PE image at `base`: the end of the furthest section
 * raw data.  Only the MZ header, the PE header and the section table are
 * read (at most 96 * 40 bytes).  `aligned` receives the same offset rounded
 * up to the image's FileAlignment, or -1 when that is no different. */
static bool iss_pe_overlay(xx_io_device *device, int64_t base, int64_t size,
                           int64_t *overlay, int64_t *aligned) {
    uint8_t mz[0x40];
    uint8_t pe[24];
    uint8_t table[ISS_MAX_SECTIONS * ISS_SECTION_SIZE];
    uint8_t alignment_field[4];
    int64_t lfanew, table_offset, end = 0;
    uint32_t sections, optional_size, index, alignment = 0U;
    if (size < (int64_t)sizeof(mz) ||
        !iss_read_at(device, base, mz, sizeof(mz)) || mz[0] != 'M' ||
        mz[1] != 'Z')
        return false;
    lfanew = (int64_t)iss_le32(mz + 0x3CU);
    if (lfanew > size - (int64_t)sizeof(pe) ||
        !iss_read_at(device, base + lfanew, pe, sizeof(pe)) || pe[0] != 'P' ||
        pe[1] != 'E' || pe[2] != 0U || pe[3] != 0U)
        return false;
    sections = iss_le16(pe + 6U);
    optional_size = iss_le16(pe + 20U);
    if (sections == 0U || sections > ISS_MAX_SECTIONS) return false;
    table_offset = lfanew + (int64_t)sizeof(pe) + (int64_t)optional_size;
    if (table_offset > size ||
        (int64_t)sections * ISS_SECTION_SIZE > size - table_offset ||
        !iss_read_at(device, base + table_offset, table,
                     (size_t)sections * ISS_SECTION_SIZE))
        return false;
    /* FileAlignment sits at +36 of both the PE32 and the PE32+ optional
     * header. */
    if (optional_size >= 40U &&
        iss_read_at(device, base + lfanew + (int64_t)sizeof(pe) + 36,
                    alignment_field, sizeof(alignment_field)))
        alignment = iss_le32(alignment_field);
    for (index = 0U; index < sections; ++index) {
        const uint8_t *section = table + (size_t)index * ISS_SECTION_SIZE;
        uint32_t raw_size = iss_le32(section + 16U);
        uint32_t raw_pointer = iss_le32(section + 20U);
        int64_t section_end = (int64_t)raw_pointer + (int64_t)raw_size;
        if (raw_size != 0U && section_end > end) end = section_end;
    }
    if (end <= 0) return false;
    *overlay = end;
    *aligned = -1;
    if (alignment >= 0x200U && alignment <= 0x10000U &&
        (alignment & (alignment - 1U)) == 0U) {
        int64_t rounded = (end + (int64_t)alignment - 1) &
                          ~((int64_t)alignment - 1);
        if (rounded != end) *aligned = rounded;
    }
    return true;
}

/* The tag either starts the device at `base` (a carved payload) or starts
 * the overlay of the PE image at `base`. */
static bool iss_locate(xx_io_device *device, int64_t base, int64_t total,
                       int64_t *tag, bool *in_pe) {
    uint8_t head[ISS_TAG_SIZE];
    int64_t size = total - base;
    int64_t candidates[2];
    size_t index;
    if (size < (int64_t)(ISS_HEADER_SIZE + ISS_RECORD_SIZE) ||
        !iss_read_at(device, base, head, sizeof(head)))
        return false;
    if (xx_rt_memcmp(head, iss_tag, ISS_TAG_SIZE) == 0) {
        *tag = base;
        *in_pe = false;
        return true;
    }
    if (head[0] != 'M' || head[1] != 'Z' ||
        !iss_pe_overlay(device, base, size, &candidates[0], &candidates[1]))
        return false;
    for (index = 0U; index < 2U; ++index) {
        int64_t at = candidates[index];
        if (at <= 0 ||
            at > size - (int64_t)(ISS_HEADER_SIZE + ISS_RECORD_SIZE) ||
            !iss_read_at(device, base + at, head, sizeof(head)) ||
            xx_rt_memcmp(head, iss_tag, ISS_TAG_SIZE) != 0)
            continue;
        *tag = base + at;
        *in_pe = true;
        return true;
    }
    return false;
}

/* ---- header and record walk ---------------------------------------------- */

static bool iss_read_layout(Abstractformat *format, iss_layout *layout) {
    uint8_t header[ISS_HEADER_SIZE];
    int64_t total;
    size_t index;
    if (!format || !format->device || !layout || format->base_address < 0)
        return false;
    xx_mem_zero(layout, sizeof(*layout));
    total = xx_io_total_size(format->device);
    if (total < format->base_address ||
        !iss_locate(format->device, format->base_address, total,
                    &layout->tag, &layout->in_pe) ||
        !iss_read_at(format->device, layout->tag, header, sizeof(header)) ||
        xx_rt_memcmp(header, iss_tag, ISS_TAG_SIZE) != 0)
        return false;
    layout->total = total;
    layout->declared = iss_le16(header + 14U);
    layout->version = header[16];
    /* The .rdata copy of the tag has count 0 and whatever data happens to
     * follow; the container has a version of 2 or 3 and 29 zero bytes. */
    if (layout->declared == 0U ||
        (layout->version != 2U && layout->version != 3U))
        return false;
    for (index = 17U; index < (size_t)ISS_HEADER_SIZE; ++index)
        if (header[index] != 0U) return false;
    return true;
}

static bool iss_record_fields(const uint8_t *record, uint32_t *name_bytes,
                              uint32_t *selector, uint32_t *stream_size,
                              uint16_t *storage) {
    uint32_t names = iss_le32(record);
    uint32_t size = iss_le32(record + 10U);
    uint32_t method = iss_le16(record + 22U);
    if (names == 0U || (names & 1U) != 0U || names > ISS_MAX_NAME_BYTES)
        return false;
    if (iss_le16(record + 8U) != 0U || iss_le32(record + 14U) != 0U ||
        iss_le32(record + 18U) != 0U)
        return false;
    if (size > ISS_MAX_STREAM || method > 1U) return false;
    *name_bytes = names;
    *selector = iss_le32(record + 4U);
    *stream_size = size;
    *storage = (uint16_t)method;
    return true;
}

/* Walk the records.  The first one must be well formed and lie wholly in
 * the device; a later one that does not ends the listing there (a truncated
 * or damaged tail), as the reference extractor also stops at it.  With
 * `items` NULL this is the probe and keeps nothing. */
static bool iss_walk(xx_io_device *device, iss_layout *layout,
                     iss_member *items, xx_pd_struct *pd) {
    int64_t position = layout->tag + ISS_HEADER_SIZE;
    uint32_t index;
    layout->complete = 0U;
    layout->truncated = false;
    for (index = 0U; index < layout->declared; ++index) {
        uint8_t record[ISS_RECORD_SIZE];
        uint32_t name_bytes, selector, stream_size;
        uint16_t storage;
        int64_t need;
        if ((index & ISS_POLL_MASK) == 0U && pd && xx_pd_is_stopped(pd))
            return false;
        if (layout->total - position < (int64_t)ISS_RECORD_SIZE ||
            !iss_read_at(device, position, record, sizeof(record)) ||
            !iss_record_fields(record, &name_bytes, &selector, &stream_size,
                               &storage)) {
            if (index == 0U) return false;
            layout->truncated = true;
            break;
        }
        need = (int64_t)ISS_RECORD_SIZE + (int64_t)name_bytes +
               (int64_t)stream_size;
        if (need > layout->total - position) {
            if (index == 0U) return false;
            layout->truncated = true;
            break;
        }
        if (items) {
            iss_member *member = &items[index];
            member->header_offset = position;
            member->data_offset =
                position + (int64_t)ISS_RECORD_SIZE + (int64_t)name_bytes;
            member->size = (int64_t)stream_size;
            member->name_bytes = name_bytes;
            member->raw_selector = selector;
            member->selector = iss_normalise_selector(selector);
            member->storage = storage;
            member->renamed = false;
        }
        position += need;
        ++layout->complete;
    }
    layout->end = position;
    return layout->complete != 0U;
}

/* ---- names ---------------------------------------------------------------- */

/* UTF-16LE -> UTF-8 as the reference converts it: a lone surrogate becomes
 * U+FFFD, U+0000 stays a zero byte.  `out` holds 3 * units bytes. */
static size_t iss_utf16_to_utf8(const uint8_t *raw, size_t units,
                                uint8_t *out) {
    size_t at = 0U, index = 0U;
    while (index < units) {
        uint32_t unit = iss_le16(raw + 2U * index);
        if (unit < 0x80U) {
            out[at++] = (uint8_t)unit;
        } else if (unit < 0x800U) {
            out[at++] = (uint8_t)(0xC0U | (unit >> 6U));
            out[at++] = (uint8_t)(0x80U | (unit & 0x3FU));
        } else if ((unit & 0xF800U) == 0xD800U) {
            uint32_t next = index + 1U < units
                                ? iss_le16(raw + 2U * (index + 1U))
                                : 0U;
            if (unit < 0xDC00U && (next & 0xFC00U) == 0xDC00U) {
                uint32_t point =
                    0x10000U + ((unit - 0xD800U) << 10U) + (next - 0xDC00U);
                out[at++] = (uint8_t)(0xF0U | (point >> 18U));
                out[at++] = (uint8_t)(0x80U | ((point >> 12U) & 0x3FU));
                out[at++] = (uint8_t)(0x80U | ((point >> 6U) & 0x3FU));
                out[at++] = (uint8_t)(0x80U | (point & 0x3FU));
                ++index;
            } else {
                out[at++] = 0xEFU;
                out[at++] = 0xBFU;
                out[at++] = 0xBDU;
            }
        } else {
            out[at++] = (uint8_t)(0xE0U | (unit >> 12U));
            out[at++] = (uint8_t)(0x80U | ((unit >> 6U) & 0x3FU));
            out[at++] = (uint8_t)(0x80U | (unit & 0x3FU));
        }
        ++index;
    }
    return at;
}

/* 64-bit FNV-1a over the display name with ASCII folded to lower case, so
 * names a case-insensitive filesystem treats as one hash alike. */
static uint64_t iss_name_hash(const char *name, size_t length) {
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    size_t index;
    for (index = 0U; index < length; ++index) {
        uint8_t c = (uint8_t)name[index];
        if (c >= (uint8_t)'A' && c <= (uint8_t)'Z')
            c = (uint8_t)(c - (uint8_t)'A' + (uint8_t)'a');
        hash ^= (uint64_t)c;
        hash *= UINT64_C(0x100000001b3);
    }
    return hash;
}

/* Insert "_<index>" before the extension of the last component. */
static void iss_insert_suffix(char *name, size_t length, uint32_t index) {
    char suffix[12];
    char digits[10];
    size_t suffix_length = 0U, digit_count = 0U, component = 0U, at, tail;
    size_t dot = length;
    for (at = 0U; at < length; ++at)
        if (name[at] == '/') component = at + 1U;
    for (at = length; at > component + 1U; --at)
        if (name[at - 1U] == '.') {
            dot = at - 1U;
            break;
        }
    do {
        digits[digit_count++] = (char)('0' + (char)(index % 10U));
        index /= 10U;
    } while (index != 0U && digit_count < sizeof(digits));
    suffix[suffix_length++] = '_';
    while (digit_count != 0U) suffix[suffix_length++] = digits[--digit_count];
    if (length + suffix_length >= ISS_NAME_BUFFER) return;
    tail = length - dot;
    for (at = tail + 1U; at > 0U; --at)
        name[dot + suffix_length + at - 1U] = name[dot + at - 1U];
    xx_rt_memcpy(name + dot, suffix, suffix_length);
}

/* Read member `index`'s name: the UTF-8 key into stream->key and the display
 * name (trailing NULs dropped, '\' as '/', duplicate suffix) into
 * stream->name.  Returns the display length through `length`. */
static bool iss_load_name(xx_io_device *device, iss_stream *stream,
                          size_t index, bool with_suffix, size_t *length) {
    const iss_member *member = &stream->items[index];
    size_t key_length, shown, at;
    if (member->name_bytes == 0U || member->name_bytes > ISS_MAX_NAME_BYTES ||
        !iss_read_at(device, member->header_offset + ISS_RECORD_SIZE,
                     stream->raw, member->name_bytes))
        return false;
    key_length = iss_utf16_to_utf8(stream->raw, member->name_bytes / 2U,
                                   stream->key);
    stream->key_length = key_length;
    shown = key_length;
    while (shown != 0U && stream->key[shown - 1U] == 0U) --shown;
    stream->name_ok = shown != 0U;
    for (at = 0U; at < shown; ++at) {
        char c = (char)stream->key[at];
        if (c == 0) stream->name_ok = false;
        stream->name[at] = c == '\\' ? '/' : c;
    }
    stream->name[shown] = 0;
    if (with_suffix && member->renamed)
        iss_insert_suffix(stream->name, shown, (uint32_t)index);
    if (length) *length = shown;
    return true;
}

/* A Windows device name (CON, PRN, AUX, NUL, COM0-9, LPT0-9, CLOCK$, CONIN$,
 * CONOUT$) as the part of a component before its first '.', trailing spaces
 * ignored. */
static bool iss_reserved_component(const char *segment, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",    "AUX",
                                          "NUL",    "CLOCK$", "CONIN$",
                                          "CONOUT$"};
    char stem[8];
    size_t stem_length = 0U, index;
    while (stem_length < length && segment[stem_length] != '.') ++stem_length;
    while (stem_length != 0U && segment[stem_length - 1U] == ' ')
        --stem_length;
    if (stem_length < 3U || stem_length > sizeof(stem) - 1U) return false;
    for (index = 0U; index < stem_length; ++index) {
        char c = segment[index];
        stem[index] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    stem[stem_length] = 0;
    if (stem_length == 4U && stem[3] >= '0' && stem[3] <= '9' &&
        ((stem[0] == 'C' && stem[1] == 'O' && stem[2] == 'M') ||
         (stem[0] == 'L' && stem[1] == 'P' && stem[2] == 'T')))
        return true;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (xx_str_len(devices[index]) == stem_length &&
            xx_rt_memcmp(stem, devices[index], stem_length) == 0)
            return true;
    return false;
}

/* Refuse absolute paths, drive letters and streams (any ':'), empty
 * components, components ending in '.' or ' ' (this covers "." and ".."),
 * device names, control characters and the characters no Windows path may
 * carry.  UTF-8 bytes above 0x7F are allowed. */
static bool iss_safe_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || c == '\\' || c == 0x7FU ||
            (c != 0U && c < 0x20U))
            return false;
        if (c == '/' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || segment[length - 1U] == '.' ||
                segment[length - 1U] == ' ' ||
                iss_reserved_component(segment, length))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

/* ---- member codec --------------------------------------------------------- */

/* A read-only view of one member stream that removes the name filter as it
 * is read, so the shared inflater can consume it without the member ever
 * being held in memory whole. */
typedef struct iss_cipher_s {
    xx_io_device *source;
    int64_t origin;
    int64_t size;
    int64_t position;
    uint32_t selector;
    const uint8_t *key; /**< Salted. */
    size_t key_length;
    uint8_t period[ISS_KEY_PERIOD]; /**< Selector 6 key stream. */
} iss_cipher;

static ssize_t iss_cipher_read(xx_io_device *self, void *buffer, size_t n) {
    iss_cipher *cipher = self ? (iss_cipher *)self->priv : NULL;
    uint8_t *out = (uint8_t *)buffer;
    size_t index;
    if (!cipher || (!buffer && n != 0U)) return -1;
    if (cipher->position >= cipher->size || n == 0U) return 0;
    if ((uint64_t)n > (uint64_t)(cipher->size - cipher->position))
        n = (size_t)(cipher->size - cipher->position);
    if (n > (size_t)ISS_MAX_STREAM) n = (size_t)ISS_MAX_STREAM;
    if (!iss_read_at(cipher->source, cipher->origin + cipher->position, out,
                     n))
        return -1;
    if (cipher->selector == 6U) {
        uint32_t slot = (uint32_t)(cipher->position & (ISS_KEY_PERIOD - 1U));
        for (index = 0U; index < n; ++index) {
            uint8_t b = out[index];
            out[index] = (uint8_t)(((uint8_t)((b << 4U) | (b >> 4U))) ^
                                   cipher->period[slot]);
            slot = (slot + 1U) & (ISS_KEY_PERIOD - 1U);
        }
    } else if (cipher->selector == 2U) {
        size_t slot = (size_t)((uint64_t)cipher->position %
                               (uint64_t)cipher->key_length);
        for (index = 0U; index < n; ++index) {
            uint8_t b = out[index];
            out[index] = (uint8_t)(((uint8_t)((b << 4U) | (b >> 4U))) ^
                                   cipher->key[slot]);
            if (++slot == cipher->key_length) slot = 0U;
        }
    }
    cipher->position += (int64_t)n;
    return (ssize_t)n;
}

static ssize_t iss_cipher_write(xx_io_device *self, const void *buffer,
                                size_t n) {
    (void)self;
    (void)buffer;
    (void)n;
    return -1;
}

static int iss_cipher_seek64(xx_io_device *self, int64_t offset, int whence) {
    iss_cipher *cipher = self ? (iss_cipher *)self->priv : NULL;
    int64_t target;
    if (!cipher) return -1;
    if (whence == SEEK_SET) target = offset;
    else if (whence == SEEK_CUR) target = cipher->position + offset;
    else if (whence == SEEK_END) target = cipher->size + offset;
    else return -1;
    if (target < 0 || target > cipher->size) return -1;
    cipher->position = target;
    return 0;
}

static int iss_cipher_seek(xx_io_device *self, long offset, int whence) {
    return iss_cipher_seek64(self, (int64_t)offset, whence);
}

static int iss_cipher_close(xx_io_device *self) {
    (void)self;
    return 0;
}

static int64_t iss_cipher_size(xx_io_device *self) {
    iss_cipher *cipher = self ? (iss_cipher *)self->priv : NULL;
    return cipher ? cipher->size : -1;
}

static int64_t iss_cipher_tell(xx_io_device *self) {
    iss_cipher *cipher = self ? (iss_cipher *)self->priv : NULL;
    return cipher ? cipher->position : -1;
}

/* The output side: counts, caps and checksums what the member produces and
 * forwards it to the destination, if there is one. */
typedef struct iss_sink_s {
    xx_io_device *destination;
    uint64_t total;
    uint32_t a;
    uint32_t b;
} iss_sink;

static ssize_t iss_sink_write(xx_io_device *self, const void *buffer,
                              size_t n) {
    iss_sink *sink = self ? (iss_sink *)self->priv : NULL;
    const uint8_t *in = (const uint8_t *)buffer;
    size_t done = 0U, index;
    if (!sink || (!buffer && n != 0U)) return -1;
    if (n == 0U) return 0;
    if ((uint64_t)n > ISS_OUTPUT_CAP - sink->total) return -1;
    index = 0U;
    while (index < n) {
        size_t block = n - index > 5552U ? 5552U : n - index;
        size_t stop = index + block;
        for (; index < stop; ++index) {
            sink->a += in[index];
            sink->b += sink->a;
        }
        sink->a %= 65521U;
        sink->b %= 65521U;
    }
    while (sink->destination && done < n) {
        ssize_t amount = xx_io_write(sink->destination, in + done, n - done);
        if (amount <= 0 || (size_t)amount > n - done) return -1;
        done += (size_t)amount;
    }
    sink->total += (uint64_t)n;
    return (ssize_t)n;
}

static ssize_t iss_sink_read(xx_io_device *self, void *buffer, size_t n) {
    (void)self;
    (void)buffer;
    (void)n;
    return -1;
}

static int iss_sink_seek(xx_io_device *self, long offset, int whence) {
    (void)self;
    (void)offset;
    (void)whence;
    return -1;
}

static int iss_sink_close(xx_io_device *self) {
    (void)self;
    return 0;
}

static int64_t iss_sink_size(xx_io_device *self) {
    iss_sink *sink = self ? (iss_sink *)self->priv : NULL;
    return sink ? (int64_t)sink->total : -1;
}

/* Produce one member into `destination` (NULL only verifies it). */
static bool iss_decode(xx_io_device *source, const iss_member *member,
                       const uint8_t *key, size_t key_length,
                       xx_io_device *destination, xx_pd_struct *pd) {
    iss_cipher *cipher = NULL;
    uint8_t *salted = NULL;
    uint8_t *buffer = NULL;
    iss_sink sink;
    xx_io_device input, output;
    size_t index;
    bool result = false;
    if (!source || !member || member->selector == ISS_SELECTOR_BAD ||
        member->size < 0 || member->data_offset < 0)
        return false;
    if (member->selector != ISS_SELECTOR_NONE && (!key || key_length == 0U))
        return false;
    cipher = (iss_cipher *)xx_mem_calloc(1U, sizeof(*cipher));
    if (!cipher) goto done;
    cipher->source = source;
    cipher->origin = member->data_offset;
    cipher->size = member->size;
    cipher->selector = member->selector;
    if (member->selector != ISS_SELECTOR_NONE) {
        salted = (uint8_t *)xx_mem_alloc(key_length);
        if (!salted) goto done;
        for (index = 0U; index < key_length; ++index)
            salted[index] = (uint8_t)(key[index] ^ iss_salt[index & 3U]);
        cipher->key = salted;
        cipher->key_length = key_length;
        for (index = 0U; index < ISS_KEY_PERIOD; ++index)
            cipher->period[index] = salted[index % key_length];
    }
    xx_mem_zero(&input, sizeof(input));
    input.read = iss_cipher_read;
    input.write = iss_cipher_write;
    input.seek = iss_cipher_seek;
    input.seek64 = iss_cipher_seek64;
    input.close = iss_cipher_close;
    input.total_size = iss_cipher_size;
    input.get_total_size = iss_cipher_size;
    input.size = iss_cipher_size;
    input.tell = iss_cipher_tell;
    input.priv = cipher;
    xx_mem_zero(&sink, sizeof(sink));
    sink.destination = destination;
    sink.a = 1U;
    xx_mem_zero(&output, sizeof(output));
    output.read = iss_sink_read;
    output.write = iss_sink_write;
    output.seek = iss_sink_seek;
    output.close = iss_sink_close;
    output.total_size = iss_sink_size;
    output.get_total_size = iss_sink_size;
    output.size = iss_sink_size;
    output.priv = &sink;

    if (member->storage == 0U) {
        int64_t remaining = member->size;
        buffer = (uint8_t *)xx_mem_alloc(ISS_CHUNK);
        if (!buffer || xx_io_seek64(&input, 0, SEEK_SET) != 0) goto done;
        while (remaining > 0) {
            size_t chunk = remaining > (int64_t)ISS_CHUNK
                               ? (size_t)ISS_CHUNK
                               : (size_t)remaining;
            if ((pd && xx_pd_is_stopped(pd)) ||
                xx_io_read(&input, buffer, chunk) != (ssize_t)chunk ||
                xx_io_write(&output, buffer, chunk) != (ssize_t)chunk)
                goto done;
            remaining -= (int64_t)chunk;
        }
        result = sink.total == (uint64_t)member->size;
    } else {
        uint8_t head[2];
        uint8_t trailer[4];
        uint32_t expected, actual;
        /* Two header bytes, at least one Deflate byte, the Adler-32. */
        if (member->size < 7) goto done;
        if (xx_io_seek64(&input, member->size - 4, SEEK_SET) != 0 ||
            xx_io_read(&input, trailer, 4U) != 4 ||
            xx_io_seek64(&input, 0, SEEK_SET) != 0 ||
            xx_io_read(&input, head, 2U) != 2 ||
            !xx_zlib_stream_header_is_valid(head, 2U))
            goto done;
        expected = ((uint32_t)trailer[0] << 24U) |
                   ((uint32_t)trailer[1] << 16U) |
                   ((uint32_t)trailer[2] << 8U) | (uint32_t)trailer[3];
        if (!xx_deflate_unpack_device(&input, 2, member->size - 2, &output,
                                      false, pd))
            goto done;
        actual = (sink.b << 16U) | sink.a;
        result = actual == expected;
    }
done:
    if (buffer) xx_mem_free(buffer);
    if (salted) xx_mem_free(salted);
    if (cipher) xx_mem_free(cipher);
    return result;
}

/* ---- record stream ---------------------------------------------------------- */

static void iss_stream_free(void *opaque) {
    iss_stream *stream = (iss_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    if (stream->raw) xx_mem_free(stream->raw);
    if (stream->key) xx_mem_free(stream->key);
    if (stream->name) xx_mem_free(stream->name);
    xx_mem_free(stream);
}

static int iss_compare_keys(const void *left, const void *right) {
    const iss_key *a = (const iss_key *)left;
    const iss_key *b = (const iss_key *)right;
    if (a->hash != b->hash) return a->hash < b->hash ? -1 : 1;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

static bool iss_open_stream(Abstractformat *format, iss_stream **result,
                            xx_pd_struct *pd) {
    iss_layout layout;
    iss_stream *stream = NULL;
    iss_key *keys = NULL;
    size_t index;
    if (!result || !iss_read_layout(format, &layout)) return false;
    stream = (iss_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    /* At most 65535 records: under 3 MiB of bookkeeping. */
    stream->items =
        (iss_member *)xx_mem_calloc(layout.declared, sizeof(iss_member));
    stream->raw = (uint8_t *)xx_mem_alloc(ISS_MAX_NAME_BYTES);
    stream->key = (uint8_t *)xx_mem_alloc(ISS_KEY_BUFFER);
    stream->name = (char *)xx_mem_alloc(ISS_NAME_BUFFER);
    if (!stream->items || !stream->raw || !stream->key || !stream->name ||
        !iss_walk(format->device, &layout, stream->items, pd))
        goto fail;
    stream->count = layout.complete;
    keys = (iss_key *)xx_mem_alloc(stream->count * sizeof(*keys));
    if (!keys) goto fail;
    for (index = 0U; index < stream->count; ++index) {
        size_t length = 0U;
        if ((index & ISS_POLL_MASK) == 0U && pd && xx_pd_is_stopped(pd))
            goto fail;
        if (!iss_load_name(format->device, stream, index, false, &length))
            goto fail;
        keys[index].hash = iss_name_hash(stream->name, length);
        keys[index].index = (uint32_t)index;
    }
    if (stream->count > 1U) {
        xx_rt_qsort(keys, stream->count, sizeof(*keys), iss_compare_keys);
        for (index = 1U; index < stream->count; ++index)
            if (keys[index].hash == keys[index - 1U].hash &&
                keys[index].index < stream->count)
                stream->items[keys[index].index].renamed = true;
    }
    xx_mem_free(keys);
    *result = stream;
    return true;
fail:
    if (keys) xx_mem_free(keys);
    iss_stream_free(stream);
    return false;
}

static bool iss_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static bool iss_set_record(Abstractformat *format, xx_archive_record *record,
                           iss_stream *stream, size_t index) {
    const iss_member *member = &stream->items[index];
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    if (!iss_load_name(format->device, stream, index, true, NULL))
        return false;
    record->header_offset = member->header_offset;
    record->header_size = (int64_t)ISS_RECORD_SIZE + (int64_t)member->name_bytes;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    if (!xx_archive_record_set_original_name(record, stream->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        (uint64_t)member->size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        (uint64_t)member->storage) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         member->selector ==
                                             ISS_SELECTOR_BAD) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false))
        return false;
    /* A zlib member's unpacked size is recorded nowhere. */
    if (member->storage == 0U &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        (uint64_t)member->size))
        return false;
    return true;
}

/* ---- public API ------------------------------------------------------------ */

void xx_installshield_issetupstream_init(xx_installshield_issetupstream *archive,
                                         xx_io_device *device,
                                         int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_INSTALLSHIELD_ISSETUPSTREAM_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-installshield");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid =
        xx_installshield_issetupstream_check_is_valid;
    archive->format.handle_base_info =
        xx_installshield_issetupstream_handle_base_info;
    archive->format.get_format_size =
        xx_installshield_issetupstream_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_installshield_issetupstream_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_installshield_issetupstream_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_installshield_issetupstream_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_installshield_issetupstream_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_installshield_issetupstream_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_installshield_issetupstream_free_archive_records_reading;
    archive->stream_offset = -1;
    archive->payload_end = -1;
}

xx_installshield_issetupstream *xx_installshield_issetupstream_create(
    xx_io_device *device, int64_t base_address) {
    xx_installshield_issetupstream *archive =
        (xx_installshield_issetupstream *)xx_mem_alloc(sizeof(*archive));
    if (archive)
        xx_installshield_issetupstream_init(archive, device, base_address);
    return archive;
}

void xx_installshield_issetupstream_destroy(
    xx_installshield_issetupstream *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_installshield_issetupstream_free(
    xx_installshield_issetupstream *archive) {
    if (!archive) return;
    xx_installshield_issetupstream_destroy(archive);
    xx_mem_free(archive);
}

bool xx_installshield_issetupstream_check_is_valid(Abstractformat *format,
                                                   xx_pd_struct *pd) {
    iss_layout layout;
    return iss_read_layout(format, &layout) &&
           iss_walk(format->device, &layout, NULL, pd);
}

bool xx_installshield_issetupstream_handle_base_info(Abstractformat *format,
                                                     xx_pd_struct *pd) {
    iss_layout layout;
    xx_installshield_issetupstream *archive;
    if (!iss_read_layout(format, &layout) ||
        !iss_walk(format->device, &layout, NULL, pd))
        return false;
    archive = (xx_installshield_issetupstream *)format;
    archive->number_of_records = layout.complete;
    archive->declared_records = layout.declared;
    archive->container_version = layout.version;
    archive->stream_offset = layout.tag;
    archive->payload_end = layout.end;
    archive->in_pe = layout.in_pe;
    archive->truncated = layout.truncated;
    xx_format_set_version(format, layout.version == 2U ? "2" : "3");
    format->number_of_archive_records = layout.complete;
    format->format_size = layout.end - format->base_address;
    if (layout.end < layout.total) {
        format->overlay_offset = layout.end;
        format->overlay_size = layout.total - layout.end;
    } else {
        format->overlay_offset = -1;
        format->overlay_size = 0;
    }
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_installshield_issetupstream_get_format_size(Abstractformat *format,
                                                       xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_installshield_issetupstream_handle_base_info(format,
                                                                      pd))
               ? format->format_size
               : -1;
}

uint64_t xx_installshield_issetupstream_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_installshield_issetupstream_handle_base_info(format,
                                                                      pd))
               ? ((xx_installshield_issetupstream *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *
xx_installshield_issetupstream_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    iss_stream *stream;
    xx_archive_record_state *state;
    if (!iss_open_stream(format, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        iss_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = iss_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!iss_copy_options(&state->options, options) ||
        !iss_set_record(format, &state->current_record, stream, 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *
xx_installshield_issetupstream_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_installshield_issetupstream_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    iss_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (iss_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = iss_set_record(format, &state->current_record, stream,
                                       stream->index);
    return state->has_record;
}

bool xx_installshield_issetupstream_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
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
    member = &stream->items[stream->index];
    path_option = iss_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        /* No destination: decode the member through, which verifies it. */
        return iss_decode(format->device, member, stream->key,
                          stream->key_length, NULL, pd);
    /* stream->name was built from the file by iss_load_name: refuse it
     * before anything is created when it could escape the output folder or
     * name a device. */
    if (!stream->name_ok || !iss_safe_name(stream->name)) return false;
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
               ? xx_str_concat3(base, "/", stream->name)
               : xx_str_concat(base, stream->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = iss_decode(format->device, member, stream->key,
                            stream->key_length, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_installshield_issetupstream_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
