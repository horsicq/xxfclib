/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * RPM package: lead, signature header, main header, payload.  xx_rpm.h
 * carries the field table.  Written from the published package layout
 * (rpm.org "RPM file format"); the naming of the payload member follows
 * what 7-Zip's Rpm handler shows, which was consulted for behaviour only.
 *
 * Only the two header structures are walked: every index entry is checked
 * against its data store (type 0..9, offset inside the store, fixed-width
 * arrays inside the store), a handful of tags are read, and the payload is
 * handed out verbatim as the single member.  All reads are bounded by the
 * device, every count by the RPM limits (0xFFFF entries, a store under
 * 256 MiB), and no allocation depends on a field.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/rpm/xx_rpm.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <stdio.h>

/* Registration placeholder: xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as RPM is registered there. */
#ifdef RPM
#define XX_RPM_FILE_TYPE XX_FILE_TYPE_RPM
#else
#define XX_RPM_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define RPM_LEAD_SIZE ((int64_t)XX_RPM_LEAD_SIZE)
#define RPM_PGP_SIGNATURE_SIZE 256
#define RPM_INTRO_SIZE 16
#define RPM_ENTRY_SIZE 16
#define RPM_MAX_ENTRIES 0xFFFFU
#define RPM_MAX_STORE 0x0FFFFFFFU
#define RPM_MAX_TYPE 9U
#define RPM_ENTRY_CHUNK 128U /* entries per read: a 2 KiB stack buffer */

#define RPM_SIGTYPE_NONE 0U
#define RPM_SIGTYPE_PGP 1U
#define RPM_SIGTYPE_HEADER 5U

#define RPM_TYPE_CHAR 1U
#define RPM_TYPE_INT8 2U
#define RPM_TYPE_INT16 3U
#define RPM_TYPE_INT32 4U
#define RPM_TYPE_INT64 5U
#define RPM_TYPE_STRING 6U
#define RPM_TYPE_BIN 7U
#define RPM_TYPE_STRING_ARRAY 8U
#define RPM_TYPE_I18NSTRING 9U

/* Signature tags. */
#define RPM_SIGTAG_LONGSIZE 270U
#define RPM_SIGTAG_SIZE 1000U
/* Main header tags. */
#define RPM_TAG_NAME 1000U
#define RPM_TAG_VERSION 1001U
#define RPM_TAG_RELEASE 1002U
#define RPM_TAG_BUILDTIME 1006U
#define RPM_TAG_ARCH 1022U
#define RPM_TAG_PAYLOADFORMAT 1124U
#define RPM_TAG_PAYLOADCOMPRESSOR 1125U

/* Longest piece of each name component that is kept. */
#define RPM_CAP_NAME 80U
#define RPM_CAP_VERSION 40U
#define RPM_CAP_RELEASE 40U
#define RPM_CAP_ARCH 16U
#define RPM_CAP_TOKEN 8U
#define RPM_STRING_READ 128U

static const uint8_t rpm_lead_magic[4] = {0xED, 0xAB, 0xEE, 0xDB};
static const uint8_t rpm_header_magic[8] = {0x8E, 0xAD, 0xE8, 0x01,
                                            0x00, 0x00, 0x00, 0x00};

typedef struct rpm_header_s {
    int64_t offset;       /**< Relative to the package start. */
    uint32_t count;
    uint32_t store_size;
    int64_t store_offset; /**< Relative. */
    int64_t end;          /**< Relative, first byte after the store. */
} rpm_header;

typedef struct rpm_tag_s {
    uint32_t tag;
    uint32_t type;
    uint32_t offset;
    uint32_t count;
    bool found;
} rpm_tag;

enum {
    RPM_SIG_LONGSIZE = 0,
    RPM_SIG_SIZE,
    RPM_SIG_COUNT
};

enum {
    RPM_MAIN_NAME = 0,
    RPM_MAIN_VERSION,
    RPM_MAIN_RELEASE,
    RPM_MAIN_BUILDTIME,
    RPM_MAIN_ARCH,
    RPM_MAIN_FORMAT,
    RPM_MAIN_COMPRESSOR,
    RPM_MAIN_COUNT
};

typedef struct rpm_layout_s {
    uint8_t major;
    uint8_t minor;
    uint16_t package_type;
    uint16_t os_number;
    uint16_t signature_type;
    int64_t signature_offset; /**< Relative; -1 without one. */
    int64_t header_offset;    /**< Relative. */
    int64_t header_size;
    int64_t payload_offset;   /**< Relative. */
    int64_t payload_size;
    int64_t declared_size;    /**< Package size the signature claims, or -1. */
    int64_t package_size;     /**< Present bytes, clamped to the device. */
    bool truncated;
    bool has_build_time;
    uint32_t build_time;
    char name[256];
} rpm_layout;

typedef struct rpm_stream_s {
    rpm_layout layout;
    size_t index;
    size_t count;
} rpm_stream;

static uint32_t rpm_be16(const uint8_t *p) {
    return ((uint32_t)p[0] << 8U) | (uint32_t)p[1];
}

static uint32_t rpm_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24U) | ((uint32_t)p[1] << 16U) |
           ((uint32_t)p[2] << 8U) | (uint32_t)p[3];
}

static uint64_t rpm_be64(const uint8_t *p) {
    return ((uint64_t)rpm_be32(p) << 32U) | (uint64_t)rpm_be32(p + 4);
}

/* Read exactly @p size bytes at relative @p offset; the caller has already
 * checked that the range lies inside the device. */
static bool rpm_read(Abstractformat *format, int64_t offset, void *buffer,
                     size_t size) {
    size_t done = 0U;
    if (!format || !format->device || offset < 0 ||
        offset > INT64_MAX - format->base_address ||
        xx_io_seek64(format->device, format->base_address + offset,
                     SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount = xx_io_read(format->device, (uint8_t *)buffer + done,
                                    size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

/* Bytes of the device from the package start on. */
static int64_t rpm_available(Abstractformat *format) {
    int64_t total;
    if (!format || !format->device || format->base_address < 0) return -1;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return -1;
    return total - format->base_address;
}

/* Header intro at @p offset: the eight-byte magic, 1..0xFFFF entries, a
 * store under 256 MiB, and the whole structure inside @p available. */
static bool rpm_header_open(Abstractformat *format, int64_t available,
                            int64_t offset, rpm_header *header) {
    uint8_t intro[RPM_INTRO_SIZE];
    int64_t end;
    if (offset < 0 || offset > available ||
        available - offset < RPM_INTRO_SIZE ||
        !rpm_read(format, offset, intro, sizeof(intro)) ||
        xx_rt_memcmp(intro, rpm_header_magic, sizeof(rpm_header_magic)) != 0)
        return false;
    header->offset = offset;
    header->count = rpm_be32(intro + 8);
    header->store_size = rpm_be32(intro + 12);
    if (header->count == 0U || header->count > RPM_MAX_ENTRIES ||
        header->store_size > RPM_MAX_STORE)
        return false;
    /* count <= 0xFFFF and store < 2^28: no overflow below. */
    header->store_offset = offset + RPM_INTRO_SIZE +
                           (int64_t)header->count * RPM_ENTRY_SIZE;
    end = header->store_offset + (int64_t)header->store_size;
    if (end > available) return false;
    header->end = end;
    return true;
}

static uint32_t rpm_type_width(uint32_t type) {
    switch (type) {
    case RPM_TYPE_CHAR:
    case RPM_TYPE_INT8:
    case RPM_TYPE_BIN: return 1U;
    case RPM_TYPE_INT16: return 2U;
    case RPM_TYPE_INT32: return 4U;
    case RPM_TYPE_INT64: return 8U;
    default: return 0U;
    }
}

/* Check every index entry against the store and note the first entry of
 * each tag in @p tags. */
static bool rpm_header_walk(Abstractformat *format, const rpm_header *header,
                            rpm_tag *tags, size_t tag_count,
                            xx_pd_struct *pd) {
    uint8_t chunk[RPM_ENTRY_CHUNK * RPM_ENTRY_SIZE];
    uint32_t done = 0U;
    size_t t;
    for (t = 0U; t < tag_count; ++t) tags[t].found = false;
    while (done < header->count) {
        uint32_t batch = header->count - done, i;
        if (batch > RPM_ENTRY_CHUNK) batch = RPM_ENTRY_CHUNK;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (!rpm_read(format,
                      header->offset + RPM_INTRO_SIZE +
                          (int64_t)done * RPM_ENTRY_SIZE,
                      chunk, (size_t)batch * RPM_ENTRY_SIZE))
            return false;
        for (i = 0U; i < batch; ++i) {
            const uint8_t *entry = chunk + (size_t)i * RPM_ENTRY_SIZE;
            uint32_t tag = rpm_be32(entry), type = rpm_be32(entry + 4),
                     offset = rpm_be32(entry + 8),
                     count = rpm_be32(entry + 12), width;
            if (type > RPM_MAX_TYPE || offset > header->store_size)
                return false;
            width = rpm_type_width(type);
            if (width != 0U &&
                (uint64_t)count > (uint64_t)(header->store_size - offset) /
                                      width)
                return false;
            if ((type == RPM_TYPE_STRING || type == RPM_TYPE_STRING_ARRAY ||
                 type == RPM_TYPE_I18NSTRING) &&
                count != 0U && offset >= header->store_size)
                return false;
            for (t = 0U; t < tag_count; ++t) {
                if (!tags[t].found && tags[t].tag == tag) {
                    tags[t].found = true;
                    tags[t].type = type;
                    tags[t].offset = offset;
                    tags[t].count = count;
                }
            }
        }
        done += batch;
    }
    return true;
}

/* First string of a STRING entry, cut at RPM_STRING_READ bytes.  Empty when
 * the entry is missing or of another type. */
static void rpm_tag_string(Abstractformat *format, const rpm_header *header,
                           const rpm_tag *tag, char *out, size_t capacity) {
    uint8_t buffer[RPM_STRING_READ];
    size_t size, length = 0U;
    out[0] = '\0';
    if (!tag->found || tag->type != RPM_TYPE_STRING || tag->count == 0U ||
        tag->offset >= header->store_size || capacity == 0U)
        return;
    size = header->store_size - tag->offset;
    if (size > sizeof(buffer)) size = sizeof(buffer);
    if (!rpm_read(format, header->store_offset + (int64_t)tag->offset,
                  buffer, size))
        return;
    while (length < size && buffer[length] != 0U && length + 1U < capacity) {
        out[length] = (char)buffer[length];
        ++length;
    }
    out[length] = '\0';
}

static bool rpm_tag_number(Abstractformat *format, const rpm_header *header,
                           const rpm_tag *tag, uint32_t type, uint64_t *out) {
    uint8_t buffer[8];
    uint32_t width = rpm_type_width(type);
    if (!tag->found || tag->type != type || tag->count == 0U || width == 0U ||
        (uint64_t)tag->offset + width > header->store_size ||
        !rpm_read(format, header->store_offset + (int64_t)tag->offset, buffer,
                  width))
        return false;
    *out = width == 8U ? rpm_be64(buffer) : (uint64_t)rpm_be32(buffer);
    return true;
}

/* ---------------------------------------------------------------------- */
/* Member name                                                             */

static bool rpm_name_char_ok(char c) {
    unsigned char u = (unsigned char)c;
    return u > 0x20U && u < 0x7FU && c != '/' && c != '\\' && c != ':' &&
           c != '*' && c != '?' && c != '"' && c != '<' && c != '>' &&
           c != '|';
}

/* Append at most @p cap characters of @p text, each unsafe one as '_'. */
static void rpm_name_append(char *name, size_t capacity, size_t *length,
                            const char *text, size_t cap) {
    size_t i;
    for (i = 0U; text[i] && i < cap && *length + 1U < capacity; ++i)
        name[(*length)++] = rpm_name_char_ok(text[i]) ? text[i] : '_';
    name[*length] = '\0';
}

static char rpm_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool rpm_stem_is(const char *name, size_t stem, const char *device) {
    size_t i;
    for (i = 0U; i < stem; ++i)
        if (!device[i] || rpm_upper(name[i]) != device[i]) return false;
    return device[stem] == '\0';
}

/* True when the part before the first '.' is a Windows device name. */
static bool rpm_is_device_stem(const char *name) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t stem = 0U, i;
    while (name[stem] && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (i = 0U; i < sizeof(devices) / sizeof(devices[0]); ++i)
        if (rpm_stem_is(name, stem, devices[i])) return true;
    return stem == 4U && name[3] >= '0' && name[3] <= '9' &&
           ((rpm_upper(name[0]) == 'C' && rpm_upper(name[1]) == 'O' &&
             rpm_upper(name[2]) == 'M') ||
            (rpm_upper(name[0]) == 'L' && rpm_upper(name[1]) == 'P' &&
             rpm_upper(name[2]) == 'T'));
}

/* A single safe path component: printable, no separators or reserved
 * characters, not dot-only, not a device. */
static bool rpm_name_is_safe(const char *name) {
    size_t i;
    bool meaningful = false;
    if (!name || !name[0]) return false;
    for (i = 0U; name[i]; ++i) {
        if (!rpm_name_char_ok(name[i])) return false;
        if (name[i] != '.') meaningful = true;
    }
    return meaningful && name[0] != '.' && !rpm_is_device_stem(name);
}

/* A payload-format / compressor token: lower-case letters and digits. */
static bool rpm_token_ok(const char *text) {
    size_t i;
    if (!text[0]) return false;
    for (i = 0U; text[i]; ++i) {
        char c = text[i];
        if (i >= RPM_CAP_TOKEN ||
            !((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
            return false;
    }
    return true;
}

static bool rpm_streq(const char *a, const char *b) {
    return xx_str_len(a) == xx_str_len(b) &&
           xx_rt_memcmp(a, b, xx_str_len(a)) == 0;
}

/* Extension of the payload: its own magic first, then the compressor tag,
 * else "bin".  An empty result means an uncompressed cpio. */
static const char *rpm_payload_extension(const uint8_t *head, size_t size,
                                         const char *compressor) {
    if (size >= 3U && head[0] == 0x1FU && head[1] == 0x8BU && head[2] == 8U)
        return "gz";
    if (size >= 6U && head[0] == 0xFDU && head[1] == '7' && head[2] == 'z' &&
        head[3] == 'X' && head[4] == 'Z' && head[5] == 0U)
        return "xz";
    if (size >= 4U && head[0] == 'B' && head[1] == 'Z' && head[2] == 'h' &&
        head[3] >= '1' && head[3] <= '9')
        return "bz2";
    if (size >= 4U && head[0] == 0x28U && head[1] == 0xB5U &&
        head[2] == 0x2FU && head[3] == 0xFDU)
        return "zst";
    if (size >= 6U && head[0] == '0' && head[1] == '7' && head[2] == '0' &&
        head[3] == '7' && head[4] == '0' &&
        (head[5] == '1' || head[5] == '2' || head[5] == '7'))
        return "";
    if (rpm_token_ok(compressor)) {
        if (rpm_streq(compressor, "gzip")) return "gz";
        if (rpm_streq(compressor, "bzip2")) return "bz2";
        if (rpm_streq(compressor, "zstd")) return "zst";
        return compressor;
    }
    if (size >= 3U && head[0] == 0x5DU && head[1] == 0U && head[2] == 0U)
        return "lzma";
    return "bin";
}

static void rpm_build_name(Abstractformat *format, const rpm_header *main,
                           const rpm_tag *tags, const uint8_t *lead,
                           rpm_layout *layout) {
    char name[RPM_STRING_READ], version[RPM_STRING_READ],
        release[RPM_STRING_READ], arch[RPM_STRING_READ],
        payload_format[RPM_STRING_READ], compressor[RPM_STRING_READ],
        lead_name[67];
    char temp[sizeof(layout->name)];
    uint8_t head[6] = {0};
    size_t head_size = 0U, length = 0U, i;
    const char *extension;

    rpm_tag_string(format, main, &tags[RPM_MAIN_NAME], name, sizeof(name));
    rpm_tag_string(format, main, &tags[RPM_MAIN_VERSION], version,
                   sizeof(version));
    rpm_tag_string(format, main, &tags[RPM_MAIN_RELEASE], release,
                   sizeof(release));
    rpm_tag_string(format, main, &tags[RPM_MAIN_ARCH], arch, sizeof(arch));
    rpm_tag_string(format, main, &tags[RPM_MAIN_FORMAT], payload_format,
                   sizeof(payload_format));
    rpm_tag_string(format, main, &tags[RPM_MAIN_COMPRESSOR], compressor,
                   sizeof(compressor));
    for (i = 0U; i < 66U && lead[10U + i]; ++i) lead_name[i] = (char)lead[10U + i];
    lead_name[i] = '\0';

    if (layout->payload_size > 0) {
        head_size = layout->payload_size < (int64_t)sizeof(head)
                        ? (size_t)layout->payload_size : sizeof(head);
        if (!rpm_read(format, layout->payload_offset, head, head_size))
            head_size = 0U;
    }

    temp[0] = '\0';
    if (name[0]) {
        rpm_name_append(temp, sizeof(temp), &length, name, RPM_CAP_NAME);
        if (version[0]) {
            rpm_name_append(temp, sizeof(temp), &length, "-", 1U);
            rpm_name_append(temp, sizeof(temp), &length, version,
                            RPM_CAP_VERSION);
        }
        if (release[0]) {
            rpm_name_append(temp, sizeof(temp), &length, "-", 1U);
            rpm_name_append(temp, sizeof(temp), &length, release,
                            RPM_CAP_RELEASE);
        }
    } else {
        rpm_name_append(temp, sizeof(temp), &length, lead_name, 66U);
    }
    if (!length) rpm_name_append(temp, sizeof(temp), &length, "package", 7U);
    if (layout->package_type == 1U) {
        rpm_name_append(temp, sizeof(temp), &length, ".src", 4U);
    } else if (arch[0]) {
        rpm_name_append(temp, sizeof(temp), &length, ".", 1U);
        rpm_name_append(temp, sizeof(temp), &length, arch, RPM_CAP_ARCH);
    }
    rpm_name_append(temp, sizeof(temp), &length, ".", 1U);
    rpm_name_append(temp, sizeof(temp), &length,
                    rpm_token_ok(payload_format) ? payload_format : "cpio",
                    RPM_CAP_TOKEN);
    extension = rpm_payload_extension(head, head_size, compressor);
    if (extension[0]) {
        rpm_name_append(temp, sizeof(temp), &length, ".", 1U);
        rpm_name_append(temp, sizeof(temp), &length, extension,
                        RPM_CAP_TOKEN);
    }
    /* A leading dot would hide the file; a device stem would open the
     * device.  Both get a '_' in front. */
    if (temp[0] == '.') temp[0] = '_';
    length = 0U;
    layout->name[0] = '\0';
    if (rpm_is_device_stem(temp))
        rpm_name_append(layout->name, sizeof(layout->name), &length, "_", 1U);
    rpm_name_append(layout->name, sizeof(layout->name), &length, temp,
                    sizeof(temp));
}

/* ---------------------------------------------------------------------- */
/* Parse                                                                   */

static bool rpm_parse(Abstractformat *format, rpm_layout *layout,
                      xx_pd_struct *pd) {
    uint8_t lead[XX_RPM_LEAD_SIZE];
    rpm_header signature, main;
    rpm_tag sig_tags[RPM_SIG_COUNT], main_tags[RPM_MAIN_COUNT];
    int64_t available, position;
    uint64_t declared = 0U, build_time = 0U;
    bool has_declared = false;

    if (!format || !layout) return false;
    xx_mem_zero(layout, sizeof(*layout));
    layout->signature_offset = -1;
    layout->declared_size = -1;
    available = rpm_available(format);
    if (available < RPM_LEAD_SIZE + RPM_INTRO_SIZE ||
        !rpm_read(format, 0, lead, sizeof(lead)) ||
        xx_rt_memcmp(lead, rpm_lead_magic, sizeof(rpm_lead_magic)) != 0)
        return false;
    layout->major = lead[4];
    layout->minor = lead[5];
    layout->package_type = (uint16_t)rpm_be16(lead + 6);
    layout->os_number = (uint16_t)rpm_be16(lead + 0x4C);
    layout->signature_type = (uint16_t)rpm_be16(lead + 0x4E);
    if ((layout->major != 3U && layout->major != 4U) ||
        layout->package_type > 1U)
        return false;

    xx_mem_zero(sig_tags, sizeof(sig_tags));
    sig_tags[RPM_SIG_LONGSIZE].tag = RPM_SIGTAG_LONGSIZE;
    sig_tags[RPM_SIG_SIZE].tag = RPM_SIGTAG_SIZE;
    switch (layout->signature_type) {
    case RPM_SIGTYPE_NONE:
        position = RPM_LEAD_SIZE;
        break;
    case RPM_SIGTYPE_PGP:
        position = RPM_LEAD_SIZE + RPM_PGP_SIGNATURE_SIZE;
        break;
    case RPM_SIGTYPE_HEADER:
        if (!rpm_header_open(format, available, RPM_LEAD_SIZE, &signature) ||
            !rpm_header_walk(format, &signature, sig_tags, RPM_SIG_COUNT, pd))
            return false;
        layout->signature_offset = RPM_LEAD_SIZE;
        /* The signature is padded to an eight-byte boundary. */
        position = (signature.end + 7) & ~(int64_t)7;
        if (rpm_tag_number(format, &signature, &sig_tags[RPM_SIG_LONGSIZE],
                           RPM_TYPE_INT64, &declared) ||
            rpm_tag_number(format, &signature, &sig_tags[RPM_SIG_SIZE],
                           RPM_TYPE_INT32, &declared))
            has_declared = true;
        break;
    default:
        return false;
    }

    xx_mem_zero(main_tags, sizeof(main_tags));
    main_tags[RPM_MAIN_NAME].tag = RPM_TAG_NAME;
    main_tags[RPM_MAIN_VERSION].tag = RPM_TAG_VERSION;
    main_tags[RPM_MAIN_RELEASE].tag = RPM_TAG_RELEASE;
    main_tags[RPM_MAIN_BUILDTIME].tag = RPM_TAG_BUILDTIME;
    main_tags[RPM_MAIN_ARCH].tag = RPM_TAG_ARCH;
    main_tags[RPM_MAIN_FORMAT].tag = RPM_TAG_PAYLOADFORMAT;
    main_tags[RPM_MAIN_COMPRESSOR].tag = RPM_TAG_PAYLOADCOMPRESSOR;
    if (!rpm_header_open(format, available, position, &main) ||
        !rpm_header_walk(format, &main, main_tags, RPM_MAIN_COUNT, pd))
        return false;
    layout->header_offset = main.offset;
    layout->header_size = main.end - main.offset;
    layout->payload_offset = main.end;

    /* The signature's size covers the main header and the payload.  One
     * that does not even cover the header says nothing and is ignored; one
     * that runs past the device marks a truncated package, whose payload is
     * measured but not handed out.  main.end <= available, so the
     * subtraction below is not negative. */
    layout->package_size = available;
    if (has_declared && declared >= (uint64_t)layout->header_size) {
        if (declared > (uint64_t)(available - main.offset)) {
            layout->truncated = true;
            layout->declared_size =
                declared > (uint64_t)(INT64_MAX - main.offset)
                    ? INT64_MAX : main.offset + (int64_t)declared;
        } else {
            layout->declared_size = main.offset + (int64_t)declared;
            layout->package_size = layout->declared_size;
        }
    }
    layout->payload_size = layout->package_size - layout->payload_offset;
    if (layout->payload_size < 0) return false;

    if (rpm_tag_number(format, &main, &main_tags[RPM_MAIN_BUILDTIME],
                       RPM_TYPE_INT32, &build_time)) {
        layout->has_build_time = true;
        layout->build_time = (uint32_t)build_time;
    }
    rpm_build_name(format, &main, main_tags, lead, layout);
    return rpm_name_is_safe(layout->name);
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

static bool rpm_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *rpm_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool rpm_set_record(Abstractformat *format, xx_archive_record *record,
                           const rpm_layout *layout) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address + layout->header_offset;
    record->header_size = layout->header_size;
    record->data_offset = format->base_address + layout->payload_offset;
    record->compressed_size = layout->payload_size;
    /* The payload is handed out as stored: its own compression (gzip, xz,
     * ...) belongs to the member, so both sizes agree and the method is
     * "none". */
    if (!xx_archive_record_set_original_name(record, layout->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        (uint64_t)layout->payload_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        (uint64_t)layout->payload_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        0U) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false))
        return false;
    if (layout->has_build_time &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                        (uint64_t)layout->build_time))
        return false;
    return true;
}

static void rpm_stream_free(void *pointer) {
    if (pointer) xx_mem_free(pointer);
}

/* Copy the payload of @p layout to @p destination, after checking that the
 * package still parses to the same layout. */
static bool rpm_unpack_payload(Abstractformat *format, const rpm_layout *layout,
                               const char *destination, xx_pd_struct *pd) {
    rpm_layout check;
    if (!format || !layout || layout->truncated || layout->payload_size <= 0 ||
        !rpm_parse(format, &check, pd) ||
        check.payload_offset != layout->payload_offset ||
        check.payload_size != layout->payload_size || check.truncated)
        return false;
    if (!destination) return true;
    return xx_store_unpack_device_to_file(
        format->device, format->base_address + layout->payload_offset,
        layout->payload_size, destination, pd);
}

/* ---------------------------------------------------------------------- */
/* Public interface                                                        */

void xx_rpm_init(xx_rpm *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_RPM_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-rpm");
    xx_format_set_extension(&archive->format, "rpm");
    archive->format.check_is_valid = xx_rpm_check_is_valid;
    archive->format.handle_base_info = xx_rpm_handle_base_info;
    archive->format.get_format_size = xx_rpm_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_rpm_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_rpm_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_rpm_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_rpm_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_rpm_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_rpm_free_archive_records_reading;
    archive->signature_offset = -1;
    archive->header_offset = -1;
    archive->payload_offset = -1;
    archive->declared_size = -1;
}

xx_rpm *xx_rpm_create(xx_io_device *device, int64_t base_address) {
    xx_rpm *archive = (xx_rpm *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_rpm_init(archive, device, base_address);
    return archive;
}

void xx_rpm_destroy(xx_rpm *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_rpm_free(xx_rpm *archive) {
    if (!archive) return;
    xx_rpm_destroy(archive);
    xx_mem_free(archive);
}

bool xx_rpm_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    rpm_layout layout;
    return rpm_parse(format, &layout, pd);
}

bool xx_rpm_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    rpm_layout layout;
    xx_rpm *archive;
    char version[16];
    int64_t total;
    if (!format || !rpm_parse(format, &layout, pd)) return false;
    archive = (xx_rpm *)format;
    archive->version_major = layout.major;
    archive->version_minor = layout.minor;
    archive->package_type = layout.package_type;
    archive->signature_type = layout.signature_type;
    archive->signature_offset = layout.signature_offset >= 0
                                    ? format->base_address +
                                          layout.signature_offset
                                    : -1;
    archive->header_offset = format->base_address + layout.header_offset;
    archive->header_size = layout.header_size;
    archive->payload_offset = format->base_address + layout.payload_offset;
    archive->payload_size = layout.payload_size;
    archive->declared_size = layout.declared_size;
    archive->truncated = layout.truncated;
    xx_rt_memcpy(archive->member_name, layout.name, sizeof(layout.name));
    archive->number_of_records = layout.payload_size > 0 ? 1U : 0U;
    (void)xx_rt_snprintf(version, sizeof(version), "%u.%u",
                         (unsigned)layout.major, (unsigned)layout.minor);
    xx_format_set_version(format, version);
    if (layout.os_number == 1U) format->os = XX_OS_LINUX;
    format->number_of_archive_records = archive->number_of_records;
    format->format_size = layout.package_size;
    total = xx_io_total_size(format->device);
    if (total > format->base_address + layout.package_size) {
        format->overlay_offset = format->base_address + layout.package_size;
        format->overlay_size = total - format->overlay_offset;
    } else {
        format->overlay_offset = -1;
        format->overlay_size = 0;
    }
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_rpm_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_rpm_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_rpm_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_rpm_handle_base_info(format, pd))
               ? ((xx_rpm *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_rpm_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    rpm_stream *stream;
    xx_archive_record_state *state;
    if (!format) return NULL;
    stream = (rpm_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!rpm_parse(format, &stream->layout, pd)) {
        xx_mem_free(stream);
        return NULL;
    }
    stream->count = stream->layout.payload_size > 0 ? 1U : 0U;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_mem_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = rpm_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!rpm_copy_options(&state->options, options)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    if (stream->count != 0U) {
        if (!rpm_set_record(format, &state->current_record, &stream->layout)) {
            xx_archive_record_state_free(state);
            return NULL;
        }
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_archive_record *xx_rpm_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_rpm_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    rpm_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format) return false;
    stream = (rpm_stream *)state->internal_state;
    if (stream && stream->index < stream->count) ++stream->index;
    xx_archive_record_cleanup(&state->current_record);
    xx_archive_record_init(&state->current_record);
    state->has_record = false;
    return false;
}

bool xx_rpm_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    rpm_stream *stream;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (rpm_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    if (!rpm_name_is_safe(stream->layout.name)) return false;
    path_option = rpm_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return rpm_unpack_payload(format, &stream->layout, NULL, pd);
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
               ? xx_str_concat3(base, "/", stream->layout.name)
               : xx_str_concat(base, stream->layout.name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    result = rpm_unpack_payload(format, &stream->layout, path, pd);
done:
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_rpm_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
