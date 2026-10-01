/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Clickteam Multimedia Fusion 2 / Fusion 2.5 stand-alone application: a PE
 * runtime stub whose overlay carries the runtime pack (mmfs2.dll, the *.mfx
 * extensions, the filters) followed by the game's own ".ccn" data.  The
 * header comment in xx_sfx_clickteam_multimedia_fusion.h has the layout.
 *
 * The executable is parsed only as far as its section table, to find where
 * the overlay starts; no code is run or emulated.
 *
 * The acceptance rules of the pack header and the first-record probe follow
 * XArchive installers/xclickteam.cpp (MIT, same author), which documents the
 * layout, and were checked against the five games of the reference corpus.
 * The code here is written from that description, not ported.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sfx_clickteam_multimedia_fusion/xx_sfx_clickteam_multimedia_fusion.h"

#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder: xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * the real file type is picked up as soon as the format is registered. */
#ifdef SFX_CLICKTEAM_MULTIMEDIA_FUSION

#define XX_SFX_CLICKTEAM_MULTIMEDIA_FUSION_FILE_TYPE \
    XX_FILE_TYPE_SFX_CLICKTEAM_MULTIMEDIA_FUSION
#else
#define XX_SFX_CLICKTEAM_MULTIMEDIA_FUSION_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define MMF_MAGIC1 UINT32_C(0x77777777)
#define MMF_MAGIC2 UINT32_C(0x12478749)
#define MMF_HEADER_SIZE 0x20U
#define MMF_MAX_FILES UINT32_C(65536)
#define MMF_MAX_NAME 512U
/* The probe reads the name length, the longest UTF-16 name and the 11 bytes
 * the size fields and the payload's first three bytes need. */
#define MMF_PROBE_SIZE (2U + 2U * MMF_MAX_NAME + 11U)
#define MMF_RECORD_HEAD_MAX (2U * MMF_MAX_NAME + 8U)
/* Published names: at most this many characters before the de-duplication
 * suffix.  The raw names of one table may take at most MMF_NAME_BUDGET
 * bytes, which caps what the decoded names can cost at a few times that. */
#define MMF_NAME_CHARS 240U
#define MMF_NAME_BUDGET (4U << 20)
#define MMF_DEDUP_TRIES 32U
#define MMF_CCN_NAME "1.ccn"

/* PE: enough of the headers to find the end of the section data. */
#define MMF_PE_MAX_SECTIONS 96U
#define MMF_PE_SECTION_SIZE 40U
#define MMF_PE_OPTIONAL_MAX 240U

/* Largest inflated member when the caller sets no limit.  Runtime files
 * are a few MB at most; stored members are only copied and are bounded by
 * the input anyway. */
#define MMF_DEFAULT_MAX_INFLATED (UINT64_C(256) << 20)
#define MMF_COPY_BUFFER 0x10000U
/* Largest packed zlib member whose stream may end before its packed size
 * (see mmf_adler_before_slack); it is decoded a second time in memory. */
#define MMF_SLACK_MAX (UINT32_C(16) << 20)
#define MMF_SSIZE_LIMIT (((size_t)-1) >> 1U)

enum {
    MMF_KIND_STORED = 0,
    MMF_KIND_ZLIB = 1,
    MMF_KIND_CCN = 2
};

typedef struct mmf_layout_s {
    int64_t pack;          /* device offset of the pack header */
    int64_t end;           /* device offset where the pack data ends */
    int64_t total;         /* bytes from base_address to the device end */
    uint32_t declared;
    bool unicode;
    bool two_fields;
    bool first_stored;
} mmf_layout;

typedef struct mmf_entry_s {
    char *name;            /* published UTF-8 name */
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint32_t checksum;
    uint8_t kind;
    bool extractable;
} mmf_entry;

typedef struct mmf_table_s {
    mmf_entry *entries;
    size_t count;          /* entries in use, "1.ccn" included */
    size_t capacity;
    uint32_t parsed;       /* complete packed-file records */
    bool truncated;
    int64_t ccn_offset;
    int64_t ccn_size;
    /* Case-insensitive set of published names: slot = entry index + 1,
     * with the name's full keyed hash beside it (see mmf_name_hash). */
    uint32_t *slots;
    uint32_t *hashes;
    size_t mask;
    uint64_t key0;
    uint64_t key1;
} mmf_table;

typedef struct mmf_stream_s {
    mmf_layout layout;
    mmf_table table;
    size_t index;
    uint64_t max_inflated;
    uint64_t max_member;   /* UINT64_MAX when unset */
} mmf_stream;

/* ---------------------------------------------------------------------- */
/* Helpers                                                                 */

#include "xxfclib/global/xx_global.h"
static size_t gb_sfx_clickteam_multimedia_fusion_capacity(void) {
    size_t n = xx_get_file_buffer_size();
    if (!n) n = XX_DEFAULT_FILE_BUFFER_SIZE;
    return n > (SIZE_MAX >> 1) ? SIZE_MAX >> 1 : n;
}
static ssize_t gb_sfx_clickteam_multimedia_fusion_read(xx_io_device *device, void *buffer, size_t size, size_t capacity) {
    size_t done = 0;
    if (size > (SIZE_MAX >> 1)) return -1;
    while (done < size) {
        size_t take = size - done;
        ssize_t n;
        if (take > capacity) take = capacity;
        n = xx_io_read(device, (uint8_t *)buffer + done, take);
        if (n < 0 || (size_t)n > take) return -1;
        if (!n) break;
        done += (size_t)n;
    }
    return (ssize_t)done;
}
static ssize_t gb_sfx_clickteam_multimedia_fusion_write(xx_io_device *device, const void *buffer, size_t size, size_t capacity) {
    size_t done = 0;
    if (size > (SIZE_MAX >> 1)) return -1;
    while (done < size) {
        size_t take = size - done;
        ssize_t n;
        if (take > capacity) take = capacity;
        n = xx_io_write(device, (const uint8_t *)buffer + done, take);
        if (n < 0 || (size_t)n > take) return -1;
        if (!n) break;
        done += (size_t)n;
    }
    return (ssize_t)done;
}


static uint32_t mmf_le16(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U);
}

static uint32_t mmf_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static bool mmf_read_at(xx_io_device *device, int64_t offset, void *buffer,
                        size_t size) {
    const size_t file_io_capacity = gb_sfx_clickteam_multimedia_fusion_capacity();
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = gb_sfx_clickteam_multimedia_fusion_read(device, (uint8_t *)buffer + done,
                                    size - done, file_io_capacity);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* The zlib header test of the first-record probe: deflate with a window of
 * at most 32K, a valid check value, no preset dictionary, and a first
 * block whose type is not the reserved one. */
static bool mmf_zlib_start(const uint8_t *p) {
    uint32_t cmf = p[0], flg = p[1];
    return (cmf & 0x0FU) == 8U && (cmf >> 4U) < 8U &&
           ((cmf << 8U) | flg) % 31U == 0U && (flg & 0x20U) == 0U &&
           ((p[2] >> 1U) & 3U) != 3U;
}

static uint32_t mmf_rol1(uint32_t value) {
    return (value << 1U) | (value >> 31U);
}

uint32_t xx_sfx_clickteam_multimedia_fusion_checksum(const uint8_t *data,
                                                     size_t size) {
    uint32_t sum = 0U;
    size_t index = 0U;
    if (!data) return 0U;
    for (; size - index >= 4U; index += 4U)
        sum = mmf_rol1(sum) + mmf_le32(data + index);
    for (; index < size; ++index) sum = mmf_rol1(sum) + data[index];
    return sum;
}

/* ---------------------------------------------------------------------- */
/* Locating the pack                                                       */

/* End of the PE image's file data (headers and every section's raw data)
 * and where an Authenticode certificate that closes the file begins.  Both
 * are relative to base_address. */
static bool mmf_pe_extent(xx_io_device *device, int64_t base, int64_t size,
                          int64_t *raw_end_out, int64_t *data_end_out,
                          uint32_t *alignment_out) {
    uint8_t dos[0x40];
    uint8_t nt[24];
    uint8_t optional[MMF_PE_OPTIONAL_MAX];
    uint8_t sections[MMF_PE_MAX_SECTIONS * MMF_PE_SECTION_SIZE];
    uint32_t nt_offset, section_count, optional_size, magic, directories;
    uint32_t security_entry, index;
    size_t optional_read;
    int64_t raw_end, section_offset, data_end = size;
    if (size < (int64_t)sizeof(dos) ||
        !mmf_read_at(device, base, dos, sizeof(dos)) ||
        dos[0] != 'M' || dos[1] != 'Z')
        return false;
    nt_offset = mmf_le32(dos + 0x3C);
    if (nt_offset < 4U || (int64_t)nt_offset > size - (int64_t)sizeof(nt) ||
        !mmf_read_at(device, base + nt_offset, nt, sizeof(nt)) ||
        xx_rt_memcmp(nt, "PE\0\0", 4U) != 0)
        return false;
    section_count = mmf_le16(nt + 6);
    optional_size = mmf_le16(nt + 20);
    if (section_count == 0U || section_count > MMF_PE_MAX_SECTIONS ||
        optional_size < 64U)
        return false;
    optional_read = optional_size < MMF_PE_OPTIONAL_MAX ? optional_size
                                                         : MMF_PE_OPTIONAL_MAX;
    section_offset = (int64_t)nt_offset + 24 + optional_size;
    if (section_offset > size ||
        (int64_t)section_count * MMF_PE_SECTION_SIZE > size - section_offset ||
        !mmf_read_at(device, base + nt_offset + 24, optional, optional_read) ||
        !mmf_read_at(device, base + section_offset, sections,
                     section_count * MMF_PE_SECTION_SIZE))
        return false;
    magic = mmf_le16(optional);
    if (magic == 0x10BU) {
        directories = 96U;
    } else if (magic == 0x20BU) {
        directories = 112U;
    } else {
        return false;
    }
    raw_end = (int64_t)mmf_le32(optional + 60);  /* SizeOfHeaders */
    if (raw_end > size) return false;
    for (index = 0U; index < section_count; ++index) {
        const uint8_t *entry = sections + index * MMF_PE_SECTION_SIZE;
        int64_t raw_size = (int64_t)mmf_le32(entry + 16);
        int64_t raw_offset = (int64_t)mmf_le32(entry + 20);
        if (raw_size == 0) continue;
        if (raw_offset > size || raw_size > size - raw_offset) return false;
        if (raw_offset + raw_size > raw_end) raw_end = raw_offset + raw_size;
    }
    /* Authenticode is appended behind the pack.  Its directory entry holds
     * a file offset; it is only taken when the certificate runs to the end
     * of the file.  mmf_locate() falls back to the whole overlay when an odd
     * entry points into the pack header. */
    security_entry = directories + 4U * 8U;
    if (optional_read >= security_entry + 8U &&
        mmf_le32(optional + directories - 4U) > 4U) {
        int64_t cert_offset = (int64_t)mmf_le32(optional + security_entry);
        int64_t cert_size = (int64_t)mmf_le32(optional + security_entry + 4U);
        if (cert_size > 0 && cert_offset > raw_end && cert_offset <= size &&
            cert_size == size - cert_offset)
            data_end = cert_offset;
    }
    *raw_end_out = raw_end;
    *data_end_out = data_end;
    *alignment_out = mmf_le32(optional + 36);  /* FileAlignment */
    return true;
}

/* One reading of the first record's size fields: the packed data starts at
 * @p data and must open with an MZ image or a zlib header and fit the pack. */
static bool mmf_probe_payload(const uint8_t *head, size_t data,
                              uint32_t packed, int64_t room, bool *stored) {
    if (packed == 0U || packed > (uint32_t)INT32_MAX ||
        (int64_t)data > room || (int64_t)packed > room - (int64_t)data)
        return false;
    if (head[data] == 'M' && head[data + 1U] == 'Z') {
        *stored = true;
        return true;
    }
    if (mmf_zlib_start(head + data)) {
        *stored = false;
        return true;
    }
    return false;
}

/* Probe the first record under one name encoding.  @p head holds @p avail
 * bytes from pack + 0x20; @p room is how many pack bytes follow there.
 *
 * As in both references, the first dword decides the layout: zero can only
 * be the empty checksum of the two-field layout (a packed size is never
 * zero), anything else is the single packed-size field.  Every known build
 * leaves the first record's checksum empty; a pack whose first record
 * carries one is not accepted, since neither reference would read it. */
static bool mmf_probe_record(const uint8_t *head, size_t avail, int64_t room,
                             bool unicode, bool *two_fields, bool *stored) {
    uint32_t length, first, second;
    size_t name_bytes, position;
    if (avail < 2U) return false;
    length = mmf_le16(head);
    if (length == 0U || length > MMF_MAX_NAME) return false;
    name_bytes = unicode ? (size_t)length * 2U : (size_t)length;
    position = 2U + name_bytes;
    if (avail < position || avail - position < 11U) return false;
    first = mmf_le32(head + position);
    second = mmf_le32(head + position + 4U);
    if (first != 0U) {
        *two_fields = false;
        return mmf_probe_payload(head, position + 4U, first, room, stored);
    }
    *two_fields = true;
    return mmf_probe_payload(head, position + 8U, second, room, stored);
}

static bool mmf_try_pack(xx_io_device *device, int64_t pack, int64_t end,
                         mmf_layout *layout) {
    uint8_t header[MMF_HEADER_SIZE];
    uint8_t head[MMF_PROBE_SIZE];
    int64_t room;
    size_t avail;
    uint32_t count;
    bool two_u = false, stored_u = false, two_a = false, stored_a = false;
    bool as_unicode, as_ansi;
    if (pack < 0 || end - pack < (int64_t)MMF_HEADER_SIZE + 2 ||
        !mmf_read_at(device, pack, header, sizeof(header)) ||
        mmf_le32(header) != MMF_MAGIC1 || mmf_le32(header + 4) != MMF_MAGIC2 ||
        mmf_le32(header + 8) != MMF_HEADER_SIZE ||
        mmf_le32(header + 0x14) != 0U || mmf_le32(header + 0x18) != 0U)
        return false;
    count = mmf_le32(header + 0x1C);
    if (count == 0U || count > MMF_MAX_FILES) return false;
    room = end - pack - (int64_t)MMF_HEADER_SIZE;
    avail = room < (int64_t)sizeof(head) ? (size_t)room : sizeof(head);
    if (!mmf_read_at(device, pack + MMF_HEADER_SIZE, head, avail)) return false;
    as_unicode = mmf_probe_record(head, avail, room, true, &two_u, &stored_u);
    as_ansi = mmf_probe_record(head, avail, room, false, &two_a, &stored_a);
    /* Exactly one encoding may fit: that is what keeps an 8-bit reading of
     * a UTF-16 name (or the reverse) from being accepted. */
    if (as_unicode == as_ansi) return false;
    layout->pack = pack;
    layout->end = end;
    layout->declared = count;
    layout->unicode = as_unicode;
    layout->two_fields = as_unicode ? two_u : two_a;
    layout->first_stored = as_unicode ? stored_u : stored_a;
    return true;
}

/* The pack header sits at the end of the section data, or at that offset
 * rounded up to the file alignment: a section whose raw size is not a
 * multiple of the alignment still makes the linker pad the file. */
static bool mmf_find_pack(Abstractformat *format, int64_t raw_end,
                          int64_t data_end, uint32_t alignment,
                          mmf_layout *found) {
    int64_t aligned;
    if (raw_end >= data_end) return false;
    if (mmf_try_pack(format->device, format->base_address + raw_end,
                     format->base_address + data_end, found))
        return true;
    if (alignment < 0x200U || alignment > 0x10000U ||
        (alignment & (alignment - 1U)) != 0U)
        return false;
    aligned = (raw_end + (int64_t)alignment - 1) & ~((int64_t)alignment - 1);
    return aligned != raw_end && aligned < data_end &&
           mmf_try_pack(format->device, format->base_address + aligned,
                        format->base_address + data_end, found);
}

static bool mmf_locate(Abstractformat *format, mmf_layout *layout) {
    int64_t total, size, raw_end = 0, data_end = 0;
    uint32_t alignment = 0U;
    mmf_layout found;
    if (!format || !format->device || !layout || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (!mmf_pe_extent(format->device, format->base_address, size, &raw_end,
                       &data_end, &alignment))
        return false;
    xx_mem_zero(&found, sizeof(found));
    if (!mmf_find_pack(format, raw_end, data_end, alignment, &found) &&
        (data_end == size ||
         !mmf_find_pack(format, raw_end, size, alignment, &found)))
        return false;
    found.total = size;
    *layout = found;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Member names                                                            */

static void mmf_put_utf8(char *out, size_t *length, uint32_t code) {
    if (code < 0x80U) {
        out[(*length)++] = (char)code;
    } else if (code < 0x800U) {
        out[(*length)++] = (char)(0xC0U | (code >> 6U));
        out[(*length)++] = (char)(0x80U | (code & 0x3FU));
    } else if (code < 0x10000U) {
        out[(*length)++] = (char)(0xE0U | (code >> 12U));
        out[(*length)++] = (char)(0x80U | ((code >> 6U) & 0x3FU));
        out[(*length)++] = (char)(0x80U | (code & 0x3FU));
    } else {
        out[(*length)++] = (char)(0xF0U | (code >> 18U));
        out[(*length)++] = (char)(0x80U | ((code >> 12U) & 0x3FU));
        out[(*length)++] = (char)(0x80U | ((code >> 6U) & 0x3FU));
        out[(*length)++] = (char)(0x80U | (code & 0x3FU));
    }
}

/* Decode a raw name to UTF-8 (8-bit names are taken as Latin-1).  Returns
 * NULL for a name that cannot be represented: NUL, an unpaired surrogate,
 * or no memory. */
static char *mmf_decode_name(const uint8_t *raw, uint32_t length,
                             bool unicode) {
    char *out = (char *)xx_mem_alloc((size_t)length * 3U + 1U);
    size_t used = 0U;
    uint32_t index;
    if (!out) return NULL;
    for (index = 0U; index < length; ++index) {
        uint32_t code;
        if (!unicode) {
            code = raw[index];
        } else {
            code = mmf_le16(raw + (size_t)index * 2U);
            if (code >= 0xD800U && code <= 0xDBFFU) {
                uint32_t low;
                if (index + 1U >= length) goto bad;
                low = mmf_le16(raw + (size_t)(index + 1U) * 2U);
                if (low < 0xDC00U || low > 0xDFFFU) goto bad;
                code = 0x10000U + ((code - 0xD800U) << 10U) + (low - 0xDC00U);
                ++index;
            } else if (code >= 0xDC00U && code <= 0xDFFFU) {
                goto bad;
            }
        }
        if (code == 0U) goto bad;
        mmf_put_utf8(out, &used, code);
    }
    out[used] = 0;
    return out;
bad:
    xx_mem_free(out);
    return NULL;
}

static char mmf_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* A plain base name that is safe to create in the output directory: no
 * separators, drive colons or wildcard/reserved punctuation, no control
 * characters, not "." or ".." (no trailing dot or space at all), and not a
 * Windows device name in any case, with or without an extension. */
static bool mmf_safe_name(const char *name) {
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL",
                                          "CONIN$", "CONOUT$", "CLOCK$"};
    size_t length, index, stem = 0U, characters = 0U, device;
    if (!name || !name[0]) return false;
    length = xx_str_len(name);
    for (index = 0U; index < length; ++index) {
        unsigned char c = (unsigned char)name[index];
        if (c < 0x20U || c == 0x7FU || c == '/' || c == '\\' || c == ':' ||
            c == '<' || c == '>' || c == '"' || c == '|' || c == '?' ||
            c == '*')
            return false;
        /* C1 controls, U+0080..U+009F, are C2 80..C2 9F in UTF-8. */
        if (c == 0xC2U && index + 1U < length &&
            (unsigned char)name[index + 1U] >= 0x80U &&
            (unsigned char)name[index + 1U] <= 0x9FU)
            return false;
        if ((c & 0xC0U) != 0x80U) ++characters;
    }
    if (characters > MMF_NAME_CHARS || name[length - 1U] == '.' ||
        name[length - 1U] == ' ')
        return false;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (device = 0U; device < sizeof(devices) / sizeof(devices[0]); ++device) {
        const char *word = devices[device];
        size_t k = 0U;
        while (k < stem && word[k] && mmf_upper(name[k]) == word[k]) ++k;
        if (k == stem && word[k] == 0) return false;
    }
    if (stem >= 4U &&
        ((mmf_upper(name[0]) == 'C' && mmf_upper(name[1]) == 'O' &&
          mmf_upper(name[2]) == 'M') ||
         (mmf_upper(name[0]) == 'L' && mmf_upper(name[1]) == 'P' &&
          mmf_upper(name[2]) == 'T'))) {
        /* COM0..COM9, LPT0..LPT9 and the superscript-digit forms
         * (U+00B9, U+00B2, U+00B3, UTF-8 C2 B9 / C2 B2 / C2 B3). */
        if (stem == 4U && name[3] >= '0' && name[3] <= '9') return false;
        if (stem == 5U && (unsigned char)name[3] == 0xC2U &&
            ((unsigned char)name[4] == 0xB9U ||
             (unsigned char)name[4] == 0xB2U ||
             (unsigned char)name[4] == 0xB3U))
            return false;
    }
    return true;
}

/* Next code point of a published name.  Published names are well-formed
 * UTF-8 (mmf_decode_name, or ASCII built here), so no validation is done
 * beyond never stepping over the terminator. */
static uint32_t mmf_next_code(const char **cursor) {
    const unsigned char *p = (const unsigned char *)*cursor;
    uint32_t code = p[0];
    size_t extra = 0U, index;
    if (code >= 0xF0U) {
        code &= 0x07U;
        extra = 3U;
    } else if (code >= 0xE0U) {
        code &= 0x0FU;
        extra = 2U;
    } else if (code >= 0xC0U) {
        code &= 0x1FU;
        extra = 1U;
    }
    for (index = 1U; index <= extra && p[index] != 0U; ++index)
        code = (code << 6U) | (p[index] & 0x3FU);
    *cursor = (const char *)(p + index);
    return code;
}

/* Upper-case one code point the way a case-insensitive file system does,
 * for the scripts file names are written in: ASCII, Latin-1, Latin
 * Extended-A and Additional, Greek, Cyrillic, Armenian, full-width Latin.
 * Folding more than NTFS does only renames a member that would not have
 * collided, which is the safe direction, so the table errs that way (the
 * dotless i, long s, final sigma and micro sign fold too). */
static uint32_t mmf_fold(uint32_t c) {
    if (c < 0x80U) return (c >= 'a' && c <= 'z') ? c - 0x20U : c;
    if (c == 0xB5U) return 0x39CU;
    if (c >= 0xE0U && c <= 0xFEU && c != 0xF7U) return c - 0x20U;
    if (c == 0xFFU) return 0x178U;
    if (c < 0x100U) return c;
    if (c < 0x180U) {
        if (c == 0x130U || c == 0x131U) return 'I';
        if (c == 0x17FU) return 'S';
        if ((c >= 0x139U && c <= 0x148U) || (c >= 0x179U && c <= 0x17EU))
            return (c & 1U) ? c : c - 1U;
        if (c <= 0x137U || (c >= 0x14AU && c <= 0x177U))
            return (c & 1U) ? c - 1U : c;
        return c;
    }
    if (c >= 0x370U && c < 0x400U) {
        if (c == 0x3ACU) return 0x386U;
        if (c >= 0x3ADU && c <= 0x3AFU) return c - 0x25U;
        if (c == 0x3C2U) return 0x3A3U;
        if (c >= 0x3B1U && c <= 0x3CBU) return c - 0x20U;
        if (c == 0x3CCU) return 0x38CU;
        if (c == 0x3CDU || c == 0x3CEU) return c - 0x3FU;
        return c;
    }
    if (c >= 0x400U && c < 0x530U) {
        if (c >= 0x430U && c <= 0x44FU) return c - 0x20U;
        if (c >= 0x450U && c <= 0x45FU) return c - 0x50U;
        if ((c >= 0x460U && c <= 0x481U) || (c >= 0x48AU && c <= 0x4BFU) ||
            c >= 0x4D0U)
            return (c & 1U) ? c - 1U : c;
        if (c >= 0x4C1U && c <= 0x4CEU) return (c & 1U) ? c : c - 1U;
        if (c == 0x4CFU) return 0x4C0U;
        return c;
    }
    if (c >= 0x561U && c <= 0x586U) return c - 0x30U;
    if (c >= 0x1E00U && c <= 0x1EFFU && !(c >= 0x1E96U && c <= 0x1E9FU))
        return (c & 1U) ? c - 1U : c;
    if (c >= 0xFF41U && c <= 0xFF5AU) return c - 0x20U;
    return c;
}

/* SipHash-1-3 over the folded code points (four bytes each).  The key is
 * picked per table (mmf_table_key), so a crafted name table cannot aim
 * its names at one run of slots: publicly known unkeyed hashes let a
 * 65,536-name table turn every lookup into a walk of the whole table. */
#define MMF_ROTL64(x, b) (((x) << (b)) | ((x) >> (64U - (b))))
#define MMF_SIPROUND(v0, v1, v2, v3)                                        \
    do {                                                                     \
        v0 += v1; v1 = MMF_ROTL64(v1, 13U); v1 ^= v0;                        \
        v0 = MMF_ROTL64(v0, 32U);                                            \
        v2 += v3; v3 = MMF_ROTL64(v3, 16U); v3 ^= v2;                        \
        v0 += v3; v3 = MMF_ROTL64(v3, 21U); v3 ^= v0;                        \
        v2 += v1; v1 = MMF_ROTL64(v1, 17U); v1 ^= v2;                        \
        v2 = MMF_ROTL64(v2, 32U);                                            \
    } while (0)

static uint32_t mmf_name_hash(const mmf_table *table, const char *name) {
    uint64_t v0 = table->key0 ^ UINT64_C(0x736f6d6570736575);
    uint64_t v1 = table->key1 ^ UINT64_C(0x646f72616e646f6d);
    uint64_t v2 = table->key0 ^ UINT64_C(0x6c7967656e657261);
    uint64_t v3 = table->key1 ^ UINT64_C(0x7465646279746573);
    uint64_t block = 0U, length = 0U;
    unsigned half = 0U;
    while (*name) {
        uint64_t code = mmf_fold(mmf_next_code(&name));
        length += 4U;
        if (half == 0U) {
            block = code;
            half = 1U;
            continue;
        }
        block |= code << 32U;
        half = 0U;
        v3 ^= block;
        MMF_SIPROUND(v0, v1, v2, v3);
        v0 ^= block;
    }
    block = (half ? block : 0U) | (length << 56U);
    v3 ^= block;
    MMF_SIPROUND(v0, v1, v2, v3);
    v0 ^= block;
    v2 ^= 0xFFU;
    MMF_SIPROUND(v0, v1, v2, v3);
    MMF_SIPROUND(v0, v1, v2, v3);
    MMF_SIPROUND(v0, v1, v2, v3);
    v0 ^= v1 ^ v2 ^ v3;
    return (uint32_t)(v0 ^ (v0 >> 32U));
}

static uint64_t mmf_mix64(uint64_t x) {
    x += UINT64_C(0x9E3779B97F4A7C15);
    x = (x ^ (x >> 30U)) * UINT64_C(0xBF58476D1CE4E5B9);
    x = (x ^ (x >> 27U)) * UINT64_C(0x94D049BB133111EB);
    return x ^ (x >> 31U);
}

/* The key only has to be unknown to whoever wrote the file: the clock,
 * heap and stack addresses (randomised by the loader) and the CRT-free
 * generator's state.  Which names collide never depends on it, so the
 * published names are the same on every run. */
static void mmf_table_key(mmf_table *table) {
    uint64_t seed = (uint64_t)xx_rt_clock_ms();
    seed = mmf_mix64(seed ^ (uint64_t)(uintptr_t)table->slots);
    seed = mmf_mix64(seed ^ (uint64_t)(uintptr_t)&seed);
    seed = mmf_mix64(seed ^ (uint64_t)(uint32_t)xx_rt_rand());
    table->key0 = seed;
    table->key1 = mmf_mix64(seed ^ (uint64_t)(uintptr_t)table->entries);
}

static bool mmf_same_name(const char *left, const char *right) {
    while (*left && *right)
        if (mmf_fold(mmf_next_code(&left)) != mmf_fold(mmf_next_code(&right)))
            return false;
    return *left == *right;
}

static bool mmf_name_taken(const mmf_table *table, const char *name) {
    uint32_t hash = mmf_name_hash(table, name);
    size_t slot = hash & table->mask, probes;
    for (probes = 0U; probes <= table->mask; ++probes) {
        uint32_t value = table->slots[slot];
        if (value == 0U) return false;
        if (table->hashes[slot] == hash &&
            mmf_same_name(table->entries[value - 1U].name, name))
            return true;
        slot = (slot + 1U) & table->mask;
    }
    return true;
}

static void mmf_name_insert(mmf_table *table, size_t index) {
    uint32_t hash = mmf_name_hash(table, table->entries[index].name);
    size_t slot = hash & table->mask, probes;
    for (probes = 0U; probes <= table->mask; ++probes) {
        if (table->slots[slot] == 0U) {
            table->slots[slot] = (uint32_t)(index + 1U);
            table->hashes[slot] = hash;
            return;
        }
        slot = (slot + 1U) & table->mask;
    }
}

/* A name shaped like a generated 8.3 alias ("LONGFI~1.DLL": a stem of at
 * most eight bytes ending in '~' and digits, an extension of at most
 * three).  On a volume with short names it can be the alias of a member
 * already written, so such a name is always published with a suffix. */
static bool mmf_alias_shaped(const char *name) {
    size_t length = xx_str_len(name), stem = length, index;
    for (index = length; index > 0U; --index)
        if (name[index - 1U] == '.') {
            stem = index - 1U;
            break;
        }
    if (stem == 0U || stem > 8U || (stem < length && length - stem - 1U > 3U))
        return false;
    index = stem;
    while (index > 0U && name[index - 1U] >= '0' && name[index - 1U] <= '9')
        --index;
    return index < stem && index > 0U && name[index - 1U] == '~';
}

/* Give entry @p index a unique, safe name derived from @p decoded (which
 * this takes ownership of; NULL when the raw name was unusable).  Unsafe
 * names become "file_NNNN"; a name already published (compared after case
 * folding) or shaped like an 8.3 alias gets "_NNNN" (and a further counter
 * if needed) appended, so no member overwrites another. */
static bool mmf_publish_name(mmf_table *table, size_t index, char *decoded,
                             uint32_t ordinal) {
    mmf_entry *entry = &table->entries[index];
    char *base = decoded;
    char *candidate = NULL;
    size_t base_length, attempt;
    if (!base || !mmf_safe_name(base)) {
        if (base) xx_mem_free(base);
        base = (char *)xx_mem_alloc(32U);
        if (!base) return false;
        (void)xx_rt_snprintf(base, 32U, "file_%04u", (unsigned)ordinal);
    }
    entry->extractable = false;
    if (!mmf_alias_shaped(base) && !mmf_name_taken(table, base)) {
        entry->name = base;
        entry->extractable = true;
    } else {
        base_length = xx_str_len(base);
        candidate = (char *)xx_mem_alloc(base_length + 32U);
        if (!candidate) {
            xx_mem_free(base);
            return false;
        }
        for (attempt = 0U; attempt < MMF_DEDUP_TRIES; ++attempt) {
            if (attempt == 0U)
                (void)xx_rt_snprintf(candidate, base_length + 32U, "%s_%04u",
                                     base, (unsigned)ordinal);
            else
                (void)xx_rt_snprintf(candidate, base_length + 32U,
                                     "%s_%04u_%u", base, (unsigned)ordinal,
                                     (unsigned)attempt);
            if (!mmf_name_taken(table, candidate)) {
                entry->extractable = true;
                break;
            }
        }
        if (entry->extractable) {
            xx_mem_free(base);
            entry->name = candidate;
        } else {
            /* Listed under its colliding name, never written. */
            xx_mem_free(candidate);
            entry->name = base;
        }
    }
    if (entry->extractable) mmf_name_insert(table, index);
    return true;
}

/* ---------------------------------------------------------------------- */
/* The record table                                                        */

static void mmf_table_free(mmf_table *table) {
    size_t index;
    if (!table) return;
    if (table->entries) {
        for (index = 0U; index < table->count; ++index)
            if (table->entries[index].name)
                xx_mem_free(table->entries[index].name);
        xx_mem_free(table->entries);
    }
    if (table->slots) xx_mem_free(table->slots);
    if (table->hashes) xx_mem_free(table->hashes);
    xx_mem_zero(table, sizeof(*table));
}

/* Walk the table.  With @p build set every record is decoded into
 * table->entries; otherwise only the counts and the "1.ccn" extent are
 * measured.  A table that ends before its count (a truncated or damaged
 * file) keeps the records read so far and has no "1.ccn". */
static bool mmf_walk(Abstractformat *format, const mmf_layout *layout,
                     mmf_table *table, bool build, xx_pd_struct *pd) {
    uint8_t head[MMF_RECORD_HEAD_MAX];
    int64_t position = layout->pack + MMF_HEADER_SIZE;
    size_t raw_names = 0U;
    uint32_t index;
    xx_mem_zero(table, sizeof(*table));
    table->ccn_offset = -1;
    if (build) {
        size_t slots = 16U;
        table->capacity = (size_t)layout->declared + 1U;
        while (slots < table->capacity * 2U) slots <<= 1U;
        table->entries = (mmf_entry *)xx_mem_calloc(table->capacity,
                                                    sizeof(mmf_entry));
        table->slots = (uint32_t *)xx_mem_calloc(slots, sizeof(uint32_t));
        table->hashes = (uint32_t *)xx_mem_calloc(slots, sizeof(uint32_t));
        table->mask = slots - 1U;
        if (!table->entries || !table->slots || !table->hashes) goto fail;
        mmf_table_key(table);
    }
    for (index = 0U; index < layout->declared; ++index) {
        uint32_t length, checksum = 0U, packed;
        size_t name_bytes, fields = layout->two_fields ? 8U : 4U;
        int64_t data;
        if ((index & 0xFFU) == 0U && pd && xx_pd_is_stopped(pd)) goto fail;
        if (layout->end - position < 2 ||
            !mmf_read_at(format->device, position, head, 2U))
            break;
        length = mmf_le16(head);
        if (length == 0U || length > MMF_MAX_NAME) break;
        name_bytes = layout->unicode ? (size_t)length * 2U : (size_t)length;
        raw_names += name_bytes;
        if (raw_names > MMF_NAME_BUDGET ||
            layout->end - position - 2 < (int64_t)(name_bytes + fields) ||
            !mmf_read_at(format->device, position + 2, head,
                         name_bytes + fields))
            break;
        if (layout->two_fields) {
            checksum = mmf_le32(head + name_bytes);
            packed = mmf_le32(head + name_bytes + 4U);
        } else {
            packed = mmf_le32(head + name_bytes);
        }
        data = position + 2 + (int64_t)(name_bytes + fields);
        if (packed == 0U || packed > (uint32_t)INT32_MAX ||
            (int64_t)packed > layout->end - data)
            break;
        if (build) {
            mmf_entry *entry = &table->entries[table->count];
            char *decoded;
            uint8_t start[3];
            entry->header_offset = position;
            entry->header_size = data - position;
            entry->data_offset = data;
            entry->packed_size = (int64_t)packed;
            entry->checksum = checksum;
            /* The first record's kind is what the probe found.  A later
             * record is inflated when it opens with a zlib header and
             * copied as it is otherwise, as XArchive does. */
            if (index == 0U) {
                entry->kind = layout->first_stored ? MMF_KIND_STORED
                                                   : MMF_KIND_ZLIB;
            } else {
                entry->kind = packed >= 3U &&
                                      mmf_read_at(format->device, data,
                                                  start, sizeof(start)) &&
                                      mmf_zlib_start(start)
                                  ? MMF_KIND_ZLIB
                                  : MMF_KIND_STORED;
            }
            decoded = mmf_decode_name(head, length, layout->unicode);
            if (!mmf_publish_name(table, table->count, decoded, index))
                goto fail;
            ++table->count;
        }
        ++table->parsed;
        position = data + (int64_t)packed;
    }
    if (table->parsed != layout->declared) {
        table->truncated = true;
        if (table->parsed == 0U) goto fail;
    } else if (position < layout->end) {
        table->ccn_offset = position;
        table->ccn_size = layout->end - position;
        if (build) {
            mmf_entry *entry = &table->entries[table->count];
            char *name = (char *)xx_mem_alloc(sizeof(MMF_CCN_NAME));
            if (!name) goto fail;
            xx_rt_memcpy(name, MMF_CCN_NAME, sizeof(MMF_CCN_NAME));
            entry->header_offset = position;
            entry->header_size = 0;
            entry->data_offset = position;
            entry->packed_size = table->ccn_size;
            entry->kind = MMF_KIND_CCN;
            if (!mmf_publish_name(table, table->count, name,
                                  layout->declared))
                goto fail;
            ++table->count;
        }
    }
    if (!build)
        table->count = (size_t)table->parsed + (table->ccn_offset >= 0 ? 1U : 0U);
    return true;
fail:
    mmf_table_free(table);
    return false;
}

/* ---------------------------------------------------------------------- */
/* Extraction                                                              */

/* Output sink: forwards to the destination (none when only verifying),
 * enforces the size limit and keeps the pack checksum and Adler-32. */
typedef struct mmf_sink_s {
    xx_io_device device;
    xx_io_device *target;
    uint64_t written;
    uint64_t limit;
    uint32_t sum;
    uint32_t pending;
    uint32_t pending_bytes;
    uint32_t adler_a;
    uint32_t adler_b;
} mmf_sink;

static ssize_t mmf_sink_write(xx_io_device *self, const void *buffer,
                              size_t size) {
    const size_t file_io_capacity = gb_sfx_clickteam_multimedia_fusion_capacity();
    mmf_sink *sink = self ? (mmf_sink *)self->priv : NULL;
    const uint8_t *bytes = (const uint8_t *)buffer;
    size_t index = 0U, done = 0U;
    if (!sink || (!bytes && size != 0U) || size > MMF_SSIZE_LIMIT ||
        (uint64_t)size > sink->limit - sink->written)
        return -1;
    while (index < size) {
        size_t chunk = size - index;
        size_t end;
        uint32_t a = sink->adler_a, b = sink->adler_b;
        if (chunk > 5552U) chunk = 5552U;
        for (end = index + chunk; index < end; ++index) {
            uint8_t value = bytes[index];
            a += value;
            b += a;
            sink->pending |= (uint32_t)value << (8U * sink->pending_bytes);
            if (++sink->pending_bytes == 4U) {
                sink->sum = mmf_rol1(sink->sum) + sink->pending;
                sink->pending = 0U;
                sink->pending_bytes = 0U;
            }
        }
        sink->adler_a = a % 65521U;
        sink->adler_b = b % 65521U;
    }
    while (sink->target && done < size) {
        ssize_t wrote = gb_sfx_clickteam_multimedia_fusion_write(sink->target, bytes + done, size - done, file_io_capacity);
        if (wrote <= 0 || (size_t)wrote > size - done) return -1;
        done += (size_t)wrote;
    }
    sink->written += size;
    return (ssize_t)size;
}

static void mmf_sink_init(mmf_sink *sink, xx_io_device *target,
                          uint64_t limit) {
    xx_mem_zero(sink, sizeof(*sink));
    sink->device.write = mmf_sink_write;
    sink->device.priv = sink;
    sink->target = target;
    sink->limit = limit;
    sink->adler_a = 1U;
}

static uint32_t mmf_sink_checksum(const mmf_sink *sink) {
    uint32_t sum = sink->sum, index;
    for (index = 0U; index < sink->pending_bytes; ++index)
        sum = mmf_rol1(sum) + ((sink->pending >> (8U * index)) & 0xFFU);
    return sum;
}

static bool mmf_copy(xx_io_device *source, int64_t offset, int64_t size,
                     mmf_sink *sink, xx_pd_struct *pd) {
    const size_t file_io_capacity = gb_sfx_clickteam_multimedia_fusion_capacity();
    uint8_t *buffer;
    bool result = true;
    if (size < 0 || (uint64_t)size > sink->limit) return false;
    buffer = (uint8_t *)xx_mem_alloc(file_io_capacity);
    if (!buffer) return false;
    while (size > 0) {
        size_t chunk = size < (int64_t)file_io_capacity ? (size_t)size
                                                       : file_io_capacity;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !mmf_read_at(source, offset, buffer, chunk) ||
            mmf_sink_write(&sink->device, buffer, chunk) != (ssize_t)chunk) {
            result = false;
            break;
        }
        offset += (int64_t)chunk;
        size -= (int64_t)chunk;
    }
    xx_mem_free(buffer);
    return result;
}

/* A zlib member whose Adler-32 is not in its last four bytes: decode the
 * deflate data once more, in memory and without output, to learn where the
 * stream ends, and check the Adler-32 that follows it against @p first,
 * the sink of the pass that wrote the member.  Only for members of at
 * most MMF_SLACK_MAX packed bytes; the output limit still applies. */
static bool mmf_adler_before_slack(xx_io_device *device,
                                   const mmf_entry *entry,
                                   const mmf_sink *first, uint64_t limit,
                                   xx_pd_struct *pd) {
    mmf_sink verify;
    uint8_t *buffer;
    size_t size, consumed = 0U;
    bool result = false;
    if (entry->packed_size - 2 > (int64_t)MMF_SLACK_MAX) return false;
    size = (size_t)(entry->packed_size - 2);
    buffer = (uint8_t *)xx_mem_alloc(size);
    if (!buffer) return false;
    mmf_sink_init(&verify, NULL, limit);
    if (mmf_read_at(device, entry->data_offset + 2, buffer, size) &&
        xx_deflate_unpack_memory_to_device_ex(buffer, size, &verify.device,
                                              &consumed, false, pd) &&
        consumed <= size && size - consumed >= 4U &&
        verify.written == first->written &&
        verify.adler_a == first->adler_a && verify.adler_b == first->adler_b) {
        const uint8_t *p = buffer + consumed;
        uint32_t adler = ((uint32_t)p[0] << 24U) | ((uint32_t)p[1] << 16U) |
                         ((uint32_t)p[2] << 8U) | (uint32_t)p[3];
        result = adler == ((first->adler_b << 16U) | first->adler_a);
    }
    xx_mem_free(buffer);
    return result;
}

static bool mmf_unpack_entry(Abstractformat *format, const mmf_stream *stream,
                             const mmf_entry *entry, xx_io_device *target,
                             xx_pd_struct *pd) {
    mmf_sink sink;
    uint64_t limit = stream->max_member;
    if (entry->kind == MMF_KIND_ZLIB && limit > stream->max_inflated)
        limit = stream->max_inflated;
    mmf_sink_init(&sink, target, limit);
    if (entry->kind == MMF_KIND_ZLIB) {
        uint8_t zlib_head[2], trailer[4];
        uint32_t cmf, flg, adler;
        /* Two header bytes, at least one deflate byte, the Adler-32. */
        if (entry->packed_size < 7 ||
            !mmf_read_at(format->device, entry->data_offset, zlib_head, 2U))
            return false;
        cmf = zlib_head[0];
        flg = zlib_head[1];
        if ((cmf & 0x0FU) != 8U || (cmf >> 4U) > 7U ||
            ((cmf << 8U) | flg) % 31U != 0U || (flg & 0x20U) != 0U)
            return false;
        if (!xx_deflate_unpack_device(format->device, entry->data_offset + 2,
                                      entry->packed_size - 2, &sink.device,
                                      false, pd) ||
            !mmf_read_at(format->device,
                         entry->data_offset + entry->packed_size - 4, trailer,
                         4U))
            return false;
        /* Every known build stores the whole zlib stream, so its Adler-32
         * closes the packed data.  When it does not, the stream may end
         * early with slack behind it (the references accept that too):
         * find where it ends and take the Adler-32 from there. */
        adler = ((uint32_t)trailer[0] << 24U) | ((uint32_t)trailer[1] << 16U) |
                ((uint32_t)trailer[2] << 8U) | (uint32_t)trailer[3];
        if (adler != ((sink.adler_b << 16U) | sink.adler_a) &&
            !mmf_adler_before_slack(format->device, entry, &sink, limit, pd))
            return false;
    } else if (!mmf_copy(format->device, entry->data_offset,
                         entry->packed_size, &sink, pd)) {
        return false;
    }
    if (entry->kind != MMF_KIND_CCN && stream->layout.two_fields &&
        entry->checksum != 0U && mmf_sink_checksum(&sink) != entry->checksum)
        return false;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Options and records                                                     */

static bool mmf_copy_options(xx_list_s *destination, const xx_list_s *source) {
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
static uint64_t mmf_max_member(const Abstractformat *format,
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

static bool mmf_set_record(xx_archive_record *record, const mmf_entry *entry) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = entry->header_offset;
    record->header_size = entry->header_size;
    record->data_offset = entry->data_offset;
    record->compressed_size = entry->packed_size;
    if (!xx_archive_record_set_original_name(record, entry->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        (uint64_t)entry->packed_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        entry->kind == MMF_KIND_ZLIB ? 8U
                                                                     : 0U) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false))
        return false;
    /* Only a stored member's size is known before it is inflated. */
    if (entry->kind != MMF_KIND_ZLIB &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        (uint64_t)entry->packed_size))
        return false;
    return true;
}

static void mmf_stream_free(void *opaque) {
    mmf_stream *stream = (mmf_stream *)opaque;
    if (!stream) return;
    mmf_table_free(&stream->table);
    xx_mem_free(stream);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_sfx_clickteam_multimedia_fusion_init(
    xx_sfx_clickteam_multimedia_fusion *archive, xx_io_device *device,
    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SFX_CLICKTEAM_MULTIMEDIA_FUSION_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.os = XX_OS_WINDOWS;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-dosexec");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid =
        xx_sfx_clickteam_multimedia_fusion_check_is_valid;
    archive->format.handle_base_info =
        xx_sfx_clickteam_multimedia_fusion_handle_base_info;
    archive->format.get_format_size =
        xx_sfx_clickteam_multimedia_fusion_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sfx_clickteam_multimedia_fusion_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sfx_clickteam_multimedia_fusion_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sfx_clickteam_multimedia_fusion_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sfx_clickteam_multimedia_fusion_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sfx_clickteam_multimedia_fusion_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sfx_clickteam_multimedia_fusion_free_archive_records_reading;
    archive->pack_offset = -1;
    archive->container_end = -1;
    archive->ccn_offset = -1;
}

xx_sfx_clickteam_multimedia_fusion *xx_sfx_clickteam_multimedia_fusion_create(
    xx_io_device *device, int64_t base_address) {
    xx_sfx_clickteam_multimedia_fusion *archive =
        (xx_sfx_clickteam_multimedia_fusion *)xx_mem_alloc(sizeof(*archive));
    if (archive)
        xx_sfx_clickteam_multimedia_fusion_init(archive, device, base_address);
    return archive;
}

void xx_sfx_clickteam_multimedia_fusion_destroy(
    xx_sfx_clickteam_multimedia_fusion *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_sfx_clickteam_multimedia_fusion_free(
    xx_sfx_clickteam_multimedia_fusion *archive) {
    if (!archive) return;
    xx_sfx_clickteam_multimedia_fusion_destroy(archive);
    xx_mem_free(archive);
}

bool xx_sfx_clickteam_multimedia_fusion_check_is_valid(Abstractformat *format,
                                                       xx_pd_struct *pd) {
    mmf_layout layout;
    (void)pd;
    return mmf_locate(format, &layout);
}

bool xx_sfx_clickteam_multimedia_fusion_handle_base_info(
    Abstractformat *format, xx_pd_struct *pd) {
    xx_sfx_clickteam_multimedia_fusion *archive;
    mmf_layout layout;
    mmf_table table;
    if (!format || !mmf_locate(format, &layout) ||
        !mmf_walk(format, &layout, &table, false, pd))
        return false;
    archive = (xx_sfx_clickteam_multimedia_fusion *)format;
    archive->pack_offset = layout.pack;
    archive->container_end = layout.end;
    archive->ccn_offset = table.ccn_offset;
    archive->ccn_size = table.ccn_offset >= 0 ? table.ccn_size : 0;
    archive->declared_files = layout.declared;
    archive->parsed_files = table.parsed;
    archive->number_of_records = (uint64_t)table.count;
    archive->unicode_names = layout.unicode;
    archive->two_size_fields = layout.two_fields;
    archive->first_stored = layout.first_stored;
    archive->truncated = table.truncated;
    mmf_table_free(&table);
    format->number_of_archive_records = archive->number_of_records;
    format->format_size = layout.total;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_sfx_clickteam_multimedia_fusion_get_format_size(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_clickteam_multimedia_fusion_handle_base_info(
                          format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_sfx_clickteam_multimedia_fusion_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_clickteam_multimedia_fusion_handle_base_info(
                          format, pd))
               ? ((xx_sfx_clickteam_multimedia_fusion *)format)
                     ->number_of_records
               : 0U;
}

xx_archive_record_state *
xx_sfx_clickteam_multimedia_fusion_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    mmf_stream *stream;
    xx_archive_record_state *state;
    if (!format) return NULL;
    stream = (mmf_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!mmf_locate(format, &stream->layout) ||
        !mmf_walk(format, &stream->layout, &stream->table, true, pd) ||
        stream->table.count == 0U) {
        mmf_stream_free(stream);
        return NULL;
    }
    stream->max_member = mmf_max_member(format, options);
    stream->max_inflated = stream->max_member != UINT64_MAX
                               ? stream->max_member
                               : MMF_DEFAULT_MAX_INFLATED;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        mmf_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = mmf_stream_free;
    state->total_records = (int64_t)stream->table.count;
    if (!mmf_copy_options(&state->options, options) ||
        !mmf_set_record(&state->current_record, &stream->table.entries[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->current_index = 0;
    state->has_record = true;
    return state;
}

const xx_archive_record *
xx_sfx_clickteam_multimedia_fusion_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_sfx_clickteam_multimedia_fusion_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    mmf_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (mmf_stream *)state->internal_state) ||
        stream->index + 1U >= stream->table.count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    if (!mmf_set_record(&state->current_record,
                        &stream->table.entries[stream->index])) {
        state->has_record = false;
        return false;
    }
    state->current_index = (int64_t)stream->index;
    state->has_record = true;
    return true;
}

bool xx_sfx_clickteam_multimedia_fusion_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    mmf_stream *stream;
    const mmf_entry *entry;
    const xx_var *path_option;
    const xx_var *overwrite_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (mmf_stream *)state->internal_state) ||
        stream->index >= stream->table.count || (pd && xx_pd_is_stopped(pd)))
        return false;
    entry = &stream->table.entries[stream->index];
    path_option = xx_format_resolve_extra_parameter(
        format, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return mmf_unpack_entry(format, stream, entry, NULL, pd);
    if (!entry->extractable || !mmf_safe_name(entry->name)) return false;
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
    /* Without XX_META_ID_OPT_OVERWRITE an existing file is never replaced:
     * besides a user's own file, this also stops a member whose name only a
     * file system's own case or alias rules make equal to an earlier one.
     */
    overwrite_option = xx_format_resolve_extra_parameter(
        format, &state->options, XX_META_ID_OPT_OVERWRITE);
    {
        bool overwrite =
            overwrite_option && xx_var_get_bool(overwrite_option);
        xx_io_device *destination =
            xx_io_file_open(path, overwrite ? "wb" : "wbx");
        created = destination != NULL;
        if (!destination) goto done;
        result = mmf_unpack_entry(format, stream, entry, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_sfx_clickteam_multimedia_fusion_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
