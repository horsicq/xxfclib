/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Indigo Rose Setup Factory 5/6 installers.  xx_setup_factory.h carries the
 * overlay layout.
 *
 * The installer is located the way XArchive's installers/xsetupfactory.cpp
 * (MIT, same author) does it: end of the last section's raw data, then the
 * E0..E7 marker and the engine chain.  The CFileInfo field order follows
 * what U3's Setup Factory handler reads, checked against the one real
 * installer in the corpus and against synthetic installers that U3 itself
 * extracts.  The DCL decoder below streams the same bit format as the
 * library's src/algo/dcl/xx_dcl.c (MIT, this library) -- same fixed trees,
 * same checks -- but reads from the device and writes through a small
 * window, so a member of any size is decoded in bounded memory.
 *
 * Only the PE headers are parsed (to find the overlay); nothing in the
 * executable is run or emulated.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/setup_factory/xx_setup_factory.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder: xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested; this
 * picks up the real file type as soon as the format is registered. */
#ifdef SETUP_FACTORY
#define XX_SETUP_FACTORY_FILE_TYPE XX_FILE_TYPE_SETUP_FACTORY
#else
#define XX_SETUP_FACTORY_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* ---- limits ------------------------------------------------------------ */

#define SF_MIN_FILE 0x200
#define SF_MAX_SECTIONS 96U
#define SF_MAX_OPTIONAL 0x1000U
#define SF_SECTION_SIZE 40U
#define SF_MAX_ENGINES 99U
#define SF_LONG_NAME 260U
#define SF_SHORT_NAME 16U
#define SF_OVERLAY_HEADER 12
/* The manifest is decoded into memory.  Setup Factory 6 spends ~420 bytes
 * per file, so even the u16 maximum of 65535 files stays under 28 MiB. */
#define SF_MANIFEST_MAX_PACKED (16U * 1024U * 1024U)
#define SF_MANIFEST_MAX_RAW (48U * 1024U * 1024U)
#define SF_MAX_STRING 0x10000U
#define SF_MAX_NAME 1024U
#define SF_MAX_VARIABLE 64U
#define SF_RENAME_ROUNDS 4U
/* Smallest serialized CFileInfo of each layout (every string empty, no
 * extra list): the count is checked against it before anything is
 * allocated for the member table. */
#define SF_MIN_RECORD_LONG 132U
#define SF_MIN_RECORD_SHORT 134U

/* ---- DCL ---------------------------------------------------------------- */

#define SF_DCL_MAX_BITS 13U
#define SF_IN_BUFFER 16384U
#define SF_OUT_WINDOW 65536U
/* The farthest a DCL match reaches back is (63 << 6) + 63 + 1 bytes. */
#define SF_HISTORY 4096U
#define SF_MAX_MATCH 518U
#define SF_DCL_END 519U
#define SF_POLL_MASK 0xFFFFUL

#define SF_METHOD_DCL XX_SETUP_FACTORY_METHOD_DCL
#define SF_METHOD_STORED XX_SETUP_FACTORY_METHOD_STORED
#define SF_METHOD_UNKNOWN XX_SETUP_FACTORY_METHOD_UNKNOWN

static const uint8_t sf_magic[8] = {0xE0, 0xE1, 0xE2, 0xE3,
                                    0xE4, 0xE5, 0xE6, 0xE7};

/* The PKWARE DCL fixed trees, as runs: high nibble is repeat - 1, low
 * nibble a code length (the public DCL description; identical tables are
 * in src/algo/dcl/xx_dcl.c). */
static const uint8_t sf_literal_runs[] = {
    11,  124, 8,   7,   28,  7,   188, 13,  76,  4,   10,  8,   12,  10,
    12,  10,  8,   23,  8,   9,   7,   6,   7,   8,   7,   6,   55,  8,
    23,  24,  12,  11,  7,   9,   11,  12,  6,   7,   22,  5,   7,   24,
    6,   11,  9,   6,   7,   22,  7,   11,  38,  7,   9,   8,   25,  11,
    8,   11,  9,   12,  8,   12,  5,   38,  5,   38,  5,   11,  7,   5,
    6,   21,  6,   10,  53,  8,   7,   24,  10,  27,  44,  253, 253, 253,
    252, 252, 252, 13,  12,  45,  12,  45,  12,  61,  12,  45,  44,  173};
static const uint8_t sf_length_runs[] = {2, 35, 36, 53, 38, 23};
static const uint8_t sf_distance_runs[] = {2, 20, 53, 230, 247, 151, 248};
static const uint16_t sf_length_base[16] = {3,  2,  4,  5,  6,   7,   8,   9,
                                            10, 12, 16, 24, 40, 72, 136, 264};
static const uint8_t sf_length_extra[16] = {0, 0, 0, 0, 0, 0, 0, 0,
                                            1, 2, 3, 4, 5, 6, 7, 8};

typedef struct sf_huffman_s {
    uint16_t count[SF_DCL_MAX_BITS + 1U];
    uint16_t symbol[256];
    unsigned symbols;
} sf_huffman;

typedef struct sf_input_s {
    xx_io_device *device;
    int64_t offset;     /**< Device offset of the next refill. */
    uint64_t remaining; /**< Stream bytes not yet buffered. */
    uint64_t fetched;   /**< Bytes moved into the bit accumulator. */
    size_t length;
    size_t position;
    uint32_t bits;
    unsigned count;
    uint8_t buffer[SF_IN_BUFFER];
} sf_input;

typedef struct sf_output_s {
    uint8_t *window;
    size_t length;    /**< Bytes staged in window. */
    size_t delivered; /**< Leading window bytes already delivered. */
    uint64_t total;   /**< Bytes produced. */
    uint64_t limit;   /**< Most bytes the stream may produce. */
    uint32_t crc;
    xx_io_device *device; /**< Optional file sink. */
    bool to_memory;       /**< Also collect everything in memory. */
    uint8_t *memory;
    size_t memory_size;
    size_t memory_capacity;
} sf_output;

/* ---- parsed installer ---------------------------------------------------- */

typedef struct sf_locate_s {
    int64_t size;            /**< Bytes from the base address to EOF. */
    int64_t overlay;         /**< All offsets here are base-relative. */
    int64_t payload;
    int64_t manifest_offset;
    uint32_t manifest_packed;
    uint32_t manifest_crc;
    uint32_t engine_count;
    uint32_t layout;
    char version[8];
} sf_locate;

typedef struct sf_member_s {
    int64_t data_offset; /**< Absolute device offset. */
    uint32_t packed_size;
    uint32_t raw_size;
    uint32_t crc;
    uint32_t mtime;
    char *name;
    uint8_t method;
    bool unsafe;  /**< Name refused for extraction. */
    bool missing; /**< Data runs past the end of the file. */
    bool duplicate; /**< Renamed because an earlier member had the name. */
    bool pending;   /**< Scratch flag of sf_resolve_duplicates. */
} sf_member;

typedef struct sf_archive_s {
    sf_locate locate;
    sf_member *members;
    size_t count;
    uint32_t schema;
    int64_t payload_end; /**< Base-relative, clamped to the file. */
    bool truncated;
} sf_archive;

typedef struct sf_stream_s {
    sf_archive archive;
    size_t index;
} sf_stream;

/* ---- small helpers ----------------------------------------------------- */

static uint32_t sf_le16(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static uint32_t sf_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool sf_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool sf_write_all(xx_io_device *device, const uint8_t *data,
                         size_t size) {
    size_t done = 0U;
    while (done < size) {
        ssize_t wrote = xx_io_write(device, data + done, size - done);
        if (wrote <= 0 || (size_t)wrote > size - done) return false;
        done += (size_t)wrote;
    }
    return true;
}

static uint8_t sf_upper(uint8_t c) {
    return (c >= 'a' && c <= 'z') ? (uint8_t)(c - 'a' + 'A') : c;
}

/* Case-insensitive ASCII comparison of @p length bytes against @p word. */
static bool sf_same_ascii(const uint8_t *text, size_t length,
                          const char *word) {
    size_t index;
    for (index = 0U; index < length; ++index)
        if (!word[index] || sf_upper(text[index]) != sf_upper((uint8_t)word[index]))
            return false;
    return word[length] == 0;
}

/* ---- DCL decoder ------------------------------------------------------- */

static bool sf_build_tree(sf_huffman *tree, const uint8_t *runs,
                          size_t run_count) {
    uint8_t lengths[256];
    unsigned offsets[SF_DCL_MAX_BITS + 1U];
    unsigned symbol_count = 0U, index, length;
    int free_codes = 1;
    xx_rt_memset(tree, 0, sizeof(*tree));
    for (index = 0U; index < run_count; ++index) {
        unsigned repeat = ((unsigned)runs[index] >> 4U) + 1U;
        length = (unsigned)runs[index] & 15U;
        if (length == 0U || length > SF_DCL_MAX_BITS ||
            repeat > (unsigned)sizeof(lengths) - symbol_count)
            return false;
        while (repeat--) lengths[symbol_count++] = (uint8_t)length;
    }
    if (symbol_count == 0U) return false;
    for (index = 0U; index < symbol_count; ++index) ++tree->count[lengths[index]];
    for (length = 1U; length <= SF_DCL_MAX_BITS; ++length) {
        free_codes = (free_codes << 1) - (int)tree->count[length];
        if (free_codes < 0) return false;
    }
    offsets[0] = 0U;
    offsets[1] = 0U;
    for (length = 1U; length < SF_DCL_MAX_BITS; ++length)
        offsets[length + 1U] = offsets[length] + tree->count[length];
    for (index = 0U; index < symbol_count; ++index)
        tree->symbol[offsets[lengths[index]]++] = (uint16_t)index;
    tree->symbols = symbol_count;
    return true;
}

static bool sf_in_byte(sf_input *in, uint32_t *value) {
    if (in->position >= in->length) {
        size_t chunk;
        if (in->remaining == 0U) return false;
        chunk = in->remaining < (uint64_t)SF_IN_BUFFER ? (size_t)in->remaining
                                                        : (size_t)SF_IN_BUFFER;
        if (!sf_read_at(in->device, in->offset, in->buffer, chunk))
            return false;
        in->offset += (int64_t)chunk;
        in->remaining -= chunk;
        in->length = chunk;
        in->position = 0U;
    }
    *value = in->buffer[in->position++];
    ++in->fetched;
    return true;
}

static bool sf_bits(sf_input *in, unsigned need, unsigned *value) {
    while (in->count < need) {
        uint32_t byte;
        if (!sf_in_byte(in, &byte)) return false;
        in->bits |= byte << in->count;
        in->count += 8U;
    }
    *value = (unsigned)(in->bits & ((1UL << need) - 1UL));
    in->bits >>= need;
    in->count -= need;
    return true;
}

/* DCL sends canonical codes most significant bit first and inverted. */
static int sf_symbol(sf_input *in, const sf_huffman *tree) {
    unsigned code = 0U, first = 0U, position = 0U, length;
    for (length = 1U; length <= SF_DCL_MAX_BITS; ++length) {
        unsigned bit;
        if (!sf_bits(in, 1U, &bit)) return -1;
        code |= bit ^ 1U;
        if (code < first + tree->count[length]) {
            unsigned at = position + code - first;
            return at < tree->symbols ? (int)tree->symbol[at] : -1;
        }
        position += tree->count[length];
        first = (first + tree->count[length]) << 1U;
        code <<= 1U;
    }
    return -1;
}

static bool sf_deliver(sf_output *out) {
    size_t amount = out->length - out->delivered;
    const uint8_t *data = out->window + out->delivered;
    if (amount == 0U) return true;
    out->crc = xx_crc32_calc(out->crc, data, amount);
    if (out->device && !sf_write_all(out->device, data, amount)) return false;
    if (out->to_memory) {
        if (amount > out->memory_capacity - out->memory_size) {
            size_t required, wanted;
            uint8_t *grown;
            if (amount > (size_t)SF_MANIFEST_MAX_RAW - out->memory_size)
                return false;
            required = out->memory_size + amount;
            wanted = out->memory_capacity ? out->memory_capacity : 65536U;
            while (wanted < required)
                wanted = wanted > (size_t)SF_MANIFEST_MAX_RAW / 2U
                             ? (size_t)SF_MANIFEST_MAX_RAW
                             : wanted * 2U;
            grown = (uint8_t *)xx_mem_realloc(out->memory, wanted);
            if (!grown) return false;
            out->memory = grown;
            out->memory_capacity = wanted;
        }
        xx_rt_memcpy(out->memory + out->memory_size, data, amount);
        out->memory_size += amount;
    }
    out->delivered = out->length;
    return true;
}

/* Deliver what is staged and keep the last SF_HISTORY bytes. */
static bool sf_make_room(sf_output *out, size_t needed) {
    if (out->length + needed <= SF_OUT_WINDOW) return true;
    if (!sf_deliver(out)) return false;
    if (out->length > SF_HISTORY) {
        xx_rt_memmove(out->window, out->window + out->length - SF_HISTORY,
                      SF_HISTORY);
        out->length = SF_HISTORY;
        out->delivered = SF_HISTORY;
    }
    return out->length + needed <= SF_OUT_WINDOW;
}

/* Decode one DCL stream to its end code.  Every loop pass reads at least
 * one bit of a stream of known length and output is capped by out->limit,
 * so the loop is bounded by its input. */
static bool sf_dcl_decode(sf_input *in, sf_output *out, xx_pd_struct *pd) {
    sf_huffman literals, lengths, distances;
    unsigned literal_mode, dictionary_bits;
    unsigned long steps = 0UL;
    if (!sf_build_tree(&literals, sf_literal_runs, sizeof(sf_literal_runs)) ||
        !sf_build_tree(&lengths, sf_length_runs, sizeof(sf_length_runs)) ||
        !sf_build_tree(&distances, sf_distance_runs,
                       sizeof(sf_distance_runs)) ||
        !sf_bits(in, 8U, &literal_mode) || !sf_bits(in, 8U, &dictionary_bits) ||
        literal_mode > 1U || dictionary_bits < 4U || dictionary_bits > 6U)
        return false;
    for (;;) {
        unsigned is_match;
        if ((++steps & SF_POLL_MASK) == 0UL && pd && xx_pd_is_stopped(pd))
            return false;
        if (!sf_bits(in, 1U, &is_match)) return false;
        if (is_match == 0U) {
            int literal;
            if (literal_mode == 0U) {
                unsigned value;
                if (!sf_bits(in, 8U, &value)) return false;
                literal = (int)value;
            } else {
                literal = sf_symbol(in, &literals);
            }
            if (literal < 0 || out->total >= out->limit ||
                !sf_make_room(out, 1U))
                return false;
            out->window[out->length++] = (uint8_t)literal;
            ++out->total;
            continue;
        }
        {
            int length_symbol = sf_symbol(in, &lengths);
            int distance_symbol;
            unsigned extra, length, distance_bits, distance_extra, copy;
            size_t distance;
            if (length_symbol < 0 || length_symbol >= 16 ||
                !sf_bits(in, sf_length_extra[length_symbol], &extra))
                return false;
            length = sf_length_base[length_symbol] + extra;
            if (length == SF_DCL_END) break;
            if (length > SF_MAX_MATCH) return false;
            distance_bits = length == 2U ? 2U : dictionary_bits;
            distance_symbol = sf_symbol(in, &distances);
            if (distance_symbol < 0 ||
                !sf_bits(in, distance_bits, &distance_extra))
                return false;
            distance = ((size_t)distance_symbol << distance_bits) +
                       distance_extra + 1U;
            if ((uint64_t)length > out->limit - out->total ||
                !sf_make_room(out, length) || distance > out->length)
                return false;
            /* A match may overlap its own output: byte by byte. */
            for (copy = 0U; copy < length; ++copy) {
                out->window[out->length] =
                    out->window[out->length - distance];
                ++out->length;
            }
            out->total += length;
        }
    }
    return sf_deliver(out);
}

/* ---- PE overlay and engine chain -------------------------------------- */

/* End of the last section's raw data, base-relative.  Only headers are
 * read: the DOS header, the NT signature and file header, the optional
 * header magic and the section table. */
static bool sf_find_overlay(Abstractformat *format, int64_t size,
                            int64_t *overlay) {
    uint8_t header[64];
    uint8_t nt[24];
    uint8_t magic[2];
    uint8_t table[SF_MAX_SECTIONS * SF_SECTION_SIZE];
    int64_t base = format->base_address;
    int64_t nt_offset, table_offset, end = 0;
    uint32_t sections, optional_size, index;
    if (size < SF_MIN_FILE || !sf_read_at(format->device, base, header, 64U) ||
        header[0] != 'M' || header[1] != 'Z')
        return false;
    nt_offset = (int64_t)sf_le32(header + 0x3C);
    if (nt_offset < 4 || nt_offset > size - 26 ||
        !sf_read_at(format->device, base + nt_offset, nt, sizeof(nt)) ||
        xx_rt_memcmp(nt, "PE\0\0", 4U) != 0)
        return false;
    sections = sf_le16(nt + 6U);
    optional_size = sf_le16(nt + 20U);
    if (sections == 0U || sections > SF_MAX_SECTIONS || optional_size < 2U ||
        optional_size > SF_MAX_OPTIONAL ||
        !sf_read_at(format->device, base + nt_offset + 24, magic, 2U))
        return false;
    if (sf_le16(magic) != 0x10BU && sf_le16(magic) != 0x20BU) return false;
    table_offset = nt_offset + 24 + (int64_t)optional_size;
    if (table_offset > size ||
        (int64_t)(sections * SF_SECTION_SIZE) > size - table_offset ||
        !sf_read_at(format->device, base + table_offset, table,
                    sections * SF_SECTION_SIZE))
        return false;
    for (index = 0U; index < sections; ++index) {
        const uint8_t *row = table + index * SF_SECTION_SIZE;
        int64_t raw_size = (int64_t)sf_le32(row + 16U);
        int64_t raw_offset = (int64_t)sf_le32(row + 20U);
        if (raw_size == 0) continue;
        if (raw_offset > size || raw_size > size - raw_offset) return false;
        if (raw_offset + raw_size > end) end = raw_offset + raw_size;
    }
    if (end < table_offset + (int64_t)(sections * SF_SECTION_SIZE) ||
        end > size - SF_OVERLAY_HEADER)
        return false;
    *overlay = end;
    return true;
}

/* A NUL-terminated, zero-padded printable name inside a fixed field. */
static bool sf_engine_name(const uint8_t *field, size_t field_size,
                           size_t *name_length) {
    size_t index, terminator = field_size;
    for (index = 0U; index < field_size; ++index) {
        if (field[index] == 0U) {
            terminator = index;
            break;
        }
        if (field[index] < 0x20U || field[index] > 0x7EU ||
            field[index] == '/' || field[index] == '\\' || field[index] == ':')
            return false;
    }
    if (terminator == 0U || terminator >= field_size) return false;
    for (index = terminator; index < field_size; ++index)
        if (field[index] != 0U) return false;
    *name_length = terminator;
    return true;
}

/* "sufNlng.*" names the language file of Setup Factory version N. */
static void sf_note_version(const uint8_t *name, size_t length,
                            char *version) {
    size_t index = 3U;
    unsigned value = 0U;
    if (version[0] || length < 7U || sf_upper(name[0]) != 'S' ||
        sf_upper(name[1]) != 'U' || sf_upper(name[2]) != 'F')
        return;
    while (index < length && index < 5U && name[index] >= '0' &&
           name[index] <= '9')
        value = value * 10U + (unsigned)(name[index++] - '0');
    if (index == 3U || value == 0U || index + 3U > length ||
        sf_upper(name[index]) != 'L' || sf_upper(name[index + 1U]) != 'N' ||
        sf_upper(name[index + 2U]) != 'G')
        return;
    (void)xx_rt_snprintf(version, 8U, "%u.0", value);
}

/* The overlay marker and the whole engine chain.  This is also the
 * detection probe, so it reads nothing but headers: the PE headers, 12
 * bytes of overlay header and one fixed record per engine file (at most
 * 99), and allocates nothing. */
static bool sf_locate_installer(Abstractformat *format, sf_locate *out) {
    uint8_t header[SF_OVERLAY_HEADER];
    uint8_t record[SF_LONG_NAME + 8U];
    sf_locate located;
    int64_t total, cursor;
    uint32_t index;
    size_t field = 0U;
    bool have_manifest = false;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    xx_mem_zero(&located, sizeof(located));
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    located.size = total - format->base_address;
    if (!sf_find_overlay(format, located.size, &located.overlay) ||
        !sf_read_at(format->device, format->base_address + located.overlay,
                    header, sizeof(header)) ||
        xx_rt_memcmp(header, sf_magic, sizeof(sf_magic)) != 0)
        return false;
    located.engine_count = sf_le32(header + 8U);
    if (located.engine_count == 0U || located.engine_count > SF_MAX_ENGINES)
        return false;
    cursor = located.overlay + SF_OVERLAY_HEADER;
    for (index = 0U; index < located.engine_count; ++index) {
        size_t name_length = 0U;
        uint32_t packed;
        int64_t data;
        if (index == 0U) {
            /* The long layout pads its 260-byte field with zeros, which in
             * the short layout would be a zero packed size: exactly one of
             * the two readings can succeed. */
            if (located.size - cursor >= (int64_t)(SF_LONG_NAME + 8U) &&
                sf_read_at(format->device, format->base_address + cursor,
                           record, SF_LONG_NAME + 8U) &&
                sf_engine_name(record, SF_LONG_NAME, &name_length) &&
                sf_le32(record + SF_LONG_NAME) != 0U) {
                field = SF_LONG_NAME;
                located.layout = XX_SETUP_FACTORY_LAYOUT_LONG;
            } else {
                field = SF_SHORT_NAME;
                located.layout = XX_SETUP_FACTORY_LAYOUT_SHORT;
            }
        }
        if (located.size - cursor < (int64_t)(field + 8U) ||
            !sf_read_at(format->device, format->base_address + cursor, record,
                        field + 8U) ||
            !sf_engine_name(record, field, &name_length))
            return false;
        packed = sf_le32(record + field);
        data = cursor + (int64_t)field + 8;
        if (packed == 0U || (int64_t)packed > located.size - data) return false;
        if (!have_manifest && sf_same_ascii(record, name_length, "irsetup.dat")) {
            if (packed < 3U || packed > SF_MANIFEST_MAX_PACKED) return false;
            located.manifest_offset = data;
            located.manifest_packed = packed;
            located.manifest_crc = sf_le32(record + field + 4U);
            have_manifest = true;
        }
        sf_note_version(record, name_length, located.version);
        cursor = data + (int64_t)packed;
    }
    if (!have_manifest) return false;
    {
        /* The manifest must at least open like a DCL stream. */
        uint8_t dcl[2];
        if (!sf_read_at(format->device,
                        format->base_address + located.manifest_offset, dcl,
                        2U) ||
            dcl[0] > 1U || dcl[1] < 4U || dcl[1] > 6U)
            return false;
    }
    located.payload = cursor;
    *out = located;
    return true;
}

/* ---- manifest ---------------------------------------------------------- */

typedef struct sf_cursor_s {
    const uint8_t *data;
    size_t size;
    size_t position;
} sf_cursor;

static bool sf_take(sf_cursor *cursor, size_t amount, const uint8_t **view) {
    if (amount > cursor->size - cursor->position) return false;
    if (view) *view = cursor->data + cursor->position;
    cursor->position += amount;
    return true;
}

static bool sf_take_u16(sf_cursor *cursor, uint32_t *value) {
    const uint8_t *view;
    if (!sf_take(cursor, 2U, &view)) return false;
    *value = sf_le16(view);
    return true;
}

/* An MFC CString: u8 length, 0xFF escapes to u16, 0xFFFF to u32. */
static bool sf_take_string(sf_cursor *cursor, const uint8_t **text,
                           size_t *length) {
    const uint8_t *view;
    uint32_t value;
    if (!sf_take(cursor, 1U, &view)) return false;
    value = view[0];
    if (value == 0xFFU) {
        if (!sf_take_u16(cursor, &value)) return false;
        if (value == 0xFFFFU) {
            if (!sf_take(cursor, 4U, &view)) return false;
            value = sf_le32(view);
        } else if (value == 0xFFFEU) {
            return false; /* a Unicode CString marker: not this schema */
        }
    }
    if (value > SF_MAX_STRING || !sf_take(cursor, value, &view)) return false;
    if (text) *text = view;
    if (length) *length = value;
    return true;
}

/* Object header: the first object introduces class CFileInfo
 * (0xFFFF, schema, name length, name); later ones refer back to it as class
 * index 1 (0x8001). */
static bool sf_take_object(sf_cursor *cursor, bool first, uint32_t *schema) {
    uint32_t tag, name_length;
    const uint8_t *name;
    if (!sf_take_u16(cursor, &tag)) return false;
    if (!first) return tag == 0x8001U;
    if (tag != 0xFFFFU || !sf_take_u16(cursor, schema) ||
        !sf_take_u16(cursor, &name_length) || name_length != 9U ||
        !sf_take(cursor, 9U, &name))
        return false;
    return xx_rt_memcmp(name, "CFileInfo", 9U) == 0;
}

typedef struct sf_entry_s {
    const uint8_t *name;
    size_t name_length;
    const uint8_t *destination;
    size_t destination_length;
    uint32_t raw_size;
    uint32_t mtime;
    uint32_t packed_size;
    uint32_t crc;
    uint8_t method;
} sf_entry;

static bool sf_take_entry(sf_cursor *cursor, uint32_t layout, bool first,
                          uint32_t *schema, sf_entry *entry) {
    const uint8_t *block;
    uint32_t extra, index;
    if (!sf_take_object(cursor, first, schema)) return false;
    if (layout == XX_SETUP_FACTORY_LAYOUT_LONG && !sf_take(cursor, 4U, NULL))
        return false;
    /* source path, file name, source folder, extension */
    if (!sf_take_string(cursor, NULL, NULL) ||
        !sf_take_string(cursor, &entry->name, &entry->name_length) ||
        !sf_take_string(cursor, NULL, NULL) ||
        !sf_take_string(cursor, NULL, NULL) || !sf_take(cursor, 43U, &block))
        return false;
    entry->raw_size = sf_le32(block + 1U);
    entry->mtime = sf_le32(block + 10U);
    if (!sf_take_string(cursor, &entry->destination,
                        &entry->destination_length) ||
        !sf_take(cursor, 5U, NULL) || !sf_take_string(cursor, NULL, NULL))
        return false;
    if (layout == XX_SETUP_FACTORY_LAYOUT_LONG) {
        if (!sf_take(cursor, 3U, NULL) || !sf_take_string(cursor, NULL, NULL) ||
            !sf_take(cursor, 5U, NULL) || !sf_take_string(cursor, NULL, NULL) ||
            !sf_take(cursor, 13U, NULL) || !sf_take_string(cursor, NULL, NULL) ||
            !sf_take_string(cursor, NULL, NULL) || !sf_take_u16(cursor, &extra))
            return false;
        /* Each string is at least one byte, so this ends with the data. */
        for (index = 0U; index < extra; ++index)
            if (!sf_take_string(cursor, NULL, NULL)) return false;
        if (!sf_take(cursor, 45U, &block)) return false;
        entry->packed_size = sf_le32(block);
        entry->crc = sf_le32(block + 4U);
        entry->method = block[8];
    } else {
        if (!sf_take(cursor, 9U, NULL) || !sf_take_string(cursor, NULL, NULL) ||
            !sf_take(cursor, 68U, &block))
            return false;
        entry->packed_size = sf_le32(block + 23U);
        entry->crc = sf_le32(block + 27U);
        entry->method = block[31];
    }
    return true;
}

/* ---- member names ------------------------------------------------------ */

typedef struct sf_name_s {
    char text[SF_MAX_NAME + 8U];
    size_t length;
    size_t components;
    bool unsafe;
} sf_name;

/* Windows-1252 0x80..0x9F (0 = unassigned, written as '_'). */
static const uint16_t sf_cp1252[32] = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
    0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};

static void sf_put(sf_name *name, uint32_t code_point) {
    uint8_t bytes[3];
    size_t count, index;
    if (code_point < 0x80U) {
        bytes[0] = (uint8_t)code_point;
        count = 1U;
    } else if (code_point < 0x800U) {
        bytes[0] = (uint8_t)(0xC0U | (code_point >> 6U));
        bytes[1] = (uint8_t)(0x80U | (code_point & 0x3FU));
        count = 2U;
    } else {
        bytes[0] = (uint8_t)(0xE0U | (code_point >> 12U));
        bytes[1] = (uint8_t)(0x80U | ((code_point >> 6U) & 0x3FU));
        bytes[2] = (uint8_t)(0x80U | (code_point & 0x3FU));
        count = 3U;
    }
    if (count > SF_MAX_NAME - name->length) {
        name->unsafe = true;
        return;
    }
    for (index = 0U; index < count; ++index)
        name->text[name->length++] = (char)bytes[index];
    name->text[name->length] = 0;
}

static bool sf_is_device(const uint8_t *text, size_t length) {
    static const char *const devices[] = {"CON",    "PRN",     "AUX",
                                          "NUL",    "CONIN$",  "CONOUT$",
                                          "CLOCK$"};
    size_t stem = 0U, index;
    while (stem < length && text[stem] != '.') ++stem;
    while (stem > 0U && text[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(devices) / sizeof(devices[0]); ++index)
        if (sf_same_ascii(text, stem, devices[index])) return true;
    if (stem == 4U && text[3] >= '1' && text[3] <= '9' &&
        (sf_same_ascii(text, 3U, "COM") || sf_same_ascii(text, 3U, "LPT")))
        return true;
    /* COM and LPT also take the superscript digits 1..3 (cp1252 B9 B2 B3). */
    if (stem == 4U && (text[3] == 0xB9U || text[3] == 0xB2U || text[3] == 0xB3U) &&
        (sf_same_ascii(text, 3U, "COM") || sf_same_ascii(text, 3U, "LPT")))
        return true;
    return false;
}

/* One path component, converted to UTF-8.  "." and empty components are
 * dropped; anything Windows would treat specially marks the name unsafe
 * (it is still listed, but never written). */
static void sf_add_component(sf_name *name, const uint8_t *text,
                             size_t length) {
    size_t index;
    if (length == 0U || (length == 1U && text[0] == '.')) return;
    if (length == 2U && text[0] == '.' && text[1] == '.') name->unsafe = true;
    if (text[length - 1U] == '.' || text[length - 1U] == ' ' ||
        sf_is_device(text, length))
        name->unsafe = true;
    if (name->length != 0U) sf_put(name, '/');
    for (index = 0U; index < length; ++index) {
        uint8_t c = text[index];
        if (c < 0x20U || c == 0x7FU || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') {
            name->unsafe = true;
            sf_put(name, '_');
        } else if (c < 0x80U) {
            sf_put(name, c);
        } else if (c < 0xA0U) {
            uint16_t mapped = sf_cp1252[c - 0x80U];
            sf_put(name, mapped ? mapped : (uint32_t)'_');
        } else {
            sf_put(name, c);
        }
    }
    ++name->components;
}

static void sf_add_path(sf_name *name, const uint8_t *text, size_t length) {
    size_t start = 0U, index;
    for (index = 0U; index <= length; ++index) {
        if (index == length || text[index] == '\\' || text[index] == '/') {
            sf_add_component(name, text + start, index - start);
            start = index + 1U;
        }
    }
}

/* "<destination>/<file name>": a leading %AppDir% is dropped, any other
 * leading %Variable% becomes a folder of that name, and a drive letter is
 * dropped. */
static void sf_compose_name(const sf_entry *entry, sf_name *name) {
    const uint8_t *rest = entry->destination;
    size_t rest_length = entry->destination_length;
    size_t before;
    name->length = 0U;
    name->components = 0U;
    name->unsafe = false;
    name->text[0] = 0;
    if (rest_length >= 2U && rest[0] == '%') {
        size_t close = 1U;
        while (close < rest_length && close <= SF_MAX_VARIABLE &&
               rest[close] != '%')
            ++close;
        if (close < rest_length && rest[close] == '%' && close >= 2U) {
            if (!sf_same_ascii(rest + 1U, close - 1U, "AppDir"))
                sf_add_component(name, rest + 1U, close - 1U);
            rest += close + 1U;
            rest_length -= close + 1U;
        }
    } else if (rest_length >= 2U && rest[1] == ':' &&
               ((rest[0] >= 'A' && rest[0] <= 'Z') ||
                (rest[0] >= 'a' && rest[0] <= 'z'))) {
        rest += 2U;
        rest_length -= 2U;
    }
    sf_add_path(name, rest, rest_length);
    before = name->components;
    sf_add_path(name, entry->name, entry->name_length);
    if (name->components == before) name->unsafe = true;
}

/* Case-insensitive comparison of two UTF-8 names as Windows would match
 * them for ASCII and Latin-1 letters. */
static uint32_t sf_fold(const uint8_t *text, size_t index) {
    uint8_t c = text[index];
    if (c >= 'a' && c <= 'z') return (uint32_t)(c - 'a' + 'A');
    if (index > 0U && text[index - 1U] == 0xC3U && c >= 0xA0U && c <= 0xBEU &&
        c != 0xB7U)
        return (uint32_t)(c - 0x20U);
    return c;
}

typedef struct sf_key_s {
    const char *name;
    uint32_t index;
} sf_key;

static int sf_compare_names(const char *left, const char *right) {
    const uint8_t *a = (const uint8_t *)left;
    const uint8_t *b = (const uint8_t *)right;
    size_t index = 0U;
    for (;;) {
        uint32_t x = a[index] ? sf_fold(a, index) : 0U;
        uint32_t y = b[index] ? sf_fold(b, index) : 0U;
        if (x != y) return x < y ? -1 : 1;
        if (x == 0U) return 0;
        ++index;
    }
}

static int sf_compare_keys(const void *left, const void *right) {
    const sf_key *a = (const sf_key *)left;
    const sf_key *b = (const sf_key *)right;
    int result = sf_compare_names(a->name, b->name);
    if (result != 0) return result;
    return a->index < b->index ? -1 : (a->index > b->index ? 1 : 0);
}

/* "<stem>_<index><extension>" for the later copies of a repeated name. */
static char *sf_suffixed(const char *name, uint32_t index) {
    char suffix[16];
    size_t length = xx_str_len(name), dot = length, cut, suffix_length;
    char *result;
    size_t at;
    for (at = length; at > 0U; --at) {
        if (name[at - 1U] == '/') break;
        if (name[at - 1U] == '.' && at - 1U > 0U && name[at - 2U] != '/') {
            dot = at - 1U;
            break;
        }
    }
    if (xx_rt_snprintf(suffix, sizeof(suffix), "_%u", (unsigned)index) <= 0)
        return NULL;
    suffix_length = xx_str_len(suffix);
    cut = dot;
    result = (char *)xx_mem_alloc(length + suffix_length + 1U);
    if (!result) return NULL;
    xx_rt_memcpy(result, name, cut);
    xx_rt_memcpy(result + cut, suffix, suffix_length);
    xx_rt_memcpy(result + cut + suffix_length, name + cut, length - cut);
    result[length + suffix_length] = 0;
    return result;
}

/* No two members may be written to the same file.  The first of a group of
 * equal names keeps it; the others get "_<record index>".  A renamed name
 * can in turn meet another one, so this repeats; whatever still collides
 * after a few rounds is refused for extraction. */
static bool sf_resolve_duplicates(sf_member *members, size_t count) {
    sf_key *keys;
    uint32_t round;
    size_t index;
    if (count < 2U) return true;
    keys = (sf_key *)xx_mem_alloc(count * sizeof(*keys));
    if (!keys) return false;
    for (round = 0U; round <= SF_RENAME_ROUNDS; ++round) {
        bool renamed = false;
        for (index = 0U; index < count; ++index) {
            keys[index].name = members[index].name;
            keys[index].index = (uint32_t)index;
            members[index].pending = false;
        }
        xx_rt_qsort(keys, count, sizeof(*keys), sf_compare_keys);
        /* Mark first, rename afterwards: the keys point at the names. */
        for (index = 1U; index < count; ++index)
            if (sf_compare_names(keys[index].name, keys[index - 1U].name) == 0)
                members[keys[index].index].pending = true;
        for (index = 0U; index < count; ++index) {
            sf_member *member = &members[index];
            char *replacement;
            if (!member->pending) continue;
            if (round == SF_RENAME_ROUNDS) {
                member->unsafe = true;
                continue;
            }
            replacement = sf_suffixed(member->name, (uint32_t)index);
            if (!replacement) {
                xx_mem_free(keys);
                return false;
            }
            xx_mem_free(member->name);
            member->name = replacement;
            member->duplicate = true;
            renamed = true;
        }
        if (!renamed) break;
    }
    xx_mem_free(keys);
    return true;
}

/* ---- whole-installer parse --------------------------------------------- */

static void sf_archive_clear(sf_archive *archive) {
    size_t index;
    if (!archive) return;
    if (archive->members) {
        for (index = 0U; index < archive->count; ++index)
            if (archive->members[index].name)
                xx_mem_free(archive->members[index].name);
        xx_mem_free(archive->members);
    }
    xx_mem_zero(archive, sizeof(*archive));
}

static bool sf_decode_manifest(Abstractformat *format, const sf_locate *where,
                               uint8_t **data, size_t *size,
                               xx_pd_struct *pd) {
    sf_input *in;
    sf_output out;
    bool ok;
    *data = NULL;
    *size = 0U;
    in = (sf_input *)xx_mem_calloc(1U, sizeof(*in));
    xx_mem_zero(&out, sizeof(out));
    out.window = (uint8_t *)xx_mem_alloc(SF_OUT_WINDOW);
    if (!in || !out.window) {
        if (in) xx_mem_free(in);
        if (out.window) xx_mem_free(out.window);
        return false;
    }
    in->device = format->device;
    in->offset = format->base_address + where->manifest_offset;
    in->remaining = where->manifest_packed;
    out.limit = SF_MANIFEST_MAX_RAW;
    out.to_memory = true;
    ok = sf_dcl_decode(in, &out, pd) && out.crc == where->manifest_crc &&
         out.memory_size == out.total && out.total >= 2U;
    xx_mem_free(in);
    xx_mem_free(out.window);
    if (!ok) {
        if (out.memory) xx_mem_free(out.memory);
        return false;
    }
    *data = out.memory;
    *size = out.memory_size;
    return true;
}

static bool sf_parse(Abstractformat *format, sf_archive *archive,
                     xx_pd_struct *pd) {
    uint8_t *manifest = NULL;
    size_t manifest_size = 0U, start, found = 0U, index;
    sf_cursor cursor;
    sf_name *name = NULL;
    uint32_t count = 0U;
    int64_t offset;
    bool ok = false, have = false;
    xx_mem_zero(archive, sizeof(*archive));
    if (!sf_locate_installer(format, &archive->locate) ||
        !sf_decode_manifest(format, &archive->locate, &manifest,
                            &manifest_size, pd))
        return false;
    /* The object list starts 6 bytes before the class name: u16 count,
     * u16 0xFFFF new-class tag, u16 schema. */
    for (start = 6U; start + 11U <= manifest_size; ++start) {
        if (manifest[start] == 9U && manifest[start + 1U] == 0U &&
            xx_rt_memcmp(manifest + start + 2U, "CFileInfo", 9U) == 0) {
            found = start - 6U;
            have = true;
            break;
        }
    }
    /* U3 looks for the list this way in Setup Factory 6 manifests but reads
     * the older (short layout) ones from their first byte; so does this. */
    if (!have || (archive->locate.layout == XX_SETUP_FACTORY_LAYOUT_SHORT &&
                  found != 0U))
        goto done;
    cursor.data = manifest;
    cursor.size = manifest_size;
    cursor.position = found;
    if (!sf_take_u16(&cursor, &count) || count == 0U ||
        (uint64_t)count * (archive->locate.layout ==
                                   XX_SETUP_FACTORY_LAYOUT_LONG
                               ? SF_MIN_RECORD_LONG
                               : SF_MIN_RECORD_SHORT) >
            (uint64_t)(manifest_size - cursor.position))
        goto done;
    archive->members = (sf_member *)xx_mem_calloc(count, sizeof(sf_member));
    name = (sf_name *)xx_mem_alloc(sizeof(*name));
    if (!archive->members || !name) goto done;
    offset = archive->locate.payload;
    for (index = 0U; index < count; ++index) {
        sf_entry entry;
        sf_member *member = &archive->members[index];
        if ((index & 0xFFU) == 0U && pd && xx_pd_is_stopped(pd)) goto done;
        xx_mem_zero(&entry, sizeof(entry));
        if (!sf_take_entry(&cursor, archive->locate.layout, index == 0U,
                           &archive->schema, &entry))
            goto done;
        sf_compose_name(&entry, name);
        if (name->length == 0U) {
            (void)xx_rt_snprintf(name->text, sizeof(name->text),
                                 "unnamed_%u", (unsigned)index);
            name->unsafe = true;
        }
        member->name = xx_str_dup(name->text);
        if (!member->name) goto done;
        archive->count = index + 1U;
        member->unsafe = name->unsafe;
        member->data_offset = format->base_address + offset;
        member->packed_size = entry.packed_size;
        member->raw_size = entry.raw_size;
        member->crc = entry.crc;
        member->mtime = entry.mtime;
        if (entry.method == 0U || entry.method == 'r') {
            member->method = SF_METHOD_DCL;
        } else if (entry.method == 1U || entry.method == 'n') {
            member->method = entry.packed_size == entry.raw_size
                                 ? SF_METHOD_STORED : SF_METHOD_UNKNOWN;
        } else {
            member->method = SF_METHOD_UNKNOWN;
        }
        /* The streams follow each other with no framing; a member whose
         * bytes are not all present is listed but cannot be unpacked. */
        if ((int64_t)entry.packed_size > archive->locate.size - offset ||
            offset > archive->locate.size) {
            member->missing = true;
            archive->truncated = true;
        }
        offset += (int64_t)entry.packed_size;
    }
    archive->payload_end =
        offset < archive->locate.size ? offset : archive->locate.size;
    if (!sf_resolve_duplicates(archive->members, archive->count)) goto done;
    ok = true;
done:
    if (manifest) xx_mem_free(manifest);
    if (name) xx_mem_free(name);
    if (!ok) sf_archive_clear(archive);
    return ok;
}

/* ---- member extraction ------------------------------------------------- */

static bool sf_extract(Abstractformat *format, const sf_member *member,
                       xx_io_device *destination, xx_pd_struct *pd) {
    sf_output out;
    bool ok = false;
    if (!format || !member || member->missing ||
        member->method == SF_METHOD_UNKNOWN)
        return false;
    if (member->packed_size == 0U && member->raw_size == 0U)
        return member->crc == 0U;
    xx_mem_zero(&out, sizeof(out));
    out.window = (uint8_t *)xx_mem_alloc(SF_OUT_WINDOW);
    if (!out.window) return false;
    out.limit = member->raw_size;
    out.device = destination;
    if (member->method == SF_METHOD_STORED) {
        uint64_t left = member->raw_size;
        int64_t at = member->data_offset;
        ok = true;
        while (left > 0U) {
            size_t chunk = left < (uint64_t)SF_OUT_WINDOW ? (size_t)left
                                                         : (size_t)SF_OUT_WINDOW;
            if ((pd && xx_pd_is_stopped(pd)) ||
                !sf_read_at(format->device, at, out.window, chunk)) {
                ok = false;
                break;
            }
            out.length = chunk;
            out.delivered = 0U;
            if (!sf_deliver(&out)) {
                ok = false;
                break;
            }
            out.total += chunk;
            at += (int64_t)chunk;
            left -= chunk;
        }
    } else {
        sf_input *in = (sf_input *)xx_mem_calloc(1U, sizeof(*in));
        if (in) {
            in->device = format->device;
            in->offset = member->data_offset;
            in->remaining = member->packed_size;
            ok = sf_dcl_decode(in, &out, pd);
            xx_mem_free(in);
        }
    }
    ok = ok && out.total == (uint64_t)member->raw_size && out.crc == member->crc;
    xx_mem_free(out.window);
    return ok;
}

/* ---- records and options ----------------------------------------------- */

static void sf_stream_free(void *opaque) {
    sf_stream *stream = (sf_stream *)opaque;
    if (!stream) return;
    sf_archive_clear(&stream->archive);
    xx_mem_free(stream);
}

static bool sf_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static bool sf_set_record(xx_archive_record *record, const sf_member *member) {
    bool ok;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = -1;
    record->header_size = 0;
    record->data_offset = member->data_offset;
    record->compressed_size = (int64_t)member->packed_size;
    ok = xx_archive_record_set_original_name(record, member->name) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        member->packed_size) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        member->raw_size) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32, member->crc) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        member->method) &&
         xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) &&
         xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
    if (ok && member->mtime != 0U)
        ok = xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                            member->mtime);
    return ok;
}

/* ---- public API -------------------------------------------------------- */

void xx_setup_factory_init(xx_setup_factory *archive, xx_io_device *device,
                           int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_SETUP_FACTORY_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format,
                            "application/x-setup-factory-installer");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_setup_factory_check_is_valid;
    archive->format.handle_base_info = xx_setup_factory_handle_base_info;
    archive->format.get_format_size = xx_setup_factory_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_setup_factory_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_setup_factory_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_setup_factory_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_setup_factory_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_setup_factory_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_setup_factory_free_archive_records_reading;
    archive->overlay_offset = -1;
    archive->payload_offset = -1;
    archive->payload_end = -1;
}

xx_setup_factory *xx_setup_factory_create(xx_io_device *device,
                                          int64_t base_address) {
    xx_setup_factory *archive =
        (xx_setup_factory *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_setup_factory_init(archive, device, base_address);
    return archive;
}

void xx_setup_factory_destroy(xx_setup_factory *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_setup_factory_free(xx_setup_factory *archive) {
    if (!archive) return;
    xx_setup_factory_destroy(archive);
    xx_mem_free(archive);
}

bool xx_setup_factory_check_is_valid(Abstractformat *format,
                                     xx_pd_struct *pd) {
    sf_locate located;
    (void)pd;
    return sf_locate_installer(format, &located);
}

bool xx_setup_factory_handle_base_info(Abstractformat *format,
                                       xx_pd_struct *pd) {
    sf_archive parsed;
    xx_setup_factory *archive;
    if (!format || !sf_parse(format, &parsed, pd)) return false;
    archive = (xx_setup_factory *)format;
    archive->number_of_records = parsed.count;
    archive->overlay_offset = parsed.locate.overlay;
    archive->payload_offset = parsed.locate.payload;
    archive->payload_end = parsed.payload_end;
    archive->engine_count = parsed.locate.engine_count;
    archive->layout = parsed.locate.layout;
    archive->schema = parsed.schema;
    archive->truncated = parsed.truncated;
    if (parsed.locate.version[0])
        xx_format_set_version(format, parsed.locate.version);
    format->number_of_archive_records = parsed.count;
    format->format_size = parsed.payload_end;
    format->overlay_offset =
        parsed.payload_end < parsed.locate.size
            ? format->base_address + parsed.payload_end : -1;
    format->overlay_size = parsed.locate.size - parsed.payload_end;
    format->is_valid = true;
    format->base_info_handled = true;
    sf_archive_clear(&parsed);
    return true;
}

int64_t xx_setup_factory_get_format_size(Abstractformat *format,
                                         xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_setup_factory_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_setup_factory_get_number_of_archive_records(Abstractformat *format,
                                                        xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_setup_factory_handle_base_info(format, pd))
               ? ((xx_setup_factory *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_setup_factory_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    sf_stream *stream;
    xx_archive_record_state *state;
    if (!format) return NULL;
    stream = (sf_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    if (!sf_parse(format, &stream->archive, pd) || stream->archive.count == 0U) {
        sf_stream_free(stream);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        sf_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = sf_stream_free;
    state->total_records = (int64_t)stream->archive.count;
    if (!sf_copy_options(&state->options, options) ||
        !sf_set_record(&state->current_record, &stream->archive.members[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->current_index = 0;
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_setup_factory_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_setup_factory_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    sf_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (sf_stream *)state->internal_state) || !state->has_record) {
        if (state) state->has_record = false;
        return false;
    }
    if (stream->index + 1U >= stream->archive.count) {
        stream->index = stream->archive.count;
        state->has_record = false;
        return false;
    }
    ++stream->index;
    state->current_index = (int64_t)stream->index;
    if (!sf_set_record(&state->current_record,
                       &stream->archive.members[stream->index])) {
        state->has_record = false;
        return false;
    }
    return true;
}

bool xx_setup_factory_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    sf_stream *stream;
    const sf_member *member;
    const xx_var *option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    xx_io_device *destination;
    bool overwrite, result = false;
    size_t base_length;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (sf_stream *)state->internal_state) ||
        stream->index >= stream->archive.count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->archive.members[stream->index];
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_MAX_MEMBER_SIZE);
    if (option && (uint64_t)member->raw_size > xx_var_get_u64(option))
        return false;
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_UNPACK_PATH);
    if (!option) return sf_extract(format, member, NULL, pd);
    if (member->unsafe || member->missing ||
        member->method == SF_METHOD_UNKNOWN)
        return false;
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(option);
    } else if (option->type == XX_VAR_TYPE_WSTRING ||
               option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned_base;
    }
    if (!base) goto done;
    base_length = xx_str_len(base);
    path = (base_length != 0U && base[base_length - 1U] != '/' &&
            base[base_length - 1U] != '\\')
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    option = xx_format_resolve_extra_parameter(format, &state->options,
                                               XX_META_ID_OPT_OVERWRITE);
    overwrite = option && xx_var_get_bool(option);
    /* Without the overwrite option an existing file is never replaced. */
    destination = xx_io_file_open(path, overwrite ? "wb" : "wbx");
    if (!destination) goto done;
    result = sf_extract(format, member, destination, pd);
    if (xx_io_close(destination) != 0) result = false;
    if (!result) xx_rt_remove(path);
done:
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_setup_factory_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
