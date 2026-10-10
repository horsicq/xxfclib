/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * LHA/LZH archives (LHarc, LHA, LArc).
 *
 * PMarc (-pm0-/-pm1-/-pm2-) reuses this container's level-0 header verbatim
 * and is therefore NOT claimed here: its payload codecs are unrelated to
 * LHA's and it has its own reader (src/formats/pma). Accepting the tag here
 * would win the dispatch race and mislabel every .pma as LHA.
 *
 * There is no archive-wide header. Members are laid end to end from the start
 * of the format; each is a header followed by its payload, and the chain ends
 * at EOF or at a single 0x00 byte where the next header size would be.
 *
 * Every header, at every level, shares these fields:
 *
 *   0x02  char[5]  method tag: '-' + two letters + one alnum + '-', e.g.
 *                  "-lh5-", "-lh0-", "-lhd-" (directory), "-lzs-"
 *   0x07  u32 LE   compressed size
 *   0x0b  u32 LE   uncompressed size
 *   0x0f  u32 LE   timestamp: MS-DOS time|date at levels 0/1, Unix at 2/3
 *   0x13  u8       MS-DOS attribute byte
 *   0x14  u8       header level, 0..3
 *
 * What surrounds them differs per level, and so does what 0x07 counts:
 *
 * Level 0 -- base header is byte[0x00] + 2 bytes long (the size byte counts
 *   neither itself nor the checksum byte), minimum 24.
 *     0x00  u8   base header size
 *     0x01  u8   additive checksum of bytes [0x02, size + 2)
 *     0x15  u8   name length n
 *     0x16  char[n]  file name
 *     0x16+n u16 LE  CRC16 of the uncompressed payload
 *   0x07 is the payload length. There is no extended-header chain.
 *
 * Level 1 -- as level 0 but minimum 27, and it carries a chain:
 *     0x18+n u8   originating OS byte
 *     0x19+n u16 LE  size of the first extended header; this is the FINAL
 *                    word of the base header, so the chain starts there.
 *   0x07 counts the extended headers AS WELL AS the payload, so the payload
 *   length is 0x07 minus the bytes the chain consumed. Walking the chain is
 *   the only way to find where the payload starts.
 *
 * Level 2 -- the size byte and the checksum byte become one 16-bit field:
 *     0x00  u16 LE  TOTAL header size, extended headers included, min 26
 *     0x15  u16 LE  CRC16 of the uncompressed payload
 *     0x17  u8      originating OS byte; 'K' (OS-9/68K) writers understate
 *                   the size field by two, so two bytes are added back
 *     0x18  u16 LE  size of the first extended header
 *   0x07 is the payload length alone. There is no name field: the name comes
 *   from extended header type 1.
 *
 * Level 3 -- like level 2 but with 32-bit chain words:
 *     0x00  u16 LE  word size, must be 4; this is the level-3 magic
 *     0x15  u16 LE  CRC16 of the uncompressed payload
 *     0x17  u8      originating OS byte
 *     0x18  u32 LE  TOTAL header size, min 32
 *     0x1c  u32 LE  size of the first extended header
 *
 * An extended header of declared size N occupies N bytes after its own size
 * word:  u8 type | data (N - 3, or N - 5 at level 3) | size word of the next
 * header (2 bytes, 4 at level 3).  A zero size word ends the chain.
 *   type 0x00  common: CRC16 of the whole header, computed with those two
 *              bytes taken as zero
 *   type 0x01  file name, replacing the base header's
 *   type 0x02  directory, 0xFF separators, rewritten to '/'
 *   type 0x42  the two sizes restated as 64-bit values
 *   type 0x50  Unix permission word; S_IFLNK there means the member is a
 *              symbolic link rather than a directory
 *
 * Methods this reader decodes: -lh0-/-lz4- (stored), -lh1- (LArc
 * adaptive-Huffman LZSS), -lh2- (up to 8 KiB), -lh3- (block Huffman),
 * -lh4-/-lh5-/-lh6-/-lh7-
 * (block Huffman, 4/8/32/64 KiB windows), -lhx- (1 MiB block Huffman),
 * LHARK's retagged -lk7-, and LArc -lzs-/-lz5- LZSS.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/formats/lha/xx_lha.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include "xxfclib/algo/lzh/xx_lzh.h"
#include "xx_lha_legacy_native.h"
#include "xx_lha_lh2_native.h"
#include "xx_lha_lh3_native.h"

#include <stdio.h>
#include "xxfclib/data/xx_data.h"

#define XX_LHA_COPY_CHUNK (64 * 1024)

typedef struct xx_lha_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint32_t method;
    uint16_t payload_crc;
    uint64_t timestamp;
    bool is_folder;
} xx_lha_member;

typedef struct xx_lha_stream_s {
    xx_lha_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
} xx_lha_stream;

static void xx_lha_vtable_destroy(Abstractformat *self);

/* ------------------------------------------------------------- helpers -- */

static bool xx_lha_read_at(Abstractformat *self, int64_t offset, uint8_t *buffer, size_t size)
{
    size_t completed = 0U;

    if (!self || !self->device || offset < 0 || xx_io_seek64(self->device, offset, SEEK_SET) != 0) {
        return false;
    }
    while (completed < size) {
        ssize_t received = xx_io_read(self->device, buffer + completed, size - completed);
        if (received <= 0 || (size_t)received > size - completed) {
            return false;
        }
        completed += (size_t)received;
    }
    return true;
}

static bool xx_lha_range_within(int64_t total, int64_t offset, int64_t size)
{
    return offset >= 0 && size >= 0 && offset <= total && size <= total - offset;
}

/* Refuse anything that would escape the extraction directory. */
static bool xx_lha_path_safe(const char *name)
{
    const char *cursor = name;

    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' || xx_rt_strchr(name, ':') || xx_rt_strchr(name, '\\')) return false;
    while (*cursor) {
        const char *end = cursor;
        size_t length;
        while (*end && *end != '/') ++end;
        length = (size_t)(end - cursor);
        if (length == 2U && cursor[0] == '.' && cursor[1] == '.') return false;
        cursor = *end ? end + 1 : end;
    }
    return true;
}

static void xx_lha_stream_free(void *pointer)
{
    xx_lha_stream *stream = (xx_lha_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index) {
        xx_str_free(stream->items[index].name);
    }
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* Append a member, taking ownership of @p name. */
static bool xx_lha_add(xx_lha_stream *stream, const xx_lha_member *member)
{
    xx_lha_member *grown = (xx_lha_member *)xx_mem_realloc(stream->items, sizeof(*grown) * (stream->count + 1U));

    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

#define XX_LHA_MAX_MEMBERS 100000 /* no count is stored: a runaway guard, not a format limit */
#define XX_LHA_MAX_HEADER 65538   /* the largest header a u16 size field can describe, plus the OS-9/68K +2 */
#define XX_LHA_MAX_NAME 1024      /* directory + name, assembled */
#define XX_LHA_PREFIX 32          /* enough to reach the level-3 total-size field at 0x18 */
#define XX_LHA_MIN_PREFIX 22      /* through the level-0/1 name-length byte */
#define XX_LHA_MAX_DECODED (256 * 1024 * 1024)
#define XX_LHA_TAG3(a, b, c) (((uint32_t)(a) << 16) | ((uint32_t)(b) << 8) | (uint32_t)(c))
#define XX_LHA_M_LH0 XX_LHA_TAG3('l', 'h', '0') /* stored */
#define XX_LHA_M_LH1 XX_LHA_TAG3('l', 'h', '1') /* LZHUF: adaptive Huffman over a 4 KiB window */
#define XX_LHA_M_LH2 XX_LHA_TAG3('l', 'h', '2') /* dynamic Huffman over an 8 KiB window */
#define XX_LHA_M_LH3 XX_LHA_TAG3('l', 'h', '3') /* static block Huffman over an 8 KiB window */
#define XX_LHA_M_LH4 XX_LHA_TAG3('l', 'h', '4')
#define XX_LHA_M_LH5 XX_LHA_TAG3('l', 'h', '5')
#define XX_LHA_M_LH6 XX_LHA_TAG3('l', 'h', '6')
#define XX_LHA_M_LH7 XX_LHA_TAG3('l', 'h', '7')
#define XX_LHA_M_LHD XX_LHA_TAG3('l', 'h', 'd') /* directory (or, with S_IFLNK, a symlink) */
#define XX_LHA_M_LK7 XX_LHA_TAG3('l', 'k', '7') /* LHARK's -lh7-, a different bitstream; see below */
#define XX_LHA_M_LZ4 XX_LHA_TAG3('l', 'z', '4') /* LArc "stored" */
#define XX_LHA_M_LZ5 XX_LHA_TAG3('l', 'z', '5') /* LArc 4 KiB LZSS */
#define XX_LHA_M_LZS XX_LHA_TAG3('l', 'z', 's') /* LArc 2 KiB LZSS */
#define XX_LHA_M_LHX XX_LHA_TAG3('l', 'h', 'x') /* UNLHA32 1 MiB LZH */

/* Forward declarations: the parse and the decode
 * call into each other's helpers. */
static uint16_t xx_lha_crc16(const uint8_t *data, size_t size, size_t skip_offset);
static bool xx_lha_checksum_ok(const uint8_t *header, int32_t base_size);
static bool xx_lha_tag_ok(const uint8_t *prefix);
static bool xx_lha_name_byte_ok(uint8_t byte);
static xx_lha_stream *xx_lha_parse(Abstractformat *self, xx_pd_struct *pd);
static bool xx_lha_decode(Abstractformat *self, const xx_lha_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd);

/* The method tag is carried raw: the three characters between the tag's two
 * dashes, packed big-endian, so "-lh5-" is 0x6C6835 and a member listing
 * still shows what the archive itself said. The mapping to a decoder lives
 * only in xx_lha_decode. */

/* CRC-16/ARC, the polynomial the type-0 common extended header uses. */
static uint16_t xx_lha_crc16(const uint8_t *data, size_t size, size_t skip_offset)
{
    static const uint8_t zero[2] = {0U, 0U};
    uint16_t crc;
    size_t skipped;
    if (skip_offset >= size) return xx_crc16_arc_calc(0U, data, size);
    crc = xx_crc16_arc_calc(0U, data, skip_offset);
    skipped = size - skip_offset < 2U ? size - skip_offset : 2U;
    crc = xx_crc16_arc_calc(crc, zero, skipped);
    return xx_crc16_arc_calc(crc, data + skip_offset + skipped, size - skip_offset - skipped);
}

/* The additive header checksum of levels 0 and 1: the low byte of the sum of
 * every base-header byte from index 2 on, stored in byte 1. With only four
 * fixed tag characters to key on, this one byte is what stops unrelated data
 * from being walked as an LHA member chain, so it must never become
 * advisory. */
static bool xx_lha_checksum_ok(const uint8_t *header, int32_t base_size)
{
    uint32_t sum = 0U;
    int32_t index;

    if (base_size < 3) return false;
    for (index = 2; index < base_size; ++index) {
        sum += (uint32_t)header[index];
    }
    return (sum & 0xFFU) == (uint32_t)header[1];
}

/* "-" + two family letters + one method character + "-". The family set is
 * closed (lh = LHarc/LHA, lz = LArc) and the method character is restricted
 * to lowercase alphanumerics, which covers every tag an LHA-family writer
 * emits (-lh0-..-lh7-, -lhd-, -lhx-, -lz4-, -lz5-, -lzs-) while keeping the
 * four fixed positions a real discriminator.
 *
 * pm (PMarc, -pm0-..-pm2-) is deliberately absent. PMarc borrows the level-0
 * header byte for byte, so it would pass every structural test here and be
 * claimed as LHA, but none of its payload codecs is an LHA codec. It belongs
 * to src/formats/pma and is left for that reader to claim. */
static bool xx_lha_tag_ok(const uint8_t *prefix)
{
    if (prefix[2] != (uint8_t)'-' || prefix[6] != (uint8_t)'-') return false;
    if (!((prefix[3] == (uint8_t)'l' && prefix[4] == (uint8_t)'h') || (prefix[3] == (uint8_t)'l' && prefix[4] == (uint8_t)'z'))) {
        return false;
    }
    return ((prefix[5] >= (uint8_t)'0' && prefix[5] <= (uint8_t)'9') || (prefix[5] >= (uint8_t)'a' && prefix[5] <= (uint8_t)'z'));
}

static bool xx_lha_selected_tag_ok(const xx_lha *archive, const uint8_t *prefix)
{
    if (!archive->sar_tags) return xx_lha_tag_ok(prefix);
    return prefix[2] == ' ' && prefix[3] == 'L' && prefix[4] == 'H' && (prefix[5] == '0' || prefix[5] == '4' || prefix[5] == '5') && prefix[6] == ' ' && prefix[20] <= 1U;
}

/* LHA is a Japanese format and genuinely permits names outside ASCII: they
 * are raw Shift-JIS (or the writer's local code page) with nothing in the
 * container to say which, so high bytes are passed through unchanged. What
 * cannot appear in a name is a control byte, and rejecting those is what
 * catches a header walk that has wandered into payload. */
static bool xx_lha_name_byte_ok(uint8_t byte)
{
    return (byte >= 0x20U) && (byte != 0x7FU);
}

static xx_lha_stream *xx_lha_parse(Abstractformat *self, xx_pd_struct *pd)
{
    xx_lha_stream *stream = NULL;
    uint8_t *header = NULL;
    uint8_t prefix[XX_LHA_PREFIX];
    char name[XX_LHA_MAX_NAME];
    int64_t total;
    int64_t span;
    int64_t offset = 0;
    bool terminated = false;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    /* The smallest member is a level-0 header with no name: 24 bytes. */
    if (span < 24) return NULL;

    /* LHarc 1.x DOS carriers place a normal LHA chain immediately after
     * their declared DOS image, sometimes after a few padding bytes. Limit
     * recognition to the named stub and validate the complete archive chain. */
    if (!((xx_lha *)self)->sar_tags) {
        uint8_t carrier[128];
        size_t bytes = span < 128 ? (size_t)span : 128, i;
        if (!xx_lha_read_at(self, self->base_address, carrier, bytes)) return NULL;
        ((xx_lha *)self)->sfx_boa_mask = false;
        /* Exact carrier markers and offset fields from the Amiga/C64
         * producers. The ordinary full LHA chain parser below still checks
         * every bounded member header; no executable instructions run. */
        if (bytes >= 56U && carrier[0] == 0 && carrier[1] == 0 && carrier[2] == 3 && carrier[3] == 0xF3 && xx_rt_memcmp(carrier + 44U, "SFX!", 4U) == 0) {
            offset = (int64_t)((uint32_t)carrier[52] << 24U | (uint32_t)carrier[53] << 16U | (uint32_t)carrier[54] << 8U | carrier[55]);
            if (offset < 56 || offset > span - 24) return NULL;
            ((xx_lha *)self)->sfx_boa_mask = offset == 0x1914;
        } else if (span >= 0xE90 && carrier[0] == 1U && carrier[2] == 0x28U && carrier[3] == 0x1CU) {
            uint8_t marker[0x160];
            if (!xx_lha_read_at(self, self->base_address + 0xD30, marker, sizeof(marker))) return NULL;
            if (marker[0] != '1' || xx_rt_memcmp(marker + 0x14, "LHA", 3) || marker[0x15B] != '-' || marker[0x15C] != 'l' || marker[0x15D] != 'h' || marker[0x15F] != '-')
                return NULL;
            offset = 0xE89;
        } else if ((bytes >= 44U && carrier[36] == 'L' && carrier[37] == 'H' && carrier[39] == '\'' && xx_rt_memcmp(carrier + 40, "s SF", 4) == 0) ||
                   (bytes >= 40U && xx_rt_memcmp(carrier + 32, "LZSS sel", 8) == 0) || (bytes >= 18U && xx_rt_memcmp(carrier + 6, "SFX of LHarc", 12) == 0) ||
                   (bytes >= 84U && carrier[37] == 'L' && carrier[38] == 'H' && xx_rt_memcmp(carrier + 76, "name to ", 8) == 0)) {
            uint64_t limit = (uint64_t)(span - 24);
            bool found = false;
            int64_t candidate;
            if (limit > 1024U * 1024U) limit = 1024U * 1024U;
            for (candidate = 18; (uint64_t)candidate <= limit; ++candidate) {
                uint8_t framed[257];
                unsigned size, j, sum = 0;
                if (pd && xx_pd_is_stopped(pd)) return NULL;
                if (!xx_lha_read_at(self, self->base_address + candidate, framed, 24)) return NULL;
                size = framed[0] + 2U;
                if (!xx_lha_tag_ok(framed) || framed[20] > 1U || size < 24U || size > (uint64_t)(span - candidate)) continue;
                if (!xx_lha_read_at(self, self->base_address + candidate, framed, size)) return NULL;
                for (j = 2U; j < size; ++j) sum += framed[j];
                if ((uint8_t)sum != framed[1]) continue;
                offset = candidate;
                found = true;
                break;
            }
            if (!found) return NULL;
        }
        if (carrier[0] == 'M' && carrier[1] == 'Z') {
            static const char banner[] = "LHarc's SFX ";
            bool identified = false, found = false;
            int64_t extent;
            uint16_t pages = xx_data_get_u16(carrier + 4, 2, 0, false), last = xx_data_get_u16(carrier + 2, 2, 0, false);
            for (i = 0; i + sizeof(banner) - 1 <= bytes; ++i)
                if (xx_rt_memcmp(carrier + i, banner, sizeof(banner) - 1) == 0) {
                    identified = true;
                    break;
                }
            if (!identified || !pages || last >= 512 || bytes < 28 || xx_data_get_u16(carrier + 24, 2, 0, false) >= 64) return NULL;
            extent = (int64_t)pages * 512 - (last ? 512 - last : 0);
            if (extent < 28 || extent > span - 24) return NULL;
            for (i = 0; i < 64 && extent + (int64_t)i <= span - 24; ++i) {
                uint8_t candidate[XX_LHA_PREFIX] = {0};
                size_t available = span - extent - (int64_t)i < XX_LHA_PREFIX ? (size_t)(span - extent - (int64_t)i) : XX_LHA_PREFIX;
                if (pd && xx_pd_is_stopped(pd)) return NULL;
                if (!xx_lha_read_at(self, self->base_address + extent + (int64_t)i, candidate, available)) return NULL;
                if (xx_lha_tag_ok(candidate) && candidate[20] <= 1 && candidate[0] >= 22) {
                    offset = extent + (int64_t)i;
                    found = true;
                    break;
                }
            }
            if (!found) return NULL;
        }
    }

    stream = (xx_lha_stream *)xx_mem_alloc(sizeof(*stream));
    if (!stream) return NULL;
    xx_mem_zero(stream, sizeof(*stream));

    header = (uint8_t *)xx_mem_alloc((size_t)XX_LHA_MAX_HEADER);
    if (!header) goto fail;

    while (offset < span) {
        xx_lha_member member;
        int64_t compressed_size;
        int64_t uncompressed_size;
        int64_t ext_total = 0;
        int64_t remaining;
        int32_t avail;
        /* Initialised because MSVC cannot see that the level dispatch below
         * assigns all three on every path that does not jump to fail. */
        int32_t base_size = 0;
        int32_t header_total = 0;
        int32_t min_base = 24;
        int32_t name_length = 0;
        int32_t name_pos = 0;
        int32_t name_size = 0;
        int32_t dir_pos = -1;
        int32_t dir_size = 0;
        int32_t crc_pos = -1;
        int32_t ext_pos = -1;
        int32_t word_size = 2;
        int32_t index;
        uint32_t method;
        uint32_t total32;
        uint16_t common_crc = 0U;
        uint16_t unix_mode = 0U;
        uint8_t level;
        uint8_t os_id = 0U;
        bool is_symlink;
        bool is_dir;
        size_t out = 0U;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (stream->count >= (size_t)XX_LHA_MAX_MEMBERS) goto fail;

        remaining = span - offset;
        avail = (remaining < XX_LHA_PREFIX) ? (int32_t)remaining : XX_LHA_PREFIX;
        if (!xx_lha_read_at(self, self->base_address + offset, prefix, (size_t)avail)) {
            goto fail;
        }
        if (avail < XX_LHA_MIN_PREFIX) {
            /* Too little left for any header. Only the end-of-archive marker
             * may live here; trailing junk is a malformed archive, not an
             * archive with an overlay, because a member chain that stops
             * mid-header means the walk was wrong. */
            if (prefix[0] != 0U) goto fail;
            terminated = true;
            break;
        }
        if (!xx_lha_selected_tag_ok((xx_lha *)self, prefix)) {
            /* A zero where a header size would be is LHA's end-of-archive
             * marker. The tag is tested FIRST because at level 2 that byte is
             * only the low half of a 16-bit size, and a legitimate 256-byte
             * level-2 header would otherwise be read as a terminator. */
            if (prefix[0] == 0U) {
                terminated = true;
                break;
            }
            goto fail;
        }

        method = ((xx_lha *)self)->sar_tags ? XX_LHA_TAG3('l', 'h', prefix[5]) : XX_LHA_TAG3(prefix[3], prefix[4], prefix[5]);
        level = prefix[20];
        compressed_size = (int64_t)xx_data_get_u32(prefix + 7, 4, 0, false);
        uncompressed_size = (int64_t)xx_data_get_u32(prefix + 11, 4, 0, false);

        if (level <= 1U) {
            /* The size byte counts neither itself nor the checksum byte. */
            base_size = (int32_t)prefix[0] + 2;
            /* Level 0 must hold the name length, the name and the payload
             * CRC; level 1 additionally the OS byte and the first chain
             * word. A header too short for its own fixed fields is the
             * cheapest way to catch a mis-parse. */
            min_base = (level == 0U) ? 24 : 27;
            if (base_size < min_base) goto fail;
        } else if (level == 2U) {
            if (avail < 26) goto fail;
            /* Levels 2 and 3 have no checksum byte: byte 1 is the high half
             * of the size field instead. */
            base_size = (int32_t)xx_data_get_u16(prefix, 2, 0, false);
            if (base_size < 26) goto fail;
            /* OS-9/68K writers leave two bytes out of the size field. */
            if (prefix[23] == (uint8_t)'K') base_size += 2;
        } else if (level == 3U) {
            if (avail < 32) goto fail;
            /* Level 3's first word is a chain word size, and 4 is the only
             * legal value. It is this level's entire magic. */
            if (xx_data_get_u16(prefix, 2, 0, false) != 4U) goto fail;
            total32 = xx_data_get_u32(prefix + 24, 4, 0, false);
            if (total32 > (uint32_t)XX_LHA_MAX_HEADER) goto fail;
            base_size = (int32_t)total32;
            if (base_size < 32) goto fail;
            word_size = 4;
        } else {
            goto fail;
        }
        if (base_size > XX_LHA_MAX_HEADER) goto fail;
        if ((int64_t)base_size > remaining) goto fail;
        if (!xx_lha_read_at(self, self->base_address + offset, header, (size_t)base_size)) {
            goto fail;
        }
        header_total = base_size;

        if (level <= 1U) {
            if (!xx_lha_checksum_ok(header, base_size)) goto fail;
            name_length = (int32_t)header[21];
            /* The name and everything the level puts behind it have to fit
             * inside the header the size byte declared. */
            if ((min_base + name_length) > base_size) goto fail;
            name_pos = 22;
            name_size = name_length;
            if (level == 1U) {
                os_id = header[24 + name_length];
                /* Level 1's compressed-size field covers the extended
                 * headers too, so the chain is read first and subtracted;
                 * what is left is the payload. The first chain word is the
                 * final word of the base header. */
                for (;;) {
                    int32_t next_size;

                    if (pd && xx_pd_is_stopped(pd)) goto fail;
                    next_size = (int32_t)xx_data_get_u16(header + header_total - 2, 2, 0, false);
                    if (next_size == 0) break;
                    /* type byte plus the next chain word is the minimum. */
                    if (next_size < 3) goto fail;
                    if (next_size > (XX_LHA_MAX_HEADER - header_total)) {
                        goto fail;
                    }
                    /* The chain is accounted inside the compressed size; a
                     * header claiming more than what is left of it would eat
                     * into the payload. */
                    if ((int64_t)next_size > (compressed_size - ext_total)) {
                        goto fail;
                    }
                    if ((int64_t)next_size > (remaining - (int64_t)header_total)) {
                        goto fail;
                    }
                    if (!xx_lha_read_at(self, self->base_address + offset + (int64_t)header_total, header + header_total, (size_t)next_size)) {
                        goto fail;
                    }
                    header_total += next_size;
                    ext_total += next_size;
                }
                compressed_size -= ext_total;
                ext_pos = base_size - 2;
            }
        } else {
            /* No name field at these levels: it arrives as extended header
             * type 1, and a member without one is rejected below. */
            os_id = header[23];
            ext_pos = (level == 2U) ? 24 : 28;
        }

        if (ext_pos >= 0) {
            for (;;) {
                int32_t ext_size;
                int32_t data_pos;
                int32_t data_size;
                uint8_t type;

                if (pd && xx_pd_is_stopped(pd)) goto fail;
                /* The chain must terminate with a zero word inside the
                 * header. Running off the end is a rejection: an unterminated
                 * chain means the declared header size and the chain disagree
                 * about where the payload begins. */
                if (ext_pos > (header_total - word_size)) goto fail;
                ext_size = (word_size == 4) ? (int32_t)xx_data_get_u32(header + ext_pos, 4, 0, false) : (int32_t)xx_data_get_u16(header + ext_pos, 2, 0, false);
                if (ext_size == 0) break;
                if (ext_size < (word_size + 1)) goto fail;
                if (ext_size > (header_total - ext_pos - word_size)) goto fail;
                type = header[ext_pos + word_size];
                data_pos = ext_pos + word_size + 1;
                /* The trailing chain word of this entry is counted in
                 * ext_size, so the payload of the entry stops short of it. */
                data_size = ext_size - word_size - 1;
                if (type == 0x00U) {
                    /* Common header: a CRC16 over the whole header. Two of
                     * them would make "which one is authoritative"
                     * undefined. */
                    if (data_size < 2 || crc_pos >= 0) goto fail;
                    crc_pos = data_pos;
                    common_crc = xx_data_get_u16(header + data_pos, 2, 0, false);
                } else if (type == 0x01U) {
                    name_pos = data_pos;
                    name_size = data_size;
                } else if (type == 0x02U) {
                    dir_pos = data_pos;
                    dir_size = data_size;
                } else if (type == 0x50U) {
                    if (data_size < 2) goto fail;
                    unix_mode = xx_data_get_u16(header + data_pos, 2, 0, false);
                } else if (type == 0x42U) {
                    /* The 64-bit restatement of the two sizes. The member
                     * fields published here come from the 32-bit ones, so a
                     * value that disagrees -- or one that needs the upper
                     * word -- would make the extent this reader publishes a
                     * lie about the file. */
                    if (data_size < 16) goto fail;
                    if (xx_data_get_u32(header + data_pos + 4, 4, 0, false) != 0U) goto fail;
                    if (xx_data_get_u32(header + data_pos + 12, 4, 0, false) != 0U) goto fail;
                    if ((int64_t)xx_data_get_u32(header + data_pos, 4, 0, false) != compressed_size) {
                        goto fail;
                    }
                    if ((int64_t)xx_data_get_u32(header + data_pos + 8, 4, 0, false) != uncompressed_size) {
                        goto fail;
                    }
                }
                ext_pos += ext_size;
            }
        }

        if (crc_pos >= 0) {
            if (xx_lha_crc16(header, (size_t)header_total, (size_t)crc_pos) != common_crc) {
                goto fail;
            }
        } else if (level >= 2U) {
            /* Levels 2 and 3 have no checksum byte, so the common header's
             * CRC16 is their only structural self-check. Without it the whole
             * defence against a false positive would be four fixed tag
             * characters, which unrelated data hits far too often -- and
             * every real level-2/3 writer emits one. */
            goto fail;
        }

        /* Level 1's subtraction can underflow a lying size field. */
        if (compressed_size < 0 || uncompressed_size < 0) goto fail;

        is_symlink = ((unix_mode & 0170000U) == 0120000U);
        is_dir = (method == XX_LHA_M_LHD) && !is_symlink;
        /* LHARK writes its own -lh7- bitstream and marks it with OS byte
         * 0x20 on a level-1 header. Renaming the method here is what keeps
         * the decoder from handing an LHARK stream to the LHA -lh7- decoder,
         * which would succeed on the length and produce garbage. */
        if ((level == 1U) && (os_id == 0x20U) && (method == XX_LHA_M_LH7)) {
            method = XX_LHA_M_LK7;
        }
        /* A stored member states the same number twice; a disagreement means
         * the header is not describing the bytes that follow it. */
        if ((method == XX_LHA_M_LH0 || method == XX_LHA_M_LZ4) && (compressed_size != uncompressed_size)) {
            goto fail;
        }
        /* A member whose extent runs past EOF is a rejection. */
        if (!xx_lha_range_within(span, offset + (int64_t)header_total, compressed_size)) {
            goto fail;
        }

        /* Directory, then name. The directory extended header separates its
         * components with 0xFF; DOS names use '\\'. Both become '/'. */
        if (dir_size > 0) {
            for (index = 0; index < dir_size; ++index) {
                uint8_t byte = header[dir_pos + index];

                if (out >= (size_t)(XX_LHA_MAX_NAME - 2)) goto fail;
                if (byte == 0xFFU || byte == (uint8_t)'\\') {
                    byte = (uint8_t)'/';
                }
                if (!xx_lha_name_byte_ok(byte)) goto fail;
                name[out++] = (char)byte;
            }
            if (out > 0U && name[out - 1U] != '/') {
                if (out >= (size_t)(XX_LHA_MAX_NAME - 2)) goto fail;
                name[out++] = '/';
            }
        }
        for (index = 0; index < name_size; ++index) {
            uint8_t byte = header[name_pos + index];

            /* MorphOS writers append a comment after a NUL inside the name
             * field. The field's length and the header CRC still cover it;
             * only the name ends here. */
            if (byte == 0U) break;
            if (out >= (size_t)(XX_LHA_MAX_NAME - 1)) goto fail;
            if (byte == (uint8_t)'\\') byte = (uint8_t)'/';
            if (!xx_lha_name_byte_ok(byte)) goto fail;
            name[out++] = (char)byte;
        }
        name[out] = '\0';
        /* GEMDOS self-extractor paths can use a literal pipe in an output
         * name. The reference reader maps that Windows-reserved character to an underscore.
         * Keep the mapping wrapper-only; plain LHA retains stored names. */
        if (((xx_lha *)self)->sanitize_sfx_drive) {
            for (index = 0; index < (int32_t)out; ++index)
                if (name[index] == '|') name[index] = '_';
            /* Map only the legacy name bytes confirmed against the reference reader in Atari
             * self-extractors; plain LHA names retain their stored bytes. */
            for (index = 0; index < (int32_t)out; ++index) {
                uint8_t encoded[3];
                size_t encoded_size;
                switch ((uint8_t)name[index]) {
                    case 0xf7U: /* approximately equal */
                        encoded[0] = 0xe2U;
                        encoded[1] = 0x89U;
                        encoded[2] = 0x88U;
                        encoded_size = 3U;
                        break;
                    case 0xe4U: /* capital sigma */
                        encoded[0] = 0xceU;
                        encoded[1] = 0xa3U;
                        encoded_size = 2U;
                        break;
                    case 0x9aU: /* capital U with diaeresis */
                        encoded[0] = 0xc3U;
                        encoded[1] = 0x9cU;
                        encoded_size = 2U;
                        break;
                    case 0x99U: /* capital O with diaeresis */
                        encoded[0] = 0xc3U;
                        encoded[1] = 0x96U;
                        encoded_size = 2U;
                        break;
                    default: continue;
                }
                if (out > (size_t)XX_LHA_MAX_NAME - encoded_size) goto fail;
                xx_rt_memmove(name + index + encoded_size, name + index + 1, out - (size_t)index);
                xx_rt_memmove(name + index, encoded, encoded_size);
                out += encoded_size - 1U;
                index += (int32_t)encoded_size - 1;
            }
        }
        /* A GEMDOS/DOS self-extractor can store either a drive-rooted path
         * (X:/foo) or a drive-relative name (X:foo).  Its wrapper opts in
         * to the safe X_ spelling that matches the reference reader; ordinary LHA still
         * rejects drive designators. */
        if (((xx_lha *)self)->sanitize_sfx_drive && out >= 3U && ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z')) && name[1] == ':')
            name[1] = '_';
        /* GEMDOS self-extractors can store one nameless level-0 member.
         * the reference reader exposes it as "_"; apply that fallback only for the validated
         * SFX wrapper.  A plain LHA archive still needs a stored name. */
        if (out == 0U) {
            if (!((xx_lha *)self)->sanitize_sfx_drive || is_dir) goto fail;
            name[0] = '_';
            name[1] = '\0';
            out = 1U;
        }
        /* Some LHArc SFX writers prefix one DOS root separator to a member.
         * Treat that single separator as archive-relative. A doubled prefix
         * (UNC), drive designator, or parent segment remains unsafe at the
         * extraction path check. */
        if (out > 1U && name[0] == '/' && name[1] != '/') {
            xx_rt_memmove(name, name + 1, out);
            --out;
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = xx_str_dup(name);
        if (!member.name) goto fail;
        member.header_offset = self->base_address + offset;
        member.header_size = header_total;
        member.data_offset = self->base_address + offset + (int64_t)header_total;
        member.compressed_size = compressed_size;
        member.uncompressed_size = uncompressed_size;
        member.method = method;
        member.payload_crc = xx_data_get_u16(header + (level <= 1U ? 22U + header[21] : 21U), 2, 0, false);
        /* Stored verbatim: the field is an MS-DOS time|date pair at levels 0
         * and 1 but a Unix time_t at levels 2 and 3, and the level is the
         * only thing that says which. */
        member.timestamp = (uint64_t)xx_data_get_u32(header + 15, 4, 0, false);
        member.is_folder = is_dir;
        if (!xx_lha_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }

        offset += (int64_t)header_total + compressed_size;
    }

    if (stream->count == 0U) goto fail;
    if (pd && xx_pd_is_stopped(pd)) goto fail;

    xx_mem_free(header);
    /* Ending exactly at EOF is as valid as the 0x00 marker; when the marker
     * is there it belongs to the format and anything past it is overlay. */
    stream->archive_size = offset + (terminated ? 1 : 0);
    if (stream->archive_size > span) goto fail;
    return stream;

fail:
    xx_mem_free(header);
    xx_lha_stream_free(stream);
    return NULL;
}

/* The stated uncompressed size is attacker-controlled, so it is capped before
 * it becomes an allocation. */

static bool xx_lha_decode(Abstractformat *self, const xx_lha_member *member, uint8_t **out, size_t *out_size, xx_pd_struct *pd)
{
    uint8_t *packed = NULL;
    uint8_t *plain = NULL;
    size_t written = 0U;
    size_t plain_size;
    int window;
    bool stored;

    *out = NULL;
    *out_size = 0U;
    if (!self || !member) return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    if (member->compressed_size < 0 || member->uncompressed_size < 0) {
        return false;
    }
    if (member->uncompressed_size > XX_LHA_MAX_DECODED) return false;
    if ((uint64_t)member->compressed_size > (uint64_t)SIZE_MAX) return false;

    stored = (member->method == XX_LHA_M_LH0) || (member->method == XX_LHA_M_LZ4);
    window = 0;
    if (member->method == XX_LHA_M_LH4) window = 4;
    else if (member->method == XX_LHA_M_LH5) window = 5;
    else if (member->method == XX_LHA_M_LH6) window = 6;
    else if (member->method == XX_LHA_M_LH7) window = 7;
    else if (member->method == XX_LHA_M_LHX) window = 8;
    else if (member->method == XX_LHA_M_LK7) window = 9;

    /* Unknown compression methods must fail here. Falling through to stored
     * output would hand back a compressed bitstream dressed as file data, which
     * nothing downstream can tell from the real thing.
     *
     * -lhd- reaching this function at all means a symbolic link: real
     * directories are short-circuited by the caller, and a link's payload is
     * a target path rather than file content. */
    if (!stored && window == 0 && member->method != XX_LHA_M_LH1 && member->method != XX_LHA_M_LH2 && member->method != XX_LHA_M_LH3 && member->method != XX_LHA_M_LZ5 &&
        member->method != XX_LHA_M_LZS)
        return false;
    if (member->method == XX_LHA_M_LHD) return false;

    if (stored && member->compressed_size != member->uncompressed_size) {
        return false;
    }
    /* Only a stored member can be empty: every LZH bitstream emits at least
     * one code, so a zero-length compressed member is malformed. */
    if (!stored && (member->uncompressed_size == 0 || member->compressed_size == 0)) {
        return false;
    }

    plain_size = (size_t)member->uncompressed_size;
    if (member->compressed_size > 0) {
        packed = (uint8_t *)xx_mem_alloc((size_t)member->compressed_size);
        if (!packed) return false;
        if (!xx_lha_read_at(self, member->data_offset, packed, (size_t)member->compressed_size)) {
            xx_mem_free(packed);
            return false;
        }
    }
    if (pd && xx_pd_is_stopped(pd)) {
        xx_mem_free(packed);
        return false;
    }

    if (((xx_lha *)self)->sfx_boa_mask) {
        static const uint8_t mask[4] = {'B', 'O', 'A', 15U};
        size_t index;
        for (index = 0U; index < (size_t)member->compressed_size; ++index) {
            if ((index & 4095U) == 0 && pd && xx_pd_is_stopped(pd)) {
                xx_mem_free(packed);
                return false;
            }
            packed[index] ^= mask[index & 3U];
        }
    }

    /* xx_mem_alloc(0) returns NULL, which the caller cannot tell from a
     * failure, so a genuinely empty member still gets one byte. */
    plain = (uint8_t *)xx_mem_alloc(plain_size ? plain_size : (size_t)1);
    if (!plain) {
        xx_mem_free(packed);
        return false;
    }

    if (stored) {
        size_t index;

        for (index = 0U; index < plain_size; ++index) {
            plain[index] = packed[index];
        }
        written = plain_size;
    } else if (member->method == XX_LHA_M_LH1) {
        if (!xx_lzh1_decode_memory(packed, (size_t)member->compressed_size, plain, plain_size, &written)) {
            xx_mem_free(packed);
            xx_mem_free(plain);
            return false;
        }
    } else if (member->method == XX_LHA_M_LH2) {
        if (!xx_lha_lh2_decode_native(packed, (size_t)member->compressed_size, plain, plain_size, pd)) {
            xx_mem_free(packed);
            xx_mem_free(plain);
            return false;
        }
        written = plain_size;
    } else if (member->method == XX_LHA_M_LH3) {
        if (!xx_lha_lh3_decode_native(packed, (size_t)member->compressed_size, plain, plain_size, pd)) {
            xx_mem_free(packed);
            xx_mem_free(plain);
            return false;
        }
        written = plain_size;
    } else if (member->method == XX_LHA_M_LZ5 || member->method == XX_LHA_M_LZS) {
        if (!xx_lha_legacy_decode_native(member->method, packed, (size_t)member->compressed_size, plain, plain_size, pd)) {
            xx_mem_free(packed);
            xx_mem_free(plain);
            return false;
        }
        written = plain_size;
    } else if (!xx_lzh5_decode_memory(packed, (size_t)member->compressed_size, plain, plain_size, window, &written)) {
        xx_mem_free(packed);
        xx_mem_free(plain);
        return false;
    }
    xx_mem_free(packed);

    /* Returning true with fewer bytes than the header promised is the one
     * failure a caller cannot detect. -lh4- through -lh7- differ only in
     * window width, and a wrong width decodes to plausible garbage of very
     * nearly the right length, which is why the width comes from the tag and
     * never from a retry. */
    if (written != plain_size) {
        xx_mem_free(plain);
        return false;
    }
    if (xx_lha_crc16(plain, plain_size, plain_size) != member->payload_crc) {
        xx_pd_set_error(pd, 1, "LHA member payload CRC mismatch");
        xx_mem_free(plain);
        return false;
    }
    *out = plain;
    *out_size = written;
    return true;
}

/* ---------------------------------------------------------- lifecycle --- */

void xx_lha_init(xx_lha *archive, xx_io_device *device, int64_t base_address)
{
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FILE_TYPE_LHA;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-lzh-compressed");
    xx_format_set_extension(&archive->format, "lha");
    archive->format.check_is_valid = xx_lha_check_is_valid;
    archive->format.handle_base_info = xx_lha_handle_base_info;
    archive->format.get_format_size = xx_lha_get_format_size;
    archive->format.get_number_of_archive_records = xx_lha_get_number_of_archive_records;
    archive->format.create_archive_records_reading = xx_lha_create_archive_records_reading;
    archive->format.get_current_archive_record = xx_lha_get_current_archive_record;
    archive->format.unpack_current_archive_record = xx_lha_unpack_current_archive_record;
    archive->format.archive_record_move_to_next = xx_lha_archive_record_move_to_next;
    archive->format.free_archive_records_reading = xx_lha_free_archive_records_reading;
    archive->format.destroy = xx_lha_vtable_destroy;
}

xx_lha *xx_lha_create(xx_io_device *device, int64_t base_address)
{
    xx_lha *archive = (xx_lha *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_lha_init(archive, device, base_address);
    return archive;
}

void xx_lha_destroy(xx_lha *archive)
{
    if (!archive) return;
    /* Not xx_format_destroy: it dispatches through format.destroy, which is
     * the wrapper below, and the two would recurse. */
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_lha_free(xx_lha *archive)
{
    if (!archive) return;
    xx_lha_destroy(archive);
    xx_mem_free(archive);
}

static void xx_lha_vtable_destroy(Abstractformat *self)
{
    xx_lha_destroy((xx_lha *)self);
}

/* -------------------------------------------------------------- format -- */

bool xx_lha_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    xx_lha_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_lha_parse(self, pd);
    if (!stream) return false;
    xx_lha_stream_free(stream);
    return true;
}

bool xx_lha_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_lha *archive = (xx_lha *)self;
    xx_lha_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;

    self->base_info_handled = true;
    stream = xx_lha_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    xx_lha_stream_free(stream);
    return true;
}

int64_t xx_lha_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0;
    }
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_lha_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) {
        return 0U;
    }
    return self->is_valid ? ((xx_lha *)self)->number_of_records : 0U;
}

/* ------------------------------------------------------------- records -- */

static bool xx_lha_set_record(xx_archive_record *record, const xx_lha_member *member)
{
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP, member->timestamp) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, member->is_folder) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false);
}

static bool xx_lha_copy_options(xx_list_s *target, const xx_list_s *options)
{
    size_t index;

    if (!target || !options) return options == NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) || !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_lha_get_option(const xx_list_s *options, uint32_t meta_id)
{
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta = (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == meta_id) return &meta->var;
    }
    return NULL;
}

xx_archive_record_state *xx_lha_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_lha_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_lha_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_lha_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_lha_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_lha_copy_options(&state->options, options) || (stream->count != 0U && !xx_lha_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_lha_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_lha_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_lha_stream *stream;

    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_lha_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_lha_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_lha_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    xx_lha_stream *stream;
    const xx_lha_member *member;
    const xx_var *path_option;
    const char *base_path = NULL;
    char *converted_path = NULL;
    char *target_path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) {
        return false;
    }
    stream = (xx_lha_stream *)state->internal_state;
    if (!stream || stream->index >= stream->count) return false;
    member = &stream->items[stream->index];
    if (!xx_lha_path_safe(member->name)) return false;

    path_option = xx_lha_get_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: decode and discard, which verifies the member
         * without writing anything. */
        if (member->is_folder) return true;
        result = xx_lha_decode(self, member, &plain, &plain_size, pd);
        xx_mem_free(plain);
        return result;
    }
    if (path_option->type == XX_VAR_TYPE_STRING || path_option->type == XX_VAR_TYPE_STRING_VIEW) {
        base_path = xx_var_get_str(path_option);
    } else if (path_option->type == XX_VAR_TYPE_WSTRING || path_option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        converted_path = xx_str_unicode_to_utf8(xx_var_get_wstr(path_option));
        base_path = converted_path;
    }
    if (!base_path) {
        xx_str_free(converted_path);
        return false;
    }
    if (base_path[0] != '\0' && base_path[xx_str_len(base_path) - 1U] != '/' && base_path[xx_str_len(base_path) - 1U] != '\\') {
        target_path = xx_str_concat3(base_path, "/", member->name);
    } else {
        target_path = xx_str_concat(base_path, member->name);
    }
    xx_str_free(converted_path);
    if (!target_path) return false;

    if (member->is_folder) {
        result = xx_store_create_dirs_a(target_path, true);
        xx_str_free(target_path);
        return result;
    }
    if (!xx_store_create_dirs_a(target_path, false) || !xx_lha_decode(self, member, &plain, &plain_size, pd)) {
        xx_str_free(target_path);
        return false;
    }
    {
        xx_io_device *output = xx_io_file_open(target_path, "wb");
        created = output != NULL;
        size_t completed = 0U;

        result = output != NULL;
        while (result && completed < plain_size) {
            ssize_t sent = xx_io_write(output, plain + completed, plain_size - completed);
            if (sent <= 0 || (size_t)sent > plain_size - completed) {
                result = false;
                break;
            }
            completed += (size_t)sent;
        }
        if (output && xx_io_close(output) != 0) result = false;
    }
    xx_mem_free(plain);
    if (!result && created) xx_rt_remove(target_path);
    xx_str_free(target_path);
    return result;
}

void xx_lha_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}
