/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * ABBYY Lingvo / FineReader distribution-disk formats; xx_lingvoarc.h carries
 * both field tables.
 *
 * Container ("lingvoArc1"/"lingvoArc2").  The layout is U3's own (VMT slot 0
 * at 0x0051d100 -> FUN_0051cd70 for the predicate, slot 1 at 0x0051d120 ->
 * FUN_0051cdc0 for the walk).  U3 copies member payloads out raw; here they
 * are decoded, because every whole member is a FINEAR stream - a 17-byte
 * header and an LHA -lh1- body with a stored plaintext length and a
 * CRC-16/ARC - and those two values are the anchor every decode is checked
 * against.  Over the 19-volume, 439-member reference corpus the descriptor's
 * method word takes three values:
 *   0  self-contained FINEAR member (two of them empty: a bare
 *      17-byte header with size 0 and CRC 0)                        408
 *   6  first fragment of a member that continues on the NEXT volume:
 *      FINEAR header here, body cut off at the end of the file       15
 *   2  continuation fragment, the tail of a member that began on the
 *      PREVIOUS volume, so it has no FINEAR header of its own        16
 * A fragment cannot be decoded from one volume, so unpack refuses any method
 * other than 0.
 *
 * Stream ("LingvoArch").  Not handled by U3 or XArchive; the layout was
 * recovered from the 11 distinct samples of the reference corpus.  Every one
 * of them carries the same 26-byte header, a complete 321-symbol canonical
 * Huffman code and a bit stream that ends in symbol 256 with at most seven
 * zero padding bits left in its last byte; the decoded CONTEXT.DLL is a
 * self-consistent NE image (all 44 per-segment relocation blocks in place,
 * the NB02 debug trailer at the very end).  The position field is an
 * absolute index into an 8176-byte zero-filled ring whose write cursor starts
 * at 8174 - the one choice under which the NE images decode.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/lingvoarc/xx_lingvoarc.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/lzh/xx_lzh.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as LINGVOARC is registered. */
#ifdef LINGVOARC
#define XX_LINGVOARC_FILE_TYPE XX_FILE_TYPE_LINGVOARC
#else
#define XX_LINGVOARC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* ---- container ---------------------------------------------------------- */

#define LVA_HEADER_SIZE 18
#define LVA_V1_ENTRY_SIZE 54
#define LVA_V2_ENTRY_SIZE 109
#define LVA_V1_NAME_SIZE 46
#define LVA_V2_NAME_SIZE 101
#define LVA_V1_DESCRIPTOR_SIZE 23
#define LVA_V2_DESCRIPTOR_SIZE 71
#define LVA_V1_SIZE_OFFSET 13
#define LVA_V2_SIZE_OFFSET 61
#define LVA_MAX_PAYLOAD ((int64_t)512 * 1024 * 1024)

/* The nested payload format: "FINEAR" dd 88 dd, u32 CRC-16/ARC, u32 plaintext
 * size, then an LHA -lh1- stream. */
#define LVA_FINEAR_HEADER_SIZE 17
/* LHA -lh1- cannot expand more than 48 times (a 60-byte match costs at least
 * a 1-bit symbol and a 9-bit position); a declared plaintext beyond 64 times
 * the packed body is a lie and must not size an allocation. */
#define LVA_FINEAR_MAX_RATIO 64

/* ---- stream ------------------------------------------------------------- */

#define LVS_HEADER_SIZE 26U
#define LVS_SYMBOLS 321U
#define LVS_TABLE_END (LVS_HEADER_SIZE + LVS_SYMBOLS)
#define LVS_WINDOW 8176U
#define LVS_START 8174U
#define LVS_POSITION_BITS 13U
#define LVS_END_SYMBOL 256U
#define LVS_FIRST_MATCH 259U
#define LVS_MAX_CODE 32U
#define LVS_IN_CHUNK 65536U
#define LVS_OUT_CHUNK 65536U
#define LVS_MAX_OUTPUT ((uint64_t)512 * 1024 * 1024)
/* The detection probe decodes at most this many bytes of the bit stream
 * (the header and the complete code are checked in full); the whole stream
 * is walked once, by handle_base_info, to find its end and length. */
#define LVS_PROBE_BYTES 4096U
#define LVS_PAYLOAD_NAME "payload"

static const uint8_t lvs_signature[LVS_HEADER_SIZE] = {
    'L', 'i', 'n', 'g', 'v', 'o', 'A', 'r', 'c', 'h',
    0x01, 0x00, 0xf0, 0x1f, 0x00, 0x01, 0x40, 0x00,
    0x47, 0x01, 0x47, 0x01, 0x1f, 0x83, 0x41, 0x01};

typedef struct lva_member_s {
    char *name;
    int64_t descriptor_offset;
    int64_t data_offset;
    int64_t size;
    uint32_t timestamp;
    uint16_t method;
} lva_member;

typedef struct lva_stream_s {
    lva_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint32_t version;
    uint32_t variant;
    uint64_t plain_size; /* stream variant */
} lva_stream;

static uint16_t lva_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t lva_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool lva_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool lva_write_all(xx_io_device *device, const uint8_t *data,
                          size_t size) {
    size_t written = 0U;
    while (written < size) {
        ssize_t amount = xx_io_write(device, data + written, size - written);
        if (amount <= 0 || (size_t)amount > size - written) return false;
        written += (size_t)amount;
    }
    return true;
}

/* Member paths are DOS style.  The record name keeps the stored bytes and
 * only swaps the backslash separator for '/'; whether the name is safe to
 * create on disk is decided at unpack time by lva_safe_output_name. */
static char *lva_copy_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t index;
    if (!bytes || size == 0U || size > SIZE_MAX - 1U) return NULL;
    name = (char *)xx_mem_alloc(size + 1U);
    if (!name) return NULL;
    for (index = 0U; index < size; ++index)
        name[index] = bytes[index] == '\\' ? '/' : (char)bytes[index];
    name[size] = 0;
    return name;
}

/* A Windows device name (CON, PRN, AUX, NUL, COM0-9, LPT0-9, CLOCK$, CONIN$,
 * CONOUT$) as the part of a component before its first '.', trailing spaces
 * ignored: "nul", "Con.txt" and "aux .dat" all open the device, not a file. */
static bool lva_reserved_component(const char *segment, size_t length) {
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
 * components, components ending in '.' or ' ' (this covers "." and ".." and
 * the names Windows would silently trim into a collision), device names,
 * control characters and the characters no Windows path may carry. */
static bool lva_safe_output_name(const char *name) {
    const char *segment;
    const char *at;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || c == 0x7fU || (c != 0U && c < 0x20U))
            return false;
        if (c == '/' || c == '\\' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || segment[length - 1U] == '.' ||
                segment[length - 1U] == ' ' ||
                lva_reserved_component(segment, length))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

static void lva_stream_free(void *opaque) {
    lva_stream *stream = (lva_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

/* ---- stream decoder ----------------------------------------------------- */

typedef struct lvs_code_s {
    uint16_t sorted[LVS_SYMBOLS];
    uint32_t count[LVS_MAX_CODE + 1U];
    uint32_t offset[LVS_MAX_CODE + 1U];
    uint64_t first[LVS_MAX_CODE + 1U];
    uint32_t max_length;
} lvs_code;

typedef struct lvs_state_s {
    lvs_code code;
    xx_io_device *device;
    xx_pd_struct *pd;
    int64_t next;  /* absolute offset of the next byte to buffer */
    int64_t limit; /* absolute end of the device */
    size_t fill;
    size_t pos;
    uint32_t byte;
    uint32_t bits_left;
    uint64_t bytes_used; /* bytes of the bit stream touched so far */
    uint8_t input[LVS_IN_CHUNK];
    uint8_t ring[LVS_WINDOW];
    uint8_t output[LVS_OUT_CHUNK];
} lvs_state;

/* All 321 lengths must lie in 1..32 and describe a complete prefix code:
 * that is what every known encoder output looks like, and it is what makes
 * the canonical walk below total. */
static bool lvs_build_code(lvs_code *code, const uint8_t *lengths) {
    uint64_t kraft = 0U;
    uint32_t next[LVS_MAX_CODE + 1U];
    uint32_t length, symbol;
    xx_mem_zero(code, sizeof(*code));
    for (symbol = 0U; symbol < LVS_SYMBOLS; ++symbol) {
        length = lengths[symbol];
        if (length == 0U || length > LVS_MAX_CODE) return false;
        ++code->count[length];
        kraft += (uint64_t)1U << (LVS_MAX_CODE - length);
        if (length > code->max_length) code->max_length = length;
    }
    if (kraft != ((uint64_t)1U << LVS_MAX_CODE)) return false;
    code->first[1] = 0U;
    code->offset[1] = 0U;
    for (length = 2U; length <= LVS_MAX_CODE; ++length) {
        code->first[length] =
            (code->first[length - 1U] + code->count[length - 1U]) << 1U;
        code->offset[length] =
            code->offset[length - 1U] + code->count[length - 1U];
    }
    for (length = 1U; length <= LVS_MAX_CODE; ++length)
        next[length] = code->offset[length];
    for (symbol = 0U; symbol < LVS_SYMBOLS; ++symbol)
        code->sorted[next[lengths[symbol]]++] = (uint16_t)symbol;
    return true;
}

static int lvs_bit(lvs_state *state) {
    if (state->bits_left == 0U) {
        if (state->pos == state->fill) {
            int64_t remaining = state->limit - state->next;
            size_t amount = remaining > (int64_t)LVS_IN_CHUNK
                                ? (size_t)LVS_IN_CHUNK : (size_t)remaining;
            if (remaining <= 0 || (state->pd && xx_pd_is_stopped(state->pd)) ||
                !lva_read_at(state->device, state->next, state->input, amount))
                return -1;
            state->next += (int64_t)amount;
            state->fill = amount;
            state->pos = 0U;
        }
        state->byte = state->input[state->pos++];
        state->bits_left = 8U;
        ++state->bytes_used;
    }
    --state->bits_left;
    return (int)((state->byte >> state->bits_left) & 1U);
}

static int lvs_symbol(lvs_state *state) {
    const lvs_code *code = &state->code;
    uint64_t value = 0U;
    uint32_t length;
    for (length = 1U; length <= code->max_length; ++length) {
        int bit = lvs_bit(state);
        if (bit < 0) return -1;
        value = (value << 1U) | (uint64_t)bit;
        if (value >= code->first[length] &&
            value - code->first[length] < code->count[length])
            return code->sorted[code->offset[length] +
                                (uint32_t)(value - code->first[length])];
    }
    return -1;
}

/* Decode the stream.  With a sink the plaintext is streamed to it through
 * the ring, so no allocation depends on the (unstored) output length.
 * Without one this is a validation walk: nothing the format can reject
 * depends on the ring contents (there is no checksum), so the copy is
 * skipped and only lengths are counted.  probe_bytes != 0 bounds the walk
 * for detection: it succeeds once that many bit-stream bytes decoded
 * cleanly, and stream_size / plain_size are then left untouched. */
static bool lvs_walk(Abstractformat *format, xx_io_device *sink,
                     xx_pd_struct *pd, uint64_t probe_bytes,
                     int64_t *stream_size, uint64_t *plain_size) {
    uint8_t header[LVS_TABLE_END];
    lvs_state *state = NULL;
    int64_t total, size;
    uint64_t produced = 0U;
    size_t out_fill = 0U;
    uint32_t cursor = LVS_START;
    bool result = false;
    if (!format || !format->device || format->base_address < 0) return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size <= (int64_t)LVS_TABLE_END ||
        !lva_read_at(format->device, format->base_address, header,
                     sizeof(header)) ||
        xx_rt_memcmp(header, lvs_signature, LVS_HEADER_SIZE) != 0)
        return false;
    state = (lvs_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return false;
    if (!lvs_build_code(&state->code, header + LVS_HEADER_SIZE)) goto done;
    state->device = format->device;
    state->pd = pd;
    state->next = format->base_address + (int64_t)LVS_TABLE_END;
    state->limit = total;
    state->fill = 0U;
    state->pos = 0U;
    state->bits_left = 0U;
    state->bytes_used = 0U;
    xx_mem_zero(state->ring, sizeof(state->ring));
    for (;;) {
        int symbol;
        uint32_t length, source, bit_index, index;
        if (probe_bytes != 0U && state->bytes_used > probe_bytes) {
            result = true;
            goto done;
        }
        symbol = lvs_symbol(state);
        if (symbol < 0) goto done;
        if ((uint32_t)symbol == LVS_END_SYMBOL) break;
        if (symbol < 256) {
            length = 1U;
            source = LVS_WINDOW; /* literal marker */
        } else {
            uint32_t position = 0U;
            /* 257 and 258 (1- and 2-byte matches) are never produced. */
            if ((uint32_t)symbol < LVS_FIRST_MATCH) goto done;
            for (bit_index = 0U; bit_index < LVS_POSITION_BITS; ++bit_index) {
                int bit = lvs_bit(state);
                if (bit < 0) goto done;
                position = (position << 1U) | (uint32_t)bit;
            }
            if (position >= LVS_WINDOW) goto done;
            length = (uint32_t)symbol - 256U;
            source = position;
        }
        if (produced + length > LVS_MAX_OUTPUT) goto done;
        if (!sink) {
            produced += length;
            continue;
        }
        for (index = 0U; index < length; ++index) {
            uint8_t value;
            if (source == LVS_WINDOW) {
                value = (uint8_t)symbol;
            } else {
                value = state->ring[source];
                if (++source == LVS_WINDOW) source = 0U;
            }
            state->ring[cursor] = value;
            if (++cursor == LVS_WINDOW) cursor = 0U;
            state->output[out_fill++] = value;
            if (out_fill == LVS_OUT_CHUNK) {
                if (!lva_write_all(sink, state->output, out_fill)) goto done;
                out_fill = 0U;
                if (pd && xx_pd_is_stopped(pd)) goto done;
            }
        }
        produced += length;
    }
    if (sink && out_fill != 0U &&
        !lva_write_all(sink, state->output, out_fill))
        goto done;
    if (stream_size)
        *stream_size = (int64_t)LVS_TABLE_END + (int64_t)state->bytes_used;
    if (plain_size) *plain_size = produced;
    result = true;
done:
    xx_mem_free(state);
    return result;
}

/* ---- container parser --------------------------------------------------- */

static bool lva_parse_container(Abstractformat *format, int64_t size,
                                const uint8_t *header, lva_stream **result) {
    uint8_t entry[LVA_V2_ENTRY_SIZE];
    uint8_t descriptor[LVA_V2_DESCRIPTOR_SIZE];
    lva_stream *stream = NULL;
    int64_t cursor, next_free, index_end;
    size_t entry_size, name_size, descriptor_size, size_offset;
    uint32_t count, index;
    if (header[9] != '1' && header[9] != '2') return false;
    /* Six constant bytes; U3's own predicate tests them, and they are what
     * keeps a ten-byte ASCII magic from being the whole of the check. */
    if (header[10] != 0x00U || header[11] != 0xfdU || header[12] != 0x00U ||
        header[13] != 0xdfU || header[14] != 0x00U || header[15] != 0xffU)
        return false;
    count = lva_le16(header + 16U);
    if (count == 0U) return false;
    if (header[9] == '1') {
        entry_size = LVA_V1_ENTRY_SIZE;
        name_size = LVA_V1_NAME_SIZE;
        descriptor_size = LVA_V1_DESCRIPTOR_SIZE;
        size_offset = LVA_V1_SIZE_OFFSET;
    } else {
        entry_size = LVA_V2_ENTRY_SIZE;
        name_size = LVA_V2_NAME_SIZE;
        descriptor_size = LVA_V2_DESCRIPTOR_SIZE;
        size_offset = LVA_V2_SIZE_OFFSET;
    }
    index_end = (int64_t)LVA_HEADER_SIZE + (int64_t)count * (int64_t)entry_size;
    if (index_end > size) return false;
    stream = (lva_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->variant = XX_LINGVOARC_VARIANT_CONTAINER;
    stream->version = (uint32_t)(header[9] - '0');
    stream->items = (lva_member *)xx_mem_calloc(count, sizeof(*stream->items));
    if (!stream->items) goto fail;
    cursor = LVA_HEADER_SIZE;
    next_free = index_end;
    for (index = 0U; index < count; ++index) {
        lva_member *member = &stream->items[index];
        int64_t descriptor_offset, payload_size, data_offset;
        size_t terminator = 0U;
        if (!lva_read_at(format->device, format->base_address + cursor, entry,
                         entry_size)) goto fail;
        cursor += (int64_t)entry_size;
        while (terminator < name_size && entry[terminator] != 0U)
            ++terminator;
        /* An entry whose name field never terminates is not this layout. */
        if (terminator == 0U || terminator >= name_size) goto fail;
        descriptor_offset = (int64_t)lva_le32(entry + name_size);
        /* Members follow the index in index order and never overlap: on
         * every real volume the first descriptor starts at the end of the
         * index and each next one exactly where the previous payload ends.
         * Gaps are tolerated, overlap and backward offsets are not - without
         * this, N index entries could name one large payload N times and
         * make the decode work grow with the entry count instead of the
         * file size. */
        if (descriptor_offset < next_free ||
            descriptor_offset > size - (int64_t)descriptor_size) goto fail;
        if (!lva_read_at(format->device,
                         format->base_address + descriptor_offset, descriptor,
                         descriptor_size)) goto fail;
        payload_size = (int64_t)lva_le32(descriptor + size_offset);
        if (payload_size > LVA_MAX_PAYLOAD) goto fail;
        data_offset = descriptor_offset + (int64_t)descriptor_size;
        if (payload_size > size - data_offset) goto fail;
        member->name = lva_copy_name(entry, terminator);
        if (!member->name) goto fail;
        stream->count = index + 1U;
        member->descriptor_offset = format->base_address + descriptor_offset;
        member->data_offset = format->base_address + data_offset;
        member->size = payload_size;
        member->timestamp = lva_le32(descriptor + size_offset + 4U);
        member->method = lva_le16(descriptor + size_offset + 8U);
        next_free = data_offset + payload_size;
    }
    /* Bytes after the last member are overlay, not part of the volume. */
    stream->archive_size = next_free;
    *result = stream;
    return true;
fail:
    lva_stream_free(stream);
    return false;
}

/* Which of the two layouts starts at base_address (by its first 18 bytes,
 * returned in `header`), and how many bytes the device holds from there. */
static uint32_t lva_identify(Abstractformat *format, int64_t *size,
                             uint8_t *header) {
    int64_t total;
    if (!format || !format->device || format->base_address < 0)
        return XX_LINGVOARC_VARIANT_NONE;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return XX_LINGVOARC_VARIANT_NONE;
    *size = total - format->base_address;
    if (*size < (int64_t)LVA_HEADER_SIZE ||
        !lva_read_at(format->device, format->base_address, header,
                     LVA_HEADER_SIZE))
        return XX_LINGVOARC_VARIANT_NONE;
    if (xx_rt_memcmp(header, "lingvoArc", 9U) == 0)
        return XX_LINGVOARC_VARIANT_CONTAINER;
    if (xx_rt_memcmp(header, lvs_signature, LVA_HEADER_SIZE) == 0)
        return XX_LINGVOARC_VARIANT_STREAM;
    return XX_LINGVOARC_VARIANT_NONE;
}

/* The one-record description of a stream already measured by lvs_walk. */
static bool lvs_make_stream(Abstractformat *format, int64_t stream_size,
                            uint64_t plain_size, lva_stream **result) {
    lva_stream *stream;
    if (stream_size <= (int64_t)LVS_TABLE_END) return false;
    stream = (lva_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->items = (lva_member *)xx_mem_calloc(1U, sizeof(*stream->items));
    stream->variant = XX_LINGVOARC_VARIANT_STREAM;
    stream->version = 1U;
    stream->archive_size = stream_size;
    stream->plain_size = plain_size;
    if (!stream->items ||
        !(stream->items[0].name = xx_str_dup(LVS_PAYLOAD_NAME))) {
        lva_stream_free(stream);
        return false;
    }
    stream->count = 1U;
    stream->items[0].descriptor_offset = format->base_address;
    stream->items[0].data_offset =
        format->base_address + (int64_t)LVS_TABLE_END;
    stream->items[0].size = stream_size - (int64_t)LVS_TABLE_END;
    stream->items[0].method = 1U;
    *result = stream;
    return true;
}

/* Full parse: the container index, or one complete walk of the stream. */
static bool lva_parse(Abstractformat *format, xx_pd_struct *pd,
                      lva_stream **result) {
    uint8_t header[LVA_HEADER_SIZE];
    int64_t size = 0, stream_size = 0;
    uint64_t plain_size = 0U;
    if (!result) return false;
    switch (lva_identify(format, &size, header)) {
    case XX_LINGVOARC_VARIANT_CONTAINER:
        return lva_parse_container(format, size, header, result);
    case XX_LINGVOARC_VARIANT_STREAM:
        return lvs_walk(format, NULL, pd, 0U, &stream_size, &plain_size) &&
               lvs_make_stream(format, stream_size, plain_size, result);
    default:
        return false;
    }
}

/* ---- FINEAR payloads ---------------------------------------------------- */

static bool lva_finear_header(Abstractformat *format, const lva_member *member,
                              uint32_t *checksum, uint32_t *unpacked) {
    uint8_t header[LVA_FINEAR_HEADER_SIZE];
    if (member->size < (int64_t)LVA_FINEAR_HEADER_SIZE ||
        !lva_read_at(format->device, member->data_offset, header,
                     sizeof(header)))
        return false;
    if (xx_rt_memcmp(header, "FINEAR", 6U) != 0 || header[6] != 0xddU ||
        header[7] != 0x88U || header[8] != 0xddU) return false;
    *checksum = lva_le32(header + 9U);
    *unpacked = lva_le32(header + 13U);
    /* The checksum field is a 16-bit CRC written into a 32-bit slot. */
    return *checksum <= 0xffffU;
}

/* Decode a whole (method 0) FINEAR payload, verifying its stored plaintext
 * length and CRC-16/ARC.  Fails closed: a payload that is not FINEAR, or one
 * that does not reproduce both, is not returned at all. */
static bool lva_decode_member(Abstractformat *format, const lva_member *member,
                              uint8_t **plain, size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t written = 0U;
    uint32_t checksum, unpacked;
    int64_t packed_size;
    if (!format || !member || !plain || !plain_size) return false;
    if (member->method != 0U ||
        !lva_finear_header(format, member, &checksum, &unpacked))
        return false;
    packed_size = member->size - (int64_t)LVA_FINEAR_HEADER_SIZE;
    if ((uint64_t)unpacked > (uint64_t)LVA_MAX_PAYLOAD ||
        (uint64_t)unpacked >
            (uint64_t)packed_size * LVA_FINEAR_MAX_RATIO + 64U)
        return false;
    output = (uint8_t *)xx_mem_alloc(unpacked != 0U ? (size_t)unpacked : 1U);
    if (!output) return false;
    if (packed_size == 0) {
        /* An empty file is stored as the bare header. */
        if (unpacked != 0U || checksum != 0U) goto fail;
    } else {
        packed = (uint8_t *)xx_mem_alloc((size_t)packed_size);
        if (!packed ||
            !lva_read_at(format->device,
                         member->data_offset + LVA_FINEAR_HEADER_SIZE, packed,
                         (size_t)packed_size))
            goto fail;
        if (!xx_lzh1_decode_memory(packed, (size_t)packed_size, output,
                                   (size_t)unpacked, &written) ||
            written != (size_t)unpacked ||
            (uint32_t)xx_crc16(XX_CRC_TYPE_CRC16_ARC, output, written) !=
                checksum)
            goto fail;
        xx_mem_free(packed);
    }
    *plain = output;
    *plain_size = written;
    return true;
fail:
    if (packed) xx_mem_free(packed);
    xx_mem_free(output);
    return false;
}

/* ---- records ------------------------------------------------------------ */

static bool lva_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *lva_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool lva_set_record(Abstractformat *format, const lva_stream *stream,
                           xx_archive_record *record,
                           const lva_member *member) {
    bool ok;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->descriptor_offset;
    record->header_size = member->data_offset - member->descriptor_offset;
    record->data_offset = member->data_offset;
    record->compressed_size = member->size;
    ok = xx_archive_record_set_original_name(record, member->name) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        (uint64_t)member->size) &&
         xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        member->method) &&
         xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) &&
         xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
    if (!ok) return false;
    if (stream->variant == XX_LINGVOARC_VARIANT_STREAM)
        return xx_archive_record_set_meta_u64(
            record, XX_META_ID_UNCOMPRESSED_SIZE, stream->plain_size);
    if (!xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                        member->timestamp) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                        member->method))
        return false;
    {
        /* The FINEAR header (whole members and first fragments) states the
         * plaintext length; a continuation fragment has none to report. */
        uint32_t checksum, unpacked;
        if (lva_finear_header(format, member, &checksum, &unpacked) &&
            !xx_archive_record_set_meta_u64(
                record, XX_META_ID_UNCOMPRESSED_SIZE, unpacked))
            return false;
    }
    return true;
}

/* ---- public API --------------------------------------------------------- */

void xx_lingvoarc_init(xx_lingvoarc *archive, xx_io_device *device,
                       int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_LINGVOARC_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-lingvoarc");
    xx_format_set_extension(&archive->format, "lva");
    archive->format.check_is_valid = xx_lingvoarc_check_is_valid;
    archive->format.handle_base_info = xx_lingvoarc_handle_base_info;
    archive->format.get_format_size = xx_lingvoarc_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_lingvoarc_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_lingvoarc_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_lingvoarc_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_lingvoarc_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_lingvoarc_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_lingvoarc_free_archive_records_reading;
}

xx_lingvoarc *xx_lingvoarc_create(xx_io_device *device, int64_t base_address) {
    xx_lingvoarc *archive = (xx_lingvoarc *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_lingvoarc_init(archive, device, base_address);
    return archive;
}

void xx_lingvoarc_destroy(xx_lingvoarc *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_lingvoarc_free(xx_lingvoarc *archive) {
    if (!archive) return;
    xx_lingvoarc_destroy(archive);
    xx_mem_free(archive);
}

/* The detection probe.  A container is parsed in full (its index is bounded
 * by the file and nothing is decoded); a stream is checked through its
 * header, its complete code and the first LVS_PROBE_BYTES of the bit
 * stream, so a file that matches the 26-byte header never costs a whole
 * decompression here.  A stream damaged further in is refused later, by
 * handle_base_info. */
bool xx_lingvoarc_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    uint8_t header[LVA_HEADER_SIZE];
    int64_t size = 0;
    lva_stream *stream = NULL;
    switch (lva_identify(format, &size, header)) {
    case XX_LINGVOARC_VARIANT_CONTAINER:
        if (!lva_parse_container(format, size, header, &stream)) return false;
        lva_stream_free(stream);
        return true;
    case XX_LINGVOARC_VARIANT_STREAM:
        return lvs_walk(format, NULL, pd, LVS_PROBE_BYTES, NULL, NULL);
    default:
        return false;
    }
}

/* The one full parse; for a stream this is the single validation walk, and
 * its results (format_size, uncompressed_size) are cached here for
 * create_archive_records_reading. */
bool xx_lingvoarc_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    lva_stream *stream;
    xx_lingvoarc *archive;
    if (!format || !lva_parse(format, pd, &stream)) return false;
    archive = (xx_lingvoarc *)format;
    archive->number_of_records = stream->count;
    archive->container_version = stream->version;
    archive->variant = stream->variant;
    archive->uncompressed_size = stream->plain_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    lva_stream_free(stream);
    return true;
}

int64_t xx_lingvoarc_get_format_size(Abstractformat *format,
                                     xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_lingvoarc_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_lingvoarc_get_number_of_archive_records(Abstractformat *format,
                                                    xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_lingvoarc_handle_base_info(format, pd))
               ? ((xx_lingvoarc *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_lingvoarc_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    lva_stream *stream = NULL;
    xx_archive_record_state *state;
    const xx_lingvoarc *archive = (const xx_lingvoarc *)format;
    bool parsed;
    if (!format ||
        (!format->base_info_handled &&
         !xx_lingvoarc_handle_base_info(format, pd)))
        return NULL;
    /* A stream was walked once by handle_base_info; reuse what it measured
     * instead of decoding it again.  A container index is cheap to re-read. */
    if (archive->variant == XX_LINGVOARC_VARIANT_STREAM)
        parsed = lvs_make_stream(format, format->format_size,
                                 archive->uncompressed_size, &stream);
    else
        parsed = lva_parse(format, pd, &stream);
    if (!parsed) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        lva_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = lva_stream_free;
    state->total_records = stream->count;
    if (!lva_copy_options(&state->options, options) ||
        !lva_set_record(format, stream, &state->current_record,
                        &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_lingvoarc_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_lingvoarc_archive_record_move_to_next(Abstractformat *format,
                                              xx_archive_record_state *state,
                                              xx_pd_struct *pd) {
    lva_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (lva_stream *)state->internal_state) ||
        stream->index + 1U >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = lva_set_record(format, stream, &state->current_record,
                                       &stream->items[stream->index]);
    return state->has_record;
}

bool xx_lingvoarc_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    lva_stream *stream;
    lva_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (lva_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!lva_safe_output_name(member->name)) return false;
    if (stream->variant == XX_LINGVOARC_VARIANT_CONTAINER &&
        !lva_decode_member(format, member, &plain, &plain_size))
        return false;
    path_option = lva_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        /* No destination: report whether the member decodes.  A container
         * member was just decoded and CRC-checked; a stream was walked in
         * full before its record could be created. */
        result = true;
        goto done;
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
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        if (!destination) goto done;
        if (stream->variant == XX_LINGVOARC_VARIANT_CONTAINER)
            result = lva_write_all(destination, plain, plain_size);
        else
            result = lvs_walk(format, destination, pd, 0U, NULL, NULL);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_lingvoarc_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
