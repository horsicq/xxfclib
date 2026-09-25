/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for MacBinary I, II and III (the original 1985 format, Yves
 * Lempereur's MacBinary II standard and the MacBinary III addendum).  The
 * container is one 128-byte header followed by up to four regions, each
 * padded to a 128-byte boundary: an optional secondary header (II and
 * later), the data fork, the resource fork and an optional Finder "Get Info"
 * comment (II and later).  Both forks are stored verbatim and are surfaced
 * as stored archive records; the secondary header and the comment are
 * Finder metadata, counted in the container size but not extracted.
 *
 *   off  size  field
 *     0     1  old version number, always 0
 *     1     1  file name length, 1..63
 *     2    63  file name, Mac Roman
 *    65     4  file type
 *    69     4  file creator
 *    73     1  Finder flags, high byte
 *    74     1  zero
 *    75     6  window position and folder id
 *    81     1  "protected" flag
 *    82     1  zero
 *    83     4  data fork length, big endian
 *    87     4  resource fork length
 *    91     4  creation date, seconds since 1904
 *    95     4  modification date
 *    99     2  Get Info comment length (II)
 *   101     1  Finder flags, low byte (II)
 *   102     4  "mBIN" (III)
 *   106    10  script, extended Finder flags, unused
 *   116     4  total unpacked length (unused for plain files)
 *   120     2  secondary header length (II)
 *   122     1  version of the writer: 129 = II, 130 = III
 *   123     1  minimum version needed to read
 *   124     2  CRC-16/XMODEM of bytes 0..123 (II)
 *   126     2  reserved
 *
 * MacBinary has no leading magic number.  MacBinary III adds "mBIN" at 102,
 * and II/III carry a CRC-16/XMODEM of the first 124 header bytes at 124;
 * MacBinary I carries neither.  A II/III header whose CRC verifies is trusted
 * as written (secondary header, comment, forks up to 2 GiB) and is what
 * xx_macbinary_check_is_valid_verified() accepts, so the detector can ask
 * for it ahead of the structural tail probes (ZIP's end-of-central-directory
 * scan would otherwise claim a MacBinary-wrapped ZIP).  Anything else --
 * MacBinary I, or a II header whose CRC field was left stale -- has to prove
 * itself structurally: the reserved areas as the original standard left
 * them, a Finder name without control codes, a printable type and creator,
 * at least one non-empty fork, and the classic 8 MiB fork bound.
 *
 * Neither fork is required to be padded when it is the last thing in the
 * file: several writers drop the final padding, and every reference reader
 * accepts that.  Whatever lies past the last region (and its padding, when
 * present) is not part of the container.
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/macbinary/xx_macbinary.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* The enum entry is added by the coordinator; keep compiling until it is. */
#ifdef MACBINARY
#define XX_MACBINARY_FILE_TYPE XX_FILE_TYPE_MACBINARY
#else
#define XX_MACBINARY_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define MB_HEADER_SIZE 128U
#define MB_BLOCK_SIZE 128U
#define MB_MAX_NAME 63U
/* A header that is not protected by a verified CRC gets the classic Mac OS
 * 8 MiB bound, which every MacBinary I era file honours and which keeps a
 * random header from describing a huge file; a verified II/III header may
 * describe any HFS fork (31 bits). */
#define MB_MAX_FORK_UNVERIFIED UINT32_C(0x007FFFFF)
#define MB_MAX_FORK_VERIFIED UINT32_C(0x7FFFFFFF)
#define MB_OFF_VERSION 0U
#define MB_OFF_NAME_LENGTH 1U
#define MB_OFF_NAME 2U
#define MB_OFF_TYPE 65U
#define MB_OFF_CREATOR 69U
#define MB_OFF_FINDER_FLAGS 73U
#define MB_OFF_ZERO_74 74U
#define MB_OFF_PROTECTED 81U
#define MB_OFF_ZERO_82 82U
#define MB_OFF_DATA_LENGTH 83U
#define MB_OFF_RSRC_LENGTH 87U
#define MB_OFF_CREATED 91U
#define MB_OFF_MODIFIED 95U
#define MB_OFF_COMMENT_LENGTH 99U
#define MB_OFF_SIGNATURE 102U
#define MB_OFF_SECONDARY 120U
#define MB_OFF_VERSION_WRITTEN 122U
#define MB_OFF_VERSION_NEEDED 123U
#define MB_OFF_CRC 124U
#define MB_OFF_RESERVED 126U
#define MB_VERSION_II 129U
#define MB_VERSION_III 130U

typedef struct mb_member_s {
    char *name;
    int64_t data_offset;
    int64_t data_size;
    bool resource;
} mb_member;

typedef struct mb_stream_s {
    mb_member items[2];
    size_t count;
    size_t index;
    int64_t header_offset;
    int64_t archive_size;
    uint32_t type;
    uint32_t creator;
    uint32_t created;
    uint32_t modified;
    uint16_t secondary_length;
    uint16_t comment_length;
    uint8_t finder_flags;
    uint8_t protected_flag;
    uint8_t version_written;
    bool has_signature;
    bool crc_verified;
} mb_stream;

/* Mac OS Roman, 0x80..0xFF, as Unicode code points (Apple's ROMAN.TXT with
 * the Mac OS 8.5 euro sign at 0xDB). */
static const uint16_t k_mb_mac_roman_high[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1,
    0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
    0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
    0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
    0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF,
    0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
    0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,
    0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
    0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
    0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA,
    0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
    0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
    0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
    0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
    0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7
};

static uint32_t mb_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

static uint16_t mb_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | (uint16_t)bytes[1]);
}

static bool mb_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* Callers only pass values below 2^33, so this cannot overflow. */
static int64_t mb_round_up(int64_t value) {
    int64_t remainder = value % (int64_t)MB_BLOCK_SIZE;
    return remainder == 0 ? value
                          : value + ((int64_t)MB_BLOCK_SIZE - remainder);
}

static size_t mb_put_utf8(char *out, uint32_t code_point) {
    if (code_point < 0x80U) {
        out[0] = (char)code_point;
        return 1U;
    }
    if (code_point < 0x800U) {
        out[0] = (char)(0xC0U | (code_point >> 6U));
        out[1] = (char)(0x80U | (code_point & 0x3FU));
        return 2U;
    }
    out[0] = (char)(0xE0U | (code_point >> 12U));
    out[1] = (char)(0x80U | ((code_point >> 6U) & 0x3FU));
    out[2] = (char)(0x80U | (code_point & 0x3FU));
    return 3U;
}

static char mb_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* True when the first @p stem bytes of @p name spell @p word, ignoring
 * ASCII case. */
static bool mb_stem_is(const char *name, size_t stem, const char *word) {
    size_t index;
    for (index = 0U; index < stem; ++index)
        if (!word[index] || mb_upper(name[index]) != word[index]) return false;
    return word[stem] == 0;
}

/* Windows opens a device instead of a file for CON, PRN, AUX, NUL, COM0-9,
 * LPT0-9, CONIN$, CONOUT$ and CLOCK$, with or without an extension, in any
 * case and with trailing spaces before the extension. */
static bool mb_is_device_name(const char *name, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t stem = 0U, index;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (mb_stem_is(name, stem, devices[index])) return true;
    return stem == 4U && name[3] >= '0' && name[3] <= '9' &&
           ((mb_upper(name[0]) == 'C' && mb_upper(name[1]) == 'O' &&
             mb_upper(name[2]) == 'M') ||
            (mb_upper(name[0]) == 'L' && mb_upper(name[1]) == 'P' &&
             mb_upper(name[2]) == 'T'));
}

/* The stored name is Mac Roman and may legally hold bytes a file system
 * would choke on.  It is converted to UTF-8 and made into one safe path
 * component: separators, wildcards and control codes become '_', trailing
 * dots and spaces go (so "." and ".." cannot survive), an empty result
 * becomes "_", and a Windows device name gets a '_' prefix.  The optional
 * suffix is appended afterwards, so "CON" and its resource fork become
 * "_CON" and "_CON.rsrc". */
static char *mb_normalize_name(const uint8_t *bytes, size_t size,
                               const char *suffix) {
    size_t suffix_length = suffix ? xx_str_len(suffix) : 0U;
    size_t input, output = 0U;
    char *name;
    if (size > MB_MAX_NAME || suffix_length > 16U) return NULL;
    /* One byte for a device prefix, three per converted character. */
    name = (char *)xx_mem_alloc(1U + size * 3U + suffix_length + 2U);
    if (!name) return NULL;
    for (input = 0U; input < size; ++input) {
        uint8_t c = bytes[input];
        if (c < 0x20U || c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
            c == 0x7fU)
            name[output++] = '_';
        else if (c < 0x80U)
            name[output++] = (char)c;
        else
            output += mb_put_utf8(name + output,
                                  k_mb_mac_roman_high[c - 0x80U]);
    }
    while (output != 0U && (name[output - 1U] == ' ' ||
                            name[output - 1U] == '.'))
        --output;
    if (output == 0U) name[output++] = '_';
    if (mb_is_device_name(name, output)) {
        xx_rt_memmove(name + 1U, name, output);
        name[0] = '_';
        ++output;
    }
    for (input = 0U; input < suffix_length; ++input)
        name[output++] = suffix[input];
    name[output] = 0;
    return name;
}

static bool mb_all_zero(const uint8_t *bytes, size_t size) {
    size_t index;
    for (index = 0U; index < size; ++index)
        if (bytes[index] != 0U) return false;
    return true;
}

/* Without a verified CRC there is nothing in the header that is unique to
 * MacBinary, so the payload fields themselves have to carry the evidence: a
 * Finder name is Mac Roman text and never holds control codes (a trailing CR
 * is the one exception, the Finder's custom-icon file "Icon\r"), and the
 * type and creator are four printable characters each.  Without this, any
 * file whose first bytes happen to be zero and a small count -- AppleDouble,
 * for one -- would be accepted. */
static bool mb_header_is_plausible(const uint8_t *header, size_t name_length) {
    size_t index;
    for (index = 0U; index < name_length; ++index) {
        uint8_t c = header[MB_OFF_NAME + index];
        if (c == 0x0DU && index + 1U == name_length && index != 0U) continue;
        if (c < 0x20U) return false;
    }
    for (index = MB_OFF_TYPE; index < MB_OFF_TYPE + 8U; ++index)
        if (header[index] < 0x20U || header[index] > 0x7eU) return false;
    return true;
}

static void mb_stream_free(void *opaque) {
    mb_stream *stream = (mb_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    xx_mem_free(stream);
}

static bool mb_add_member(mb_stream *stream, const uint8_t *header,
                          size_t name_length, const char *suffix,
                          int64_t offset, int64_t size, bool resource) {
    mb_member *member;
    if (stream->count >= sizeof(stream->items) / sizeof(stream->items[0]))
        return false;
    member = &stream->items[stream->count];
    member->name = mb_normalize_name(header + MB_OFF_NAME, name_length,
                                     suffix);
    if (!member->name) return false;
    member->data_offset = offset;
    member->data_size = size;
    member->resource = resource;
    ++stream->count;
    return true;
}

/* Validates the header at the format's base address and, when @p result is
 * not NULL, builds the member list.  With @p verified_only set, only a II/III
 * header whose CRC verifies is accepted. */
static bool mb_parse(Abstractformat *format, bool verified_only,
                     mb_stream **result) {
    uint8_t header[MB_HEADER_SIZE];
    mb_stream *stream;
    int64_t total, size;
    int64_t data_offset, rsrc_offset = 0, end, padded_end, archive_size;
    uint32_t data_length, rsrc_length, fork_limit;
    uint16_t secondary_length = 0U, comment_length = 0U;
    uint8_t name_length, version_written, version_needed;
    bool verified, needs_exact_fit = false;

    if (result) *result = NULL;
    if (!format || !format->device || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < 0 || format->base_address > total) return false;
    size = total - format->base_address;
    if (size < (int64_t)MB_HEADER_SIZE ||
        !mb_read_at(format->device, format->base_address, header,
                    sizeof(header)))
        return false;

    /* Structural invariants shared by every MacBinary generation. */
    if (header[MB_OFF_VERSION] != 0U || header[MB_OFF_ZERO_74] != 0U ||
        header[MB_OFF_ZERO_82] != 0U)
        return false;
    name_length = header[MB_OFF_NAME_LENGTH];
    if (name_length == 0U || name_length > MB_MAX_NAME ||
        header[MB_OFF_NAME] < 0x20U)
        return false;

    version_written = header[MB_OFF_VERSION_WRITTEN];
    version_needed = header[MB_OFF_VERSION_NEEDED];
    verified = version_written >= MB_VERSION_II &&
               xx_crc16_xmodem_calc(0U, header, MB_OFF_CRC) ==
                   mb_be16(header + MB_OFF_CRC);
    if (verified) {
        /* A reader of revision III must refuse a file that says it needs a
         * later one; its layout is then not known. */
        if (version_needed > MB_VERSION_III) return false;
        secondary_length = mb_be16(header + MB_OFF_SECONDARY);
        comment_length = mb_be16(header + MB_OFF_COMMENT_LENGTH);
    } else {
        if (verified_only) return false;
        if (version_written >= MB_VERSION_II) {
            /* A MacBinary II header whose CRC does not verify is accepted
             * only when nothing else in it is suspect -- some writers left
             * the field stale -- and the file ends exactly where the header
             * says it does.  Without the CRC a secondary header length
             * cannot be trusted to move the data fork. */
            if (version_needed > version_written ||
                mb_be16(header + MB_OFF_SECONDARY) != 0U)
                return false;
            comment_length = mb_be16(header + MB_OFF_COMMENT_LENGTH);
            needs_exact_fit = true;
        } else if (!mb_all_zero(header + MB_OFF_COMMENT_LENGTH,
                                MB_OFF_RESERVED - MB_OFF_COMMENT_LENGTH)) {
            /* MacBinary I defines nothing in bytes 99..125 and wrote them
             * as zero (126..127, later the computer and OS id, are seen
             * set).  Some writers left garbage there, so such a header is
             * taken only when the file size matches it exactly; the bytes
             * themselves are ignored. */
            needs_exact_fit = true;
        }
        if (!mb_header_is_plausible(header, name_length)) return false;
    }

    data_length = mb_be32(header + MB_OFF_DATA_LENGTH);
    rsrc_length = mb_be32(header + MB_OFF_RSRC_LENGTH);
    fork_limit = verified ? MB_MAX_FORK_VERIFIED : MB_MAX_FORK_UNVERIFIED;
    if (data_length > fork_limit || rsrc_length > fork_limit) return false;
    /* An unverified header describing no content at all is just a run of
     * zero bytes in the length fields; nothing would confirm it. */
    if (!verified && data_length == 0U && rsrc_length == 0U) return false;

    /* Bound every region against the real file before it is used.  All the
     * values are below 2^31 + 2^17 each, so the sums cannot overflow. */
    data_offset = (int64_t)MB_HEADER_SIZE +
                  mb_round_up((int64_t)secondary_length);
    end = data_offset + (int64_t)data_length;
    if (end > size) return false;
    if (rsrc_length != 0U) {
        rsrc_offset = data_offset + mb_round_up((int64_t)data_length);
        end = rsrc_offset + (int64_t)rsrc_length;
        if (end > size) return false;
    }
    /* The padding after the last fork is optional; take it when present. */
    padded_end = mb_round_up(end);
    archive_size = padded_end <= size ? padded_end : size;
    /* The Get Info comment follows the padded resource fork.  It is part of
     * the container only when all of it is really there. */
    if (comment_length != 0U && padded_end <= size &&
        (int64_t)comment_length <= size - padded_end) {
        int64_t comment_end =
            padded_end + mb_round_up((int64_t)comment_length);
        archive_size = comment_end <= size ? comment_end : size;
    }
    /* "Exact" means the file ends inside the padding of the last region. */
    if (needs_exact_fit && archive_size != size) return false;
    if (!result) return true;

    stream = (mb_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->header_offset = format->base_address;
    stream->type = mb_be32(header + MB_OFF_TYPE);
    stream->creator = mb_be32(header + MB_OFF_CREATOR);
    stream->created = mb_be32(header + MB_OFF_CREATED);
    stream->modified = mb_be32(header + MB_OFF_MODIFIED);
    stream->finder_flags = header[MB_OFF_FINDER_FLAGS];
    stream->protected_flag = header[MB_OFF_PROTECTED];
    stream->version_written = version_written;
    stream->secondary_length = secondary_length;
    stream->comment_length = comment_length;
    stream->has_signature =
        xx_rt_memcmp(header + MB_OFF_SIGNATURE, "mBIN", 4U) == 0;
    stream->crc_verified = verified;

    /* An empty data fork is only surfaced when there is no resource fork,
     * so that every valid container has at least one record; the reference
     * extractors skip it too. */
    if ((data_length != 0U || rsrc_length == 0U) &&
        !mb_add_member(stream, header, name_length, NULL,
                       format->base_address + data_offset,
                       (int64_t)data_length, false)) {
        mb_stream_free(stream);
        return false;
    }
    if (rsrc_length != 0U &&
        !mb_add_member(stream, header, name_length, ".rsrc",
                       format->base_address + rsrc_offset,
                       (int64_t)rsrc_length, true)) {
        mb_stream_free(stream);
        return false;
    }
    stream->archive_size = archive_size;
    *result = stream;
    return true;
}

static bool mb_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *mb_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool mb_set_record(xx_archive_record *record, const mb_stream *stream,
                          const mb_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->resource ? -1 : stream->header_offset;
    record->header_size = (int64_t)MB_HEADER_SIZE;
    record->data_offset = member->data_offset;
    record->compressed_size = member->data_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          0U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          stream->modified) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          stream->finder_flags) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          stream->type) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

void xx_macbinary_init(xx_macbinary *archive, xx_io_device *device,
                       int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_MACBINARY_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-macbinary");
    xx_format_set_extension(&archive->format, "bin");
    archive->format.check_is_valid = xx_macbinary_check_is_valid;
    archive->format.handle_base_info = xx_macbinary_handle_base_info;
    archive->format.get_format_size = xx_macbinary_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_macbinary_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_macbinary_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_macbinary_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_macbinary_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_macbinary_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_macbinary_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_macbinary *xx_macbinary_create(xx_io_device *device, int64_t base_address) {
    xx_macbinary *archive = (xx_macbinary *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_macbinary_init(archive, device, base_address);
    return archive;
}

void xx_macbinary_destroy(xx_macbinary *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_macbinary_free(xx_macbinary *archive) {
    if (!archive) return;
    xx_macbinary_destroy(archive);
    xx_mem_free(archive);
}

bool xx_macbinary_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    (void)pd;
    return mb_parse(format, false, NULL);
}

bool xx_macbinary_check_is_valid_verified(Abstractformat *format,
                                          xx_pd_struct *pd) {
    (void)pd;
    return mb_parse(format, true, NULL);
}

bool xx_macbinary_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    mb_stream *stream;
    xx_macbinary *archive;
    (void)pd;
    if (!format || !mb_parse(format, false, &stream)) {
        if (format) format->is_valid = false;
        return false;
    }
    archive = (xx_macbinary *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    archive->version = stream->version_written;
    archive->has_signature = stream->has_signature;
    archive->crc_verified = stream->crc_verified;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->file_type = XX_MACBINARY_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    mb_stream_free(stream);
    return true;
}

int64_t xx_macbinary_get_format_size(Abstractformat *format,
                                     xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_macbinary_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_macbinary_get_number_of_archive_records(Abstractformat *format,
                                                    xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_macbinary_handle_base_info(format, pd))
               ? ((xx_macbinary *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_macbinary_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    mb_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!mb_parse(format, false, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        mb_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = mb_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!mb_copy_options(&state->options, options) ||
        !mb_set_record(&state->current_record, stream, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_macbinary_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_macbinary_archive_record_move_to_next(Abstractformat *format,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    mb_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (mb_stream *)state->internal_state) ||
        stream->index >= stream->count || ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = mb_set_record(&state->current_record, stream,
                                      &stream->items[stream->index]);
    return state->has_record;
}

bool xx_macbinary_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    mb_stream *stream;
    const mb_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;

    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (mb_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    path_option = mb_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        int64_t total = xx_io_total_size(format->device);
        return member->data_offset >= 0 && member->data_size >= 0 &&
               member->data_offset <= total &&
               member->data_size <= total - member->data_offset;
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
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    result = xx_store_unpack_device_to_file(format->device,
                                            member->data_offset,
                                            member->data_size, path, pd);
done:
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_macbinary_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
