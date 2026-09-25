/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Gentee installer (the script-driven setup builder from gentee.com).
 *
 * Ported from the XArchive reference modules installers/xgentee.cpp and
 * Algos/xgenteedecoder.cpp (MIT, Copyright (c) 2026 hors), restructured to
 * stream from the device instead of loading the payload into memory.
 *
 * Carrier: a 7.5 KB PE32 stub ("Gentee installer" at 0x1000).  Its header
 * padding holds a 12-byte locator at file offset 0x3F0:
 *   +0 u32 payload offset (0x1E00 normally, further out when the stub's
 *          resources were rebuilt and the overlay starts later)
 *   +4 u32 length of the first block (u32 size word plus its bit stream)
 *   +8 u32 decoded size of the first block
 * The locator is used when it points into the overlay; otherwise the
 * payload is looked for at the overlay start.  Nothing is executed.
 *
 * Payload: a chain of blocks, each
 *   u32 decoded size, then an MSB-first bit stream that ends as soon as that
 *   many bytes were produced (the leftover bits of the last byte are dropped)
 * No block stores its packed length, so the chain is walked by decoding it.
 * The first block is the installer runtime (ginstall.dll); every build ships
 * the same one, so its stream always starts AB 67 A7 36 FF 4D FB 6F, which is
 * what identifies the format.
 *
 * Codec: LZ77 over a 0x8000 window with three adaptive Huffman trees.
 *   main tree, 0x112 symbols: < 0x100 literal; 0x100 + n a match of length
 *     n + 3, n == 0x11 escaping into the length tree (0xED symbols) for
 *     0x11 + s + 3.
 *   distance tree, 0x22 symbols: < 0x1E a slot base plus extra bits read
 *     LOW BIT FIRST; 0x1E..0x21 one of four recent distances.  Every distance
 *     then moves to the front of the recent list.
 *   the match source is window[pos - length - distance].
 * A tree starts as the Huffman tree of weights 1..n built with the reference
 * builder's quirky minimum selection; after each symbol the leaf's weight is
 * raised along the path to the root, swapping a node with its uncle once the
 * uncle is no heavier, and every weight is halved when the root reaches
 * 0x200.  The trees therefore depend on every earlier symbol of their state.
 *
 * Layout after the runtime block (whose state is then reset):
 *   20 raw bytes   u32 at +0 is the archive end as a file offset; u16 at +14
 *                  must be zero (other values are a form this reader does
 *                  not know)
 *   2 blocks       language strings and a table, decoded and dropped
 *   command blocks same state as the two above; u16 tag at +0, body at +3.
 *                  0x87F0 ends the archive; 0x87F4 is a file member:
 *                    +0x00 u32 attributes   +0x04 i32 size   +0x08 FILETIME
 *                    +0x19 u8  0 = data follows stored, else as one block
 *                    +0x1F asciiz name ("#setuppath#\dir\file")
 *                  every other tag is an installer action and is skipped.
 * Compressed member blocks share ONE decoder state of their own, so member k
 * can only be decoded after the compressed members before it.  Listing walks
 * the whole chain; extraction replays only the member data blocks, whose
 * offsets the listing recorded.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/gentee_installer/xx_gentee_installer.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/data/xx_pd.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

#ifdef GENTEE_INSTALLER
#define XX_GENTEE_INSTALLER_FILE_TYPE XX_FILE_TYPE_GENTEE_INSTALLER
#else
#define XX_GENTEE_INSTALLER_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define GI_LOCATOR_OFFSET 0x3F0
#define GI_LOCATOR_SIZE 12U
#define GI_PAYLOAD_HEADER 12U
#define GI_MAX_LFANEW 0x10000U
#define GI_MAX_SECTIONS 96U
#define GI_MAX_RUNTIME 0x100000U
#define GI_MAX_SKIPPED_BLOCK 0x1000000U
#define GI_ARCHIVE_HEADER 20U
#define GI_ARCHIVE_HEADER_FLAG 14U
#define GI_SKIPPED_BLOCKS 2U
#define GI_CMD_HEADER 3U
#define GI_MAX_CMD 0x10000U
#define GI_RECORD_MIN 0x1EU
#define GI_RECORD_SIZE_OFFSET 4U
#define GI_RECORD_TIME_OFFSET 8U
#define GI_RECORD_STORED_OFFSET 0x19U
#define GI_RECORD_NAME_OFFSET 0x1FU
#define GI_TAG_END 0x87F0U
#define GI_TAG_FILE 0x87F4U
/* A setup program with this many files is not plausible; the caps only keep
 * a crafted chain from growing the member table or the walk without bound. */
#define GI_MAX_MEMBERS 100000U
#define GI_MAX_COMMANDS 4000000UL
/* Installer paths stay far below MAX_PATH; a command block may be 64 KiB,
 * so without these a crafted chain could make the name table gigabytes. */
#define GI_MAX_NAME 1024U
#define GI_MAX_NAME_BYTES 0x800000U

#define GI_WINDOW 0x8000U
#define GI_MAIN_SYMBOLS 0x112
#define GI_DIST_SYMBOLS 0x22
#define GI_LEN_SYMBOLS 0xED
#define GI_MAX_NODES (2 * GI_MAIN_SYMBOLS)
#define GI_DIST_SLOTS 30
#define GI_RECENT 4
#define GI_MAX_WEIGHT 0x200
#define GI_LENGTH_ESCAPE 0x11
#define GI_MIN_LENGTH 3

#define GI_IN_BUFFER 0x10000U
#define GI_OUT_BUFFER 0x10000U
#define GI_PD_MASK 0xFFFFUL

static const uint8_t g_gi_signature[8] = {0xAB, 0x67, 0xA7, 0x36,
                                          0xFF, 0x4D, 0xFB, 0x6F};

static const uint8_t g_gi_dist_bits[GI_DIST_SLOTS] = {
    0, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4, 5, 5, 6,
    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

/* ---------------------------------------------------------------------- */
/* Small helpers                                                           */

static uint16_t gi_le16(const uint8_t *b) {
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8U));
}

static uint32_t gi_le32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8U) | ((uint32_t)b[2] << 16U) |
           ((uint32_t)b[3] << 24U);
}

static uint64_t gi_le64(const uint8_t *b) {
    return (uint64_t)gi_le32(b) | ((uint64_t)gi_le32(b + 4U) << 32U);
}

static bool gi_read_at(xx_io_device *device, int64_t offset, void *buffer,
                       size_t size) {
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 ||
        xx_io_seek64(device, offset, SEEK_SET) != 0)
        return false;
    while (done < size) {
        ssize_t amount =
            xx_io_read(device, (uint8_t *)buffer + done, size - done);
        if (amount <= 0 || (size_t)amount > size - done) return false;
        done += (size_t)amount;
    }
    return true;
}

static bool gi_stopped(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }

/* ---------------------------------------------------------------------- */
/* Carrier                                                                 */

typedef struct gi_location_s {
    int64_t total;          /**< Bytes from base to the end of the device. */
    int64_t overlay;        /**< Base-relative end of the PE image. */
    int64_t payload;        /**< Base-relative payload start. */
    uint32_t runtime_size;
} gi_location;

static bool gi_payload_header_ok(const uint8_t *h, uint32_t *runtime) {
    uint32_t size = gi_le32(h);
    if (size == 0U || size >= GI_MAX_RUNTIME ||
        xx_rt_memcmp(h + 4U, g_gi_signature, sizeof(g_gi_signature)) != 0)
        return false;
    if (runtime) *runtime = size;
    return true;
}

/* Reads just enough of the PE to know where its image ends, then looks for
 * the payload header at the stub's locator and at the overlay start.  At
 * most five small reads, so it is cheap enough for every MZ file. */
static bool gi_locate(Abstractformat *format, gi_location *out) {
    uint8_t mz[0x40];
    uint8_t nt[24];
    uint8_t sections[GI_MAX_SECTIONS * 40U];
    uint8_t head[GI_PAYLOAD_HEADER];
    int64_t base, total, table, overlay = 0, candidate;
    uint32_t lfanew, nsec, optsz, index, runtime = 0U;
    if (!format || !format->device || !out || format->base_address < 0)
        return false;
    base = format->base_address;
    total = xx_io_total_size(format->device);
    if (total < base) return false;
    total -= base;
    if (total < (int64_t)(GI_LOCATOR_OFFSET + GI_LOCATOR_SIZE)) return false;
    if (!gi_read_at(format->device, base, mz, sizeof(mz)) || mz[0] != 'M' ||
        mz[1] != 'Z')
        return false;
    lfanew = gi_le32(mz + 0x3C);
    if (lfanew < 0x40U || lfanew > GI_MAX_LFANEW ||
        (int64_t)lfanew + (int64_t)sizeof(nt) > total ||
        !gi_read_at(format->device, base + lfanew, nt, sizeof(nt)) ||
        nt[0] != 'P' || nt[1] != 'E' || nt[2] != 0 || nt[3] != 0)
        return false;
    nsec = gi_le16(nt + 6);
    optsz = gi_le16(nt + 20);
    if (nsec == 0U || nsec > GI_MAX_SECTIONS) return false;
    table = (int64_t)lfanew + 24 + (int64_t)optsz;
    if (table + (int64_t)nsec * 40 > total ||
        !gi_read_at(format->device, base + table, sections, nsec * 40U))
        return false;
    for (index = 0U; index < nsec; ++index) {
        const uint8_t *s = sections + index * 40U;
        int64_t raw_size = (int64_t)gi_le32(s + 16);
        int64_t raw_ptr = (int64_t)gi_le32(s + 20);
        if (raw_size != 0 && raw_ptr + raw_size > overlay)
            overlay = raw_ptr + raw_size;
    }
    if (overlay <= 0 || overlay + (int64_t)GI_PAYLOAD_HEADER > total)
        return false;

    /* The locator in the stub's header padding. */
    {
        uint8_t locator[GI_LOCATOR_SIZE];
        if (gi_read_at(format->device, base + GI_LOCATOR_OFFSET, locator,
                       sizeof(locator))) {
            candidate = (int64_t)gi_le32(locator);
            if (candidate >= overlay &&
                candidate + (int64_t)GI_PAYLOAD_HEADER <= total &&
                gi_read_at(format->device, base + candidate, head,
                           sizeof(head)) &&
                gi_payload_header_ok(head, &runtime)) {
                out->total = total;
                out->overlay = overlay;
                out->payload = candidate;
                out->runtime_size = runtime;
                return true;
            }
        }
    }
    /* Otherwise the payload has to open the overlay. */
    if (!gi_read_at(format->device, base + overlay, head, sizeof(head)) ||
        !gi_payload_header_ok(head, &runtime))
        return false;
    out->total = total;
    out->overlay = overlay;
    out->payload = overlay;
    out->runtime_size = runtime;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Buffered bit input                                                      */

typedef struct gi_input_s {
    xx_io_device *device;
    int64_t next;    /**< Absolute offset of the next byte to buffer. */
    int64_t end;     /**< Absolute end of readable data. */
    size_t length;
    size_t position;
    uint32_t count;  /**< Bits left in current. */
    uint8_t current;
    uint8_t buffer[GI_IN_BUFFER];
} gi_input;

static void gi_in_seek(gi_input *in, int64_t offset) {
    in->next = offset;
    in->length = 0U;
    in->position = 0U;
    in->count = 0U;
}

static int64_t gi_in_tell(const gi_input *in) {
    return in->next - (int64_t)(in->length - in->position);
}

static bool gi_in_byte(gi_input *in, uint8_t *value) {
    if (in->position >= in->length) {
        int64_t left = in->end - in->next;
        size_t want;
        if (left <= 0) return false;
        want = left < (int64_t)GI_IN_BUFFER ? (size_t)left : GI_IN_BUFFER;
        if (!gi_read_at(in->device, in->next, in->buffer, want)) return false;
        in->next += (int64_t)want;
        in->length = want;
        in->position = 0U;
    }
    *value = in->buffer[in->position++];
    return true;
}

/* Blocks start on a byte boundary: the bits left over are dropped. */
static void gi_in_align(gi_input *in) { in->count = 0U; }

static bool gi_in_u32(gi_input *in, uint32_t *value) {
    uint8_t b[4];
    size_t index;
    gi_in_align(in);
    for (index = 0U; index < 4U; ++index)
        if (!gi_in_byte(in, &b[index])) return false;
    *value = gi_le32(b);
    return true;
}

static int gi_in_bit(gi_input *in) {
    int result;
    if (in->count == 0U) {
        if (!gi_in_byte(in, &in->current)) return -1;
        in->count = 8U;
    }
    --in->count;
    result = (in->current >> 7U) & 1;
    in->current = (uint8_t)(in->current << 1U);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Adaptive Huffman tree                                                   */

typedef struct gi_tree_s {
    int32_t weight[GI_MAX_NODES];
    int16_t parent[GI_MAX_NODES];
    int16_t left[GI_MAX_NODES];
    int16_t right[GI_MAX_NODES];
    int32_t symbols;
    int32_t nodes;
    int32_t root;
} gi_tree;

/* Builds the starting tree over weights 1..n.  The displaced minimum only
 * becomes the second minimum when that slot is still empty; the reference
 * builder does this and the tree shape depends on it. */
static bool gi_tree_init(gi_tree *t, int32_t symbols) {
    int32_t i, count, total = symbols * 2 - 1, last = -1;
    t->symbols = symbols;
    for (i = 0; i < symbols * 2; ++i) {
        t->weight[i] = i < symbols ? i + 1 : 0;
        t->parent[i] = -1;
        t->left[i] = -1;
        t->right[i] = -1;
    }
    count = symbols;
    while (count < total) {
        int32_t m1 = -1, m2 = -1;
        for (i = 0; i < count; ++i) {
            if (t->parent[i] != -1) continue;
            if (m1 < 0 || t->weight[i] < t->weight[m1]) {
                if (m1 >= 0 && m2 < 0) m2 = m1;
                m1 = i;
            } else if (m2 < 0 || t->weight[i] < t->weight[m2]) {
                m2 = i;
            }
        }
        if (m1 < 0 || m2 < 0) break;
        t->weight[count] = t->weight[m1] + t->weight[m2];
        t->left[count] = (int16_t)m1;
        t->right[count] = (int16_t)m2;
        t->parent[m1] = (int16_t)count;
        t->parent[m2] = (int16_t)count;
        last = count;
        ++count;
    }
    t->nodes = count;
    if (last < 0) return false;
    t->parent[last] = -1;
    t->root = last;
    return true;
}

static int32_t gi_half(int32_t w) { return w >= 0 ? w / 2 : -((1 - w) / 2); }

/* Exchanges a with b and, when the heavier child of the node moved in
 * outweighs the sibling left behind, repeats once one level down. */
static bool gi_tree_swap(gi_tree *t, int32_t a, int32_t b) {
    bool extra = true;
    int32_t guard;
    for (guard = 0; guard < 2; ++guard) {
        int32_t pa = t->parent[a], pb, sibling, heavy;
        if (pa < 0) return false;
        sibling = t->left[pa] == a ? t->right[pa] : t->left[pa];
        heavy = t->left[b];
        if (heavy >= 0) {
            if (t->right[b] < 0) return false;
            if (t->weight[t->left[b]] <= t->weight[t->right[b]])
                heavy = t->right[b];
        }
        if (t->left[pa] == a) t->left[pa] = (int16_t)b;
        else t->right[pa] = (int16_t)b;
        pb = t->parent[b];
        if (pb < 0) return false;
        if (t->left[pb] == b) t->left[pb] = (int16_t)a;
        else t->right[pb] = (int16_t)a;
        t->parent[a] = (int16_t)pb;
        t->parent[b] = (int16_t)pa;
        if (!extra || heavy < 0 || sibling < 0 ||
            t->weight[heavy] <= t->weight[sibling])
            break;
        extra = false;
        t->weight[b] += t->weight[sibling] - t->weight[heavy];
        a = heavy;
        b = sibling;
    }
    return true;
}

static bool gi_tree_update(gi_tree *t, int32_t symbol) {
    int32_t node = symbol, steps = 0;
    while (node >= 0) {
        int32_t par = t->parent[node];
        if (++steps > GI_MAX_NODES) return false;
        if (par >= 0 && t->parent[par] >= 0) {
            int32_t grand = t->parent[par];
            int32_t uncle = t->left[grand] == par ? t->right[grand]
                                                   : t->left[grand];
            if (uncle < 0) return false;
            if (t->weight[uncle] <= t->weight[node] &&
                !gi_tree_swap(t, node, uncle))
                return false;
        }
        ++t->weight[node];
        node = t->parent[node];
    }
    if (t->weight[t->root] >= GI_MAX_WEIGHT) {
        int32_t i;
        for (i = 0; i < t->nodes; ++i) t->weight[i] = gi_half(t->weight[i]);
    }
    return true;
}

static int32_t gi_tree_symbol(gi_tree *t, gi_input *in) {
    int32_t node = t->root, depth = 0;
    do {
        int bit = gi_in_bit(in);
        if (bit < 0 || ++depth > GI_MAX_NODES) return -1;
        node = bit ? t->right[node] : t->left[node];
        if (node < 0) return -1;
    } while (t->left[node] >= 0);
    if (node >= t->symbols || !gi_tree_update(t, node)) return -1;
    return node;
}

/* ---------------------------------------------------------------------- */
/* Decoder state and output                                                */

typedef struct gi_codec_s {
    gi_tree main_tree;
    gi_tree dist_tree;
    gi_tree len_tree;
    uint32_t recent[GI_RECENT];
    uint32_t position;
    uint8_t window[GI_WINDOW];
} gi_codec;

static bool gi_codec_reset(gi_codec *c) {
    uint32_t i;
    if (!gi_tree_init(&c->main_tree, GI_MAIN_SYMBOLS) ||
        !gi_tree_init(&c->dist_tree, GI_DIST_SYMBOLS) ||
        !gi_tree_init(&c->len_tree, GI_LEN_SYMBOLS))
        return false;
    for (i = 0U; i < GI_RECENT; ++i) c->recent[i] = i;
    c->position = 0U;
    xx_mem_zero(c->window, sizeof(c->window));
    return true;
}

typedef struct gi_sink_s {
    uint8_t *memory;      /**< Memory target of capacity bytes, or NULL. */
    size_t capacity;
    size_t length;
    xx_io_device *device; /**< Device target, or NULL. */
    uint8_t *buffer;      /**< GI_OUT_BUFFER bytes staging for device. */
    size_t staged;
    bool failed;
} gi_sink;

static bool gi_sink_flush(gi_sink *s) {
    size_t done = 0U;
    if (!s || !s->device || s->failed) return !s || !s->failed;
    while (done < s->staged) {
        ssize_t amount = xx_io_write(s->device, s->buffer + done,
                                     s->staged - done);
        if (amount <= 0 || (size_t)amount > s->staged - done) {
            s->failed = true;
            return false;
        }
        done += (size_t)amount;
    }
    s->staged = 0U;
    return true;
}

static void gi_sink_put(gi_sink *s, uint8_t value) {
    if (!s || s->failed) return;
    if (s->memory) {
        if (s->length >= s->capacity) {
            s->failed = true;
            return;
        }
        s->memory[s->length++] = value;
    }
    if (s->device) {
        s->buffer[s->staged++] = value;
        if (s->staged == GI_OUT_BUFFER) (void)gi_sink_flush(s);
    }
}

static int32_t gi_dist_base(int32_t slot) {
    int32_t base = 0, i;
    for (i = 0; i < slot; ++i) base += 1 << g_gi_dist_bits[i];
    return base;
}

/* Decodes one block body of exactly size bytes. */
static bool gi_unpack(gi_codec *c, gi_input *in, uint32_t size, gi_sink *sink,
                      xx_pd_struct *pd) {
    unsigned long counter = 0UL;
    while (size > 0U) {
        int32_t symbol;
        uint32_t length, distance;
        int32_t dsym;
        uint32_t index, source;
        if ((counter++ & GI_PD_MASK) == 0UL && gi_stopped(pd)) return false;
        if (sink && sink->failed) return false;
        symbol = gi_tree_symbol(&c->main_tree, in);
        if (symbol < 0) return false;
        if (symbol < 0x100) {
            gi_sink_put(sink, (uint8_t)symbol);
            c->window[c->position] = (uint8_t)symbol;
            c->position = (c->position + 1U) & (GI_WINDOW - 1U);
            --size;
            continue;
        }
        symbol -= 0x100;
        if (symbol >= GI_LENGTH_ESCAPE) {
            symbol = gi_tree_symbol(&c->len_tree, in);
            if (symbol < 0) return false;
            symbol += GI_LENGTH_ESCAPE;
        }
        length = (uint32_t)symbol + GI_MIN_LENGTH;
        dsym = gi_tree_symbol(&c->dist_tree, in);
        if (dsym < 0) return false;
        if (dsym < GI_DIST_SLOTS) {
            uint32_t extra = 0U, mask = 1U, bits = g_gi_dist_bits[dsym];
            for (index = 0U; index < bits; ++index) {
                int bit = gi_in_bit(in);
                if (bit < 0) return false;
                if (bit) extra |= mask;
                mask <<= 1U;
            }
            distance = (uint32_t)gi_dist_base(dsym) + extra;
        } else {
            distance = c->recent[dsym - GI_DIST_SLOTS];
        }
        index = 0U;
        while (index < GI_RECENT && c->recent[index] != distance) ++index;
        if (index > GI_RECENT - 1U) index = GI_RECENT - 1U;
        for (; index > 0U; --index) c->recent[index] = c->recent[index - 1U];
        c->recent[0] = distance;
        if (size < length) return false;
        size -= length;
        source = (c->position - length - distance) & (GI_WINDOW - 1U);
        for (index = 0U; index < length; ++index) {
            uint8_t value = c->window[source];
            gi_sink_put(sink, value);
            c->window[c->position] = value;
            source = (source + 1U) & (GI_WINDOW - 1U);
            c->position = (c->position + 1U) & (GI_WINDOW - 1U);
        }
    }
    return !(sink && sink->failed);
}

/* One block: its size word (checked against limit when limit != 0), then
 * the bit stream.  The input is left on the first byte not taken. */
static bool gi_block(gi_codec *c, gi_input *in, uint32_t limit,
                     uint32_t *decoded, gi_sink *sink, xx_pd_struct *pd) {
    uint32_t size;
    if (!gi_in_u32(in, &size) || (limit != 0U && size > limit)) return false;
    if (decoded) *decoded = size;
    if (!gi_unpack(c, in, size, sink, pd)) return false;
    gi_in_align(in);
    return true;
}

/* ---------------------------------------------------------------------- */
/* Member table                                                            */

typedef struct gi_member_s {
    char *name;           /**< UTF-8, '/' separated, unique among safe names. */
    bool safe;
    bool stored;
    uint32_t size;
    uint32_t attributes;
    uint64_t filetime;
    int64_t command_offset; /**< Absolute. */
    int64_t command_size;
    int64_t data_offset;    /**< Absolute. */
    int64_t data_size;      /**< Bytes the member data occupies. */
} gi_member;

typedef struct gi_replay_s {
    gi_input *input;
    gi_codec *codec;
    uint8_t *out_buffer;
    size_t next; /**< Member whose data the codec would decode next. */
} gi_replay;

typedef struct gi_list_s {
    gi_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    gi_location location;
    int64_t end;  /**< Base-relative end of what was walked. */
    bool complete;
    gi_replay replay;
} gi_list;

static void gi_list_free(void *opaque) {
    gi_list *list = (gi_list *)opaque;
    size_t index;
    if (!list) return;
    /* A walk without names only counts: items stays NULL then. */
    if (list->items) {
        for (index = 0U; index < list->count; ++index)
            if (list->items[index].name) xx_mem_free(list->items[index].name);
        xx_mem_free(list->items);
    }
    if (list->replay.input) xx_mem_free(list->replay.input);
    if (list->replay.codec) xx_mem_free(list->replay.codec);
    if (list->replay.out_buffer) xx_mem_free(list->replay.out_buffer);
    xx_mem_free(list);
}

static bool gi_list_add(gi_list *list, const gi_member *member) {
    if (list->count >= GI_MAX_MEMBERS) return false;
    if (list->count == list->capacity) {
        size_t grown = list->capacity ? list->capacity * 2U : 16U;
        gi_member *items;
        if (grown > GI_MAX_MEMBERS) grown = GI_MAX_MEMBERS;
        items = (gi_member *)xx_mem_realloc(list->items,
                                            grown * sizeof(*items));
        if (!items) return false;
        list->items = items;
        list->capacity = grown;
    }
    list->items[list->count++] = *member;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Names                                                                   */

static char gi_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* True when the component's stem (before its first dot, trailing spaces
 * ignored) is a Windows device name. */
static bool gi_is_device(const uint8_t *comp, size_t length) {
    static const char *const names[] = {"CON", "PRN", "AUX", "NUL",
                                        "CONIN$", "CONOUT$", "CLOCK$"};
    size_t stem = 0U, index, k;
    while (stem < length && comp[stem] != '.') ++stem;
    while (stem > 0U && comp[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        const char *n = names[index];
        for (k = 0U; k < stem && n[k]; ++k)
            if (gi_upper((char)comp[k]) != n[k]) break;
        if (k == stem && n[k] == 0) return true;
    }
    if (stem == 4U && comp[3] >= '0' && comp[3] <= '9') {
        char a = gi_upper((char)comp[0]), b = gi_upper((char)comp[1]),
             c = gi_upper((char)comp[2]);
        if ((a == 'C' && b == 'O' && c == 'M') ||
            (a == 'L' && b == 'P' && c == 'T'))
            return true;
    }
    return false;
}

/* A name is extracted only when it is relative, has no empty, "." or ".."
 * (or dots-and-spaces-only) component, no control byte, no character that
 * Windows reserves and no device-name component.  A component ending in a
 * dot or a space is refused too: Windows drops those, so "a.txt." would land
 * on the file of a different member "a.txt". */
static bool gi_name_safe(const uint8_t *raw, size_t length) {
    size_t start = 0U, index;
    if (length == 0U) return false;
    for (index = 0U; index < length; ++index) {
        uint8_t c = raw[index];
        if (c < 0x20U || c == 0x7FU || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|')
            return false;
    }
    for (index = 0U; index <= length; ++index) {
        if (index == length || raw[index] == '\\' || raw[index] == '/') {
            size_t k;
            bool meaningful = false;
            if (index == start) return false;
            for (k = start; k < index; ++k)
                if (raw[k] != '.' && raw[k] != ' ') meaningful = true;
            if (!meaningful || raw[index - 1U] == '.' ||
                raw[index - 1U] == ' ' ||
                gi_is_device(raw + start, index - start))
                return false;
            start = index + 1U;
        }
    }
    return true;
}

/* '\' becomes '/', bytes 0x80..0xFF are taken as Latin-1 and written as
 * UTF-8, control bytes and '%' as %XX.  suffix > 0 appends "_<suffix>"
 * before the last component's extension. */
static char *gi_make_name(const uint8_t *raw, size_t length, uint32_t suffix) {
    static const char hex[] = "0123456789ABCDEF";
    char digits[12];
    size_t ndigits = 0U, out_length = 0U, index, insert = length, o = 0U;
    char *name;
    if (suffix) {
        uint32_t v = suffix;
        size_t last_sep = 0U;
        bool has_sep = false;
        while (v && ndigits < sizeof(digits)) {
            digits[ndigits++] = (char)('0' + v % 10U);
            v /= 10U;
        }
        for (index = 0U; index < length; ++index)
            if (raw[index] == '\\' || raw[index] == '/') {
                last_sep = index;
                has_sep = true;
            }
        for (index = length; index > (has_sep ? last_sep + 1U : 0U);
             --index)
            if (raw[index - 1U] == '.') {
                insert = index - 1U;
                break;
            }
        if (insert == (has_sep ? last_sep + 1U : 0U)) insert = length;
    }
    for (index = 0U; index < length; ++index) {
        uint8_t c = raw[index];
        out_length += (c >= 0x80U) ? 2U
                      : (c < 0x20U || c == 0x7FU || c == '%') ? 3U
                                                               : 1U;
    }
    if (suffix) out_length += 1U + ndigits;
    name = (char *)xx_mem_alloc(out_length + 1U);
    if (!name) return NULL;
    for (index = 0U; index <= length; ++index) {
        uint8_t c;
        if (suffix && index == insert) {
            size_t d;
            name[o++] = '_';
            for (d = ndigits; d > 0U; --d) name[o++] = digits[d - 1U];
        }
        if (index == length) break;
        c = raw[index];
        if (c == '\\' || c == '/') {
            name[o++] = '/';
        } else if (c >= 0x80U) {
            name[o++] = (char)(0xC0U | (c >> 6U));
            name[o++] = (char)(0x80U | (c & 0x3FU));
        } else if (c < 0x20U || c == 0x7FU || c == '%') {
            name[o++] = '%';
            name[o++] = hex[c >> 4U];
            name[o++] = hex[c & 0x0FU];
        } else {
            name[o++] = (char)c;
        }
    }
    name[o] = 0;
    return name;
}

/* Names compare without case (output lands on case-insensitive file
 * systems); a small open-addressing set keeps duplicates linear. */
static uint32_t gi_hash(const char *s) {
    uint32_t h = 2166136261U;
    while (*s) {
        h ^= (uint8_t)gi_upper(*s++);
        h *= 16777619U;
    }
    return h;
}

static bool gi_same(const char *a, const char *b) {
    while (*a && *b && gi_upper(*a) == gi_upper(*b)) ++a, ++b;
    return *a == 0 && *b == 0;
}

/* Each slot also keeps the last suffix handed out for that name, so a run of
 * identical names costs one probe each instead of a quadratic search. */
typedef struct gi_names_s {
    const char **slots;
    uint32_t *hints;
    size_t mask;
} gi_names;

static size_t gi_names_find(const gi_names *set, const char *name) {
    size_t at = gi_hash(name) & set->mask;
    while (set->slots[at]) {
        if (gi_same(set->slots[at], name)) return at;
        at = (at + 1U) & set->mask;
    }
    return SIZE_MAX;
}

static void gi_names_insert(gi_names *set, const char *name) {
    size_t at = gi_hash(name) & set->mask;
    while (set->slots[at]) at = (at + 1U) & set->mask;
    set->slots[at] = name;
    set->hints[at] = 0U;
}

/* Gives m a name no earlier safe member uses.  The set holds at most count
 * names, so at most count + 1 suffixes are ever tried. */
static bool gi_unique_name(gi_names *set, gi_member *m, const uint8_t *raw,
                           size_t length, size_t count) {
    size_t slot = gi_names_find(set, m->name), tries = 0U;
    uint32_t suffix;
    char *candidate = NULL;
    if (slot == SIZE_MAX) {
        gi_names_insert(set, m->name);
        return true;
    }
    suffix = set->hints[slot];
    do {
        if (candidate) xx_mem_free(candidate);
        if (suffix == 0xFFFFFFFFU || ++tries > count + 1U) {
            m->safe = false; /* never reached with count <= GI_MAX_MEMBERS */
            return true;
        }
        candidate = gi_make_name(raw, length, ++suffix);
        if (!candidate) return false;
    } while (gi_names_find(set, candidate) != SIZE_MAX);
    set->hints[slot] = suffix;
    xx_mem_free(m->name);
    m->name = candidate;
    gi_names_insert(set, m->name);
    return true;
}

/* ---------------------------------------------------------------------- */
/* Walk                                                                    */

typedef struct gi_raw_names_s {
    uint8_t *bytes;
    size_t length;
    size_t capacity;
    size_t *offsets; /**< Per member: offset of its raw name in bytes. */
    size_t *lengths;
} gi_raw_names;

static bool gi_walk(Abstractformat *format, gi_list **result, bool names,
                    xx_pd_struct *pd) {
    gi_list *list = NULL;
    gi_input *in = NULL;
    gi_codec *cmd = NULL, *data = NULL;
    uint8_t *buffer = NULL;
    gi_raw_names raw;
    int64_t base, last_good;
    unsigned long commands = 0UL;
    size_t name_bytes = 0U;
    uint32_t decoded;
    bool ok = false;
    xx_mem_zero(&raw, sizeof(raw));
    if (!result) return false;
    *result = NULL;
    list = (gi_list *)xx_mem_calloc(1U, sizeof(*list));
    if (!list) return false;
    if (!gi_locate(format, &list->location)) goto done;
    base = format->base_address;
    in = (gi_input *)xx_mem_calloc(1U, sizeof(*in));
    cmd = (gi_codec *)xx_mem_alloc(sizeof(*cmd));
    data = (gi_codec *)xx_mem_alloc(sizeof(*data));
    buffer = (uint8_t *)xx_mem_alloc(GI_MAX_CMD);
    if (!in || !cmd || !data || !buffer || !gi_codec_reset(cmd) ||
        !gi_codec_reset(data))
        goto done;
    in->device = format->device;
    in->end = base + list->location.total;
    gi_in_seek(in, base + list->location.payload);

    /* The runtime image; its state is dropped afterwards. */
    if (!gi_block(cmd, in, GI_MAX_RUNTIME - 1U, &decoded, NULL, pd) ||
        decoded == 0U || !gi_codec_reset(cmd))
        goto done;
    {
        uint8_t header[GI_ARCHIVE_HEADER];
        size_t index;
        for (index = 0U; index < sizeof(header); ++index)
            if (!gi_in_byte(in, &header[index])) goto done;
        if (gi_le16(header + GI_ARCHIVE_HEADER_FLAG) != 0U) goto done;
    }
    {
        uint32_t index;
        for (index = 0U; index < GI_SKIPPED_BLOCKS; ++index)
            if (!gi_block(cmd, in, GI_MAX_SKIPPED_BLOCK, NULL, NULL, pd))
                goto done;
    }
    last_good = gi_in_tell(in);

    for (;;) {
        gi_sink sink;
        uint32_t size, tag;
        int64_t command_offset = gi_in_tell(in);
        const uint8_t *record;
        size_t record_size, name_end, name_length;
        gi_member member;
        if (++commands > GI_MAX_COMMANDS || gi_stopped(pd)) break;
        if (!gi_in_u32(in, &size) || size <= 2U || size > GI_MAX_CMD) break;
        xx_mem_zero(&sink, sizeof(sink));
        sink.memory = buffer;
        sink.capacity = GI_MAX_CMD;
        if (!gi_unpack(cmd, in, size, &sink, pd) || sink.length != size)
            break;
        gi_in_align(in);
        tag = gi_le16(buffer);
        if (tag == GI_TAG_END) {
            list->complete = true;
            last_good = gi_in_tell(in);
            break;
        }
        if (tag != GI_TAG_FILE) {
            last_good = gi_in_tell(in);
            continue;
        }
        record = buffer + GI_CMD_HEADER;
        record_size = size - GI_CMD_HEADER;
        if (record_size <= GI_RECORD_MIN) break;
        name_end = GI_RECORD_NAME_OFFSET;
        while (name_end < record_size && record[name_end] != 0U) ++name_end;
        if (name_end >= record_size || name_end == GI_RECORD_NAME_OFFSET ||
            name_end - GI_RECORD_NAME_OFFSET > GI_MAX_NAME)
            break;
        xx_mem_zero(&member, sizeof(member));
        member.attributes = gi_le32(record);
        member.size = gi_le32(record + GI_RECORD_SIZE_OFFSET);
        if (member.size > 0x7FFFFFFFU) break;
        member.filetime = gi_le64(record + GI_RECORD_TIME_OFFSET);
        member.stored = record[GI_RECORD_STORED_OFFSET] == 0U;
        member.command_offset = command_offset;
        member.command_size = gi_in_tell(in) - command_offset;
        member.data_offset = gi_in_tell(in);
        if (member.stored) {
            if ((int64_t)member.size > in->end - member.data_offset) break;
            gi_in_seek(in, member.data_offset + (int64_t)member.size);
        } else {
            uint32_t block_size;
            int64_t probe = member.data_offset;
            uint8_t word[4];
            if (probe + 4 > in->end ||
                !gi_read_at(in->device, probe, word, sizeof(word)))
                break;
            block_size = gi_le32(word);
            if (block_size != member.size ||
                !gi_block(data, in, 0U, NULL, NULL, pd))
                break;
        }
        member.data_size = gi_in_tell(in) - member.data_offset;
        /* The same caps with and without names, so counting and listing
         * always agree on where the chain stops. */
        name_length = name_end - GI_RECORD_NAME_OFFSET;
        if (list->count >= GI_MAX_MEMBERS ||
            name_bytes + name_length > GI_MAX_NAME_BYTES)
            break;
        name_bytes += name_length;
        if (names) {
            if (raw.length + name_length > raw.capacity) {
                size_t grown = raw.capacity ? raw.capacity * 2U : 4096U;
                uint8_t *bytes;
                while (grown < raw.length + name_length) grown *= 2U;
                bytes = (uint8_t *)xx_mem_realloc(raw.bytes, grown);
                if (!bytes) goto done;
                raw.bytes = bytes;
                raw.capacity = grown;
            }
            xx_rt_memcpy(raw.bytes + raw.length,
                         record + GI_RECORD_NAME_OFFSET, name_length);
            if (list->count == list->capacity) {
                size_t grown = list->capacity ? list->capacity * 2U : 16U;
                size_t *offsets, *lengths;
                if (grown > GI_MAX_MEMBERS) grown = GI_MAX_MEMBERS;
                offsets = (size_t *)xx_mem_realloc(raw.offsets,
                                                   grown * sizeof(size_t));
                if (!offsets) goto done;
                raw.offsets = offsets;
                lengths = (size_t *)xx_mem_realloc(raw.lengths,
                                                   grown * sizeof(size_t));
                if (!lengths) goto done;
                raw.lengths = lengths;
            }
            raw.offsets[list->count] = raw.length;
            raw.lengths[list->count] = name_length;
            raw.length += name_length;
            if (!gi_list_add(list, &member)) goto done;
        } else {
            ++list->count;
        }
        last_good = gi_in_tell(in);
    }

    /* A chain that stops early is a truncated carrier: keep what decoded
     * whole.  Nothing at all is not an installer this reader understands. */
    if (!list->complete && list->count == 0U) goto done;
    list->end = last_good - base;

    if (names && list->count) {
        gi_names set;
        size_t slots = 16U, index;
        while (slots < list->count * 2U) slots *= 2U;
        set.slots = (const char **)xx_mem_calloc(slots, sizeof(char *));
        set.hints = (uint32_t *)xx_mem_calloc(slots, sizeof(uint32_t));
        set.mask = slots - 1U;
        if (!set.slots || !set.hints) {
            if (set.slots) xx_mem_free((void *)set.slots);
            if (set.hints) xx_mem_free(set.hints);
            goto done;
        }
        for (index = 0U; index < list->count; ++index) {
            gi_member *m = &list->items[index];
            const uint8_t *r = raw.bytes + raw.offsets[index];
            size_t n = raw.lengths[index];
            m->safe = gi_name_safe(r, n);
            m->name = gi_make_name(r, n, 0U);
            if (!m->name) break;
            if (m->safe && !gi_unique_name(&set, m, r, n, list->count)) break;
        }
        xx_mem_free((void *)set.slots);
        xx_mem_free(set.hints);
        if (index != list->count) goto done;
    }
    ok = true;
done:
    if (raw.bytes) xx_mem_free(raw.bytes);
    if (raw.offsets) xx_mem_free(raw.offsets);
    if (raw.lengths) xx_mem_free(raw.lengths);
    if (in) xx_mem_free(in);
    if (cmd) xx_mem_free(cmd);
    if (data) xx_mem_free(data);
    if (buffer) xx_mem_free(buffer);
    if (!ok) {
        gi_list_free(list);
        return false;
    }
    *result = list;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Extraction                                                              */

static bool gi_replay_member(Abstractformat *format, gi_list *list,
                             size_t target, xx_io_device *destination,
                             xx_pd_struct *pd) {
    gi_replay *r = &list->replay;
    gi_sink sink;
    const gi_member *m;
    size_t k;
    if (target >= list->count) return false;
    if (!r->input) {
        r->input = (gi_input *)xx_mem_calloc(1U, sizeof(*r->input));
        r->codec = (gi_codec *)xx_mem_alloc(sizeof(*r->codec));
        r->out_buffer = (uint8_t *)xx_mem_alloc(GI_OUT_BUFFER);
        if (!r->input || !r->codec || !r->out_buffer) return false;
        r->next = SIZE_MAX;
    }
    r->input->device = format->device;
    r->input->end = format->base_address + list->location.total;
    if (r->next > target) {
        if (!gi_codec_reset(r->codec)) return false;
        r->next = 0U;
    }
    /* Everything compressed ahead of the target feeds the shared state. */
    for (k = r->next; k < target; ++k) {
        m = &list->items[k];
        r->next = SIZE_MAX;
        if (!m->stored) {
            gi_in_seek(r->input, m->data_offset);
            if (!gi_block(r->codec, r->input, 0U, NULL, NULL, pd))
                return false;
        }
        r->next = k + 1U;
    }
    m = &list->items[target];
    xx_mem_zero(&sink, sizeof(sink));
    sink.device = destination;
    sink.buffer = r->out_buffer;
    r->next = SIZE_MAX;
    if (m->stored) {
        int64_t done = 0;
        gi_in_seek(r->input, m->data_offset);
        while (done < (int64_t)m->size) {
            uint8_t value;
            if ((done & (int64_t)GI_PD_MASK) == 0 && gi_stopped(pd))
                return false;
            if (!gi_in_byte(r->input, &value)) return false;
            if (destination) gi_sink_put(&sink, value);
            ++done;
        }
    } else {
        uint32_t decoded = 0U;
        gi_in_seek(r->input, m->data_offset);
        if (!gi_block(r->codec, r->input, 0U, &decoded,
                      destination ? &sink : NULL, pd) ||
            decoded != m->size)
            return false;
    }
    r->next = target + 1U;
    return gi_sink_flush(&sink);
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

static bool gi_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *gi_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool gi_set_record(xx_archive_record *record, const gi_member *m) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = m->command_offset;
    record->header_size = m->command_size;
    record->data_offset = m->data_offset;
    record->compressed_size = m->data_size;
    return xx_archive_record_set_original_name(record, m->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          m->size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)m->data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          m->stored ? 0U : 1U) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                          m->attributes) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                          m->filetime) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */

void xx_gentee_installer_init(xx_gentee_installer *archive,
                              xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_GENTEE_INSTALLER_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format,
                            "application/x-gentee-installer");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid = xx_gentee_installer_check_is_valid;
    archive->format.handle_base_info = xx_gentee_installer_handle_base_info;
    archive->format.get_format_size = xx_gentee_installer_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_gentee_installer_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_gentee_installer_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_gentee_installer_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_gentee_installer_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_gentee_installer_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_gentee_installer_free_archive_records_reading;
    archive->payload_offset = -1;
}

xx_gentee_installer *xx_gentee_installer_create(xx_io_device *device,
                                                int64_t base_address) {
    xx_gentee_installer *archive =
        (xx_gentee_installer *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_gentee_installer_init(archive, device, base_address);
    return archive;
}

void xx_gentee_installer_destroy(xx_gentee_installer *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_gentee_installer_free(xx_gentee_installer *archive) {
    if (!archive) return;
    xx_gentee_installer_destroy(archive);
    xx_mem_free(archive);
}

/* Cheap: the PE headers, the locator and one 12-byte payload header. */
bool xx_gentee_installer_check_is_valid(Abstractformat *format,
                                        xx_pd_struct *pd) {
    gi_location location;
    (void)pd;
    return gi_locate(format, &location);
}

bool xx_gentee_installer_handle_base_info(Abstractformat *format,
                                          xx_pd_struct *pd) {
    gi_list *list;
    xx_gentee_installer *archive;
    if (!format) return false;
    if (!gi_walk(format, &list, false, pd)) {
        format->is_valid = false;
        format->base_info_handled = false;
        format->format_size = -1;
        format->number_of_archive_records = 0U;
        return false;
    }
    archive = (xx_gentee_installer *)format;
    archive->number_of_records = list->count;
    archive->payload_offset = format->base_address + list->location.payload;
    archive->runtime_size = list->location.runtime_size;
    archive->complete = list->complete;
    format->number_of_archive_records = list->count;
    format->format_size = list->end;
    format->overlay_offset = -1;
    format->overlay_size = 0;
    format->file_type = XX_GENTEE_INSTALLER_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    gi_list_free(list);
    return true;
}

int64_t xx_gentee_installer_get_format_size(Abstractformat *format,
                                            xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_gentee_installer_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_gentee_installer_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_gentee_installer_handle_base_info(format, pd))
               ? ((xx_gentee_installer *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_gentee_installer_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    gi_list *list;
    xx_archive_record_state *state;
    if (!gi_walk(format, &list, true, pd)) return NULL;
    if (list->count == 0U) {
        gi_list_free(list);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        gi_list_free(list);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = list;
    state->free_internal = gi_list_free;
    state->total_records = (int64_t)list->count;
    if (!gi_copy_options(&state->options, options) ||
        !gi_set_record(&state->current_record, &list->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_gentee_installer_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_gentee_installer_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    gi_list *list;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(list = (gi_list *)state->internal_state) ||
        ++list->index >= list->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record =
        gi_set_record(&state->current_record, &list->items[list->index]);
    return state->has_record;
}

bool xx_gentee_installer_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    gi_list *list;
    gi_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(list = (gi_list *)state->internal_state) ||
        list->index >= list->count || gi_stopped(pd))
        return false;
    member = &list->items[list->index];
    path_option = gi_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option)
        return gi_replay_member(format, list, list->index, NULL, pd);
    if (!member->safe || !member->name) return false;
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
        result = gi_replay_member(format, list, list->index, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_rt_remove(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_gentee_installer_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
