/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * JGsoft DeployMaster 2.x setup package: a Delphi PE stub whose overlay
 * carries the bzip2-packed setup engine, the zlib-packed setup settings and
 * the zlib-packed files.  The header comment in
 * xx_sfx_jgsoft_deploymaster_package.h has the layout.
 *
 * No reference implementation exists; the layout was worked out from the
 * two DeployMaster packages of the reference corpus (2.0 and 2.7) and is
 * cross-checked on every file by the size and CRC-32 the package's own file
 * table records.  The executable is parsed only as far as its section table
 * and security directory, to find where the overlay starts and ends; no
 * code is run or emulated.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/sfx_jgsoft_deploymaster_package/xx_sfx_jgsoft_deploymaster_package.h"

#include "xxfclib/algo/bzip2/xx_bzip2.h"
#include "xxfclib/algo/crc/xx_crc.h"
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
#ifdef SFX_JGSOFT_DEPLOYMASTER_PACKAGE
#define XX_SFX_JGSOFT_DEPLOYMASTER_PACKAGE_FILE_TYPE \
    XX_FILE_TYPE_SFX_JGSOFT_DEPLOYMASTER_PACKAGE
#else
#define XX_SFX_JGSOFT_DEPLOYMASTER_PACKAGE_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* PE: enough of the headers to find the end of the section data. */
#define DM_PE_MAX_SECTIONS 96U
#define DM_PE_SECTION_SIZE 40U
#define DM_PE_OPTIONAL_MAX 240U

/* The bzip2 engine: a stream header, then a bit-aligned end-of-stream
 * marker (the BCD digits of sqrt(pi)) and the 32-bit stream CRC, padded
 * with zero bits to a byte.  The scan for the marker is bounded; the known
 * engines pack to under 200 KB. */
#define DM_BZ_HEADER 10U
#define DM_BZ_EOS UINT64_C(0x177245385090)
#define DM_BZ_MASK UINT64_C(0xFFFFFFFFFFFF)
#define DM_BZ_SCAN_MAX (INT64_C(16) << 20)
#define DM_BZ_CHUNK 0x10000U

#define DM_MARKER UINT32_C(0xFFFFFFFF)
/* Two header bytes, an empty final block, the Adler-32. */
#define DM_ZLIB_MIN 8U
#define DM_STRINGS_MAX_PACKED (1U << 20)
#define DM_STRINGS_MAX_UNPACKED (4U << 20)
#define DM_FONT_MAX 0x10000U
#define DM_LIST_MAX_PACKED (8U << 20)
#define DM_LIST_MAX_UNPACKED (16U << 20)
/* How far past the project strings the font record is looked for, and how
 * many candidate records may be inflated on the way. */
#define DM_RESYNC_MAX 4096U
#define DM_RESYNC_TRIES 16U
#define DM_MAX_FILES 65536U
#define DM_PROJECT_FIELDS 11U
#define DM_FIRST_SPECIAL 7U
#define DM_SPECIALS 4U
#define DM_ENTRY_SIZE 24U

#define DM_ENGINE_NAME "Deploy.exe"
#define DM_FALLBACK_NAME "package.dat"
/* Largest engine when the caller sets no limit (the known ones are about
 * 400 KB). */
#define DM_ENGINE_MAX (UINT64_C(64) << 20)
/* Published names: raw bytes accepted, characters per path component. */
#define DM_NAME_BYTES 1024U
#define DM_NAME_CHARS 240U
#define DM_DEDUP_TRIES 32U
#define DM_COPY_BUFFER 0x10000U
#define DM_SSIZE_LIMIT (((size_t)-1) >> 1U)

enum {
    DM_KIND_ENGINE = 0,
    DM_KIND_ZLIB = 1,
    DM_KIND_RAW = 2
};

typedef struct dm_layout_s {
    int64_t total;          /* bytes from base_address to the device end */
    int64_t data_end;       /* end of the package data, from base */
    int64_t overlay;        /* the bzip2 stream, from base */
    int64_t bz_end;         /* first byte behind it: the 0xFFFFFFFF marker */
    int64_t after_project;  /* first byte behind the project strings */
} dm_layout;

typedef struct dm_entry_s {
    char *name;
    int64_t header_offset;  /* from base */
    int64_t header_size;
    int64_t data_offset;    /* from base */
    int64_t packed_size;
    uint64_t size;          /* from the file table */
    uint32_t crc;
    uint32_t dos_datetime;
    uint8_t kind;
    bool has_table_info;
    bool extractable;
    bool broken;            /* listed, but its record is unreadable */
} dm_entry;

typedef struct dm_table_s {
    dm_entry *entries;
    size_t count;
    size_t capacity;
    /* Case-insensitive set of published names: slot = entry index + 1. */
    uint32_t *slots;
    size_t mask;
    bool walked;
    uint32_t specials;
    uint32_t listed;
    uint32_t missing;
    uint32_t broken;
    int64_t table_offset;
    int64_t records_end;    /* end of the last file record, -1 if none */
} dm_table;

typedef struct dm_stream_s {
    dm_layout layout;
    dm_table table;
    size_t index;
    uint64_t max_member;    /* UINT64_MAX when unset */
} dm_stream;

/* A location found by the settings walk, before names are published. */
typedef struct dm_slot_s {
    const uint8_t *raw_name;
    size_t raw_length;
    int64_t record;         /* offset of the u32 length, from base */
    int64_t packed;
    uint32_t table_index;
    bool broken;            /* no readable record at the table's offset */
} dm_slot;

/* ---------------------------------------------------------------------- */
/* Helpers                                                                 */

static uint32_t dm_le16(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U);
}

static uint32_t dm_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static bool dm_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* A zlib header DeployMaster can have written: deflate with a window of at
 * most 32K, a valid check value, no preset dictionary, and a first block
 * whose type is not the reserved one. */
static bool dm_zlib_start(const uint8_t *p) {
    uint32_t cmf = p[0], flg = p[1];
    return (cmf & 0x0FU) == 8U && (cmf >> 4U) < 8U &&
           ((cmf << 8U) | flg) % 31U == 0U && (flg & 0x20U) == 0U &&
           ((p[2] >> 1U) & 3U) != 3U;
}

/* Growable memory sink with a hard limit, for the settings records. */
typedef struct dm_membuf_s {
    xx_io_device device;
    uint8_t *data;
    size_t size;
    size_t capacity;
    size_t limit;
} dm_membuf;

static ssize_t dm_membuf_write(xx_io_device *self, const void *buffer,
                               size_t size) {
    dm_membuf *sink = self ? (dm_membuf *)self->priv : NULL;
    if (!sink || (!buffer && size != 0U) || size > DM_SSIZE_LIMIT ||
        size > sink->limit - sink->size)
        return -1;
    if (size > sink->capacity - sink->size) {
        size_t wanted = sink->capacity ? sink->capacity : 4096U;
        uint8_t *grown;
        while (wanted < sink->size + size) {
            if (wanted > sink->limit / 2U) {
                wanted = sink->limit;
                break;
            }
            wanted *= 2U;
        }
        grown = (uint8_t *)xx_mem_realloc(sink->data, wanted);
        if (!grown) return -1;
        sink->data = grown;
        sink->capacity = wanted;
    }
    if (size) xx_rt_memcpy(sink->data + sink->size, buffer, size);
    sink->size += size;
    return (ssize_t)size;
}

/* Inflate the zlib record whose @p packed bytes start at @p offset (from
 * base).  The deflate data must end exactly where the Adler-32 trailer
 * starts, and the trailer must match.  On success the output is returned
 * in *out (caller frees; may be NULL when not wanted). */
static bool dm_inflate_record(Abstractformat *format, int64_t offset,
                              uint32_t packed, size_t limit, uint8_t **out,
                              size_t *out_size) {
    uint8_t *input;
    dm_membuf sink;
    size_t consumed = 0U;
    bool result = false;
    if (out) *out = NULL;
    if (out_size) *out_size = 0U;
    if (packed < DM_ZLIB_MIN) return false;
    input = (uint8_t *)xx_mem_alloc(packed);
    if (!input) return false;
    xx_mem_zero(&sink, sizeof(sink));
    sink.device.write = dm_membuf_write;
    sink.device.priv = &sink;
    sink.limit = limit;
    if (dm_read_at(format->device, format->base_address + offset, input,
                   packed) &&
        dm_zlib_start(input) &&
        xx_deflate_unpack_memory_to_device_ex(input + 2, packed - 2U,
                                              &sink.device, &consumed, false,
                                              NULL) &&
        consumed == (size_t)packed - 6U &&
        xx_zlib_stream_trailer_matches(input, packed, sink.data, sink.size))
        result = true;
    xx_mem_free(input);
    if (result && out) {
        if (!sink.data) {
            /* An empty record still hands back a buffer. */
            sink.data = (uint8_t *)xx_mem_alloc(1U);
            if (!sink.data) return false;
        }
        *out = sink.data;
        if (out_size) *out_size = sink.size;
        return true;
    }
    if (sink.data) xx_mem_free(sink.data);
    return result;
}

/* The length and zlib header of the record at @p offset (from base): the
 * whole record must lie before @p end. */
static bool dm_record_at(Abstractformat *format, int64_t offset, int64_t end,
                         uint32_t max_packed, uint32_t *packed) {
    uint8_t head[7];
    uint32_t length;
    if (offset < 0 || offset > end || end - offset < 4 + (int64_t)DM_ZLIB_MIN ||
        !dm_read_at(format->device, format->base_address + offset, head,
                    sizeof(head)))
        return false;
    length = dm_le32(head);
    if (length < DM_ZLIB_MIN || length > max_packed ||
        (int64_t)length > end - offset - 4 || !dm_zlib_start(head + 4))
        return false;
    *packed = length;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Locating the package                                                    */

/* End of the PE image's file data (headers and every section's raw data)
 * and where an Authenticode certificate that closes the file begins.  Both
 * are relative to base_address. */
static bool dm_pe_extent(xx_io_device *device, int64_t base, int64_t size,
                         int64_t *raw_end_out, int64_t *data_end_out,
                         uint32_t *alignment_out) {
    uint8_t dos[0x40];
    uint8_t nt[24];
    uint8_t optional[DM_PE_OPTIONAL_MAX];
    uint8_t sections[DM_PE_MAX_SECTIONS * DM_PE_SECTION_SIZE];
    uint32_t nt_offset, section_count, optional_size, magic, directories;
    uint32_t security_entry, index;
    size_t optional_read;
    int64_t raw_end, section_offset, data_end = size;
    if (size < (int64_t)sizeof(dos) ||
        !dm_read_at(device, base, dos, sizeof(dos)) || dos[0] != 'M' ||
        dos[1] != 'Z')
        return false;
    nt_offset = dm_le32(dos + 0x3C);
    if (nt_offset < 4U || (int64_t)nt_offset > size - (int64_t)sizeof(nt) ||
        !dm_read_at(device, base + nt_offset, nt, sizeof(nt)) ||
        xx_rt_memcmp(nt, "PE\0\0", 4U) != 0)
        return false;
    section_count = dm_le16(nt + 6);
    optional_size = dm_le16(nt + 20);
    if (section_count == 0U || section_count > DM_PE_MAX_SECTIONS ||
        optional_size < 64U)
        return false;
    optional_read = optional_size < DM_PE_OPTIONAL_MAX ? optional_size
                                                        : DM_PE_OPTIONAL_MAX;
    section_offset = (int64_t)nt_offset + 24 + optional_size;
    if (section_offset > size ||
        (int64_t)section_count * DM_PE_SECTION_SIZE > size - section_offset ||
        !dm_read_at(device, base + nt_offset + 24, optional, optional_read) ||
        !dm_read_at(device, base + section_offset, sections,
                    section_count * DM_PE_SECTION_SIZE))
        return false;
    magic = dm_le16(optional);
    if (magic == 0x10BU)
        directories = 96U;
    else if (magic == 0x20BU)
        directories = 112U;
    else
        return false;
    raw_end = (int64_t)dm_le32(optional + 60); /* SizeOfHeaders */
    if (raw_end > size) return false;
    for (index = 0U; index < section_count; ++index) {
        const uint8_t *entry = sections + index * DM_PE_SECTION_SIZE;
        int64_t raw_size = (int64_t)dm_le32(entry + 16);
        int64_t raw_offset = (int64_t)dm_le32(entry + 20);
        if (raw_size == 0) continue;
        if (raw_offset > size || raw_size > size - raw_offset) return false;
        if (raw_offset + raw_size > raw_end) raw_end = raw_offset + raw_size;
    }
    /* The security directory holds a file offset; the certificate is only
     * taken as the end of the package when it runs to the end of the file.
     * dm_locate() retries with the whole overlay should that be wrong. */
    security_entry = directories + 4U * 8U;
    if (optional_read >= security_entry + 8U &&
        dm_le32(optional + directories - 4U) > 4U) {
        int64_t cert_offset = (int64_t)dm_le32(optional + security_entry);
        int64_t cert_size = (int64_t)dm_le32(optional + security_entry + 4U);
        if (cert_size > 0 && cert_offset > raw_end && cert_offset <= size &&
            cert_size == size - cert_offset)
            data_end = cert_offset;
    }
    *raw_end_out = raw_end;
    *data_end_out = data_end;
    *alignment_out = dm_le32(optional + 36); /* FileAlignment */
    return true;
}

/* A bzip2 end-of-stream marker ending in the byte @p last (from base) at
 * bit shift @p shift: the stream then ends 4 bytes later, and there the
 * 0xFFFFFFFF marker and a zlib record have to follow. */
static bool dm_engine_end_ok(Abstractformat *format, int64_t last,
                             unsigned shift, int64_t end, int64_t *stream_end) {
    uint8_t probe[12];
    int64_t candidate = last + 5;
    uint32_t packed;
    if (candidate > end || end - candidate < 8 + 4 + (int64_t)DM_ZLIB_MIN ||
        !dm_read_at(format->device, format->base_address + last + 4, probe,
                    sizeof(probe)))
        return false;
    /* The padding behind the CRC is zero. */
    if ((probe[0] & ((1U << shift) - 1U)) != 0U) return false;
    if (dm_le32(probe + 1) != DM_MARKER) return false;
    packed = dm_le32(probe + 5);
    if (packed < DM_ZLIB_MIN || packed > DM_STRINGS_MAX_PACKED ||
        (int64_t)packed > end - candidate - 8 || !dm_zlib_start(probe + 9))
        return false;
    *stream_end = candidate;
    return true;
}

static bool dm_engine_end(Abstractformat *format, int64_t start, int64_t end,
                          int64_t *stream_end) {
    uint8_t *buffer;
    uint64_t window = 0U;
    int64_t position = start, limit = end;
    int64_t seen = 0;
    bool found = false;
    if (limit - start > DM_BZ_SCAN_MAX) limit = start + DM_BZ_SCAN_MAX;
    buffer = (uint8_t *)xx_mem_alloc(DM_BZ_CHUNK);
    if (!buffer) return false;
    while (!found && position < limit) {
        size_t chunk = limit - position < (int64_t)DM_BZ_CHUNK
                           ? (size_t)(limit - position)
                           : DM_BZ_CHUNK;
        size_t index;
        if (!dm_read_at(format->device, format->base_address + position,
                        buffer, chunk))
            break;
        for (index = 0U; index < chunk && !found; ++index) {
            unsigned shift;
            window = (window << 8U) | buffer[index];
            if (++seen < 6) continue;
            for (shift = 0U; shift < 8U; ++shift) {
                if (((window >> shift) & DM_BZ_MASK) == DM_BZ_EOS &&
                    dm_engine_end_ok(format, position + (int64_t)index, shift,
                                     end, stream_end)) {
                    found = true;
                    break;
                }
            }
        }
        position += (int64_t)chunk;
    }
    xx_mem_free(buffer);
    return found;
}

/* The package at @p overlay (from base): the bzip2 engine, the marked
 * string table and the project strings, both of which must inflate. */
static bool dm_try_overlay(Abstractformat *format, int64_t overlay,
                           int64_t end, dm_layout *layout, uint8_t **project,
                           size_t *project_size) {
    static const uint8_t block_magic[6] = {0x31, 0x41, 0x59, 0x26, 0x53, 0x59};
    uint8_t head[DM_BZ_HEADER];
    int64_t bz_end = 0, position;
    uint32_t packed;
    if (overlay < 0 || end - overlay < 64 ||
        !dm_read_at(format->device, format->base_address + overlay, head,
                    sizeof(head)) ||
        head[0] != 'B' || head[1] != 'Z' || head[2] != 'h' ||
        head[3] < '1' || head[3] > '9' ||
        xx_rt_memcmp(head + 4, block_magic, sizeof(block_magic)) != 0 ||
        !dm_engine_end(format, overlay, end, &bz_end))
        return false;
    /* The engine end check guarantees the marker and a sane length. */
    position = bz_end + 4;
    if (!dm_record_at(format, position, end, DM_STRINGS_MAX_PACKED, &packed) ||
        !dm_inflate_record(format, position + 4, packed,
                           DM_STRINGS_MAX_UNPACKED, NULL, NULL))
        return false;
    position += 4 + (int64_t)packed;
    if (!dm_record_at(format, position, end, DM_STRINGS_MAX_PACKED, &packed) ||
        !dm_inflate_record(format, position + 4, packed,
                           DM_STRINGS_MAX_UNPACKED, project, project_size))
        return false;
    layout->data_end = end;
    layout->overlay = overlay;
    layout->bz_end = bz_end;
    layout->after_project = position + 4 + (int64_t)packed;
    return true;
}

static bool dm_find_overlay(Abstractformat *format, int64_t raw_end,
                            int64_t end, uint32_t alignment,
                            dm_layout *layout, uint8_t **project,
                            size_t *project_size) {
    int64_t aligned;
    if (raw_end >= end) return false;
    if (dm_try_overlay(format, raw_end, end, layout, project, project_size))
        return true;
    if (alignment < 0x200U || alignment > 0x10000U ||
        (alignment & (alignment - 1U)) != 0U)
        return false;
    aligned = (raw_end + (int64_t)alignment - 1) & ~((int64_t)alignment - 1);
    return aligned != raw_end && aligned < end &&
           dm_try_overlay(format, aligned, end, layout, project, project_size);
}

static bool dm_locate(Abstractformat *format, dm_layout *layout,
                      uint8_t **project, size_t *project_size) {
    int64_t total, size, raw_end = 0, data_end = 0;
    uint32_t alignment = 0U;
    dm_layout found;
    if (project) *project = NULL;
    if (project_size) *project_size = 0U;
    if (!format || !format->device || !layout || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (!dm_pe_extent(format->device, format->base_address, size, &raw_end,
                      &data_end, &alignment))
        return false;
    xx_mem_zero(&found, sizeof(found));
    if (!dm_find_overlay(format, raw_end, data_end, alignment, &found, project,
                         project_size) &&
        (data_end == size ||
         !dm_find_overlay(format, raw_end, size, alignment, &found, project,
                          project_size)))
        return false;
    found.total = size;
    *layout = found;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Member names                                                            */

static char dm_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* One path component that is safe to create: no separators, drive colons
 * or wildcard/reserved punctuation, no control characters, not "." or
 * ".." (no trailing dot or space at all), and not a Windows device name in
 * any case, with or without an extension.  @p name is UTF-8. */
static bool dm_safe_component(const char *name, size_t length) {
    static const char *const devices[] = {"CON", "PRN", "AUX", "NUL",
                                          "CONIN$", "CONOUT$", "CLOCK$"};
    size_t index, stem = 0U, characters = 0U, device;
    if (!name || length == 0U) return false;
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
    if (characters > DM_NAME_CHARS || name[length - 1U] == '.' ||
        name[length - 1U] == ' ')
        return false;
    while (stem < length && name[stem] != '.') ++stem;
    while (stem > 0U && name[stem - 1U] == ' ') --stem;
    for (device = 0U; device < sizeof(devices) / sizeof(devices[0]); ++device) {
        const char *word = devices[device];
        size_t k = 0U;
        while (k < stem && word[k] && dm_upper(name[k]) == word[k]) ++k;
        if (k == stem && word[k] == 0) return false;
    }
    if (stem >= 4U &&
        ((dm_upper(name[0]) == 'C' && dm_upper(name[1]) == 'O' &&
          dm_upper(name[2]) == 'M') ||
         (dm_upper(name[0]) == 'L' && dm_upper(name[1]) == 'P' &&
          dm_upper(name[2]) == 'T'))) {
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

/* A relative path of safe components separated by '/'. */
static bool dm_safe_path(const char *path) {
    size_t start = 0U, index = 0U;
    if (!path || !path[0]) return false;
    for (;;) {
        if (path[index] == '/' || path[index] == 0) {
            if (!dm_safe_component(path + start, index - start)) return false;
            if (path[index] == 0) return true;
            start = index + 1U;
        }
        ++index;
    }
}

/* The 8-bit (ANSI, taken as Latin-1) name as UTF-8, with '\' turned into
 * '/'.  NULL for a name that is empty, too long, holds a NUL, or is not a
 * safe relative path. */
static char *dm_decode_name(const uint8_t *raw, size_t length) {
    char *out;
    size_t used = 0U, index;
    if (!raw || length == 0U || length > DM_NAME_BYTES) return NULL;
    out = (char *)xx_mem_alloc(length * 2U + 1U);
    if (!out) return NULL;
    for (index = 0U; index < length; ++index) {
        uint8_t c = raw[index];
        if (c == 0U) goto bad;
        if (c == '\\') c = '/';
        if (c < 0x80U) {
            out[used++] = (char)c;
        } else {
            out[used++] = (char)(0xC0U | (c >> 6U));
            out[used++] = (char)(0x80U | (c & 0x3FU));
        }
    }
    out[used] = 0;
    if (dm_safe_path(out)) return out;
bad:
    xx_mem_free(out);
    return NULL;
}

/* Byte @p index of a published name folded the way Windows compares file
 * names: ASCII letters, and the Latin-1 letters U+00E0..U+00FE (without
 * U+00F7), which are C3 A0..C3 BE in UTF-8, to upper case. */
static uint8_t dm_fold(const char *name, size_t index) {
    uint8_t c = (uint8_t)name[index];
    if (c >= 'a' && c <= 'z') return (uint8_t)(c - 'a' + 'A');
    if (index > 0U && (uint8_t)name[index - 1U] == 0xC3U && c >= 0xA0U &&
        c <= 0xBEU && c != 0xB7U)
        return (uint8_t)(c - 0x20U);
    return c;
}

static uint32_t dm_name_hash(const char *name) {
    uint32_t hash = UINT32_C(2166136261);
    size_t index;
    for (index = 0U; name[index]; ++index) {
        hash ^= dm_fold(name, index);
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static bool dm_same_name(const char *left, const char *right) {
    size_t index;
    for (index = 0U; left[index] && right[index]; ++index)
        if (dm_fold(left, index) != dm_fold(right, index)) return false;
    return left[index] == right[index];
}

static bool dm_name_taken(const dm_table *table, const char *name) {
    size_t slot = dm_name_hash(name) & table->mask, probes;
    for (probes = 0U; probes <= table->mask; ++probes) {
        uint32_t value = table->slots[slot];
        if (value == 0U) return false;
        if (table->entries[value - 1U].extractable &&
            dm_same_name(table->entries[value - 1U].name, name))
            return true;
        slot = (slot + 1U) & table->mask;
    }
    return true;
}

static void dm_name_insert(dm_table *table, size_t index) {
    size_t slot = dm_name_hash(table->entries[index].name) & table->mask,
           probes;
    for (probes = 0U; probes <= table->mask; ++probes) {
        if (table->slots[slot] == 0U) {
            table->slots[slot] = (uint32_t)(index + 1U);
            return;
        }
        slot = (slot + 1U) & table->mask;
    }
}

/* "stem_NNNN.ext" (or "name_NNNN" without an extension in the last path
 * component), with a further counter after the first attempt. */
static void dm_suffixed(char *out, size_t out_size, const char *base,
                        uint32_t ordinal, size_t attempt) {
    size_t length = xx_str_len(base), dot = length, index;
    for (index = length; index > 0U; --index) {
        if (base[index - 1U] == '/') break;
        if (base[index - 1U] == '.') {
            dot = index - 1U;
            break;
        }
    }
    if (dot == 0U || base[dot - 1U] == '/') dot = length;
    if (attempt == 0U)
        (void)xx_rt_snprintf(out, out_size, "%.*s_%04u%s", (int)dot, base,
                             (unsigned)ordinal, base + dot);
    else
        (void)xx_rt_snprintf(out, out_size, "%.*s_%04u_%u%s", (int)dot, base,
                             (unsigned)ordinal, (unsigned)attempt, base + dot);
}

/* Give entry @p index a unique, safe name derived from @p decoded (which
 * this takes ownership of; NULL when the raw name was unusable).  Unusable
 * names become "file_NNNN"; a name already published gets "_NNNN" (and a
 * further counter if needed) before its extension, so no member overwrites
 * another. */
static bool dm_publish_name(dm_table *table, size_t index, char *decoded,
                            uint32_t ordinal) {
    dm_entry *entry = &table->entries[index];
    char *base = decoded;
    char *candidate;
    size_t base_length, attempt;
    if (!base) {
        base = (char *)xx_mem_alloc(32U);
        if (!base) return false;
        (void)xx_rt_snprintf(base, 32U, "file_%04u", (unsigned)ordinal);
    }
    entry->extractable = false;
    if (!dm_name_taken(table, base)) {
        entry->name = base;
        entry->extractable = true;
        dm_name_insert(table, index);
        return true;
    }
    base_length = xx_str_len(base);
    candidate = (char *)xx_mem_alloc(base_length + 32U);
    if (!candidate) {
        xx_mem_free(base);
        return false;
    }
    for (attempt = 0U; attempt < DM_DEDUP_TRIES; ++attempt) {
        dm_suffixed(candidate, base_length + 32U, base, ordinal, attempt);
        if (!dm_name_taken(table, candidate)) {
            entry->extractable = true;
            break;
        }
    }
    if (entry->extractable) {
        xx_mem_free(base);
        entry->name = candidate;
        dm_name_insert(table, index);
    } else {
        /* Listed under its colliding name, never written. */
        xx_mem_free(candidate);
        entry->name = base;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* The settings walk                                                       */

static void dm_table_free(dm_table *table) {
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

typedef struct dm_walk_s {
    uint8_t *list;          /* inflated file list */
    size_t list_size;
    uint8_t *table;         /* raw file table, entries * 24 bytes */
    uint32_t entries;       /* specials + listed files */
    uint32_t specials;
    uint32_t listed;
    int64_t table_offset;
    dm_slot special[DM_SPECIALS];
    dm_slot *files;         /* listed files, in list order */
} dm_walk_t;

static void dm_walk_free(dm_walk_t *walk) {
    if (walk->list) xx_mem_free(walk->list);
    if (walk->table) xx_mem_free(walk->table);
    if (walk->files) xx_mem_free(walk->files);
    xx_mem_zero(walk, sizeof(*walk));
}

/* Walk the settings from the project strings to the file table.  Fails
 * (without claiming anything) when the layout does not match the one the
 * 2.x packages use. */
static bool dm_walk_settings(Abstractformat *format, const dm_layout *layout,
                             const uint8_t *project, size_t project_size,
                             dm_walk_t *walk, xx_pd_struct *pd) {
    const uint8_t *field[DM_PROJECT_FIELDS];
    size_t field_length[DM_PROJECT_FIELDS];
    size_t count = 0U, start = 0U, index;
    uint8_t window[DM_RESYNC_MAX + 8U];
    size_t window_size, tries = 0U;
    int64_t position = layout->after_project, end = layout->data_end;
    int64_t table_end;
    uint32_t packed, components, special, file;
    bool found = false;
    xx_mem_zero(walk, sizeof(*walk));

    /* Fields 7..10 of the project strings name the special files. */
    for (index = 0U; index < project_size && count < DM_PROJECT_FIELDS;
         ++index) {
        if (project[index] == 0x0CU) {
            field[count] = project + start;
            field_length[count] = index - start;
            ++count;
            start = index + 1U;
        }
    }
    if (count < DM_PROJECT_FIELDS) return false;

    /* The raw settings between the project strings and the font record are
     * not self-describing (69 bytes in 2.0, 110 in 2.7): the font record is
     * the first record there that inflates exactly. */
    if (position < 0 || position >= end) return false;
    window_size = end - position < (int64_t)sizeof(window)
                      ? (size_t)(end - position)
                      : sizeof(window);
    if (window_size < 4U + DM_ZLIB_MIN ||
        !dm_read_at(format->device, format->base_address + position, window,
                    window_size))
        return false;
    for (index = 0U; index + 7U <= window_size && index <= DM_RESYNC_MAX &&
                     tries < DM_RESYNC_TRIES;
         ++index) {
        uint32_t length = dm_le32(window + index);
        int64_t data = position + (int64_t)index + 4;
        if (length < DM_ZLIB_MIN || length > DM_FONT_MAX ||
            (int64_t)length > end - data || !dm_zlib_start(window + index + 4))
            continue;
        ++tries;
        if (dm_inflate_record(format, data, length, DM_FONT_MAX, NULL, NULL)) {
            position = data + (int64_t)length;
            found = true;
            break;
        }
    }
    if (!found) return false;

    /* The special files, in field order. */
    for (index = 0U; index < DM_SPECIALS; ++index) {
        size_t f = DM_FIRST_SPECIAL + index;
        if (field_length[f] == 0U) continue;
        if (!dm_record_at(format, position, end, UINT32_MAX, &packed))
            return false;
        walk->special[walk->specials].raw_name = field[f];
        walk->special[walk->specials].raw_length = field_length[f];
        walk->special[walk->specials].record = position;
        walk->special[walk->specials].packed = packed;
        walk->special[walk->specials].table_index = walk->specials;
        ++walk->specials;
        position += 4 + (int64_t)packed;
    }

    /* The components: a count byte and one record each. */
    {
        uint8_t byte;
        if (position >= end ||
            !dm_read_at(format->device, format->base_address + position,
                        &byte, 1U))
            return false;
        components = byte;
        ++position;
    }
    for (index = 0U; index < components; ++index) {
        if (!dm_record_at(format, position, end, UINT32_MAX, &packed))
            return false;
        position += 4 + (int64_t)packed;
    }
    if (pd && xx_pd_is_stopped(pd)) return false;

    /* The file list: one CRLF-terminated name per installed file. */
    if (!dm_record_at(format, position, end, DM_LIST_MAX_PACKED, &packed) ||
        !dm_inflate_record(format, position + 4, packed, DM_LIST_MAX_UNPACKED,
                           &walk->list, &walk->list_size))
        goto fail;
    position += 4 + (int64_t)packed;
    for (index = 0U, start = 0U; index < walk->list_size; ++index) {
        if (walk->list[index] == '\n' && index > start &&
            walk->list[index - 1U] == '\r') {
            if (++walk->listed > DM_MAX_FILES) goto fail;
            start = index + 1U;
        }
    }
    /* Anything behind the last CRLF is not a list the 2.x packages write. */
    if (start != walk->list_size) goto fail;
    walk->entries = walk->specials + walk->listed;
    if (walk->entries == 0U) goto fail;
    if (walk->listed) {
        walk->files = (dm_slot *)xx_mem_calloc(walk->listed, sizeof(dm_slot));
        if (!walk->files) goto fail;
        for (index = 0U, start = 0U, file = 0U; index < walk->list_size;
             ++index) {
            if (walk->list[index] == '\n' && index > start &&
                walk->list[index - 1U] == '\r') {
                walk->files[file].raw_name = walk->list + start;
                walk->files[file].raw_length = index - 1U - start;
                walk->files[file].table_index = walk->specials + file;
                walk->files[file].record = -1;
                ++file;
                start = index + 1U;
            }
        }
    }

    /* The file table. */
    if ((int64_t)walk->entries * DM_ENTRY_SIZE > end - position) goto fail;
    walk->table_offset = position;
    table_end = position + (int64_t)walk->entries * DM_ENTRY_SIZE;
    walk->table = (uint8_t *)xx_mem_alloc((size_t)walk->entries *
                                          DM_ENTRY_SIZE);
    if (!walk->table ||
        !dm_read_at(format->device, format->base_address + position,
                    walk->table, (size_t)walk->entries * DM_ENTRY_SIZE))
        goto fail;
    /* A special file's data is inline, so its offset is the marker. */
    for (special = 0U; special < walk->specials; ++special)
        if (dm_le32(walk->table + 4U * special) != DM_MARKER) goto fail;
    for (file = 0U; file < walk->listed; ++file) {
        uint32_t offset = dm_le32(walk->table +
                                  4U * (walk->specials + file));
        if ((file & 0xFFU) == 0U && pd && xx_pd_is_stopped(pd)) goto fail;
        if (offset == DM_MARKER) continue;       /* not in this file */
        /* Every file record lies behind the settings. */
        if ((int64_t)offset < table_end) goto fail;
        walk->files[file].record = (int64_t)offset;
        /* A record cut off by truncation or damaged: still listed, so the
         * loss shows, but it fails to unpack. */
        if (!dm_record_at(format, (int64_t)offset, end, UINT32_MAX, &packed))
            walk->files[file].broken = true;
        else
            walk->files[file].packed = packed;
    }
    return true;
fail:
    dm_walk_free(walk);
    return false;
}

static void dm_fill_from_table(dm_entry *entry, const dm_walk_t *walk,
                               uint32_t table_index) {
    const uint8_t *table = walk->table;
    uint32_t entries = walk->entries;
    entry->dos_datetime = dm_le32(table + 4U * (entries + table_index));
    entry->size = dm_le32(table + 16U * entries + 4U * table_index);
    entry->crc = dm_le32(table + 20U * entries + 4U * table_index);
    entry->has_table_info = true;
}

/* Build the member table: the engine, then either every table entry with
 * data here or, when the settings cannot be walked, the rest of the
 * overlay as one raw member.  With @p build clear only the counts are
 * measured. */
static bool dm_build(Abstractformat *format, const dm_layout *layout,
                     const uint8_t *project, size_t project_size,
                     dm_table *table, bool build, xx_pd_struct *pd) {
    dm_walk_t walk;
    size_t capacity, slots = 16U;
    uint32_t index;
    char *name;
    xx_mem_zero(table, sizeof(*table));
    table->table_offset = -1;
    table->records_end = -1;
    table->walked = dm_walk_settings(format, layout, project, project_size,
                                     &walk, pd);
    if (table->walked) {
        uint32_t present = walk.specials;
        for (index = 0U; index < walk.listed; ++index) {
            const dm_slot *slot = &walk.files[index];
            if (slot->record < 0) continue;
            ++present;
            if (slot->broken) {
                ++table->broken;
                continue;
            }
            if (slot->record + 4 + slot->packed > table->records_end)
                table->records_end = slot->record + 4 + slot->packed;
        }
        table->specials = walk.specials;
        table->listed = walk.listed;
        table->missing = walk.entries - present;
        table->table_offset = walk.table_offset;
        capacity = 1U + (size_t)present;
    } else {
        capacity = 2U;
    }
    if (!build) {
        table->count = capacity;
        if (table->walked) dm_walk_free(&walk);
        return true;
    }
    while (slots < capacity * 2U) slots <<= 1U;
    table->capacity = capacity;
    table->entries = (dm_entry *)xx_mem_calloc(capacity, sizeof(dm_entry));
    table->slots = (uint32_t *)xx_mem_calloc(slots, sizeof(uint32_t));
    table->mask = slots - 1U;
    if (!table->entries || !table->slots) goto fail;

    /* The setup engine. */
    {
        dm_entry *entry = &table->entries[table->count];
        entry->header_offset = layout->overlay;
        entry->header_size = 0;
        entry->data_offset = layout->overlay;
        entry->packed_size = layout->bz_end - layout->overlay;
        entry->kind = DM_KIND_ENGINE;
        name = (char *)xx_mem_alloc(sizeof(DM_ENGINE_NAME));
        if (!name) goto fail;
        xx_rt_memcpy(name, DM_ENGINE_NAME, sizeof(DM_ENGINE_NAME));
        if (!dm_publish_name(table, table->count, name, 0U)) goto fail;
        ++table->count;
    }
    if (!table->walked) {
        dm_entry *entry = &table->entries[table->count];
        entry->header_offset = layout->bz_end;
        entry->header_size = 0;
        entry->data_offset = layout->bz_end;
        entry->packed_size = layout->data_end - layout->bz_end;
        entry->size = (uint64_t)entry->packed_size;
        entry->kind = DM_KIND_RAW;
        name = (char *)xx_mem_alloc(sizeof(DM_FALLBACK_NAME));
        if (!name) goto fail;
        xx_rt_memcpy(name, DM_FALLBACK_NAME, sizeof(DM_FALLBACK_NAME));
        if (!dm_publish_name(table, table->count, name, 1U)) goto fail;
        ++table->count;
        return true;
    }
    for (index = 0U; index < walk.specials + walk.listed; ++index) {
        const dm_slot *slot = index < walk.specials
                                  ? &walk.special[index]
                                  : &walk.files[index - walk.specials];
        dm_entry *entry;
        if (slot->record < 0) continue;
        entry = &table->entries[table->count];
        entry->header_offset = slot->record;
        entry->header_size = 4;
        entry->data_offset = slot->record + 4;
        entry->packed_size = slot->packed;
        entry->kind = DM_KIND_ZLIB;
        entry->broken = slot->broken;
        dm_fill_from_table(entry, &walk, slot->table_index);
        if (!dm_publish_name(table, table->count,
                             dm_decode_name(slot->raw_name, slot->raw_length),
                             slot->table_index + 1U))
            goto fail;
        ++table->count;
    }
    dm_walk_free(&walk);
    return true;
fail:
    if (table->walked) dm_walk_free(&walk);
    dm_table_free(table);
    return false;
}

/* ---------------------------------------------------------------------- */
/* Extraction                                                              */

/* Output sink: forwards to the destination (none when only verifying),
 * enforces the size limit and keeps the CRC-32 and the Adler-32. */
typedef struct dm_sink_s {
    xx_io_device device;
    xx_io_device *target;
    uint64_t written;
    uint64_t limit;
    uint32_t crc;
    uint32_t adler_a;
    uint32_t adler_b;
} dm_sink;

static ssize_t dm_sink_write(xx_io_device *self, const void *buffer,
                             size_t size) {
    dm_sink *sink = self ? (dm_sink *)self->priv : NULL;
    const uint8_t *bytes = (const uint8_t *)buffer;
    size_t index = 0U, done = 0U;
    if (!sink || (!bytes && size != 0U) || size > DM_SSIZE_LIMIT ||
        (uint64_t)size > sink->limit - sink->written)
        return -1;
    if (size) sink->crc = xx_crc32_calc(sink->crc, bytes, size);
    while (index < size) {
        size_t chunk = size - index;
        size_t stop;
        uint32_t a = sink->adler_a, b = sink->adler_b;
        if (chunk > 5552U) chunk = 5552U;
        for (stop = index + chunk; index < stop; ++index) {
            a += bytes[index];
            b += a;
        }
        sink->adler_a = a % 65521U;
        sink->adler_b = b % 65521U;
    }
    while (sink->target && done < size) {
        ssize_t wrote = xx_io_write(sink->target, bytes + done, size - done);
        if (wrote <= 0 || (size_t)wrote > size - done) return -1;
        done += (size_t)wrote;
    }
    sink->written += size;
    return (ssize_t)size;
}

static void dm_sink_init(dm_sink *sink, xx_io_device *target,
                         uint64_t limit) {
    xx_mem_zero(sink, sizeof(*sink));
    sink->device.write = dm_sink_write;
    sink->device.priv = sink;
    sink->target = target;
    sink->limit = limit;
    sink->adler_a = 1U;
}

static bool dm_copy(xx_io_device *source, int64_t offset, int64_t size,
                    dm_sink *sink, xx_pd_struct *pd) {
    uint8_t *buffer;
    bool result = true;
    if (size < 0 || (uint64_t)size > sink->limit) return false;
    buffer = (uint8_t *)xx_mem_alloc(DM_COPY_BUFFER);
    if (!buffer) return false;
    while (size > 0) {
        size_t chunk = size < (int64_t)DM_COPY_BUFFER ? (size_t)size
                                                      : DM_COPY_BUFFER;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !dm_read_at(source, offset, buffer, chunk) ||
            dm_sink_write(&sink->device, buffer, chunk) != (ssize_t)chunk) {
            result = false;
            break;
        }
        offset += (int64_t)chunk;
        size -= (int64_t)chunk;
    }
    xx_mem_free(buffer);
    return result;
}

static bool dm_unpack_entry(Abstractformat *format, const dm_stream *stream,
                            const dm_entry *entry, xx_io_device *target,
                            xx_pd_struct *pd) {
    dm_sink sink;
    int64_t base = format->base_address;
    if (entry->kind == DM_KIND_ENGINE) {
        uint64_t limit = stream->max_member < DM_ENGINE_MAX
                             ? stream->max_member
                             : DM_ENGINE_MAX;
        dm_sink_init(&sink, target, limit);
        return xx_bzip2_unpack_device(format->device, base + entry->data_offset,
                                      entry->packed_size, &sink.device, pd) &&
               sink.written > 0U;
    }
    if (entry->kind == DM_KIND_RAW) {
        dm_sink_init(&sink, target, stream->max_member);
        return dm_copy(format->device, base + entry->data_offset,
                       entry->packed_size, &sink, pd);
    }
    {
        uint8_t zlib_head[3], trailer[4];
        uint32_t adler;
        /* The table's size is exact: nothing larger is ever written. */
        if (entry->broken || entry->size > stream->max_member) return false;
        dm_sink_init(&sink, target, entry->size);
        if (entry->packed_size < (int64_t)DM_ZLIB_MIN ||
            !dm_read_at(format->device, base + entry->data_offset, zlib_head,
                        sizeof(zlib_head)) ||
            !dm_zlib_start(zlib_head))
            return false;
        if (!xx_deflate_unpack_device(format->device,
                                      base + entry->data_offset + 2,
                                      entry->packed_size - 2, &sink.device,
                                      false, pd) ||
            !dm_read_at(format->device,
                        base + entry->data_offset + entry->packed_size - 4,
                        trailer, sizeof(trailer)))
            return false;
        adler = ((uint32_t)trailer[0] << 24U) | ((uint32_t)trailer[1] << 16U) |
                ((uint32_t)trailer[2] << 8U) | (uint32_t)trailer[3];
        return adler == ((sink.adler_b << 16U) | sink.adler_a) &&
               sink.written == entry->size && sink.crc == entry->crc;
    }
}

/* ---------------------------------------------------------------------- */
/* Options and records                                                     */

static bool dm_copy_options(xx_list_s *destination, const xx_list_s *source) {
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
static uint64_t dm_max_member(const Abstractformat *format,
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

static bool dm_set_record(xx_archive_record *record, const dm_entry *entry,
                          int64_t base) {
    uint32_t method = entry->kind == DM_KIND_ENGINE ? 12U
                      : entry->kind == DM_KIND_ZLIB ? 8U
                                                    : 0U;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = base + entry->header_offset;
    record->header_size = entry->header_size;
    record->data_offset = base + entry->data_offset;
    record->compressed_size = entry->packed_size;
    if (!xx_archive_record_set_original_name(record, entry->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        (uint64_t)entry->packed_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        method) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false))
        return false;
    if (entry->kind != DM_KIND_ENGINE &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        entry->size))
        return false;
    if (entry->has_table_info &&
        (!xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                         entry->crc) ||
         !xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                         entry->dos_datetime & 0xFFFFU) ||
         !xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                         entry->dos_datetime >> 16U)))
        return false;
    return true;
}

/* The package ends with the last file record, or with the Authenticode
 * certificate (8-byte aligned) right behind it.  Without a file record in
 * this file the end of the trailing settings is unknown, so everything to
 * the end of the device is taken. */
static int64_t dm_format_size(const dm_layout *layout, const dm_table *table) {
    int64_t end = table->records_end;
    if (end < 0 || end > layout->data_end || table->broken)
        return layout->total;
    if (layout->data_end < layout->total && layout->data_end - end < 8)
        return layout->total;
    return end;
}

static void dm_stream_free(void *opaque) {
    dm_stream *stream = (dm_stream *)opaque;
    if (!stream) return;
    dm_table_free(&stream->table);
    xx_mem_free(stream);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_sfx_jgsoft_deploymaster_package_init(
    xx_sfx_jgsoft_deploymaster_package *archive, xx_io_device *device,
    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SFX_JGSOFT_DEPLOYMASTER_PACKAGE_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.os = XX_OS_WINDOWS;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-dosexec");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid =
        xx_sfx_jgsoft_deploymaster_package_check_is_valid;
    archive->format.handle_base_info =
        xx_sfx_jgsoft_deploymaster_package_handle_base_info;
    archive->format.get_format_size =
        xx_sfx_jgsoft_deploymaster_package_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_sfx_jgsoft_deploymaster_package_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_sfx_jgsoft_deploymaster_package_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_sfx_jgsoft_deploymaster_package_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_sfx_jgsoft_deploymaster_package_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_sfx_jgsoft_deploymaster_package_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_sfx_jgsoft_deploymaster_package_free_archive_records_reading;
    archive->overlay_start = -1;
    archive->data_end = -1;
    archive->table_offset = -1;
}

xx_sfx_jgsoft_deploymaster_package *xx_sfx_jgsoft_deploymaster_package_create(
    xx_io_device *device, int64_t base_address) {
    xx_sfx_jgsoft_deploymaster_package *archive =
        (xx_sfx_jgsoft_deploymaster_package *)xx_mem_alloc(sizeof(*archive));
    if (archive)
        xx_sfx_jgsoft_deploymaster_package_init(archive, device, base_address);
    return archive;
}

void xx_sfx_jgsoft_deploymaster_package_destroy(
    xx_sfx_jgsoft_deploymaster_package *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_sfx_jgsoft_deploymaster_package_free(
    xx_sfx_jgsoft_deploymaster_package *archive) {
    if (!archive) return;
    xx_sfx_jgsoft_deploymaster_package_destroy(archive);
    xx_mem_free(archive);
}

bool xx_sfx_jgsoft_deploymaster_package_check_is_valid(Abstractformat *format,
                                                       xx_pd_struct *pd) {
    dm_layout layout;
    (void)pd;
    return dm_locate(format, &layout, NULL, NULL);
}

bool xx_sfx_jgsoft_deploymaster_package_handle_base_info(
    Abstractformat *format, xx_pd_struct *pd) {
    xx_sfx_jgsoft_deploymaster_package *archive;
    dm_layout layout;
    dm_table table;
    uint8_t *project = NULL;
    size_t project_size = 0U;
    bool built;
    if (!format || !dm_locate(format, &layout, &project, &project_size))
        return false;
    built = dm_build(format, &layout, project, project_size, &table, false, pd);
    xx_mem_free(project);
    if (!built) return false;
    archive = (xx_sfx_jgsoft_deploymaster_package *)format;
    archive->overlay_start = layout.overlay;
    archive->engine_size = layout.bz_end - layout.overlay;
    archive->data_end = layout.data_end;
    archive->table_offset = table.table_offset;
    archive->specials = table.specials;
    archive->listed_files = table.listed;
    archive->missing_files = table.missing;
    archive->damaged_files = table.broken;
    archive->walked = table.walked;
    archive->number_of_records = (uint64_t)table.count;
    format->number_of_archive_records = archive->number_of_records;
    format->format_size = dm_format_size(&layout, &table);
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_sfx_jgsoft_deploymaster_package_get_format_size(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_jgsoft_deploymaster_package_handle_base_info(
                          format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_sfx_jgsoft_deploymaster_package_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_sfx_jgsoft_deploymaster_package_handle_base_info(
                          format, pd))
               ? ((xx_sfx_jgsoft_deploymaster_package *)format)
                     ->number_of_records
               : 0U;
}

xx_archive_record_state *
xx_sfx_jgsoft_deploymaster_package_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    dm_stream *stream;
    xx_archive_record_state *state;
    uint8_t *project = NULL;
    size_t project_size = 0U;
    bool built;
    if (!format) return NULL;
    stream = (dm_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!dm_locate(format, &stream->layout, &project, &project_size)) {
        dm_stream_free(stream);
        return NULL;
    }
    built = dm_build(format, &stream->layout, project, project_size,
                     &stream->table, true, pd);
    xx_mem_free(project);
    if (!built || stream->table.count == 0U) {
        dm_stream_free(stream);
        return NULL;
    }
    stream->max_member = dm_max_member(format, options);
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        dm_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = dm_stream_free;
    state->total_records = (int64_t)stream->table.count;
    if (!dm_copy_options(&state->options, options) ||
        !dm_set_record(&state->current_record, &stream->table.entries[0],
                       format->base_address)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->current_index = 0;
    state->has_record = true;
    return state;
}

const xx_archive_record *
xx_sfx_jgsoft_deploymaster_package_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_sfx_jgsoft_deploymaster_package_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    dm_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (dm_stream *)state->internal_state) ||
        stream->index + 1U >= stream->table.count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    if (!dm_set_record(&state->current_record,
                       &stream->table.entries[stream->index],
                       format->base_address)) {
        state->has_record = false;
        return false;
    }
    state->current_index = (int64_t)stream->index;
    state->has_record = true;
    return true;
}

bool xx_sfx_jgsoft_deploymaster_package_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state,
    xx_pd_struct *pd) {
    dm_stream *stream;
    const dm_entry *entry;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (dm_stream *)state->internal_state) ||
        stream->index >= stream->table.count || (pd && xx_pd_is_stopped(pd)))
        return false;
    entry = &stream->table.entries[stream->index];
    path_option = xx_format_resolve_extra_parameter(
        format, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return dm_unpack_entry(format, stream, entry, NULL, pd);
    if (!entry->extractable || !dm_safe_path(entry->name)) return false;
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
        result = dm_unpack_entry(format, stream, entry, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_sfx_jgsoft_deploymaster_package_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
