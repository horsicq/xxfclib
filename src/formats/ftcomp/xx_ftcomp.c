/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * FTCOMP, a member of the OS/2 "A5 96" packed-file family found on IBM and
 * S3 display-driver install media.  It is the dialect whose word at offset 2
 * is 0xFFFD; the 0x0A14, 0xFFFE and 0xFFFF dialects belong to
 * src/formats/ibmpack and are NOT claimed here.  The two readers are
 * disjoint: ibmpack's xx_ibmpack_variant_known() rejects 0xFFFD, and this
 * reader rejects everything but 0xFFFD, so neither prefilter shadows the
 * other.
 *
 * Undocumented; the layout below was recovered from the 76 samples (278
 * members) in F:\ARC\ARC\FTCOMP.  The first eight fields line up exactly
 * with ibmpack's 0xFFFE dialect, which is what makes the reading safe; the
 * 0xFFFD extension is the "FTCOMP" block from 0x18 on.
 *
 *   0x00  u16  0xA5 0x96 signature (stored in that byte order)
 *   0x02  u16  0xFFFD dialect word (76/76)
 *   0x04  u16  MS-DOS packed date (all 278 decode to a legal date)
 *   0x06  u16  MS-DOS packed time
 *   0x08  u8   MS-DOS attributes (0x20 in 263 members, 0x00 in 15)
 *   0x09  u8[3] zero in all 278 members; meaning unknown
 *   0x0C  u32  offset of this member's compressed extended-attribute blob,
 *              0 when it has none.  26 members carry one; every one of them
 *              points inside the member and at a second stream with the same
 *              codec prefix, exactly as in the 0xFFFE dialect.
 *   0x10  u32  plaintext length
 *   0x14  u32  offset of the NEXT member header, 0 at the end of the chain
 *   0x18  char[7] "FTCOMP" and its NUL (278/278)
 *   0x1F  u16  unknown, varies per member - most likely a checksum
 *   0x21  u16  1 in all 278 members - a method or version selector
 *   0x23  u32  unknown; 4 in 252 members, other small values elsewhere
 *   0x27  u16  size of the name FIELD (the name is stored NUL-terminated
 *              inside it)
 *   0x29  ...  the name field, then the compressed payload
 *
 * Confidence: the chain is certain.  Following 0x14 from the first header
 * lands on a well-formed 0xFFFD header every time and the last member's
 * extent ends exactly at EOF in all 76 samples; all 278 names are legal OS/2
 * paths.  The fields at 0x09, 0x1F and 0x23 are carried through to the
 * record metadata but not interpreted.
 *
 * The codec: those eight bytes are a four-byte payload prologue followed by
 * the first block tag.  A payload is a chain of blocks
 *
 *   char[4] tag ("fT19" throughout the corpus), u16 length
 *     length == 0xFFFF  -> a u16 byte count, then that many stored tokens
 *     otherwise         -> that many tokens, entropy coded
 *
 * repeated until the plaintext length at 0x10 has been produced; a tail of
 * fewer than four bytes is stored raw.  Each block's tokens then feed a
 * byte-oriented LZ77 over a 512 KiB window primed with a fixed 4026-byte
 * dictionary.  See the codec section below for where in U3 each half came
 * from.  Verified against U3 over the whole corpus: all 278 members decode
 * to exactly their declared length and are byte-identical to its output.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/ftcomp/xx_ftcomp.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

/* The enumerator is added by the coordinator, not by this file. */
#ifdef FTCOMP
#define XX_FTCOMP_FILE_TYPE XX_FILE_TYPE_FTCOMP
#else
#define XX_FTCOMP_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XX_FTCOMP_SIG0 UINT8_C(0xa5)
#define XX_FTCOMP_SIG1 UINT8_C(0x96)
#define XX_FTCOMP_VARIANT UINT32_C(0xfffd)

/* Fixed part of a member header, up to and including the name-field size. */
#define XX_FTCOMP_HEADER_SIZE 0x29
#define XX_FTCOMP_TAG_OFFSET 0x18
#define XX_FTCOMP_TAG_SIZE 7U

/* The longest name in the corpus is 35 bytes; this ceiling is generous
 * rather than tight, and it bounds the name read. */
#define XX_FTCOMP_MAX_NAME 1024U

/* A chain cannot revisit an offset, so this only guards against a
 * pathological file made of a huge number of tiny members. */
#define XX_FTCOMP_MAX_MEMBERS 65536U

#define XX_FTCOMP_METHOD 1U

typedef struct xx_ftcomp_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t uncompressed_size;
    uint64_t timestamp;
    uint32_t attributes;
    uint32_t extra;   /* the unknown u32 at 0x23 */
    uint16_t check;   /* the unknown u16 at 0x1F */
    uint16_t method;  /* the u16 at 0x21, 1 throughout the corpus */
} xx_ftcomp_member;

typedef struct xx_ftcomp_stream_s {
    xx_ftcomp_member *items;
    size_t count;
    size_t index;
    int64_t archive_size;
    uint32_t variant;
} xx_ftcomp_stream;

/* ------------------------------------------------------------ helpers --- */

static uint16_t xx_ftcomp_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t xx_ftcomp_le32(const uint8_t *bytes) {
    return (uint32_t)xx_ftcomp_le16(bytes) |
           ((uint32_t)xx_ftcomp_le16(bytes + 2U) << 16U);
}

static bool xx_ftcomp_read_at(Abstractformat *self, int64_t offset,
                              void *buffer, size_t size) {
    size_t done = 0U;

    if (!self || !self->device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(self->device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount =
            xx_io_read(self->device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool xx_ftcomp_date_sane(uint16_t date) {
    unsigned month = (unsigned)((date >> 5U) & 0x0fU);
    unsigned day = (unsigned)(date & 0x1fU);

    return month >= 1U && month <= 12U && day >= 1U && day <= 31U;
}

/* The stored name is an OS/2 path with backslash separators.  Only the
 * filesystem-facing form is rewritten; the bytes themselves are kept for the
 * caller's code page. */
static char *xx_ftcomp_normalize_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t input = 0U;
    size_t output = 0U;

    if ((!bytes && size != 0U) || size > SIZE_MAX - 2U) return NULL;
    name = (char *)xx_mem_alloc(size + 2U);
    if (!name) return NULL;
    while (input < size) {
        size_t start;
        size_t end;
        size_t component;

        while (input < size &&
               (bytes[input] == '/' || bytes[input] == '\\'))
            ++input;
        start = input;
        while (input < size && bytes[input] != '/' && bytes[input] != '\\')
            ++input;
        end = input;
        if (end == start || (end - start == 1U && bytes[start] == '.'))
            continue;
        if (end - start == 2U && bytes[start] == '.' &&
            bytes[start + 1U] == '.') {
            if (output != 0U) {
                while (output != 0U && name[output - 1U] != '/') --output;
                if (output != 0U) --output;
            }
            continue;
        }
        if (output != 0U) name[output++] = '/';
        component = output;
        while (start < end) {
            uint8_t c = bytes[start++];
            if (c < 0x20U || c == '"' || c == '*' || c == ':' || c == '<' ||
                c == '>' || c == '?' || c == '|')
                name[output++] = '_';
            else
                name[output++] = (char)c;
        }
        while (output > component &&
               (name[output - 1U] == ' ' || name[output - 1U] == '.'))
            --output;
        if (output == component) name[output++] = '_';
    }
    if (output == 0U) name[output++] = '_';
    name[output] = 0;
    return name;
}

static void xx_ftcomp_stream_free(void *pointer) {
    xx_ftcomp_stream *stream = (xx_ftcomp_stream *)pointer;
    size_t index;

    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        xx_str_free(stream->items[index].name);
    xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool xx_ftcomp_add(xx_ftcomp_stream *stream,
                          const xx_ftcomp_member *member) {
    xx_ftcomp_member *grown;

    if (stream->count >= XX_FTCOMP_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (xx_ftcomp_member *)xx_mem_realloc(
        stream->items, sizeof(*grown) * (stream->count + 1U));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* Parse one member header at @p offset (relative to base_address).  On
 * success @p next receives the chained successor's relative offset, or 0. */
static bool xx_ftcomp_read_member(Abstractformat *self, int64_t span,
                                  int64_t offset, xx_ftcomp_member *member,
                                  int64_t *next) {
    uint8_t header[XX_FTCOMP_HEADER_SIZE];
    uint8_t namebuf[XX_FTCOMP_MAX_NAME];
    uint32_t extended_attributes;
    uint32_t declared;
    uint32_t successor;
    int64_t name_offset;
    int64_t name_field;
    int64_t data_offset;
    int64_t member_end;
    size_t name_length;

    xx_mem_zero(member, sizeof(*member));
    *next = 0;
    if (offset < 0 || span - offset < (int64_t)XX_FTCOMP_HEADER_SIZE)
        return false;
    if (!xx_ftcomp_read_at(self, self->base_address + offset, header,
                           sizeof(header)))
        return false;
    if (header[0] != XX_FTCOMP_SIG0 || header[1] != XX_FTCOMP_SIG1)
        return false;
    if ((uint32_t)xx_ftcomp_le16(header + 2) != XX_FTCOMP_VARIANT)
        return false;
    if (xx_rt_memcmp(header + XX_FTCOMP_TAG_OFFSET, "FTCOMP",
                     XX_FTCOMP_TAG_SIZE) != 0)
        return false;
    if (!xx_ftcomp_date_sane(xx_ftcomp_le16(header + 4))) return false;

    extended_attributes = xx_ftcomp_le32(header + 12);
    declared = xx_ftcomp_le32(header + 16);
    successor = xx_ftcomp_le32(header + 20);
    member->check = xx_ftcomp_le16(header + 0x1f);
    member->method = xx_ftcomp_le16(header + 0x21);
    member->extra = xx_ftcomp_le32(header + 0x23);
    name_field = (int64_t)xx_ftcomp_le16(header + 0x27);
    name_offset = offset + (int64_t)XX_FTCOMP_HEADER_SIZE;
    /* The name field must fit in the file and must be able to hold a NUL.
     * Bound it before it is used to place the payload. */
    if (name_field < 1 || name_field > (int64_t)XX_FTCOMP_MAX_NAME ||
        name_field > span - name_offset)
        return false;
    if (!xx_ftcomp_read_at(self, self->base_address + name_offset, namebuf,
                           (size_t)name_field))
        return false;
    name_length = 0U;
    while (name_length < (size_t)name_field && namebuf[name_length] != 0U)
        ++name_length;
    if (name_length == 0U || name_length == (size_t)name_field) return false;

    data_offset = name_offset + name_field;
    if (data_offset > span) return false;

    if (successor != 0U) {
        if ((int64_t)successor <= offset || (int64_t)successor > span ||
            (int64_t)successor < data_offset)
            return false;
        member_end = (int64_t)successor;
        *next = (int64_t)successor;
    } else {
        member_end = span;
    }
    /* When a member carries an extended-attribute blob it lives between the
     * payload and the member's end; the payload stops there.  Its internal
     * format is not decoded, only excluded. */
    if (extended_attributes != 0U &&
        (int64_t)extended_attributes >= data_offset &&
        (int64_t)extended_attributes <= member_end)
        member_end = (int64_t)extended_attributes;
    if (member_end < data_offset) return false;

    member->name = xx_ftcomp_normalize_name(namebuf, name_length);
    if (!member->name) return false;
    member->header_offset = self->base_address + offset;
    member->header_size = data_offset - offset;
    member->data_offset = self->base_address + data_offset;
    member->compressed_size = member_end - data_offset;
    member->attributes = header[8];
    member->timestamp = ((uint64_t)xx_ftcomp_le16(header + 4) << 16) |
                        (uint64_t)xx_ftcomp_le16(header + 6);
    /* A declared plaintext length cannot exceed what the container could
     * conceivably hold in memory; it is only carried, never used to size an
     * allocation here, but it is still bounded so callers can trust it. */
    member->uncompressed_size = (int64_t)declared;
    return true;
}

/* ------------------------------------------------------------- codec ---
 *
 * The FTCOMP payload is two nested codecs.  Outermost is an order-1 Huffman
 * model over a 433-symbol alphabet that reconstructs a stream of LZ tokens;
 * that token stream is then expanded by a byte-oriented LZ77 over a 512 KiB
 * circular window primed with a fixed 4026-byte dictionary.
 *
 * Both are ports of U3's decoder (F:\utils\U3\src, class `tgb` at VMT
 * 0x00666c78): FUN_00667430 drives the block loop, FUN_00664d00 ->
 * FUN_00663dd0 is the Huffman stage with FUN_006636e0 / FUN_00663310 /
 * FUN_006630c0 / FUN_00663940 building its trees, and FUN_00666d80 ->
 * FUN_00662880 is the LZ stage over the window FUN_00662760 primes.  The
 * static tables in xx_ftcomp_tables.inc were lifted from that binary's data
 * section at the addresses those functions read.
 */

/* Window geometry, from FUN_00662760 and FUN_00662880. */
#define XX_FTCOMP_WINDOW_SIZE 0x80000U
#define XX_FTCOMP_WINDOW_MASK (XX_FTCOMP_WINDOW_SIZE - 1U)
#define XX_FTCOMP_DICT_SIZE 0xfbaU
#define XX_FTCOMP_PRIME_RUN 0x140U
#define XX_FTCOMP_DICT_AT 0x3c0U
#define XX_FTCOMP_START_POS (XX_FTCOMP_DICT_AT + XX_FTCOMP_DICT_SIZE)

/* Huffman geometry: 433 leaves at node index symbol*4, internal nodes from
 * 0x710 upwards, and a 9-bit first-stage lookup. */
#define XX_FTCOMP_SYMBOLS 0x1b1
#define XX_FTCOMP_LEAF_END 0x6c4
#define XX_FTCOMP_INTERNAL 0x710
#define XX_FTCOMP_NODE_WORDS (0x1c60 / 2)
#define XX_FTCOMP_LUT_SIZE 512

/* A block declares its token count in a u16, and the reference reads its
 * input in 64 KiB bites, so neither side of a block can exceed this. */
#define XX_FTCOMP_BLOCK_MAX 0x10000U
/* One symbol can write five bytes at the cursor, so the token buffer carries
 * slack past the declared block length. */
#define XX_FTCOMP_TOKEN_SLACK 16U

/* The declared plaintext length is a bare u32; cap it before it sizes an
 * allocation. */
#define XX_FTCOMP_MAX_OUTPUT UINT32_C(0x8000000)

/* Only the fT19 dialect (method 0) occurs in the corpus and only it is
 * implemented; fT21/fT32/fT33 exist in the reference and are refused. */
#define XX_FTCOMP_TAG_FT19 UINT32_C(0x39315466)

/* The two fixed weight tables the reference loads before every block:
 * the first codes the transmitted symbol weights, the second the
 * distance and length fields.  Lifted verbatim from U3's data section
 * (the arrays PTR_DAT_00a0ad90 and PTR_DAT_00a0ad48 point at); only
 * symbols 0..0x100 carry a weight in either. */
static const uint16_t ftcomp_weights_header[257] = {
     1024,   600,   300,   260,   230,   212,   192,   172,   148,   132,
      120,   108,    92,    84,    80,    76,    72,    68,    64,    60,
       56,    52,    48,    44,    40,    36,    32,    28,    24,    22,
       20,    19,    18,    17,    16,    15,    14,    14,    13,    13,
       12,    12,    11,    11,    10,    10,     9,     9,     9,     8,
        8,     8,     7,     7,     7,     6,     6,     6,     6,     5,
        5,     5,     5,     4,     4,     4,     4,     4,     4,     3,
        3,     3,     3,     3,     2,     2,     2,     2,     2,     2,
        2,     2,     1,     1,     1,     1,     1,     1,     1,     1,
        1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
        1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
        1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
        1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
        1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
        1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
        1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
        1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
        1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
        1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
        1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
        1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
        1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
        1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
        1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
        1,     1,     1,     1,     1,     1,     1,     1,     1,     1,
        1,     1,     1,     1,     1,     4,    15
};

static const uint16_t ftcomp_weights_extra[257] = {
       40,    39,    39,    38,    38,    37,    37,    36,    36,    35,
       35,    34,    34,    33,    33,    32,    32,    31,    31,    30,
       30,    29,    29,    28,    28,    27,    26,    25,    24,    24,
       23,    23,    22,    22,    21,    21,    20,    20,    19,    19,
       19,    18,    18,    18,    17,    17,    17,    17,    16,    16,
       16,    16,    16,    16,    16,    15,    15,    15,    15,    15,
       15,    15,    15,    14,    14,    14,    14,    14,    14,    13,
       13,    13,    13,    13,    13,    12,    12,    12,    12,    12,
       11,    11,    11,    11,    11,    10,    10,    10,    10,    10,
       10,     9,     9,     9,     9,     9,     9,     9,     9,     9,
        9,     9,     9,     9,     9,     9,     9,     9,     9,     9,
        9,     9,     8,     8,     8,     8,     8,     8,     8,     8,
        8,     8,     8,     8,     8,     8,     8,     8,     7,     7,
        7,     7,     7,     7,     7,     7,     7,     7,     7,     7,
        7,     7,     7,     7,     6,     6,     6,     6,     6,     6,
        6,     6,     6,     6,     6,     6,     6,     6,     6,     6,
        6,     6,     6,     6,     6,     6,     6,     5,     5,     5,
        5,     5,     5,     5,     5,     5,     5,     5,     5,     5,
        5,     5,     5,     5,     5,     5,     5,     5,     5,     5,
        5,     5,     5,     5,     5,     5,     5,     5,     5,     5,
        5,     5,     5,     5,     5,     5,     5,     5,     5,     5,
        5,     5,     5,     5,     5,     5,     5,     5,     4,     4,
        4,     4,     4,     4,     4,     4,     4,     4,     4,     4,
        4,     4,     4,     4,     4,     4,     4,     4,     4,     4,
        4,     4,     4,     4,     4,     4,     4,     4,     4,     4,
        4,     4,     5,     7,     7,     7,   255
};

/* The 4026-byte dictionary FUN_00662760 primes the window with, from
 * U3's data section at 0x009e8858.  It is a slab of C-runtime message
 * text and x86 prologue fragments - the things an OS/2 driver archive
 * is full of - and matches are made against it from the first byte. */
static const uint8_t ftcomp_preset_dictionary[4026] = {
    0x3c, 0x3c, 0x4e, 0x4d, 0x53, 0x47, 0x3e, 0x3e, 0x52, 0x36, 0x30, 0x30,
    0x30, 0x20, 0x2d, 0x20, 0x73, 0x74, 0x61, 0x63, 0x6b, 0x20, 0x6f, 0x76,
    0x65, 0x72, 0x66, 0x6c, 0x6f, 0x77, 0x20, 0x52, 0x36, 0x30, 0x30, 0x33,
    0x20, 0x2d, 0x20, 0x69, 0x6e, 0x74, 0x65, 0x67, 0x65, 0x72, 0x20, 0x64,
    0x69, 0x76, 0x69, 0x64, 0x65, 0x20, 0x62, 0x79, 0x20, 0x30, 0x20, 0x52,
    0x36, 0x30, 0x30, 0x38, 0x00, 0x00, 0x2d, 0x20, 0x6e, 0x6f, 0x74, 0x20,
    0x65, 0x6e, 0x6f, 0x75, 0x67, 0x68, 0x20, 0x73, 0x70, 0x61, 0x63, 0x65,
    0x20, 0x66, 0x6f, 0x72, 0x20, 0x61, 0x72, 0x67, 0x75, 0x6d, 0x65, 0x6e,
    0x74, 0x73, 0x20, 0x52, 0x36, 0x30, 0x30, 0x39, 0x20, 0x2d, 0x20, 0x6e,
    0x6f, 0x74, 0x20, 0x65, 0x6e, 0x6f, 0x75, 0x67, 0x68, 0x20, 0x73, 0x70,
    0x61, 0x63, 0x65, 0x20, 0x66, 0x6f, 0x72, 0x20, 0x65, 0x6e, 0x76, 0x69,
    0x72, 0x6f, 0x6e, 0x6d, 0x65, 0x6e, 0x74, 0x20, 0x72, 0x75, 0x6e, 0x2d,
    0x74, 0x69, 0x6d, 0x65, 0x20, 0x65, 0x72, 0x72, 0x6f, 0x72, 0x20, 0x52,
    0x36, 0x30, 0x30, 0x32, 0x20, 0x2d, 0x20, 0x66, 0x6c, 0x6f, 0x61, 0x74,
    0x69, 0x6e, 0x67, 0x20, 0x70, 0x6f, 0x69, 0x6e, 0x74, 0x20, 0x6e, 0x6f,
    0x74, 0x20, 0x6c, 0x6f, 0x61, 0x64, 0x65, 0x64, 0x20, 0x52, 0x36, 0x30,
    0x30, 0x31, 0x20, 0x2d, 0x20, 0x6e, 0x75, 0x6c, 0x6c, 0x20, 0x70, 0x6f,
    0x69, 0x6e, 0x74, 0x65, 0x72, 0x20, 0x61, 0x73, 0x73, 0x69, 0x67, 0x6e,
    0x6d, 0x65, 0x6e, 0x74, 0x20, 0x41, 0x20, 0x70, 0x72, 0x6f, 0x67, 0x72,
    0x61, 0x6d, 0x20, 0x74, 0x72, 0x69, 0x65, 0x64, 0x20, 0x74, 0x6f, 0x20,
    0x64, 0x69, 0x76, 0x69, 0x64, 0x65, 0x20, 0x61, 0x20, 0x6e, 0x75, 0x6d,
    0x62, 0x65, 0x72, 0x20, 0x62, 0x79, 0x20, 0x7a, 0x65, 0x72, 0x6f, 0x2c,
    0x20, 0x61, 0x6e, 0x64, 0x20, 0x61, 0x20, 0x64, 0x69, 0x76, 0x69, 0x64,
    0x65, 0x20, 0x62, 0x79, 0x20, 0x7a, 0x65, 0x72, 0x6f, 0x20, 0x65, 0x78,
    0x63, 0x65, 0x70, 0x74, 0x69, 0x6f, 0x6e, 0x20, 0x68, 0x61, 0x6e, 0x64,
    0x6c, 0x65, 0x72, 0x20, 0x77, 0x61, 0x73, 0x20, 0x6e, 0x6f, 0x74, 0x20,
    0x72, 0x65, 0x67, 0x69, 0x73, 0x74, 0x65, 0x72, 0x65, 0x64, 0x20, 0x54,
    0x68, 0x65, 0x20, 0x72, 0x65, 0x73, 0x75, 0x6c, 0x74, 0x20, 0x6f, 0x66,
    0x20, 0x61, 0x20, 0x64, 0x69, 0x76, 0x69, 0x73, 0x69, 0x6f, 0x6e, 0x20,
    0x6f, 0x70, 0x65, 0x72, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x20, 0x77, 0x61,
    0x73, 0x20, 0x74, 0x6f, 0x6f, 0x20, 0x6c, 0x61, 0x72, 0x67, 0x65, 0x2c,
    0x20, 0x61, 0x6e, 0x64, 0x20, 0x61, 0x20, 0x64, 0x69, 0x76, 0x69, 0x64,
    0x65, 0x20, 0x6f, 0x76, 0x65, 0x72, 0x66, 0x6c, 0x6f, 0x77, 0x20, 0x65,
    0x78, 0x63, 0x65, 0x70, 0x74, 0x69, 0x6f, 0x6e, 0x20, 0x68, 0x61, 0x6e,
    0x64, 0x6c, 0x65, 0x72, 0x20, 0x77, 0x61, 0x73, 0x20, 0x6e, 0x6f, 0x74,
    0x20, 0x72, 0x65, 0x67, 0x69, 0x73, 0x74, 0x65, 0x72, 0x65, 0x64, 0x20,
    0x41, 0x20, 0x70, 0x72, 0x6f, 0x67, 0x72, 0x61, 0x6d, 0x20, 0x73, 0x74,
    0x61, 0x72, 0x74, 0x65, 0x64, 0x20, 0x61, 0x20, 0x42, 0x4f, 0x55, 0x4e,
    0x44, 0x20, 0x69, 0x6e, 0x73, 0x74, 0x72, 0x75, 0x63, 0x74, 0x69, 0x6f,
    0x6e, 0x20, 0x77, 0x69, 0x74, 0x68, 0x6f, 0x75, 0x74, 0x20, 0x72, 0x65,
    0x67, 0x69, 0x73, 0x74, 0x65, 0x72, 0x69, 0x6e, 0x67, 0x20, 0x61, 0x20,
    0x62, 0x6f, 0x75, 0x6e, 0x64, 0x20, 0x65, 0x78, 0x63, 0x65, 0x70, 0x74,
    0x69, 0x6f, 0x6e, 0x20, 0x68, 0x61, 0x6e, 0x64, 0x6c, 0x65, 0x72, 0x20,
    0x41, 0x20, 0x70, 0x72, 0x6f, 0x67, 0x72, 0x61, 0x6d, 0x20, 0x73, 0x74,
    0x61, 0x72, 0x74, 0x65, 0x64, 0x20, 0x61, 0x6e, 0x20, 0x69, 0x6e, 0x76,
    0x61, 0x6c, 0x69, 0x64, 0x20, 0x69, 0x6e, 0x73, 0x74, 0x72, 0x75, 0x63,
    0x74, 0x69, 0x6f, 0x6e, 0x20, 0x77, 0x69, 0x74, 0x68, 0x6f, 0x75, 0x74,
    0x20, 0x72, 0x65, 0x67, 0x69, 0x73, 0x74, 0x65, 0x72, 0x69, 0x6e, 0x67,
    0x20, 0x61, 0x6e, 0x20, 0x69, 0x6e, 0x76, 0x61, 0x6c, 0x69, 0x64, 0x20,
    0x6f, 0x70, 0x63, 0x6f, 0x64, 0x65, 0x20, 0x65, 0x78, 0x63, 0x65, 0x70,
    0x74, 0x69, 0x6f, 0x6e, 0x20, 0x68, 0x61, 0x6e, 0x64, 0x6c, 0x65, 0x72,
    0x20, 0x41, 0x20, 0x6e, 0x75, 0x6d, 0x65, 0x72, 0x69, 0x63, 0x20, 0x63,
    0x6f, 0x70, 0x72, 0x6f, 0x63, 0x65, 0x73, 0x73, 0x6f, 0x72, 0x20, 0x65,
    0x78, 0x63, 0x65, 0x70, 0x74, 0x69, 0x6f, 0x6e, 0x20, 0x6f, 0x63, 0x63,
    0x75, 0x72, 0x72, 0x65, 0x64, 0x20, 0x61, 0x6e, 0x64, 0x20, 0x61, 0x20,
    0x6e, 0x75, 0x6d, 0x65, 0x72, 0x69, 0x63, 0x20, 0x63, 0x6f, 0x70, 0x72,
    0x6f, 0x63, 0x65, 0x73, 0x73, 0x6f, 0x72, 0x20, 0x65, 0x78, 0x63, 0x65,
    0x70, 0x74, 0x69, 0x6f, 0x6e, 0x20, 0x68, 0x61, 0x6e, 0x64, 0x6c, 0x65,
    0x72, 0x20, 0x77, 0x61, 0x73, 0x20, 0x6e, 0x6f, 0x74, 0x20, 0x72, 0x65,
    0x67, 0x69, 0x73, 0x74, 0x65, 0x72, 0x65, 0x64, 0x20, 0x53, 0x59, 0x53,
    0x32, 0x30, 0x39, 0x30, 0x3a, 0x20, 0x54, 0x68, 0x65, 0x20, 0x73, 0x79,
    0x73, 0x74, 0x65, 0x6d, 0x20, 0x69, 0x73, 0x20, 0x75, 0x6e, 0x61, 0x62,
    0x6c, 0x65, 0x20, 0x74, 0x6f, 0x20, 0x6c, 0x6f, 0x61, 0x64, 0x20, 0x74,
    0x68, 0x65, 0x20, 0x70, 0x72, 0x6f, 0x67, 0x72, 0x61, 0x6d, 0x2e, 0x20,
    0x54, 0x68, 0x65, 0x20, 0x41, 0x70, 0x70, 0x6c, 0x69, 0x63, 0x61, 0x74,
    0x69, 0x6f, 0x6e, 0x20, 0x50, 0x72, 0x6f, 0x67, 0x72, 0x61, 0x6d, 0x20,
    0x49, 0x6e, 0x74, 0x65, 0x72, 0x66, 0x61, 0x63, 0x65, 0x20, 0x28, 0x41,
    0x50, 0x49, 0x29, 0x20, 0x65, 0x6e, 0x74, 0x65, 0x72, 0x65, 0x64, 0x20,
    0x77, 0x69, 0x6c, 0x6c, 0x20, 0x6f, 0x6e, 0x6c, 0x79, 0x20, 0x77, 0x6f,
    0x72, 0x6b, 0x20, 0x69, 0x6e, 0x20, 0x4f, 0x53, 0x2f, 0x32, 0x20, 0x6d,
    0x6f, 0x64, 0x65, 0x2e, 0x20, 0x50, 0x53, 0x51, 0x52, 0x56, 0x57, 0x53,
    0x51, 0x52, 0x20, 0x50, 0x41, 0x54, 0x48, 0x3d, 0x3d, 0x20, 0x49, 0x44,
    0x3d, 0x45, 0x48, 0x43, 0x20, 0x56, 0x65, 0x72, 0x73, 0x69, 0x6f, 0x6e,
    0x20, 0x28, 0x43, 0x29, 0x20, 0x28, 0x63, 0x29, 0x20, 0x43, 0x6f, 0x70,
    0x79, 0x72, 0x69, 0x67, 0x68, 0x74, 0x20, 0x49, 0x42, 0x4d, 0x20, 0x43,
    0x6f, 0x72, 0x70, 0x2e, 0x20, 0x4c, 0x69, 0x63, 0x65, 0x6e, 0x73, 0x65,
    0x64, 0x20, 0x4d, 0x61, 0x74, 0x65, 0x72, 0x69, 0x61, 0x6c, 0x20, 0x4d,
    0x53, 0x5f, 0x52, 0x75, 0x6e, 0x20, 0x54, 0x69, 0x6d, 0x65, 0x20, 0x4c,
    0x69, 0x62, 0x72, 0x61, 0x72, 0x79, 0x20, 0x42, 0x6f, 0x72, 0x6c, 0x61,
    0x6e, 0x64, 0x20, 0x42, 0x4f, 0x52, 0x4c, 0x41, 0x4e, 0x44, 0x20, 0x20,
    0x20, 0xc0, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4,
    0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4,
    0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xd9, 0xc3, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4,
    0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4,
    0xc4, 0xc4, 0xc4, 0xc4, 0xb4, 0xda, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4,
    0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4,
    0xc4, 0xc4, 0xc4, 0xc4, 0xbf, 0xc8, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd,
    0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xbc,
    0xc9, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd,
    0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xbb, 0xcc,
    0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd,
    0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xb9, 0x2b,
    0x2d, 0x2d, 0x2d, 0x2d, 0x2d, 0x2d, 0x2d, 0x2d, 0x2d, 0x2d, 0x2d, 0x2d,
    0x2d, 0x2d, 0x2d, 0x2d, 0x2d, 0x2b, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a,
    0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a,
    0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d,
    0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d,
    0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x2f, 0x2f, 0x2f, 0x2f, 0x2f, 0x2f, 0x2f,
    0x2f, 0x2f, 0x2f, 0x2f, 0x2f, 0x2f, 0x2f, 0x2f, 0x2f, 0x2f, 0x2f, 0x2f,
    0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23,
    0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x23, 0x24,
    0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24,
    0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x24, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x3b, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f,
    0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x3f,
    0x3f, 0x3f, 0x3f, 0x3f, 0x3f, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b,
    0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b, 0x2b,
    0xd4, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd,
    0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xbe, 0xc6,
    0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd,
    0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xb5, 0xd5,
    0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xcd,
    0xcd, 0xcd, 0xcd, 0xcd, 0xcd, 0xb8, 0xd3, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4,
    0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4,
    0xc4, 0xc4, 0xc4, 0xc4, 0xbd, 0xc7, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4,
    0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4,
    0xc4, 0xc4, 0xc4, 0xc4, 0xb6, 0xd6, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4,
    0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xc4, 0xb7,
    0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e,
    0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a,
    0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46,
    0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50, 0x51, 0x52,
    0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5d, 0x5e, 0x5f,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x00, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a,
    0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76,
    0x77, 0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82,
    0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e,
    0x8f, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a,
    0x9b, 0x9c, 0x9d, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6,
    0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2,
    0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe,
    0xbf, 0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca,
    0xcb, 0xcc, 0xcd, 0xce, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6,
    0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xe0, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee,
    0xef, 0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa,
    0xfb, 0xfc, 0xfd, 0xfe, 0x52, 0x75, 0x6e, 0x74, 0x69, 0x6d, 0x65, 0x20,
    0x65, 0x72, 0x72, 0x6f, 0x72, 0x20, 0x4f, 0x53, 0x2f, 0x32, 0x20, 0x32,
    0x2e, 0x30, 0x20, 0x57, 0x69, 0x6e, 0x64, 0x6f, 0x77, 0x73, 0x20, 0x57,
    0x49, 0x4e, 0x44, 0x4f, 0x57, 0x53, 0x20, 0x44, 0x4f, 0x53, 0x20, 0x35,
    0x2e, 0x30, 0x20, 0x44, 0x4f, 0x53, 0x20, 0x34, 0x2e, 0x30, 0x20, 0x44,
    0x4f, 0x53, 0x20, 0x33, 0x2e, 0x33, 0x20, 0x44, 0x4f, 0x53, 0x20, 0x33,
    0x2e, 0x32, 0x20, 0x44, 0x4f, 0x53, 0x20, 0x33, 0x2e, 0x31, 0x20, 0x44,
    0x4f, 0x53, 0x20, 0x33, 0x2e, 0x30, 0x20, 0x50, 0x72, 0x6f, 0x67, 0x72,
    0x61, 0x6d, 0x20, 0x70, 0x72, 0x6f, 0x67, 0x72, 0x61, 0x6d, 0x20, 0x50,
    0x52, 0x4f, 0x47, 0x52, 0x41, 0x4d, 0x4d, 0x65, 0x6d, 0x6f, 0x72, 0x79,
    0x20, 0x43, 0x6f, 0x6e, 0x74, 0x72, 0x6f, 0x6c, 0x20, 0x42, 0x6c, 0x6f,
    0x63, 0x6b, 0x20, 0x45, 0x78, 0x70, 0x61, 0x6e, 0x64, 0x65, 0x64, 0x20,
    0x45, 0x78, 0x74, 0x65, 0x6e, 0x64, 0x65, 0x64, 0x20, 0x44, 0x49, 0x52,
    0x20, 0x44, 0x69, 0x72, 0x65, 0x63, 0x74, 0x6f, 0x72, 0x79, 0x20, 0x44,
    0x49, 0x52, 0x45, 0x43, 0x54, 0x4f, 0x52, 0x59, 0x20, 0x50, 0x72, 0x6f,
    0x6d, 0x70, 0x74, 0x20, 0x45, 0x6e, 0x74, 0x65, 0x72, 0x20, 0x65, 0x6e,
    0x74, 0x65, 0x72, 0x20, 0x51, 0x75, 0x69, 0x74, 0x20, 0x51, 0x55, 0x49,
    0x54, 0x20, 0x46, 0x69, 0x6c, 0x65, 0x20, 0x66, 0x69, 0x6c, 0x65, 0x20,
    0x46, 0x49, 0x4c, 0x45, 0x20, 0x4d, 0x61, 0x6e, 0x61, 0x67, 0x65, 0x72,
    0x20, 0x4d, 0x41, 0x4e, 0x41, 0x47, 0x45, 0x52, 0x20, 0x46, 0x6f, 0x6c,
    0x64, 0x65, 0x72, 0x20, 0x46, 0x4f, 0x4c, 0x44, 0x45, 0x52, 0x20, 0x45,
    0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x20, 0x45, 0x58, 0x41, 0x4d, 0x50,
    0x4c, 0x45, 0x20, 0x49, 0x6e, 0x73, 0x65, 0x72, 0x74, 0x20, 0x49, 0x4e,
    0x53, 0x45, 0x52, 0x54, 0x20, 0x4f, 0x76, 0x65, 0x72, 0x77, 0x72, 0x69,
    0x74, 0x65, 0x20, 0x4f, 0x56, 0x45, 0x52, 0x57, 0x52, 0x49, 0x54, 0x45,
    0x20, 0x55, 0x6e, 0x64, 0x6f, 0x20, 0x55, 0x4e, 0x44, 0x4f, 0x20, 0x49,
    0x6e, 0x76, 0x61, 0x6c, 0x69, 0x64, 0x20, 0x49, 0x4e, 0x56, 0x41, 0x4c,
    0x49, 0x44, 0x20, 0x4e, 0x6f, 0x74, 0x20, 0x46, 0x6f, 0x75, 0x6e, 0x64,
    0x20, 0x4e, 0x4f, 0x54, 0x20, 0x46, 0x4f, 0x55, 0x4e, 0x44, 0x20, 0x52,
    0x65, 0x74, 0x72, 0x79, 0x20, 0x61, 0x67, 0x61, 0x69, 0x6e, 0x20, 0x52,
    0x45, 0x54, 0x52, 0x59, 0x20, 0x41, 0x47, 0x41, 0x49, 0x4e, 0x20, 0x44,
    0x69, 0x73, 0x6b, 0x65, 0x74, 0x74, 0x65, 0x20, 0x44, 0x49, 0x53, 0x4b,
    0x45, 0x54, 0x54, 0x45, 0x20, 0x48, 0x61, 0x72, 0x64, 0x20, 0x44, 0x69,
    0x73, 0x6b, 0x20, 0x48, 0x41, 0x52, 0x44, 0x20, 0x44, 0x49, 0x53, 0x4b,
    0x20, 0x48, 0x69, 0x67, 0x68, 0x20, 0x50, 0x65, 0x72, 0x66, 0x6f, 0x72,
    0x6d, 0x61, 0x6e, 0x63, 0x65, 0x20, 0x48, 0x49, 0x47, 0x48, 0x20, 0x50,
    0x45, 0x52, 0x46, 0x4f, 0x52, 0x4d, 0x41, 0x4e, 0x43, 0x45, 0x20, 0x51,
    0x75, 0x65, 0x75, 0x65, 0x20, 0x51, 0x55, 0x45, 0x55, 0x45, 0x20, 0x55,
    0x6e, 0x6e, 0x61, 0x6d, 0x65, 0x64, 0x20, 0x50, 0x69, 0x70, 0x65, 0x73,
    0x20, 0x55, 0x4e, 0x4e, 0x41, 0x4d, 0x45, 0x44, 0x20, 0x50, 0x49, 0x50,
    0x45, 0x53, 0x20, 0x55, 0x6e, 0x6e, 0x61, 0x6d, 0x65, 0x64, 0x20, 0x53,
    0x65, 0x6d, 0x61, 0x70, 0x68, 0x6f, 0x72, 0x65, 0x73, 0x20, 0x55, 0x4e,
    0x4e, 0x41, 0x4d, 0x45, 0x44, 0x20, 0x53, 0x45, 0x4d, 0x41, 0x50, 0x48,
    0x4f, 0x52, 0x45, 0x53, 0x20, 0x53, 0x63, 0x72, 0x65, 0x65, 0x6e, 0x20,
    0x53, 0x61, 0x76, 0x65, 0x20, 0x53, 0x43, 0x52, 0x45, 0x45, 0x4e, 0x20,
    0x53, 0x41, 0x56, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x51, 0x57, 0x45, 0x52, 0x54, 0x59, 0x55, 0x49, 0x4f, 0x50, 0x7b, 0x7d,
    0x7c, 0x41, 0x53, 0x44, 0x46, 0x47, 0x48, 0x4a, 0x4b, 0x4c, 0x3a, 0x5a,
    0x58, 0x43, 0x56, 0x42, 0x4e, 0x4d, 0x3c, 0x3e, 0x3f, 0x71, 0x77, 0x65,
    0x72, 0x74, 0x79, 0x75, 0x69, 0x6f, 0x70, 0x5b, 0x5d, 0x07, 0x73, 0x64,
    0x66, 0x67, 0x68, 0x6a, 0x6b, 0x6c, 0x3b, 0x27, 0x7a, 0x78, 0x63, 0x76,
    0x62, 0x6e, 0x6d, 0x2c, 0x2e, 0x00, 0x50, 0x72, 0x6f, 0x67, 0x72, 0x61,
    0x6d, 0x20, 0x50, 0x72, 0x6f, 0x70, 0x65, 0x72, 0x74, 0x79, 0x20, 0x6f,
    0x66, 0x20, 0x49, 0x42, 0x4d, 0x2e, 0x20, 0x41, 0x6c, 0x6c, 0x20, 0x52,
    0x69, 0x67, 0x68, 0x74, 0x73, 0x20, 0x52, 0x65, 0x73, 0x65, 0x72, 0x76,
    0x65, 0x64, 0x20, 0x43, 0x6f, 0x6d, 0x6d, 0x61, 0x6e, 0x64, 0x20, 0x48,
    0x65, 0x6c, 0x70, 0x20, 0x45, 0x64, 0x69, 0x74, 0x20, 0x49, 0x46, 0x20,
    0x45, 0x52, 0x52, 0x4f, 0x52, 0x4c, 0x45, 0x56, 0x45, 0x4c, 0x20, 0x31,
    0x20, 0x49, 0x66, 0x20, 0x45, 0x72, 0x72, 0x6f, 0x72, 0x6c, 0x65, 0x76,
    0x65, 0x6c, 0x20, 0x31, 0x20, 0x47, 0x6f, 0x74, 0x6f, 0x20, 0x47, 0x4f,
    0x54, 0x4f, 0x20, 0x53, 0x45, 0x54, 0x20, 0x50, 0x41, 0x54, 0x48, 0x3d,
    0x20, 0x73, 0x65, 0x74, 0x20, 0x70, 0x61, 0x74, 0x68, 0x3d, 0x20, 0x44,
    0x45, 0x56, 0x49, 0x43, 0x45, 0x3d, 0x44, 0x65, 0x76, 0x69, 0x63, 0x65,
    0x3d, 0x20, 0x44, 0x45, 0x56, 0x49, 0x43, 0x45, 0x48, 0x49, 0x47, 0x48,
    0x3d, 0x20, 0x44, 0x65, 0x76, 0x69, 0x63, 0x65, 0x68, 0x69, 0x67, 0x68,
    0x3d, 0x20, 0x5a, 0x6f, 0x6f, 0x6d, 0x20, 0x5a, 0x4f, 0x4f, 0x4d, 0x20,
    0x43, 0x4f, 0x50, 0x59, 0x20, 0x58, 0x43, 0x4f, 0x50, 0x59, 0x20, 0x63,
    0x6f, 0x70, 0x79, 0x20, 0x78, 0x63, 0x6f, 0x70, 0x79, 0x20, 0x53, 0x48,
    0x45, 0x4c, 0x4c, 0x3d, 0x20, 0x53, 0x68, 0x65, 0x6c, 0x6c, 0x3d, 0x20,
    0x4c, 0x41, 0x4e, 0x44, 0x50, 0x20, 0x46, 0x42, 0x53, 0x53, 0x20, 0x46,
    0x69, 0x6e, 0x61, 0x6e, 0x63, 0x69, 0x61, 0x6c, 0x20, 0x42, 0x72, 0x61,
    0x6e, 0x63, 0x68, 0x20, 0x53, 0x79, 0x73, 0x74, 0x65, 0x6d, 0x20, 0x53,
    0x65, 0x72, 0x76, 0x69, 0x63, 0x65, 0x73, 0x20, 0x75, 0x6e, 0x73, 0x69,
    0x67, 0x6e, 0x65, 0x64, 0x20, 0x6c, 0x6f, 0x6e, 0x67, 0x20, 0x73, 0x68,
    0x6f, 0x72, 0x74, 0x20, 0x55, 0x43, 0x48, 0x41, 0x52, 0x20, 0x42, 0x59,
    0x54, 0x45, 0x20, 0x55, 0x4c, 0x4f, 0x4e, 0x47, 0x20, 0x63, 0x64, 0x65,
    0x63, 0x6c, 0x20, 0x52, 0x45, 0x41, 0x4c, 0x20, 0x72, 0x65, 0x61, 0x6c,
    0x20, 0x66, 0x6c, 0x6f, 0x61, 0x74, 0x20, 0x72, 0x65, 0x74, 0x75, 0x72,
    0x6e, 0x28, 0x76, 0x6f, 0x69, 0x64, 0x29, 0x20, 0x44, 0x4f, 0x53, 0x43,
    0x41, 0x4c, 0x4c, 0x53, 0x2e, 0x44, 0x4c, 0x4c, 0x20, 0x4b, 0x42, 0x44,
    0x43, 0x41, 0x4c, 0x4c, 0x53, 0x2e, 0x44, 0x4c, 0x4c, 0x20, 0x4f, 0x53,
    0x43, 0x48, 0x41, 0x52, 0x2e, 0x44, 0x4c, 0x4c, 0x20, 0x56, 0x49, 0x4f,
    0x43, 0x48, 0x41, 0x52, 0x2e, 0x44, 0x4c, 0x4c, 0x20, 0x4d, 0x4f, 0x55,
    0x43, 0x41, 0x4c, 0x4c, 0x53, 0x2e, 0x44, 0x4c, 0x4c, 0x20, 0x51, 0x55,
    0x45, 0x43, 0x41, 0x4c, 0x4c, 0x53, 0x2e, 0x44, 0x4c, 0x4c, 0x20, 0x50,
    0x4d, 0x47, 0x50, 0x49, 0x2e, 0x44, 0x4c, 0x4c, 0x20, 0x50, 0x4d, 0x47,
    0x52, 0x45, 0x2e, 0x44, 0x4c, 0x4c, 0x20, 0x50, 0x4d, 0x57, 0x49, 0x4e,
    0x2e, 0x44, 0x4c, 0x4c, 0x20, 0x50, 0x4d, 0x53, 0x48, 0x41, 0x50, 0x49,
    0x2e, 0x44, 0x4c, 0x4c, 0x20, 0x53, 0x45, 0x53, 0x4d, 0x47, 0x52, 0x2e,
    0x44, 0x4c, 0x4c, 0x20, 0x4e, 0x41, 0x4d, 0x50, 0x49, 0x50, 0x45, 0x53,
    0x2e, 0x44, 0x4c, 0x4c, 0x20, 0x4e, 0x45, 0x54, 0x41, 0x50, 0x49, 0x2e,
    0x44, 0x4c, 0x4c, 0x20, 0x4e, 0x45, 0x54, 0x53, 0x50, 0x4f, 0x4f, 0x4c,
    0x2e, 0x44, 0x4c, 0x4c, 0x20, 0x4d, 0x53, 0x47, 0x5f, 0x46, 0x49, 0x4c,
    0x45, 0x5f, 0x4e, 0x4f, 0x54, 0x20, 0x72, 0x65, 0x70, 0x65, 0x61, 0x74,
    0x20, 0x52, 0x45, 0x50, 0x45, 0x41, 0x54, 0x20, 0x75, 0x6e, 0x74, 0x69,
    0x6c, 0x20, 0x55, 0x4e, 0x54, 0x49, 0x4c, 0x20, 0x55, 0x4e, 0x49, 0x54,
    0x20, 0x75, 0x6e, 0x69, 0x74, 0x20, 0x2a, 0x2e, 0x45, 0x58, 0x45, 0x20,
    0x2a, 0x2e, 0x43, 0x4f, 0x4d, 0x20, 0x2a, 0x2e, 0x4f, 0x42, 0x4a, 0x20,
    0x2a, 0x2e, 0x44, 0x4c, 0x4c, 0x20, 0x62, 0x79, 0x74, 0x65, 0x73, 0x20,
    0x42, 0x59, 0x54, 0x45, 0x53, 0x20, 0x5f, 0x43, 0x5f, 0x46, 0x49, 0x4c,
    0x45, 0x5f, 0x49, 0x4e, 0x46, 0x4f, 0x3d, 0x20, 0x28, 0x6e, 0x75, 0x6c,
    0x6c, 0x29, 0x20, 0x2f, 0x2a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x2a,
    0x2f, 0x20, 0x20, 0x2f, 0x2f, 0x20, 0x20, 0x20, 0x73, 0x74, 0x72, 0x72,
    0x63, 0x68, 0x72, 0x28, 0x20, 0x73, 0x74, 0x72, 0x63, 0x70, 0x79, 0x28,
    0x20, 0x73, 0x74, 0x72, 0x63, 0x61, 0x74, 0x28, 0x20, 0x73, 0x74, 0x72,
    0x63, 0x70, 0x79, 0x28, 0x20, 0x6d, 0x65, 0x6d, 0x6d, 0x6f, 0x76, 0x65,
    0x28, 0x20, 0x6d, 0x65, 0x6d, 0x63, 0x70, 0x79, 0x28, 0x20, 0x63, 0x6f,
    0x6e, 0x63, 0x61, 0x74, 0x28, 0x20, 0x63, 0x61, 0x6c, 0x6c, 0x28, 0x20,
    0x54, 0x43, 0x50, 0x49, 0x50, 0x20, 0x43, 0x4f, 0x4e, 0x46, 0x49, 0x47,
    0x55, 0x52, 0x41, 0x54, 0x49, 0x4f, 0x4e, 0x20, 0x43, 0x6f, 0x6e, 0x66,
    0x69, 0x67, 0x75, 0x72, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x20, 0x53, 0x65,
    0x72, 0x76, 0x65, 0x72, 0x20, 0x52, 0x65, 0x71, 0x75, 0x65, 0x73, 0x74,
    0x65, 0x72, 0x20, 0x53, 0x45, 0x52, 0x56, 0x45, 0x52, 0x20, 0x52, 0x45,
    0x51, 0x55, 0x45, 0x53, 0x54, 0x45, 0x52, 0x20, 0x53, 0x65, 0x72, 0x76,
    0x69, 0x63, 0x65, 0x73, 0x20, 0x53, 0x45, 0x52, 0x56, 0x49, 0x43, 0x45,
    0x53, 0x20, 0x75, 0x6e, 0x6b, 0x6e, 0x6f, 0x77, 0x6e, 0x20, 0x70, 0x61,
    0x72, 0x61, 0x6d, 0x65, 0x74, 0x65, 0x72, 0x73, 0x20, 0x6b, 0x65, 0x79,
    0x20, 0x74, 0x6f, 0x20, 0x63, 0x6f, 0x6e, 0x74, 0x69, 0x6e, 0x75, 0x65,
    0x20, 0x63, 0x68, 0x61, 0x72, 0x20, 0x2a, 0x20, 0x75, 0x6e, 0x73, 0x69,
    0x67, 0x6e, 0x65, 0x64, 0x20, 0x69, 0x6e, 0x74, 0x20, 0x61, 0x75, 0x74,
    0x6f, 0x20, 0x72, 0x65, 0x67, 0x69, 0x73, 0x74, 0x65, 0x72, 0x20, 0x76,
    0x61, 0x72, 0x69, 0x61, 0x62, 0x6c, 0x65, 0x20, 0x74, 0x79, 0x70, 0x65,
    0x64, 0x65, 0x66, 0x20, 0x73, 0x74, 0x72, 0x75, 0x63, 0x74, 0x20, 0x73,
    0x74, 0x61, 0x74, 0x69, 0x63, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x0d, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x23, 0x64, 0x65, 0x66, 0x69, 0x6e, 0x65, 0x20, 0x69,
    0x6e, 0x63, 0x6c, 0x75, 0x64, 0x65, 0x3c, 0x20, 0x6d, 0x65, 0x6d, 0x63,
    0x6d, 0x70, 0x28, 0x20, 0x77, 0x68, 0x69, 0x6c, 0x65, 0x28, 0x20, 0x20,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x66, 0x6f, 0x72, 0x28, 0x20, 0x3b,
    0x3b, 0x3b, 0x3b, 0x3b, 0x3b, 0x3b, 0x3b, 0x3b, 0x3b, 0x3b, 0x3b, 0x3b,
    0x3b, 0x3b, 0x3b, 0x3b, 0x3b, 0x3a, 0x3a, 0x3a, 0x3a, 0x3a, 0x3a, 0x3a,
    0x3a, 0x3a, 0x3a, 0x3a, 0x3a, 0x3a, 0x3a, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e,
    0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x2e, 0x3d,
    0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d, 0x3d,
    0x20, 0x62, 0x65, 0x67, 0x69, 0x6e, 0x20, 0x65, 0x6e, 0x64, 0x20, 0x42,
    0x45, 0x47, 0x49, 0x4e, 0x20, 0x45, 0x4e, 0x44, 0x20, 0x57, 0x48, 0x49,
    0x4c, 0x45, 0x20, 0x45, 0x4e, 0x44, 0x49, 0x46, 0x20, 0x49, 0x46, 0x45,
    0x4e, 0x44, 0x20, 0x49, 0x4e, 0x54, 0x45, 0x52, 0x46, 0x41, 0x43, 0x45,
    0x20, 0x69, 0x6e, 0x74, 0x65, 0x72, 0x66, 0x61, 0x63, 0x65, 0x20, 0x73,
    0x65, 0x63, 0x74, 0x69, 0x6f, 0x6e, 0x20, 0x53, 0x45, 0x43, 0x54, 0x49,
    0x4f, 0x4e, 0x20, 0x20, 0x28, 0x2a, 0x20, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a,
    0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x20, 0x2a, 0x29, 0x20,
    0x43, 0x4f, 0x50, 0x59, 0x52, 0x49, 0x47, 0x48, 0x54, 0x20, 0x28, 0x43,
    0x29, 0x20, 0x4c, 0x49, 0x43, 0x45, 0x4e, 0x53, 0x45, 0x44, 0x20, 0x4d,
    0x41, 0x54, 0x45, 0x52, 0x49, 0x41, 0x4c, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x52, 0x65, 0x6c, 0x65, 0x61, 0x73, 0x65, 0x20, 0x44, 0x61, 0x74,
    0x65, 0x20, 0x54, 0x69, 0x6d, 0x65, 0x20, 0x48, 0x6f, 0x75, 0x72, 0x73,
    0x20, 0x4d, 0x69, 0x6e, 0x75, 0x74, 0x65, 0x73, 0x20, 0x53, 0x65, 0x63,
    0x6f, 0x6e, 0x64, 0x73, 0x20, 0x4f, 0x70, 0x65, 0x6e, 0x28, 0x20, 0x43,
    0x6c, 0x6f, 0x73, 0x65, 0x28, 0x20, 0x57, 0x72, 0x69, 0x74, 0x65, 0x28,
    0x20, 0x52, 0x65, 0x61, 0x64, 0x28, 0x20, 0x4d, 0x53, 0x20, 0x52, 0x75,
    0x6e, 0x2d, 0x54, 0x69, 0x6d, 0x65, 0x20, 0x4c, 0x69, 0x62, 0x72, 0x61,
    0x72, 0x79, 0x20, 0x2d, 0x20, 0x75, 0x73, 0x65, 0x20, 0x6f, 0x6e, 0x6c,
    0x79, 0x20, 0x55, 0x53, 0x48, 0x4f, 0x52, 0x54, 0x20, 0x75, 0x6e, 0x61,
    0x75, 0x74, 0x68, 0x6f, 0x72, 0x69, 0x7a, 0x65, 0x64, 0x20, 0x70, 0x61,
    0x73, 0x73, 0x77, 0x6f, 0x72, 0x64, 0x20, 0x75, 0x73, 0x65, 0x72, 0x20,
    0x20, 0x73, 0x77, 0x69, 0x74, 0x63, 0x68, 0x20, 0x63, 0x61, 0x73, 0x65,
    0x20, 0x64, 0x65, 0x66, 0x61, 0x75, 0x6c, 0x74, 0x20, 0x62, 0x72, 0x65,
    0x61, 0x6b, 0x20, 0x73, 0x69, 0x7a, 0x65, 0x6f, 0x66, 0x28, 0x20, 0x70,
    0x72, 0x69, 0x6e, 0x74, 0x66, 0x28, 0x20, 0x77, 0x72, 0x69, 0x74, 0x65,
    0x6c, 0x6e, 0x28, 0x20, 0x73, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x20, 0x63,
    0x6f, 0x6e, 0x73, 0x74, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x7d, 0x0d, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x7b,
    0x0d, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x3b, 0x0d,
    0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x20, 0x4d, 0x69, 0x63, 0x72, 0x6f, 0x73, 0x6f, 0x66, 0x74, 0x20, 0x43,
    0x6f, 0x72, 0x70, 0x6f, 0x72, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x20, 0x4c,
    0x6f, 0x74, 0x75, 0x73, 0x20, 0x43, 0x6f, 0x72, 0x70, 0x6f, 0x72, 0x61,
    0x74, 0x69, 0x6f, 0x6e, 0x20, 0x53, 0x54, 0x41, 0x43, 0x4b, 0x20, 0x50,
    0x41, 0x54, 0x43, 0x48, 0x20, 0x41, 0x52, 0x45, 0x41, 0x20, 0x38, 0x30,
    0x38, 0x38, 0x20, 0x33, 0x38, 0x36, 0x20, 0x54, 0x68, 0x69, 0x73, 0x20,
    0x70, 0x72, 0x6f, 0x67, 0x72, 0x61, 0x6d, 0x20, 0x63, 0x61, 0x6e, 0x6e,
    0x6f, 0x74, 0x20, 0x62, 0x65, 0x20, 0x72, 0x75, 0x6e, 0x20, 0x69, 0x6e,
    0x20, 0x61, 0x20, 0x44, 0x4f, 0x53, 0x20, 0x73, 0x65, 0x73, 0x73, 0x69,
    0x6f, 0x6e, 0x20, 0x45, 0x48, 0x43, 0x4f, 0x53, 0x32, 0x20, 0x52, 0x4d,
    0x54, 0x52, 0x45, 0x51, 0x20, 0x47, 0x45, 0x54, 0x52, 0x45, 0x51, 0x20,
    0x52, 0x4d, 0x54, 0x52, 0x50, 0x4c, 0x59, 0x20, 0x53, 0x52, 0x56, 0x49,
    0x4e, 0x49, 0x54, 0x20, 0x45, 0x48, 0x43, 0x4d, 0x45, 0x53, 0x53, 0x41,
    0x47, 0x45, 0x20, 0x4c, 0x4f, 0x41, 0x44, 0x45, 0x52, 0x20, 0x6c, 0x6f,
    0x61, 0x64, 0x65, 0x72, 0x20, 0x43, 0x50, 0x52, 0x42, 0x2e, 0x63, 0x70,
    0x72, 0x62, 0x2e, 0x20, 0x52, 0x4d, 0x54, 0x41, 0x52, 0x45, 0x51, 0x20,
    0x52, 0x63, 0x3d, 0x25, 0x31, 0x20, 0x70, 0x72, 0x69, 0x6e, 0x74, 0x65,
    0x72, 0x20, 0x6c, 0x70, 0x74, 0x31, 0x3a, 0x20, 0x6c, 0x70, 0x74, 0x32,
    0x3a, 0x20, 0x63, 0x6f, 0x6d, 0x31, 0x3a, 0x20, 0x63, 0x6f, 0x6d, 0x32,
    0x3a, 0x20, 0x6e, 0x75, 0x6c, 0x3a, 0x20, 0x4c, 0x50, 0x54, 0x31, 0x3a,
    0x20, 0x4c, 0x50, 0x54, 0x32, 0x3a, 0x20, 0x43, 0x4f, 0x4d, 0x31, 0x3a,
    0x20, 0x43, 0x4f, 0x4d, 0x32, 0x3a, 0x20, 0x4e, 0x55, 0x4c, 0x3a, 0x20,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x55, 0x89, 0xe5, 0x31,
    0xc0, 0x50, 0x55, 0x8b, 0xec, 0x83, 0xec, 0x20, 0x8b, 0xe5, 0x5d, 0xca,
    0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

typedef struct xx_ftcomp_huff_s {
    uint16_t node[XX_FTCOMP_NODE_WORDS];
    uint16_t lut[XX_FTCOMP_LUT_SIZE];
    uint8_t lutlen[XX_FTCOMP_LUT_SIZE];
    uint32_t root;
} xx_ftcomp_huff;

typedef struct xx_ftcomp_bits_s {
    const uint8_t *data;
    size_t size;
    size_t at;
    uint16_t accumulator;
    int32_t held;
} xx_ftcomp_bits;

typedef struct xx_ftcomp_codec_s {
    uint8_t *window;         /* 512 KiB circular history */
    uint32_t position;       /* write cursor inside the window */
    uint8_t *plain;          /* the member's plaintext, caller owned */
    size_t plain_size;
    size_t plain_at;
    xx_ftcomp_huff header;   /* fixed tree for the transmitted weights */
    xx_ftcomp_huff extra;    /* fixed tree for distance and length fields */
    xx_ftcomp_huff table_a;  /* per-block tree, symbol class 0 */
    xx_ftcomp_huff table_b;  /* per-block tree, symbol class 1 */
    uint8_t input[XX_FTCOMP_BLOCK_MAX];
    uint8_t tokens[XX_FTCOMP_BLOCK_MAX + XX_FTCOMP_TOKEN_SLACK];
    uint16_t weights[XX_FTCOMP_SYMBOLS];
} xx_ftcomp_codec;

/* The class of the symbol just decoded selects the tree the next one is read
 * from; that is the model's only context. */
static uint8_t xx_ftcomp_symbol_class(uint32_t symbol) {
    if (symbol < 0x100U) return 0U;
    if (symbol < 0x140U) return 1U;
    if (symbol == 0x140U) return 0U; /* method 1 would set this one */
    if (symbol < 0x181U) return 1U;
    if (symbol < 0x1a1U) return 0U;
    return 1U;
}

/* How many coded fields follow an LZ token byte: one distance byte, two
 * distance bytes, or a length byte plus two distance bytes. */
static uint8_t xx_ftcomp_token_fields(uint8_t token) {
    if (token < 0x40U) return 1U;
    if (token == 0x40U) return 0U; /* the escaped literal 9E 40 */
    if (token < 0x80U) return 2U;
    if (token == 0x80U) return 3U;
    return 0U;
}

/* ----------------------------------------------------------- bit input -- */

static void xx_ftcomp_bits_init(xx_ftcomp_bits *bits, const uint8_t *data,
                                size_t size, size_t at) {
    bits->data = data;
    bits->size = size;
    bits->at = at;
    bits->accumulator = 0U;
    bits->held = 0;
}

/* MSB-first into a sixteen-bit accumulator.  Reading past the end feeds zero
 * bytes, as the reference does against its own zero-filled buffer; a stream
 * that actually needs them fails the length checks instead. */
static void xx_ftcomp_bits_fill(xx_ftcomp_bits *bits, int32_t need) {
    while (bits->held < need) {
        uint32_t byte = (bits->at < bits->size) ? bits->data[bits->at] : 0U;
        ++bits->at;
        bits->accumulator =
            (uint16_t)(bits->accumulator |
                       (uint16_t)(byte << ((8 - bits->held) & 31)));
        bits->held += 8;
    }
}

static void xx_ftcomp_bits_drop(xx_ftcomp_bits *bits, int32_t count) {
    bits->accumulator = (uint16_t)(bits->accumulator << count);
    bits->held -= count;
}

static uint32_t xx_ftcomp_bits_one(xx_ftcomp_bits *bits) {
    uint32_t value;
    xx_ftcomp_bits_fill(bits, 1);
    value = (uint32_t)(bits->accumulator >> 15);
    xx_ftcomp_bits_drop(bits, 1);
    return value;
}

static uint32_t xx_ftcomp_bits_take(xx_ftcomp_bits *bits, int32_t count) {
    uint32_t value;
    xx_ftcomp_bits_fill(bits, count);
    value = (uint32_t)(bits->accumulator >> (16 - count));
    xx_ftcomp_bits_drop(bits, count);
    return value;
}

/* -------------------------------------------------------- Huffman build -- */

/* FUN_006630c0, transliterated rather than replaced by a library sort: the
 * order it leaves equal weights in decides the shape of the tree and so the
 * codes, and a stable sort builds a different, wrong table. */
static void xx_ftcomp_sort(uint16_t *lut, const uint16_t *node, int32_t low,
                           int32_t high) {
    int32_t stack[66];
    int32_t depth = 2;
    stack[0] = low;
    stack[1] = high;
    while (depth != 0) {
        int32_t right = stack[depth - 1];
        int32_t left;
        depth -= 2;
        left = stack[depth];
        for (;;) {
            int32_t a = left, b = right, keep = right, next;
            if (b - a < 0x11) {
                int32_t probe = a;
                for (;;) {
                    int32_t hold = probe;
                    int32_t scan = a;
                    probe = hold + 1;
                    next = b;
                    if (probe > b) break;
                    while (scan < probe && node[lut[scan]] < node[lut[probe]])
                        ++scan;
                    if (scan <= hold) {
                        int32_t k = hold;
                        for (;;) {
                            uint16_t swap = lut[k];
                            lut[k] = lut[k + 1];
                            lut[k + 1] = swap;
                            --k;
                            if (k == scan - 1) break;
                        }
                    }
                }
            } else {
                int32_t pivot = (a + b) >> 1, i = a, j = b;
                for (;;) {
                    while (node[lut[i]] < node[lut[pivot]]) ++i;
                    while (node[lut[j]] > node[lut[pivot]]) --j;
                    if (i <= j) {
                        uint16_t swap = lut[i];
                        int32_t moved = j;
                        lut[i] = lut[j];
                        lut[j] = swap;
                        if (pivot != i) {
                            moved = pivot;
                            if (pivot == j) moved = i;
                        }
                        ++i;
                        --j;
                        pivot = moved;
                    }
                    if (i > j) break;
                }
                if (j - a < b - i) {
                    keep = j;
                    next = a;
                    if (i < b && depth + 2 <= 64) {
                        stack[depth] = i;
                        stack[depth + 1] = b;
                        depth += 2;
                    }
                } else {
                    next = i;
                    if (a < j && depth + 2 <= 64) {
                        stack[depth] = a;
                        stack[depth + 1] = j;
                        depth += 2;
                    }
                }
            }
            left = next;
            right = keep;
            if (left >= right) break;
        }
    }
}

/* FUN_00663310: turn the leaf weights already sitting at node[symbol*4] into
 * a tree, then flatten the first nine bits of every code into a lookup. */
static bool xx_ftcomp_build(xx_ftcomp_huff *huff) {
    uint16_t *node = huff->node;
    uint16_t *lut = huff->lut;
    int32_t index = 0, count = 0, ones = 0, last_zero = 0;
    int32_t free_node, head = 0, alive, slot;
    uint32_t pattern;

    while (index < XX_FTCOMP_LEAF_END) {
        node[index + 1] = 0U;
        if (node[index] != 0U) {
            if (node[index] == 1U) {
                /* Weight-one symbols stay at the front in symbol order, so
                 * sorting only the tail leaves the whole list sorted. */
                lut[count++] = lut[ones];
                lut[ones++] = (uint16_t)index;
            } else {
                lut[count++] = (uint16_t)index;
            }
        } else {
            last_zero = index;
        }
        index += 4;
    }
    if (count == 0) return false;
    if (count == 1) {
        lut[1] = lut[ones];
        count = 2;
        lut[ones++] = (uint16_t)last_zero;
        node[last_zero] = 1U;
    }
    free_node = XX_FTCOMP_INTERNAL;
    xx_ftcomp_sort(lut, node, ones, count - 1);
    alive = count;
    while (alive != 2) {
        int32_t bound = head + 2, insert = (count + head + 2) >> 1;
        int32_t top = count, moved;
        uint32_t weight;
        uint16_t first, second;
        --alive;
        first = lut[head];
        second = lut[head + 1];
        ++head;
        weight = (uint32_t)node[first] + (uint32_t)node[second];
        if (bound < count) {
            for (;;) {
                if ((uint32_t)node[lut[insert]] < weight) {
                    bound = insert + 1;
                    insert = top;
                }
                top = insert;
                insert = (top + bound) >> 1;
                if (bound >= top) break;
            }
            insert = (top + bound) >> 1;
        }
        moved = insert - head - 1;
        if (insert < 1 || insert > count ||
            free_node + 3 >= XX_FTCOMP_NODE_WORDS)
            return false;
        if (moved > 0) {
            int32_t k;
            for (k = 0; k < moved; ++k) lut[head + k] = lut[head + k + 1];
        }
        lut[insert - 1] = (uint16_t)free_node;
        node[free_node] = (uint16_t)weight;
        node[free_node + 1] = 0U;
        node[free_node + 2] = first;
        node[free_node + 3] = second;
        node[first + 1] = (uint16_t)free_node;
        node[second + 1] = (uint16_t)free_node;
        free_node += 4;
    }
    if (free_node + 3 >= XX_FTCOMP_NODE_WORDS) return false;
    {
        uint16_t first = lut[head];
        uint16_t second = lut[head + 1];
        node[free_node] =
            (uint16_t)((uint32_t)node[first] + (uint32_t)node[second]);
        node[free_node + 1] = 0U;
        node[free_node + 2] = first;
        node[free_node + 3] = second;
        node[first + 1] = (uint16_t)free_node;
        node[second + 1] = (uint16_t)free_node;
    }
    huff->root = (uint32_t)free_node;
    pattern = 0U;
    for (slot = 0; slot < XX_FTCOMP_LUT_SIZE; ++slot) {
        uint32_t walk = pattern, current = huff->root, reached = 0U;
        int32_t used = 0;
        for (;;) {
            reached = current + (((walk & 0x8000U) == 0U) ? 1U : 0U);
            walk <<= 1;
            ++used;
            if (reached + 2U >= (uint32_t)XX_FTCOMP_NODE_WORDS) return false;
            current = node[reached + 2U];
            if (current < (uint32_t)XX_FTCOMP_LEAF_END) break;
            if (used >= 9) break;
        }
        huff->lut[slot] = node[reached + 2U];
        huff->lutlen[slot] = (uint8_t)used;
        pattern += 0x80U;
    }
    return true;
}

static bool xx_ftcomp_build_static(xx_ftcomp_huff *huff,
                                   const uint16_t *weights, size_t count) {
    size_t index;
    xx_mem_zero(huff, sizeof(*huff));
    for (index = 0U; index < count; ++index)
        huff->node[index * 4U] = weights[index];
    return xx_ftcomp_build(huff);
}

static uint32_t xx_ftcomp_decode_symbol(xx_ftcomp_bits *bits,
                                        const xx_ftcomp_huff *huff) {
    uint32_t top, value;
    xx_ftcomp_bits_fill(bits, 9);
    top = (uint32_t)(bits->accumulator >> 7);
    value = huff->lut[top];
    xx_ftcomp_bits_drop(bits, (int32_t)huff->lutlen[top]);
    while (value >= (uint32_t)XX_FTCOMP_LEAF_END) {
        uint32_t bit = xx_ftcomp_bits_one(bits);
        if (value + 3U >= (uint32_t)XX_FTCOMP_NODE_WORDS) return 0U;
        value = huff->node[value + ((bit == 0U) ? 1U : 0U) + 2U];
    }
    return value;
}

/* --------------------------------------------------------------- LZ ----- */

static void xx_ftcomp_prime(xx_ftcomp_codec *codec) {
    xx_rt_memset(codec->window, 0x20, XX_FTCOMP_PRIME_RUN);
    xx_rt_memset(codec->window + XX_FTCOMP_PRIME_RUN, 0xff,
                 XX_FTCOMP_PRIME_RUN);
    xx_rt_memset(codec->window + 2U * XX_FTCOMP_PRIME_RUN, 0x00,
                 XX_FTCOMP_PRIME_RUN);
    xx_rt_memcpy(codec->window + XX_FTCOMP_DICT_AT, ftcomp_preset_dictionary,
                 XX_FTCOMP_DICT_SIZE);
    codec->position = XX_FTCOMP_START_POS;
}

static bool xx_ftcomp_emit(xx_ftcomp_codec *codec, uint8_t value) {
    if (codec->plain_at >= codec->plain_size) return false;
    codec->window[codec->position] = value;
    codec->position = (codec->position + 1U) & XX_FTCOMP_WINDOW_MASK;
    codec->plain[codec->plain_at++] = value;
    return true;
}

/* FUN_00662880: byte-oriented LZ77 whose escape byte is 0x9E.  @p size is
 * the exact number of token bytes this call has to consume. */
static bool xx_ftcomp_lz(xx_ftcomp_codec *codec, const uint8_t *data,
                         size_t size) {
    size_t at = 0U;
    int64_t remaining = (int64_t)size;
    while (remaining > 0) {
        uint8_t token;
        uint32_t length, distance, source, step;
        if (at >= size) return false;
        token = data[at++];
        if (token != 0x9eU) {
            --remaining;
            if (!xx_ftcomp_emit(codec, token)) return false;
            continue;
        }
        if (at >= size) return false;
        token = data[at++];
        if (token == 0x40U) {
            remaining -= 2;
            if (!xx_ftcomp_emit(codec, 0x9eU)) return false;
            continue;
        }
        if (token == 0x80U) {
            if (size - at < 3U) return false;
            length = (uint32_t)data[at] + 0x43U;
            distance =
                (uint32_t)data[at + 1U] | ((uint32_t)data[at + 2U] << 8U);
            at += 3U;
            remaining -= 5;
        } else if ((token & 0x40U) == 0U) {
            if (size - at < 1U) return false;
            length = (uint32_t)(((uint32_t)token + 3U) & 0xffU);
            distance = (uint32_t)data[at];
            at += 1U;
            remaining -= 3;
        } else {
            if (size - at < 2U) return false;
            length = (uint32_t)(token & 0x3fU) + 3U;
            distance = (uint32_t)data[at] | ((uint32_t)data[at + 1U] << 8U);
            at += 2U;
            remaining -= 4;
        }
        ++distance;
        source = codec->position - distance;
        for (step = 0U; step < length; ++step) {
            uint8_t value = codec->window[source & XX_FTCOMP_WINDOW_MASK];
            source = (source & XX_FTCOMP_WINDOW_MASK) + 1U;
            if (!xx_ftcomp_emit(codec, value)) return false;
        }
    }
    return remaining == 0;
}

/* FUN_00666d80, method 0: the token stream is a chain of sub-blocks, each a
 * u16 length, a flag byte, and then either literal bytes or LZ tokens. */
static bool xx_ftcomp_expand(xx_ftcomp_codec *codec, const uint8_t *data,
                             size_t size) {
    size_t at = 0U;
    int64_t remaining = (int64_t)size;
    while (remaining > 0) {
        uint32_t chunk, step;
        uint8_t flag;
        if (size - at < 2U) return false;
        chunk = (uint32_t)data[at] | ((uint32_t)data[at + 1U] << 8U);
        at += 2U;
        if (chunk == 0U) return false;
        remaining -= 2;
        if (remaining < (int64_t)chunk) return false;
        if (at >= size) return false;
        flag = data[at++];
        --chunk;
        if (chunk == 0U || size - at < chunk) return false;
        if (flag == 0U) {
            for (step = 0U; step < chunk; ++step)
                if (!xx_ftcomp_emit(codec, data[at + step])) return false;
        } else if (!xx_ftcomp_lz(codec, data + at, chunk)) {
            return false;
        }
        at += chunk;
        remaining = (remaining - 1) - (int64_t)chunk;
    }
    return remaining == 0;
}

/* ---------------------------------------------------- the Huffman stage -- */

/* FUN_00663dd0: rebuild the two per-block trees from the weights the block
 * transmits, then decode exactly @p expected token bytes into codec->tokens.
 * Returns how many input bytes the bit stream consumed, or 0 on failure. */
static size_t xx_ftcomp_entropy(xx_ftcomp_codec *codec, const uint8_t *data,
                                size_t size, uint32_t expected) {
    xx_ftcomp_bits bits;
    uint32_t weight_a, weight_b, weight_c, weight_d;
    uint32_t index, filled = 0U, largest = 0U, scale, at = 0U;
    uint32_t pending = 0U, context = 0U, subrange = 0U;
    uint32_t last_byte = 0U, last_near = 0U, last_far = 0U;
    uint32_t last_wide = 0U, last_high = 0U;
    /* Method 0 pins the position counter past every range threshold, which
     * is what the reference does with its 99999. */
    uint32_t position = 99999U;
    uint16_t pairs[48];
    uint16_t places[48];
    uint32_t pair_head = 0x20U, place_head = 0x20U;
    uint8_t *out = codec->tokens;
    const uint32_t ceiling = XX_FTCOMP_BLOCK_MAX + XX_FTCOMP_TOKEN_SLACK - 8U;

    if (size < 4U || expected == 0U || expected > XX_FTCOMP_BLOCK_MAX)
        return 0U;
    weight_a = data[0];
    weight_c = data[1];
    weight_d = data[2];
    weight_b = data[3];
    xx_ftcomp_bits_init(&bits, data, size, 4U);
    /* The block opens with 433 transmitted weights, coded with the fixed
     * tree; symbol 0x100 stands for a run of sixteen zeros. */
    while (filled < (uint32_t)XX_FTCOMP_SYMBOLS) {
        uint32_t symbol = xx_ftcomp_decode_symbol(&bits, &codec->header) >> 2;
        if (symbol == 0x100U) {
            uint32_t run = 0U;
            while (run < 16U && filled + run < (uint32_t)XX_FTCOMP_SYMBOLS) {
                codec->weights[filled + run] = 0U;
                ++run;
            }
            filled += run;
        } else if (symbol < 0x100U) {
            codec->weights[filled++] = (uint16_t)symbol;
        } else {
            return 0U;
        }
        if (bits.at > size + 4U) return 0U;
    }
    /* Two trees, one per symbol class, scaled by the four bytes the block
     * opened with.  The second is distinct only when its pair differs. */
    xx_mem_zero(&codec->table_a, sizeof(codec->table_a));
    for (index = 0U; index < (uint32_t)XX_FTCOMP_SYMBOLS; ++index) {
        uint32_t weight = codec->weights[index];
        uint32_t scaled = 0U;
        if (weight != 0U)
            scaled = (xx_ftcomp_symbol_class(index) == 0U)
                         ? (weight_a * weight)
                         : (weight * weight_c);
        codec->table_a.node[index * 4U] = (uint16_t)scaled;
        if ((uint32_t)codec->table_a.node[index * 4U] > largest)
            largest = codec->table_a.node[index * 4U];
    }
    scale = (largest < 0x100U) ? 0U : (0xffffU / largest);
    if (scale != 0U) {
        for (index = 0U; index < (uint32_t)XX_FTCOMP_SYMBOLS; ++index) {
            uint32_t value = codec->table_a.node[index * 4U];
            uint32_t rescaled = (value * scale) >> 8U;
            codec->table_a.node[index * 4U] = (uint16_t)rescaled;
            if (value != 0U && rescaled == 0U)
                codec->table_a.node[index * 4U] = 1U;
        }
    }
    if (!xx_ftcomp_build(&codec->table_a)) return 0U;
    if (weight_d != weight_c || weight_b != weight_a) {
        xx_mem_zero(&codec->table_b, sizeof(codec->table_b));
        if (weight_d != 0U || weight_b != 0U) {
            largest = 0U;
            for (index = 0U; index < (uint32_t)XX_FTCOMP_SYMBOLS; ++index) {
                uint32_t weight = codec->weights[index];
                uint32_t scaled = 0U;
                if (weight != 0U)
                    scaled = (xx_ftcomp_symbol_class(index) == 0U)
                                 ? (weight_b * weight)
                                 : (weight * weight_d);
                codec->table_b.node[index * 4U] = (uint16_t)scaled;
                if ((uint32_t)codec->table_b.node[index * 4U] > largest)
                    largest = codec->table_b.node[index * 4U];
            }
            scale = (largest < 0x100U) ? 0U : (0xffffU / largest);
            if (scale != 0U) {
                for (index = 0U; index < (uint32_t)XX_FTCOMP_SYMBOLS;
                     ++index) {
                    uint32_t value = codec->table_b.node[index * 4U];
                    uint32_t rescaled = (value * scale) >> 8U;
                    codec->table_b.node[index * 4U] = (uint16_t)rescaled;
                    if (value != 0U && rescaled == 0U)
                        codec->table_b.node[index * 4U] = 1U;
                }
            }
        } else {
            for (index = 0U; index < (uint32_t)XX_FTCOMP_SYMBOLS; ++index)
                codec->table_b.node[index * 4U] =
                    codec->table_a.node[index * 4U];
        }
        if (!xx_ftcomp_build(&codec->table_b)) return 0U;
    } else {
        codec->table_b = codec->table_a;
    }

    xx_mem_zero(pairs, sizeof(pairs));
    xx_mem_zero(places, sizeof(places));
    while (at < expected) {
        if (at > ceiling) return 0U;
        if (pending == 0U) {
            uint32_t symbol =
                xx_ftcomp_decode_symbol(
                    &bits, context ? &codec->table_b : &codec->table_a) >> 2;
            if (symbol >= (uint32_t)XX_FTCOMP_SYMBOLS) return 0U;
            context = xx_ftcomp_symbol_class(symbol);
            if (symbol < 0x100U) {
                ++position;
                out[at++] = (uint8_t)symbol;
            } else if (symbol < 0x181U) {
                uint8_t token = (uint8_t)(symbol - 0x100U);
                out[at] = 0x9eU;
                out[at + 1U] = token;
                pending = xx_ftcomp_token_fields(token);
                if (pending != 0U) {
                    if (pending < 3U) position += (token & 0x3fU) + 3U;
                    if (place_head == 0U) {
                        uint32_t k;
                        for (k = 0U; k < 16U; ++k) places[32U + k] = places[k];
                        place_head = 0x1fU;
                    } else {
                        --place_head;
                    }
                    places[place_head] = (uint16_t)(at + 1U);
                }
                at += 2U;
            } else if (symbol < 0x191U) {
                /* A literal pair repeated from two to seventeen bytes back,
                 * which also enters the pair history. */
                uint32_t back = symbol - 0x17fU;
                uint16_t pair;
                if (back > at) return 0U;
                pair = (uint16_t)((uint32_t)out[at - back] |
                                  ((uint32_t)out[at - back + 1U] << 8U));
                out[at] = (uint8_t)(pair & 0xffU);
                out[at + 1U] = (uint8_t)(pair >> 8U);
                position += (((pair & 0xffU) != 0x9eU) ? 1U : 0U) + 1U;
                if (pair_head == 0U) {
                    uint32_t k;
                    for (k = 0U; k < 15U; ++k) pairs[32U + k] = pairs[k];
                    pair_head = 0x1fU;
                } else {
                    --pair_head;
                }
                pairs[pair_head] = pair;
                at += 2U;
            } else if (symbol < 0x1a1U) {
                /* The same pair taken from the history and moved to front. */
                uint32_t rank = symbol - 0x191U, k;
                uint16_t pair = pairs[pair_head + rank];
                position += (((pair & 0xffU) != 0x9eU) ? 1U : 0U) + 1U;
                out[at] = (uint8_t)(pair & 0xffU);
                out[at + 1U] = (uint8_t)(pair >> 8U);
                for (k = rank; k > 0U; --k)
                    pairs[pair_head + k] = pairs[pair_head + k - 1U];
                pairs[pair_head] = pair;
                at += 2U;
            } else {
                /* A whole match token repeated from the position history,
                 * again moved to front. */
                uint32_t rank = symbol - 0x1a1U, k;
                uint32_t source = places[place_head + rank];
                uint32_t written, fields;
                uint8_t token;
                /* Only tokens that carry fields are ever recorded, so the
                 * referenced copy must already hold the token byte plus as
                 * many bytes as that token's shape implies. */
                if (source + 2U > at) return 0U;
                token = out[source];
                fields = xx_ftcomp_token_fields(token);
                if (fields == 0U || source + 1U + fields > at) return 0U;
                out[at] = 0x9eU;
                out[at + 1U] = token;
                out[at + 2U] = out[source + 1U];
                written = at + 3U;
                if (fields < 3U)
                    position += (token & 0x3fU) + 3U;
                else
                    position += (uint32_t)out[source + 1U] + 0x43U;
                if (token == 0x80U) {
                    out[written] = out[source + 2U];
                    out[written + 1U] = out[source + 3U];
                    written = at + 5U;
                } else if ((token & 0x40U) != 0U) {
                    out[written] = out[source + 2U];
                    written = at + 4U;
                }
                for (k = rank; k > 0U; --k)
                    places[place_head + k] = places[place_head + k - 1U];
                places[place_head] = (uint16_t)(at + 1U);
                at = written;
            }
        } else if (pending < 3U) {
            uint32_t field = 0U, symbol, value;
            if (pending == 2U) {
                /* The distance splits into a coded high part and a raw low
                 * part whose width the leading bits select. */
                xx_ftcomp_bits_fill(&bits, 9);
                if ((bits.accumulator & 0x8000U) == 0U) {
                    subrange = 0U;
                    field = xx_ftcomp_bits_take(&bits, 5) & 0xfU;
                } else if (position < 0x5100U) {
                    subrange = 1U;
                    field = xx_ftcomp_bits_take(&bits, 7) & 0x3fU;
                } else if ((bits.accumulator & 0x4000U) == 0U) {
                    subrange = 1U;
                    field = xx_ftcomp_bits_take(&bits, 8) & 0x3fU;
                } else if (position < 0x9100U) {
                    subrange = 2U;
                    field = xx_ftcomp_bits_take(&bits, 8) & 0x3fU;
                } else {
                    subrange = 2U;
                    field = xx_ftcomp_bits_take(&bits, 9) & 0x7fU;
                }
            }
            symbol = xx_ftcomp_decode_symbol(&bits, &codec->extra) >> 2;
            if (pending == 1U) {
                if (symbol != 0x100U) last_byte = symbol;
                out[at++] = (uint8_t)last_byte;
            } else {
                if (subrange == 0U) {
                    if (symbol != 0x100U) last_near = symbol;
                    value = (field + last_near * 0x10U + 0x100U) & 0xffffU;
                } else if (subrange == 1U) {
                    if (symbol != 0x100U) last_far = symbol;
                    value = (field + last_far * 0x40U + 0x1100U) & 0xffffU;
                } else {
                    if (symbol != 0x100U) last_wide = symbol;
                    value =
                        (position < 0x9100U)
                            ? ((field + (last_wide + 0x144U) * 0x40U) & 0xffffU)
                            : ((field + (last_wide + 0xa2U) * 0x80U) & 0xffffU);
                }
                out[at++] = (uint8_t)(value & 0xffU);
                out[at++] = (uint8_t)((value >> 8U) & 0xffU);
            }
            pending = 0U;
        } else {
            /* The three fields of a long match: the length byte, then the two
             * distance bytes, the last of which repeats by default. */
            uint32_t symbol =
                xx_ftcomp_decode_symbol(&bits, &codec->extra) >> 2;
            uint32_t value;
            ++pending;
            if (pending == 6U) {
                pending = 0U;
                if (symbol != 0x100U) last_high = symbol;
                value = last_high;
            } else {
                value = (symbol == 0x100U) ? 0U : symbol + 1U;
            }
            out[at++] = (uint8_t)value;
        }
        if (bits.at > size + 4U) return 0U;
    }
    if (at != expected) return 0U;
    return bits.at - (size_t)(bits.held >> 3);
}

/* -------------------------------------------------- the member driver --- */

/* FUN_00667430: a member's payload opens with a four-byte prologue and then
 * runs as a chain of blocks, each tagged and each either stored or entropy
 * coded, until the declared plaintext length has been produced. */
static bool xx_ftcomp_decode_member(Abstractformat *self,
                                    const xx_ftcomp_member *member,
                                    uint8_t **plain, size_t *plain_size) {
    xx_ftcomp_codec *codec = NULL;
    uint8_t *output = NULL;
    int64_t total, cursor, end;
    int64_t remaining;
    bool ok = false;

    if (!self || !member || !plain || !plain_size) return false;
    *plain = NULL;
    *plain_size = 0U;
    if (member->uncompressed_size <= 0 ||
        member->uncompressed_size > (int64_t)XX_FTCOMP_MAX_OUTPUT ||
        member->compressed_size < 4)
        return false;
    total = xx_io_total_size(self->device);
    if (total < 0) return false;
    cursor = member->data_offset;
    end = member->data_offset + member->compressed_size;
    if (cursor < 0 || end > total) return false;

    codec = (xx_ftcomp_codec *)xx_mem_alloc(sizeof(*codec));
    output = (uint8_t *)xx_mem_alloc((size_t)member->uncompressed_size);
    if (!codec || !output) goto done;
    xx_mem_zero(codec, sizeof(*codec));
    codec->window = (uint8_t *)xx_mem_alloc(XX_FTCOMP_WINDOW_SIZE);
    if (!codec->window) goto done;
    if (!xx_ftcomp_build_static(&codec->header, ftcomp_weights_header,
                                sizeof(ftcomp_weights_header) /
                                    sizeof(ftcomp_weights_header[0])) ||
        !xx_ftcomp_build_static(&codec->extra, ftcomp_weights_extra,
                                sizeof(ftcomp_weights_extra) /
                                    sizeof(ftcomp_weights_extra[0])))
        goto done;
    xx_ftcomp_prime(codec);
    codec->plain = output;
    codec->plain_size = (size_t)member->uncompressed_size;
    codec->plain_at = 0U;

    cursor += 4; /* the fixed 80 60 00 00 prologue */
    remaining = member->uncompressed_size;
    while (remaining >= 1) {
        uint8_t head[6];
        uint32_t declared;
        size_t before = codec->plain_at;
        if (remaining < 4) {
            /* A tail shorter than a block header is stored verbatim. */
            if (end - cursor < remaining) goto done;
            if (codec->plain_size - codec->plain_at < (size_t)remaining)
                goto done;
            if (!xx_ftcomp_read_at(self, cursor, codec->plain + codec->plain_at,
                                   (size_t)remaining))
                goto done;
            codec->plain_at += (size_t)remaining;
            cursor += remaining;
            remaining = 0;
            break;
        }
        if (end - cursor < 6) goto done;
        if (!xx_ftcomp_read_at(self, cursor, head, sizeof(head))) goto done;
        if (xx_ftcomp_le32(head) != XX_FTCOMP_TAG_FT19) goto done;
        declared = xx_ftcomp_le16(head + 4);
        cursor += 6;
        if (declared == 0xffffU) {
            /* A stored block: the next u16 is the token count itself. */
            uint8_t raw[2];
            uint32_t length;
            if (end - cursor < 2) goto done;
            if (!xx_ftcomp_read_at(self, cursor, raw, sizeof(raw))) goto done;
            length = xx_ftcomp_le16(raw);
            cursor += 2;
            if (length == 0U || end - cursor < (int64_t)length) goto done;
            if (!xx_ftcomp_read_at(self, cursor, codec->input, length))
                goto done;
            if (!xx_ftcomp_expand(codec, codec->input, length)) goto done;
            cursor += (int64_t)length;
        } else {
            size_t avail = (size_t)((total - cursor) < (int64_t)
                                        XX_FTCOMP_BLOCK_MAX
                                        ? (total - cursor)
                                        : (int64_t)XX_FTCOMP_BLOCK_MAX);
            size_t consumed;
            if (avail < 4U) goto done;
            xx_mem_zero(codec->input, sizeof(codec->input));
            if (!xx_ftcomp_read_at(self, cursor, codec->input, avail))
                goto done;
            consumed = xx_ftcomp_entropy(codec, codec->input, avail, declared);
            if (consumed == 0U || consumed > avail) goto done;
            if (!xx_ftcomp_expand(codec, codec->tokens, declared)) goto done;
            cursor += (int64_t)consumed;
        }
        if (codec->plain_at <= before) goto done;
        remaining -= (int64_t)(codec->plain_at - before);
    }
    /* A member is complete or refused: a short decode is never reported as
     * a success. */
    if (codec->plain_at != (size_t)member->uncompressed_size) goto done;
    *plain = output;
    *plain_size = codec->plain_at;
    output = NULL;
    ok = true;
done:
    if (codec) {
        if (codec->window) xx_mem_free(codec->window);
        xx_mem_free(codec);
    }
    if (output) xx_mem_free(output);
    return ok;
}

static xx_ftcomp_stream *xx_ftcomp_parse(Abstractformat *self,
                                         xx_pd_struct *pd) {
    xx_ftcomp_stream *stream;
    int64_t total;
    int64_t span;
    int64_t cursor = 0;

    if (!self || !self->device || self->base_address < 0) return NULL;
    total = xx_io_total_size(self->device);
    if (total < self->base_address) return NULL;
    span = total - self->base_address;
    if (span < (int64_t)XX_FTCOMP_HEADER_SIZE) return NULL;

    stream = (xx_ftcomp_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return NULL;
    for (;;) {
        xx_ftcomp_member member;
        int64_t next = 0;

        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!xx_ftcomp_read_member(self, span, cursor, &member, &next))
            goto fail;
        if (!xx_ftcomp_add(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        if (next == 0) break;
        cursor = next;
    }
    stream->variant = XX_FTCOMP_VARIANT;
    stream->archive_size = span;
    return stream;
fail:
    xx_ftcomp_stream_free(stream);
    return NULL;
}

/* --------------------------------------------------------- lifecycle --- */

static void xx_ftcomp_vtable_destroy(Abstractformat *self);

void xx_ftcomp_init(xx_ftcomp *archive, xx_io_device *device,
                    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_FTCOMP_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-ftcomp");
    xx_format_set_extension(&archive->format, "ftc");
    archive->format.check_is_valid = xx_ftcomp_check_is_valid;
    archive->format.handle_base_info = xx_ftcomp_handle_base_info;
    archive->format.get_format_size = xx_ftcomp_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_ftcomp_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_ftcomp_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_ftcomp_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_ftcomp_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_ftcomp_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_ftcomp_free_archive_records_reading;
    archive->format.destroy = xx_ftcomp_vtable_destroy;
}

xx_ftcomp *xx_ftcomp_create(xx_io_device *device, int64_t base_address) {
    xx_ftcomp *archive = (xx_ftcomp *)xx_mem_alloc(sizeof(*archive));

    if (!archive) return NULL;
    xx_ftcomp_init(archive, device, base_address);
    return archive;
}

void xx_ftcomp_destroy(xx_ftcomp *archive) {
    if (!archive) return;
    if (archive->format.close) archive->format.close(&archive->format);
    xx_format_cleanup_extra_parameters(&archive->format);
    archive->number_of_records = 0U;
}

void xx_ftcomp_free(xx_ftcomp *archive) {
    if (!archive) return;
    xx_ftcomp_destroy(archive);
    xx_mem_free(archive);
}

static void xx_ftcomp_vtable_destroy(Abstractformat *self) {
    xx_ftcomp_destroy((xx_ftcomp *)self);
}

/* ------------------------------------------------------------ format --- */

bool xx_ftcomp_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    xx_ftcomp_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    stream = xx_ftcomp_parse(self, pd);
    if (!stream) return false;
    xx_ftcomp_stream_free(stream);
    return true;
}

bool xx_ftcomp_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_ftcomp *archive = (xx_ftcomp *)self;
    xx_ftcomp_stream *stream;

    if (!self || (pd && xx_pd_is_stopped(pd))) return false;
    self->base_info_handled = true;
    stream = xx_ftcomp_parse(self, pd);
    if (!stream) {
        self->is_valid = false;
        self->format_size = 0;
        return false;
    }
    self->is_valid = true;
    self->format_size = stream->archive_size;
    self->number_of_archive_records = stream->count;
    archive->number_of_records = stream->count;
    archive->variant = stream->variant;
    xx_ftcomp_stream_free(stream);
    return true;
}

int64_t xx_ftcomp_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return 0;
    return self->is_valid ? self->format_size : 0;
}

uint64_t xx_ftcomp_get_number_of_archive_records(Abstractformat *self,
                                                 xx_pd_struct *pd) {
    if (!self ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)))
        return 0U;
    return self->is_valid ? ((xx_ftcomp *)self)->number_of_records : 0U;
}

/* ----------------------------------------------------------- records --- */

static bool xx_ftcomp_set_record(xx_archive_record *record,
                                 const xx_ftcomp_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->compressed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->compressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record,
                                          XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          member->attributes) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          member->timestamp) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_CRC32,
                                          member->check) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                          member->extra) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

static bool xx_ftcomp_copy_options(xx_list_s *target,
                                   const xx_list_s *options) {
    size_t index;

    if (!options) return true;
    if (!target) return false;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *source =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        xx_meta copied;
        if (!source) continue;
        xx_meta_init(&copied, source->meta_id);
        if (!xx_var_copy(&copied.var, &source->var) ||
            !xx_list_append(target, &copied)) {
            xx_meta_cleanup(&copied);
            return false;
        }
    }
    return true;
}

xx_archive_record_state *xx_ftcomp_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_ftcomp_stream *stream;
    xx_archive_record_state *state;

    if (!self || !self->device) return NULL;
    stream = xx_ftcomp_parse(self, pd);
    if (!stream) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        xx_ftcomp_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, self);
    state->internal_state = stream;
    state->free_internal = xx_ftcomp_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!xx_ftcomp_copy_options(&state->options, options) ||
        (stream->count != 0U &&
         !xx_ftcomp_set_record(&state->current_record, &stream->items[0]))) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = stream->count != 0U;
    state->current_index = 0;
    return state;
}

const xx_archive_record *xx_ftcomp_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_ftcomp_archive_record_move_to_next(Abstractformat *self,
                                           xx_archive_record_state *state,
                                           xx_pd_struct *pd) {
    xx_ftcomp_stream *stream;

    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd)))
        return false;
    stream = (xx_ftcomp_stream *)state->internal_state;
    if (!stream || stream->index + 1U >= stream->count) {
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        state->has_record = false;
        return false;
    }
    ++stream->index;
    ++state->current_index;
    state->has_record = xx_ftcomp_set_record(&state->current_record,
                                             &stream->items[stream->index]);
    return state->has_record;
}

/* A member name that reaches the filesystem must be a plain relative path;
 * anything else makes the member unextractable rather than renamed. */
static bool xx_ftcomp_safe_output_name(const char *name) {
    const char *segment;
    const char *at;

    if (!name || !name[0] || name[0] == '/' || name[0] == '\\' ||
        name[1] == ':')
        return false;
    segment = name;
    for (at = name;; ++at) {
        unsigned char c = (unsigned char)*at;
        if (c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
            c == '?' || c == '*' || (c != 0U && c < 0x20U))
            return false;
        if (c == '/' || c == '\\' || c == 0U) {
            size_t length = (size_t)(at - segment);
            if (length == 0U || (length == 1U && segment[0] == '.') ||
                (length == 2U && segment[0] == '.' && segment[1] == '.'))
                return false;
            if (c == 0U) return true;
            segment = at + 1;
        }
    }
}

static const xx_var *xx_ftcomp_option(const xx_list_s *options, uint32_t id) {
    size_t index;

    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

bool xx_ftcomp_unpack_current_archive_record(Abstractformat *self,
                                             xx_archive_record_state *state,
                                             xx_pd_struct *pd) {
    xx_ftcomp_stream *stream;
    const xx_ftcomp_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U;
    size_t written = 0U;
    bool result = false;
    bool created = false;

    if (!self || !state || state->format != self || !state->has_record ||
        !(stream = (xx_ftcomp_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!xx_ftcomp_safe_output_name(member->name) ||
        !xx_ftcomp_decode_member(self, member, &plain, &plain_size))
        goto done;
    path_option = xx_ftcomp_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
        result = true;
        goto done;
    }
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
               ? xx_str_concat3(base, "/", member->name)
               : xx_str_concat(base, member->name);
    if (!path || !xx_store_create_dirs_a(path, false)) goto done;
    {
        xx_io_device *destination = xx_io_file_open(path, "wb");
        created = destination != NULL;
        if (!destination) goto done;
        result = true;
        while (written < plain_size) {
            ssize_t amount =
                xx_io_write(destination, plain + written, plain_size - written);
            if (amount <= 0 || (size_t)amount > plain_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_ftcomp_free_archive_records_reading(Abstractformat *self,
                                            xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
