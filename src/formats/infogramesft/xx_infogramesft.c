/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Infogrames / Alone in the Dark engine .PAK resource container.  The layout,
 * the table-versus-chain validation rule and the method-1 decoder are ported
 * from XArchive (games/xinfogramespak.cpp, Algos/xinfogramespakdecoder.cpp);
 * xx_infogramesft.h carries the field table.
 *
 * The container is headerless, so acceptance rests on two independent
 * descriptions of the same set of records agreeing: the offset table and the
 * record chain.  Every non-zero table slot must land exactly on a chain record
 * header, the chain must tile the file, and one compressed member must decode
 * to exactly its declared size before the file is accepted.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/infogramesft/xx_infogramesft.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>

/* Registration placeholder.  xxfc_defs.h is shared and is not edited from
 * here, so the alias macro defined next to the enumerator is tested instead;
 * this picks up the real file type as soon as INFOGRAMESFT is registered. */
#ifdef INFOGRAMESFT
#define XX_INFOGRAMESFT_FILE_TYPE XX_FILE_TYPE_INFOGRAMESFT
#else
#define XX_INFOGRAMESFT_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

/* +0x00 extra size | +0x04 packed | +0x08 unpacked | +0x0c method |
 * +0x0d info | +0x0e inline descriptor size.  Sixteen bytes account for every
 * header byte; the record carries no timestamp of its own. */
#define IPAK_RECORD_HEADER_SIZE 16
/* A table of at least three slots: [0] = 0, [1] = table size, [2] = one id. */
#define IPAK_MIN_TABLE_SIZE 12
#define IPAK_MIN_FILE_SIZE (IPAK_MIN_TABLE_SIZE + IPAK_RECORD_HEADER_SIZE)
#define IPAK_MAX_ENTRIES 1048576
#define IPAK_MAX_UNCOMPRESSED_SIZE 0x10000000 /* 256 MB sanity cap */
#define IPAK_MAX_EXTRA_SIZE 0x10000
#define IPAK_MAX_DESCRIPTOR_SIZE 4096
/* The chain tiles the file, so the only slack an archive may end on is the
 * PKZIP crumb that closes it. */
#define IPAK_MAX_TAIL 4096

#define IPAK_METHOD_STORED 0x00U
#define IPAK_METHOD_IMPLODE 0x01U
#define IPAK_METHOD_DEFLATE 0x04U

/* Inline descriptor: 0x49, its own total size, then a NUL-padded 8.3 name. */
#define IPAK_DESCRIPTOR_TAG 0x49U
#define IPAK_MIN_NAMED_DESCRIPTOR 3
#define IPAK_CRUMB_SIZE 16
#define IPAK_DESCRIPTOR_PROBE 64
#define IPAK_STEP_PROBE \
    (IPAK_CRUMB_SIZE + IPAK_RECORD_HEADER_SIZE + IPAK_DESCRIPTOR_PROBE)

/* 5 digits, '_', an 8.3 name, NUL. */
#define IPAK_NAME_SIZE 24

typedef struct ipak_member_s {
    char name[IPAK_NAME_SIZE];
    int32_t resource_id; /**< Lowest table slot naming this record, or -1. */
    int64_t crumb_offset; /**< Start of the PKZIP crumb, or -1. */
    int64_t header_offset;
    int64_t data_offset;
    int64_t packed_size;
    int64_t unpacked_size;
    uint32_t extra_size;
    uint16_t descriptor_size;
    uint16_t crc16;
    uint16_t dos_time;
    uint16_t dos_date;
    uint8_t method;
    bool has_crc;
    bool has_stored_name;
} ipak_member;

/* A non-zero table slot: the record offset it names and the resource id it
 * stands for.  Sorted by (offset, id) so that the record walk can look each
 * offset up and take the lowest id that names it. */
typedef struct ipak_slot_s {
    uint32_t offset;
    uint32_t id;
} ipak_slot;

typedef struct ipak_stream_s {
    ipak_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    int64_t archive_size;
    int64_t table_size;
} ipak_stream;

static uint16_t ipak_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static uint32_t ipak_le32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
           ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
}

static bool ipak_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static bool ipak_range_within(int64_t total, int64_t offset, int64_t size) {
    return total >= 0 && offset >= 0 && size >= 0 && offset <= total &&
           size <= total - offset;
}

static bool ipak_is_known_method(uint8_t method) {
    return method == IPAK_METHOD_STORED || method == IPAK_METHOD_IMPLODE ||
           method == IPAK_METHOD_DEFLATE;
}

/* ------------------------------------------------------------------ */
/* Method 1: the Infogrames implode variant.                           */
/*                                                                     */
/* An "imploded" stream in the PKZIP method-6 family: two Shannon-Fano */
/* trees of 64 symbols each (length tree first, then distance tree)    */
/* written as run-length pairs, then a bit stream of 1-bit literal     */
/* flags.  It is NOT the ZIP explode decoder: the dictionary is fixed  */
/* at 4K, literals are always raw 8-bit, and the minimum match length  */
/* is still 2 - the combination PKZIP never emits.  Copies may reach   */
/* in front of the member; the window is pre-filled with zeroes, and   */
/* real archives depend on that (a leading run of zeroes is coded as a */
/* back-reference), so it is an exact substitution, not a guard.       */
/* ------------------------------------------------------------------ */

#define IPAK_MAXBITS 16
#define IPAK_NSYMBOLS 64
#define IPAK_DICT_BITS 6
#define IPAK_MIN_MATCH 2
#define IPAK_LENGTH_ESCAPE 63

typedef struct ipak_bits_s {
    const uint8_t *data;
    int64_t size;
    int64_t position;
    uint32_t buffer;
    int32_t count;
} ipak_bits;

static bool ipak_bits_read(ipak_bits *bits, int32_t need, uint32_t *value) {
    if (!bits || !value || need < 0 || need > 8) return false;
    while (bits->count < need) {
        if (bits->position >= bits->size) return false;
        bits->buffer |= (uint32_t)bits->data[bits->position] <<
                        (uint32_t)bits->count;
        ++bits->position;
        bits->count += 8;
    }
    *value = bits->buffer & ((UINT32_C(1) << need) - 1U);
    bits->buffer >>= need;
    bits->count -= need;
    return true;
}

/* Canonical prefix code ordered by (length, symbol) ascending.  Codes arrive
 * most-significant-bit-first and every bit is inverted - the PKWARE
 * convention. */
typedef struct ipak_tree_s {
    int32_t count[IPAK_MAXBITS + 1];
    int32_t symbol[IPAK_NSYMBOLS];
} ipak_tree;

/* A tree is one count byte (pairs - 1) then that many pair bytes, each
 * carrying (bit length - 1) low and (repeat count - 1) high.  The runs must
 * cover all 64 symbols exactly; that exact-cover rule is the format's
 * cheapest self-check. */
static bool ipak_read_tree(const uint8_t *data, int64_t size,
                           int64_t *position, ipak_tree *tree) {
    uint8_t lengths[IPAK_NSYMBOLS];
    int32_t offsets[IPAK_MAXBITS + 2];
    int64_t at;
    int32_t pairs, filled = 0, i, length;
    if (!data || !position || !tree) return false;
    at = *position;
    if (at < 0 || at >= size) return false;
    pairs = (int32_t)data[at] + 1;
    ++at;
    for (i = 0; i < pairs; ++i) {
        uint8_t pair;
        int32_t run_length, repeat, j;
        if (at >= size) return false;
        pair = data[at];
        ++at;
        run_length = (int32_t)(pair & 0x0fU) + 1;
        repeat = (int32_t)((pair >> 4U) & 0x0fU) + 1;
        if (filled + repeat > IPAK_NSYMBOLS) return false;
        for (j = 0; j < repeat; ++j) lengths[filled++] = (uint8_t)run_length;
    }
    if (filled != IPAK_NSYMBOLS) return false;
    for (i = 0; i <= IPAK_MAXBITS; ++i) tree->count[i] = 0;
    for (i = 0; i < IPAK_NSYMBOLS; ++i) tree->count[lengths[i]]++;
    offsets[0] = 0;
    offsets[1] = 0;
    for (length = 1; length <= IPAK_MAXBITS; ++length)
        offsets[length + 1] = offsets[length] + tree->count[length];
    for (i = 0; i < IPAK_NSYMBOLS; ++i) {
        tree->symbol[offsets[lengths[i]]] = i;
        offsets[lengths[i]]++;
    }
    *position = at;
    return true;
}

static bool ipak_decode_symbol(ipak_bits *bits, const ipak_tree *tree,
                               int32_t *symbol) {
    int32_t code = 0, first = 0, index = 0, length;
    if (!bits || !tree || !symbol) return false;
    for (length = 1; length <= IPAK_MAXBITS; ++length) {
        uint32_t bit = 0U;
        int32_t count;
        if (!ipak_bits_read(bits, 1, &bit)) return false;
        code |= (int32_t)(bit ^ 1U);
        count = tree->count[length];
        if ((code - first) < count) {
            int32_t position = index + (code - first);
            if (position < 0 || position >= IPAK_NSYMBOLS) return false;
            *symbol = tree->symbol[position];
            return true;
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return false;
}

/* Decode `packed` into `output` of exactly `expected` bytes.  `consumed`
 * receives the number of packed bytes the stream actually used. */
static bool ipak_implode_decode(const uint8_t *packed, int64_t packed_size,
                                uint8_t *output, int64_t expected,
                                int64_t *consumed) {
    ipak_tree length_tree, distance_tree;
    ipak_bits bits;
    int64_t position = 0, produced = 0;
    if (!packed || !output || expected < 0 || packed_size < 0) return false;
    if (packed_size == 0) return expected == 0;
    if (!ipak_read_tree(packed, packed_size, &position, &length_tree) ||
        !ipak_read_tree(packed, packed_size, &position, &distance_tree))
        return false;
    bits.data = packed + position;
    bits.size = packed_size - position;
    bits.position = 0;
    bits.buffer = 0U;
    bits.count = 0;
    while (produced < expected) {
        uint32_t flag = 0U, low = 0U;
        int32_t distance_code = 0, length_code = 0;
        int64_t distance, length, i;
        if (!ipak_bits_read(&bits, 1, &flag)) return false;
        if (flag) {
            uint32_t literal = 0U;
            if (!ipak_bits_read(&bits, 8, &literal)) return false;
            output[produced++] = (uint8_t)literal;
            continue;
        }
        if (!ipak_bits_read(&bits, IPAK_DICT_BITS, &low) ||
            !ipak_decode_symbol(&bits, &distance_tree, &distance_code))
            return false;
        distance = ((int64_t)distance_code << IPAK_DICT_BITS) + (int64_t)low +
                   1;
        if (!ipak_decode_symbol(&bits, &length_tree, &length_code))
            return false;
        length = length_code;
        if (length_code == IPAK_LENGTH_ESCAPE) {
            uint32_t extra = 0U;
            if (!ipak_bits_read(&bits, 8, &extra)) return false;
            length += (int64_t)extra;
        }
        length += IPAK_MIN_MATCH;
        /* The final copy of a stream may overshoot the declared size; the
         * declared size is authoritative, so the overshoot is dropped rather
         * than treated as corruption. */
        for (i = 0; i < length; ++i) {
            int64_t source = produced - distance;
            uint8_t value = source >= 0 ? output[source] : (uint8_t)0U;
            if (produced >= expected) break;
            output[produced++] = value;
        }
    }
    if (consumed) *consumed = position + bits.position;
    return produced == expected;
}

/* ------------------------------------------------------------------ */

static void ipak_stream_free(void *opaque) {
    ipak_stream *stream = (ipak_stream *)opaque;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static bool ipak_add_member(ipak_stream *stream, const ipak_member *member) {
    if (!stream || !member || stream->count >= (size_t)IPAK_MAX_ENTRIES)
        return false;
    if (stream->count == stream->capacity) {
        size_t capacity = stream->capacity == 0U ? 64U : stream->capacity * 2U;
        ipak_member *grown;
        if (capacity > SIZE_MAX / sizeof(*grown)) return false;
        grown = (ipak_member *)(stream->items
                                    ? xx_mem_realloc(stream->items,
                                                     capacity * sizeof(*grown))
                                    : xx_mem_alloc(capacity * sizeof(*grown)));
        if (!grown) return false;
        stream->items = grown;
        stream->capacity = capacity;
    }
    stream->items[stream->count++] = *member;
    return true;
}

/* Heapsort by (offset, id) ascending.  Both keys matter: the walk takes the
 * FIRST entry for an offset, which has to be the lowest id naming it. */
static bool ipak_slot_less(const ipak_slot *a, const ipak_slot *b) {
    return a->offset != b->offset ? a->offset < b->offset : a->id < b->id;
}

static void ipak_sift(ipak_slot *slots, size_t start, size_t count) {
    size_t root = start;
    while (root * 2U + 1U < count) {
        size_t child = root * 2U + 1U;
        ipak_slot swap;
        if (child + 1U < count &&
            ipak_slot_less(&slots[child], &slots[child + 1U]))
            ++child;
        if (!ipak_slot_less(&slots[root], &slots[child])) return;
        swap = slots[root];
        slots[root] = slots[child];
        slots[child] = swap;
        root = child;
    }
}

static void ipak_sort_slots(ipak_slot *slots, size_t count) {
    size_t index;
    if (count < 2U) return;
    index = count / 2U;
    while (index != 0U) {
        --index;
        ipak_sift(slots, index, count);
    }
    index = count;
    while (index > 1U) {
        ipak_slot swap = slots[0];
        --index;
        slots[0] = slots[index];
        slots[index] = swap;
        ipak_sift(slots, 0U, index);
    }
}

/* Index of the first slot naming `offset`, or (size_t)-1. */
static size_t ipak_find_slot(const ipak_slot *slots, size_t count,
                             uint32_t offset) {
    size_t low = 0U, high = count;
    while (low < high) {
        size_t middle = low + (high - low) / 2U;
        if (slots[middle].offset < offset)
            low = middle + 1U;
        else
            high = middle;
    }
    return (low < count && slots[low].offset == offset) ? low : (size_t)-1;
}

/* The 8.3 name is NUL padded to the end of its field.  A name that fills the
 * field exactly leaves no terminator, so the scan is bounded by the field
 * size and never by a terminator search. */
static bool ipak_decode_name(const uint8_t *field, size_t size, char *out,
                             size_t out_size) {
    size_t index = 0U;
    if (!field || !out || out_size == 0U || size >= out_size) return false;
    while (index < size) {
        uint8_t c = field[index];
        if (c == 0U) break;
        if (c < 0x20U || c > 0x7eU || c == '/' || c == '\\' || c == ':')
            return false;
        out[index++] = (char)c;
    }
    out[index] = 0;
    return index != 0U;
}

/* Write `value` as five decimal digits into `out`, returning the count. */
static size_t ipak_write_id(char *out, uint32_t value) {
    size_t index;
    uint32_t divisor = 10000U;
    for (index = 0U; index < 5U; ++index) {
        out[index] = (char)('0' + (char)((value / divisor) % 10U));
        divisor /= 10U;
    }
    return 5U;
}

static bool ipak_decode_member(Abstractformat *format,
                               const ipak_member *member, uint8_t **plain,
                               size_t *plain_size);

static bool ipak_parse(Abstractformat *format, ipak_stream **result) {
    uint8_t prefix[8];
    uint8_t *table = NULL;
    ipak_slot *slots = NULL;
    ipak_stream *stream = NULL;
    int64_t total, size, table_size, position, footer_offset = -1;
    size_t slot_count, used_slots = 0U, distinct = 0U, resolved = 0U, index;
    bool any_payload = false, name_by_id;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)IPAK_MIN_FILE_SIZE ||
        !ipak_read_at(format->device, format->base_address, prefix,
                      sizeof(prefix)))
        return false;
    /* Slot 0 is a hard zero in every known archive.  It is the only fixed
     * byte pattern this headerless format has, so it stays a reject. */
    if (ipak_le32(prefix) != 0U) return false;
    table_size = (int64_t)ipak_le32(prefix + 4U);
    if (table_size < IPAK_MIN_TABLE_SIZE || (table_size % 4) != 0) return false;
    if (!ipak_range_within(size, 0, table_size)) return false;
    if (table_size + IPAK_RECORD_HEADER_SIZE > size) return false;
    slot_count = (size_t)(table_size / 4);
    if (slot_count - 1U > (size_t)IPAK_MAX_ENTRIES) return false;

    table = (uint8_t *)xx_mem_alloc((size_t)table_size);
    slots = (ipak_slot *)xx_mem_alloc((slot_count - 1U) * sizeof(*slots));
    if (!table || !slots ||
        !ipak_read_at(format->device, format->base_address, table,
                      (size_t)table_size))
        goto fail;
    /* Slot -> record offset is many-to-one.  A slot may repeat and may point
     * backwards, but never into the table and never past the file. */
    for (index = 1U; index < slot_count; ++index) {
        uint32_t slot = ipak_le32(table + index * 4U);
        if (slot == 0U) continue;
        if ((int64_t)slot < table_size || (int64_t)slot >= size) goto fail;
        slots[used_slots].offset = slot;
        slots[used_slots].id = (uint32_t)(index - 1U);
        ++used_slots;
    }
    if (used_slots == 0U) goto fail;
    ipak_sort_slots(slots, used_slots);
    distinct = 1U;
    for (index = 1U; index < used_slots; ++index)
        if (slots[index].offset != slots[index - 1U].offset) ++distinct;

    stream = (ipak_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) goto fail;

    /* Membership is the record chain, not the table: walk from the first
     * record to the last byte of the file. */
    position = table_size;
    while (position < size) {
        uint8_t probe[IPAK_STEP_PROBE];
        const uint8_t *record;
        ipak_member member;
        int64_t probe_size, header_shift = 0, record_offset, extra_size;
        int64_t packed_size, unpacked_size, descriptor_size, skip, data_offset;
        size_t found;
        uint8_t method, info;
        if (stream->count >= (size_t)IPAK_MAX_ENTRIES) goto fail;
        if (size - position < (int64_t)IPAK_RECORD_HEADER_SIZE) {
            footer_offset = position;
            break;
        }
        probe_size = size - position < (int64_t)IPAK_STEP_PROBE
                         ? size - position : (int64_t)IPAK_STEP_PROBE;
        if (!ipak_read_at(format->device, format->base_address + position,
                          probe, (size_t)probe_size)) goto fail;
        /* A central directory crumb carries no member: it closes the chain. */
        if (xx_rt_memcmp(probe, "PK\x01\x02", 4U) == 0) {
            footer_offset = position;
            break;
        }
        if (xx_rt_memcmp(probe, "PK\x03\x04", 4U) == 0) {
            header_shift = IPAK_CRUMB_SIZE;
            if (probe_size - header_shift < (int64_t)IPAK_RECORD_HEADER_SIZE) {
                /* A local crumb with no room for the record it announces is
                 * the head of a member that was cut off with the rest. */
                footer_offset = position;
                break;
            }
        }
        record_offset = position + header_shift;
        record = probe + header_shift;
        extra_size = (int64_t)ipak_le32(record);
        packed_size = (int64_t)ipak_le32(record + 4U);
        unpacked_size = (int64_t)ipak_le32(record + 8U);
        method = record[12];
        info = record[13];
        descriptor_size = (int64_t)ipak_le16(record + 14U);
        if (!ipak_is_known_method(method)) goto fail;
        /* The info byte is the codec parameter slot of the engine's own
         * loader.  It is zero on every reference record; a non-zero value
         * would mean a codec variant this reader was never validated
         * against, so it is rejected rather than guessed at. */
        if (info != 0U) goto fail;
        if (extra_size > IPAK_MAX_EXTRA_SIZE) goto fail;
        if (descriptor_size > IPAK_MAX_DESCRIPTOR_SIZE) goto fail;
        if (unpacked_size > IPAK_MAX_UNCOMPRESSED_SIZE) goto fail;
        if (packed_size > IPAK_MAX_UNCOMPRESSED_SIZE) goto fail;
        skip = (int64_t)IPAK_RECORD_HEADER_SIZE + descriptor_size + extra_size;
        if (!ipak_range_within(size, record_offset, skip)) goto fail;
        data_offset = record_offset + skip;
        if (!ipak_range_within(size, data_offset, packed_size)) goto fail;
        if (method == IPAK_METHOD_STORED) {
            if (packed_size != unpacked_size) goto fail;
        } else {
            if (packed_size <= 0 || unpacked_size <= 0) goto fail;
            if (method == IPAK_METHOD_IMPLODE && packed_size < 3) goto fail;
        }

        xx_mem_zero(&member, sizeof(member));
        member.resource_id = -1;
        member.crumb_offset = header_shift != 0 ? format->base_address + position
                                                : -1;
        member.header_offset = format->base_address + record_offset;
        member.data_offset = format->base_address + data_offset;
        member.packed_size = packed_size;
        member.unpacked_size = unpacked_size;
        member.method = method;
        member.extra_size = (uint32_t)extra_size;
        member.descriptor_size = (uint16_t)descriptor_size;
        if (record_offset <= (int64_t)UINT32_MAX) {
            found = ipak_find_slot(slots, used_slots, (uint32_t)record_offset);
            if (found != (size_t)-1) {
                member.resource_id = (int32_t)slots[found].id;
                ++resolved;
            }
        }
        if (descriptor_size >= IPAK_MIN_NAMED_DESCRIPTOR &&
            header_shift + IPAK_RECORD_HEADER_SIZE + descriptor_size <=
                probe_size &&
            record[IPAK_RECORD_HEADER_SIZE] == IPAK_DESCRIPTOR_TAG &&
            (int64_t)record[IPAK_RECORD_HEADER_SIZE + 1] == descriptor_size)
            member.has_stored_name = ipak_decode_name(
                record + IPAK_RECORD_HEADER_SIZE + 2,
                (size_t)(descriptor_size - 2), member.name, IPAK_NAME_SIZE);
        if (header_shift != 0) {
            member.has_crc = true;
            member.dos_time = ipak_le16(probe + 10U);
            member.dos_date = ipak_le16(probe + 12U);
            member.crc16 = ipak_le16(probe + 14U);
        }
        if (unpacked_size > 0) any_payload = true;
        if (!ipak_add_member(stream, &member)) goto fail;
        /* Every step consumes at least the sixteen header bytes, so the walk
         * always advances and the loop always terminates. */
        position = data_offset + packed_size;
    }
    if (stream->count == 0U || !any_payload) goto fail;
    /* Every resource id has to have landed on a chain record.  This is what
     * rules out a file whose first two words merely read like a table: the
     * table and the chain are two independent descriptions of the same set of
     * records, and on real archives they agree exactly. */
    if (resolved != distinct) goto fail;
    if (footer_offset >= 0 && size - footer_offset > IPAK_MAX_TAIL) goto fail;

    /* Members keep chain order.  The published number is the resource id,
     * which is unique per record because only the lowest id per offset is
     * kept.  If any record has no id at all the whole listing falls back to
     * chain positions, so two members can never race for one output name. */
    name_by_id = resolved == stream->count;
    for (index = 0U; index < stream->count; ++index) {
        ipak_member *member = &stream->items[index];
        uint32_t number = name_by_id ? (uint32_t)member->resource_id
                                     : (uint32_t)index;
        char stored[IPAK_NAME_SIZE];
        size_t at, copied = 0U;
        if (member->has_stored_name) {
            while (copied < IPAK_NAME_SIZE && member->name[copied] != 0) {
                stored[copied] = member->name[copied];
                ++copied;
            }
        }
        at = ipak_write_id(member->name, number);
        if (copied != 0U) {
            size_t source = 0U;
            member->name[at++] = '_';
            while (source < copied && at + 1U < IPAK_NAME_SIZE)
                member->name[at++] = stored[source++];
        } else {
            const char *suffix = ".bin";
            size_t source = 0U;
            while (suffix[source] != 0 && at + 1U < IPAK_NAME_SIZE)
                member->name[at++] = suffix[source++];
        }
        member->name[at] = 0;
    }

    /* Bounded trial decode: the format has no magic, so one real member has
     * to come out at exactly its declared size before the file is accepted.
     * A compressed member is picked over a stored one because a stored member
     * proves nothing about the codec. */
    for (index = 0U; index < stream->count; ++index) {
        uint8_t *plain = NULL;
        size_t plain_size = 0U;
        bool decoded;
        if (stream->items[index].method == IPAK_METHOD_STORED ||
            stream->items[index].unpacked_size <= 0) continue;
        decoded = ipak_decode_member(format, &stream->items[index], &plain,
                                     &plain_size);
        if (plain) xx_mem_free(plain);
        if (!decoded) goto fail;
        break;
    }

    stream->archive_size = size;
    stream->table_size = table_size;
    xx_mem_free(table);
    xx_mem_free(slots);
    *result = stream;
    return true;
fail:
    if (table) xx_mem_free(table);
    if (slots) xx_mem_free(slots);
    ipak_stream_free(stream);
    return false;
}

static bool ipak_decode_member(Abstractformat *format,
                               const ipak_member *member, uint8_t **plain,
                               size_t *plain_size) {
    uint8_t *packed = NULL;
    uint8_t *output = NULL;
    size_t output_size;
    bool decoded = false;
    if (!format || !member || !plain || !plain_size) return false;
    if (member->unpacked_size < 0 ||
        member->unpacked_size > IPAK_MAX_UNCOMPRESSED_SIZE ||
        member->packed_size < 0 ||
        member->packed_size > IPAK_MAX_UNCOMPRESSED_SIZE)
        return false;
    output_size = (size_t)member->unpacked_size;
    packed = (uint8_t *)xx_mem_alloc(member->packed_size != 0
                                         ? (size_t)member->packed_size : 1U);
    output = (uint8_t *)xx_mem_alloc(output_size != 0U ? output_size : 1U);
    if (!packed || !output) goto fail;
    if (member->packed_size != 0 &&
        !ipak_read_at(format->device, member->data_offset, packed,
                      (size_t)member->packed_size)) goto fail;
    if (member->method == IPAK_METHOD_STORED) {
        if ((uint64_t)member->packed_size != (uint64_t)member->unpacked_size)
            goto fail;
        if (output_size != 0U) xx_mem_copy(output, packed, output_size);
        decoded = true;
    } else if (member->method == IPAK_METHOD_DEFLATE) {
        size_t written = 0U;
        decoded = xx_deflate_decompress_memory(packed,
                                               (size_t)member->packed_size,
                                               output, output_size, &written,
                                               false) &&
                  written == output_size;
    } else if (member->method == IPAK_METHOD_IMPLODE) {
        int64_t consumed = 0;
        decoded = ipak_implode_decode(packed, member->packed_size, output,
                                      member->unpacked_size, &consumed) &&
                  consumed == member->packed_size;
    }
    if (!decoded) goto fail;
    /* The crumb carries only the LOW HALF of the member's CRC32, so it is a
     * real anchor but never a whole checksum: it is verified here and never
     * published as a CRC32. */
    if (member->has_crc && member->method != IPAK_METHOD_IMPLODE &&
        (uint16_t)(xx_crc32_calc(0U, output, output_size) & 0xffffU) !=
            member->crc16)
        goto fail;
    xx_mem_free(packed);
    *plain = output;
    *plain_size = output_size;
    return true;
fail:
    if (packed) xx_mem_free(packed);
    if (output) xx_mem_free(output);
    return false;
}

static bool ipak_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *ipak_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool ipak_set_record(xx_archive_record *record,
                            const ipak_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->crumb_offset >= 0 ? member->crumb_offset
                                                      : member->header_offset;
    record->header_size = member->data_offset - record->header_offset;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          (uint64_t)member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          member->method) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_TIME,
                                          member->dos_time) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_LAST_MOD_DATE,
                                          member->dos_date) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

void xx_infogramesft_init(xx_infogramesft *archive, xx_io_device *device,
                          int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = XX_INFOGRAMESFT_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-infogrames-pak");
    xx_format_set_extension(&archive->format, "pak");
    archive->format.check_is_valid = xx_infogramesft_check_is_valid;
    archive->format.handle_base_info = xx_infogramesft_handle_base_info;
    archive->format.get_format_size = xx_infogramesft_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_infogramesft_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_infogramesft_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_infogramesft_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_infogramesft_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_infogramesft_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_infogramesft_free_archive_records_reading;
    archive->table_size = -1;
}

xx_infogramesft *xx_infogramesft_create(xx_io_device *device,
                                        int64_t base_address) {
    xx_infogramesft *archive =
        (xx_infogramesft *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_infogramesft_init(archive, device, base_address);
    return archive;
}

void xx_infogramesft_destroy(xx_infogramesft *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_infogramesft_free(xx_infogramesft *archive) {
    if (!archive) return;
    xx_infogramesft_destroy(archive);
    xx_mem_free(archive);
}

bool xx_infogramesft_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    ipak_stream *stream;
    (void)pd;
    if (!ipak_parse(format, &stream)) return false;
    ipak_stream_free(stream);
    return true;
}

bool xx_infogramesft_handle_base_info(Abstractformat *format,
                                      xx_pd_struct *pd) {
    ipak_stream *stream;
    xx_infogramesft *archive;
    (void)pd;
    if (!format || !ipak_parse(format, &stream)) return false;
    archive = (xx_infogramesft *)format;
    archive->number_of_records = stream->count;
    archive->table_size = stream->table_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    ipak_stream_free(stream);
    return true;
}

int64_t xx_infogramesft_get_format_size(Abstractformat *format,
                                        xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_infogramesft_handle_base_info(format, pd))
               ? format->format_size : -1;
}

uint64_t xx_infogramesft_get_number_of_archive_records(Abstractformat *format,
                                                       xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_infogramesft_handle_base_info(format, pd))
               ? ((xx_infogramesft *)format)->number_of_records : 0U;
}

xx_archive_record_state *xx_infogramesft_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    ipak_stream *stream;
    xx_archive_record_state *state;
    (void)pd;
    if (!ipak_parse(format, &stream)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        ipak_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = ipak_stream_free;
    state->total_records = stream->count;
    if (!ipak_copy_options(&state->options, options) ||
        !ipak_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_infogramesft_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record : NULL;
}

bool xx_infogramesft_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    ipak_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (ipak_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = ipak_set_record(&state->current_record,
                                        &stream->items[stream->index]);
    return state->has_record;
}

bool xx_infogramesft_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    ipak_stream *stream;
    ipak_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    size_t plain_size = 0U, written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (ipak_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!ipak_decode_member(format, member, &plain, &plain_size)) goto done;
    path_option = ipak_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) {
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
        result = true;
        while (written < plain_size) {
            ssize_t amount = xx_io_write(destination, plain + written,
                                         plain_size - written);
            if (amount <= 0 || (size_t)amount > plain_size - written) {
                result = false;
                break;
            }
            written += (size_t)amount;
        }
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path) xx_rt_remove(path);
    if (plain) xx_mem_free(plain);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_infogramesft_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
