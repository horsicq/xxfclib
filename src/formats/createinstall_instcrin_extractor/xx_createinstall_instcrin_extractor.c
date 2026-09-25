/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * CreateInstall "instcrin" self-extractor: the setup programs of the
 * pre-Gentee CreateInstall builder.  xx_createinstall_instcrin_extractor.h
 * carries the container layout.
 *
 * Sources.  The adaptive Huffman model (the 1257-node tree, its update and the
 * weight halving at 2000) and the container rules (prelude search, the 77/79
 * byte prelude, the 17-byte records, the stream chains of large members) are
 * ported from XArchive Algos/xcreateinstalldecoder.cpp and
 * installers/xcreateinstallsfx.cpp (MIT, Copyright (c) 2019-2026
 * hors<horsicq@gmail.com>).  The code below is a C rewrite that streams from
 * the device instead of loading the overlay; U3 served as the extraction
 * oracle only.
 *
 * Only the PE headers are parsed, to find the overlay; nothing in the stub is
 * executed or emulated.
 *
 * Costs.  check_is_valid() reads the PE headers and the first eight overlay
 * bytes, and only when those are the runtime signature does it decode the
 * runtime stream (about 33 KiB of input) and look at the first record header.
 * Nothing records a compressed length, so handle_base_info() and the record
 * listing decode every member once to find where the next record starts.
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/createinstall_instcrin_extractor/xx_createinstall_instcrin_extractor.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>

/* Registration placeholder: xxfc_defs.h is shared and not edited from here,
 * so the alias macro that sits next to the enumerator is tested instead. */
#ifdef CREATEINSTALL_INSTCRIN_EXTRACTOR
#define XX_CREATEINSTALL_INSTCRIN_EXTRACTOR_FILE_TYPE \
    XX_FILE_TYPE_CREATEINSTALL_INSTCRIN_EXTRACTOR
#else
#define XX_CREATEINSTALL_INSTCRIN_EXTRACTOR_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define CI_FILE_TYPE XX_CREATEINSTALL_INSTCRIN_EXTRACTOR_FILE_TYPE

/* ---------------------------------------------------------------------- */
/* Constants                                                               */

/* Carrier. */
#define CI_DOS_HEADER 0x40
#define CI_MAX_LFANEW 0x10000
#define CI_PE_HEADER 24
#define CI_PE32_MAGIC 0x010bU
#define CI_MIN_OPTIONAL 64
#define CI_MAX_OPTIONAL 0x1000
#define CI_MAX_SECTIONS 96
#define CI_SECTION_SIZE 40
/* The runtime stream alone is tens of KiB; anything shorter is not one. */
#define CI_MIN_CONTAINER 0x40

/* Codec. */
#define CI_SYMBOLS 629U                  /* leaves 629..1257 */
#define CI_NODES (2U * CI_SYMBOLS - 1U)   /* node 1 is the root */
#define CI_EOF 256U
#define CI_LENGTHS 62U
#define CI_MIN_MATCH 3U
#define CI_SLOTS 6U
#define CI_WINDOW 0x8000U
#define CI_WINDOW_MASK (CI_WINDOW - 1U)
#define CI_WEIGHT_LIMIT 2000U
#define CI_INPUT_BUFFER 0x10000U
#define CI_STAGE 0x10000U

/* Container. */
#define CI_RECORD_HEADER 17
#define CI_PRELUDE_SHORT 77
#define CI_PRELUDE_LONG 79
#define CI_PRELUDE_HEAD 0x12
#define CI_PRELUDE_SKIP 8
#define CI_SCAN_LIMIT 0x101
#define CI_RECORD_FILE 1U
#define CI_RECORD_ENTER 2U
#define CI_RECORD_LEAVE 3U
#define CI_RECORD_END 4U

/* Ceilings.  These are desktop setup programs; the limits only keep a corrupt
 * carrier from asking for unbounded work or memory. */
#define CI_MAX_RUNTIME INT64_C(0x400000)
#define CI_MAX_MEMBER INT64_C(0x20000000)
#define CI_MAX_RECORDS 65536U
#define CI_MAX_NAME 1024
#define CI_MAX_DEPTH 64U
#define CI_MAX_PATH 2048U
#define CI_MAX_NAME_BYTES (16U * 1024U * 1024U)

/* FILETIME of 1970-01-01 and of roughly 2107: a record time outside them is
 * not published. */
#define CI_FILETIME_MIN UINT64_C(116444736000000000)
#define CI_FILETIME_MAX UINT64_C(160000000000000000)

static const uint8_t ci_signature[XX_CREATEINSTALL_INSTCRIN_SIGNATURE_SIZE] = {
    0x61, 0x57, 0x41, 0x57, 0xAE, 0x40, 0x60, 0x1B};

/* Extra bits of each distance slot and the running sum of 1 << bits in front
 * of it. */
static const uint32_t ci_slot_bits[CI_SLOTS] = {4U, 6U, 8U, 10U, 12U, 14U};
static const uint32_t ci_slot_base[CI_SLOTS] = {0U, 16U, 80U, 336U, 1360U,
                                                5456U};

/* ---------------------------------------------------------------------- */
/* Byte helpers                                                            */

static uint32_t ci_le16(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U);
}

static uint32_t ci_le32(const uint8_t *bytes) {
    return ci_le16(bytes) | (ci_le16(bytes + 2U) << 16U);
}

static uint64_t ci_le64(const uint8_t *bytes) {
    return (uint64_t)ci_le32(bytes) | ((uint64_t)ci_le32(bytes + 4U) << 32U);
}

static bool ci_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

/* ---------------------------------------------------------------------- */
/* Adaptive Huffman model                                                  */

typedef struct ci_model_s {
    uint16_t left[CI_NODES + 1U];
    uint16_t right[CI_NODES + 1U];
    uint16_t parent[CI_NODES + 1U];
    uint16_t weight[CI_NODES + 1U];
    bool bad;
} ci_model;

#define CI_NODE_OK(node) ((node) >= 1U && (node) <= CI_NODES)

/* The complete tree: node i has children 2i and 2i + 1, every node weighs 1. */
static void ci_model_reset(ci_model *model) {
    uint32_t index;
    xx_mem_zero(model, sizeof(*model));
    for (index = 2U; index <= CI_NODES; ++index) {
        model->weight[index] = 1U;
        model->parent[index] = (uint16_t)(index >> 1U);
    }
    for (index = 1U; index < CI_SYMBOLS; ++index) {
        model->left[index] = (uint16_t)(index * 2U);
        model->right[index] = (uint16_t)(index * 2U + 1U);
    }
}

/* Recompute the weights from @p node up to the root, then halve every weight
 * once the root reaches the limit. */
static void ci_propagate(ci_model *model, uint32_t node, uint32_t sibling) {
    uint32_t steps = 0U, index;
    for (;;) {
        uint32_t child = node, above;
        node = model->parent[node];
        if (!CI_NODE_OK(node) || !CI_NODE_OK(sibling) ||
            ++steps > CI_NODES) {
            model->bad = true;
            return;
        }
        model->weight[node] =
            (uint16_t)(model->weight[child] + model->weight[sibling]);
        if (node == 1U) break;
        above = model->parent[node];
        if (!CI_NODE_OK(above)) {
            model->bad = true;
            return;
        }
        sibling = model->left[above];
        if (sibling == node) sibling = model->right[above];
    }
    if (model->weight[1] == CI_WEIGHT_LIMIT)
        for (index = 1U; index <= CI_NODES; ++index)
            model->weight[index] = (uint16_t)(model->weight[index] >> 1U);
}

/* Count one occurrence of leaf @p node and move it up past lighter uncles. */
static void ci_update(ci_model *model, uint32_t node) {
    uint32_t above, sibling, current, steps = 0U;
    model->weight[node] = (uint16_t)(model->weight[node] + 1U);
    above = model->parent[node];
    if (above == 1U) return;
    if (!CI_NODE_OK(above)) {
        model->bad = true;
        return;
    }
    sibling = model->left[above];
    if (sibling == node) sibling = model->right[above];
    ci_propagate(model, node, sibling);
    if (model->bad) return;
    current = above;
    for (;;) {
        uint32_t grand, grand_left, uncle;
        grand = model->parent[current];
        if (!CI_NODE_OK(grand) || ++steps > CI_NODES) {
            model->bad = true;
            return;
        }
        grand_left = model->left[grand];
        uncle = current == grand_left ? model->right[grand] : grand_left;
        if (!CI_NODE_OK(uncle)) {
            model->bad = true;
            return;
        }
        if (model->weight[uncle] < model->weight[node]) {
            uint32_t other;
            if (current == grand_left)
                model->right[grand] = (uint16_t)node;
            else
                model->left[grand] = (uint16_t)node;
            other = model->left[current];
            if (node == other) {
                other = model->right[current];
                model->left[current] = (uint16_t)uncle;
            } else {
                model->right[current] = (uint16_t)uncle;
            }
            model->parent[uncle] = (uint16_t)current;
            model->parent[node] = (uint16_t)grand;
            ci_propagate(model, uncle, other);
            if (model->bad) return;
            node = uncle;
        }
        node = model->parent[node];
        if (!CI_NODE_OK(node)) {
            model->bad = true;
            return;
        }
        current = model->parent[node];
        if (current == 1U) break;
    }
}

/* ---------------------------------------------------------------------- */
/* Bit input                                                               */

typedef struct ci_input_s {
    xx_io_device *device;
    const uint8_t *memory;
    int64_t offset;     /**< Device offset of the next refill. */
    uint64_t remaining; /**< Region bytes not yet buffered. */
    uint64_t fetched;   /**< Bytes handed to the bit reader. */
    size_t length;
    size_t position;
    uint32_t bits;
    uint32_t count;
    uint8_t buffer[CI_INPUT_BUFFER];
} ci_input;

static void ci_input_open(ci_input *in, xx_io_device *device,
                          const uint8_t *memory, int64_t offset,
                          uint64_t size) {
    in->device = device;
    in->memory = memory;
    in->offset = offset;
    in->remaining = size;
    in->fetched = 0U;
    in->length = 0U;
    in->position = 0U;
    in->bits = 0U;
    in->count = 0U;
}

static int32_t ci_byte(ci_input *in) {
    if (in->position == in->length) {
        size_t amount;
        if (in->remaining == 0U) return -1;
        amount = in->remaining < (uint64_t)CI_INPUT_BUFFER
                     ? (size_t)in->remaining
                     : (size_t)CI_INPUT_BUFFER;
        if (in->memory) {
            xx_rt_memcpy(in->buffer, in->memory, amount);
            in->memory += amount;
        } else if (!ci_read_at(in->device, in->offset, in->buffer, amount)) {
            in->remaining = 0U;
            return -1;
        }
        in->offset += (int64_t)amount;
        in->remaining -= amount;
        in->length = amount;
        in->position = 0U;
    }
    ++in->fetched;
    return (int32_t)in->buffer[in->position++];
}

/* Most significant bit first. */
static int32_t ci_bit(ci_input *in) {
    int32_t bit;
    if (in->count == 0U) {
        int32_t value = ci_byte(in);
        if (value < 0) return -1;
        in->bits = (uint32_t)value;
        in->count = 8U;
    }
    --in->count;
    bit = (int32_t)((in->bits >> 7U) & 1U);
    in->bits = (in->bits << 1U) & 0xFFU;
    return bit;
}

/* An extra-bits field: the first bit read is bit 0 of the value. */
static int32_t ci_bits_lsb(ci_input *in, uint32_t width) {
    int32_t result = 0;
    uint32_t index;
    for (index = 0U; index < width; ++index) {
        int32_t bit = ci_bit(in);
        if (bit < 0) return -1;
        if (bit) result |= (int32_t)(1U << index);
    }
    return result;
}

/* ---------------------------------------------------------------------- */
/* Decoder                                                                 */

typedef struct ci_codec_s {
    ci_model model;
    ci_input in;
    uint8_t window[CI_WINDOW];
    /* Sink: a device, a memory block, or nothing (measure only). */
    bool emit;
    bool failed;
    xx_io_device *sink_device;
    uint8_t *sink_memory;
    uint64_t sink_capacity;
    uint64_t written;
    size_t staged;
    uint8_t stage[CI_STAGE];
} ci_codec;

static ci_codec *ci_codec_create(void) {
    return (ci_codec *)xx_mem_calloc(1U, sizeof(ci_codec));
}

static void ci_sink_none(ci_codec *codec) {
    codec->emit = false;
    codec->failed = false;
    codec->sink_device = NULL;
    codec->sink_memory = NULL;
    codec->sink_capacity = 0U;
    codec->written = 0U;
    codec->staged = 0U;
}

static void ci_flush(ci_codec *codec) {
    size_t amount = codec->staged, done = 0U;
    codec->staged = 0U;
    if (amount == 0U || codec->failed) return;
    if (codec->sink_memory) {
        if ((uint64_t)amount > codec->sink_capacity - codec->written) {
            codec->failed = true;
            return;
        }
        xx_rt_memcpy(codec->sink_memory + codec->written, codec->stage,
                     amount);
    } else if (codec->sink_device) {
        while (done < amount) {
            ssize_t wrote = xx_io_write(codec->sink_device, codec->stage + done,
                                        amount - done);
            if (wrote <= 0 || (size_t)wrote > amount - done) {
                codec->failed = true;
                return;
            }
            done += (size_t)wrote;
        }
    }
    codec->written += amount;
}

static void ci_emit(ci_codec *codec, uint32_t *position, uint8_t value) {
    codec->window[*position] = value;
    *position = (*position + 1U) & CI_WINDOW_MASK;
    if (codec->emit) {
        codec->stage[codec->staged++] = value;
        if (codec->staged == CI_STAGE) ci_flush(codec);
    }
}

static int32_t ci_symbol(ci_codec *codec) {
    uint32_t node = 1U, depth = 0U;
    for (;;) {
        int32_t bit = ci_bit(&codec->in);
        if (bit < 0) return -1;
        node = bit ? codec->model.right[node] : codec->model.left[node];
        if (!CI_NODE_OK(node) || ++depth > CI_NODES) return -1;
        if (node >= CI_SYMBOLS) break;
    }
    ci_update(&codec->model, node);
    if (codec->model.bad) return -1;
    return (int32_t)(node - CI_SYMBOLS);
}

/* One stream, from a fresh model and an empty window, starting on the next
 * byte boundary.  It may produce at most @p limit bytes and succeeds only on
 * its end symbol.  @p consumed counts every byte the bit reader touched. */
static bool ci_decode_stream(ci_codec *codec, uint64_t limit,
                             uint64_t *produced_out, uint64_t *consumed_out,
                             xx_pd_struct *pd) {
    uint64_t produced = 0U, start = codec->in.fetched;
    uint32_t position = 0U;
    unsigned long ticks = 0UL;
    bool result = false;
    ci_model_reset(&codec->model);
    codec->in.count = 0U;
    for (;;) {
        int32_t symbol;
        if ((++ticks & 0xFFFFUL) == 0UL && pd && xx_pd_is_stopped(pd)) break;
        symbol = ci_symbol(codec);
        if (symbol < 0) break;
        if ((uint32_t)symbol == CI_EOF) {
            result = true;
            break;
        }
        if ((uint32_t)symbol < CI_EOF) {
            if (produced >= limit) break;
            ci_emit(codec, &position, (uint8_t)symbol);
            ++produced;
        } else {
            uint32_t code = (uint32_t)symbol - CI_EOF - 1U;
            uint32_t length = code % CI_LENGTHS + CI_MIN_MATCH;
            uint32_t slot = code / CI_LENGTHS;
            uint32_t source, index;
            uint64_t distance;
            int32_t extra;
            if (slot >= CI_SLOTS) break;
            extra = ci_bits_lsb(&codec->in, ci_slot_bits[slot]);
            if (extra < 0) break;
            distance = (uint64_t)extra + length + ci_slot_base[slot];
            /* The window starts empty for every stream: a match may never
             * reach behind this stream's first byte. */
            if (distance > produced || (uint64_t)length > limit - produced)
                break;
            source = (position - (uint32_t)distance) & CI_WINDOW_MASK;
            for (index = 0U; index < length; ++index) {
                uint8_t value = codec->window[source];
                source = (source + 1U) & CI_WINDOW_MASK;
                ci_emit(codec, &position, value);
            }
            produced += length;
        }
        if (codec->failed) break;
    }
    if (produced_out) *produced_out = produced;
    if (consumed_out) *consumed_out = codec->in.fetched - start;
    return result && !codec->failed;
}

/* A member's chain: streams one after another until exactly @p size bytes
 * have been produced.  Every stream must produce something, so the chain
 * cannot spin. */
static bool ci_decode_chain(ci_codec *codec, uint64_t size,
                            uint64_t *consumed_out, xx_pd_struct *pd) {
    uint64_t remaining = size, consumed = 0U;
    while (remaining > 0U) {
        uint64_t produced = 0U, used = 0U;
        if (!ci_decode_stream(codec, remaining, &produced, &used, pd))
            return false;
        if (produced == 0U || used == 0U || produced > remaining) return false;
        remaining -= produced;
        consumed += used;
    }
    ci_flush(codec);
    if (consumed_out) *consumed_out = consumed;
    return !codec->failed;
}

bool xx_createinstall_instcrin_extractor_decode_memory(
    const uint8_t *packed, size_t packed_size, uint8_t *output,
    size_t output_size, size_t *consumed) {
    ci_codec *codec;
    uint64_t used = 0U;
    bool result;
    if (consumed) *consumed = 0U;
    if (output_size == 0U) return true;
    if (!packed || !output || packed_size == 0U ||
        (uint64_t)output_size > (uint64_t)CI_MAX_MEMBER)
        return false;
    codec = ci_codec_create();
    if (!codec) return false;
    ci_input_open(&codec->in, NULL, packed, 0, (uint64_t)packed_size);
    ci_sink_none(codec);
    codec->emit = true;
    codec->sink_memory = output;
    codec->sink_capacity = (uint64_t)output_size;
    result = ci_decode_chain(codec, (uint64_t)output_size, &used, NULL) &&
             codec->written == (uint64_t)output_size;
    if (result && consumed) *consumed = (size_t)used;
    xx_mem_free(codec);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Carrier                                                                 */

/* The overlay of the PE32 stub: past the headers and every section's raw
 * data.  Offsets are relative to the format base. */
static bool ci_locate(Abstractformat *format, int64_t *available_out,
                      int64_t *container_out) {
    uint8_t dos[CI_DOS_HEADER];
    uint8_t pe[CI_PE_HEADER];
    uint8_t optional[CI_MIN_OPTIONAL];
    uint8_t sections[CI_MAX_SECTIONS * CI_SECTION_SIZE];
    uint8_t head[XX_CREATEINSTALL_INSTCRIN_SIGNATURE_SIZE];
    int64_t total, available, base, table;
    uint64_t image_end;
    uint32_t lfanew, count, optional_size, index;
    if (!format || !format->device || format->base_address < 0) return false;
    base = format->base_address;
    total = xx_io_total_size(format->device);
    if (total < base) return false;
    available = total - base;
    if (available < CI_DOS_HEADER + CI_PE_HEADER + CI_MIN_CONTAINER ||
        !ci_read_at(format->device, base, dos, sizeof(dos)) ||
        dos[0] != 'M' || dos[1] != 'Z')
        return false;
    lfanew = ci_le32(dos + 0x3c);
    if (lfanew < CI_DOS_HEADER || lfanew > CI_MAX_LFANEW ||
        (int64_t)lfanew > available - CI_PE_HEADER - CI_MIN_OPTIONAL ||
        !ci_read_at(format->device, base + lfanew, pe, sizeof(pe)) ||
        pe[0] != 'P' || pe[1] != 'E' || pe[2] != 0U || pe[3] != 0U)
        return false;
    count = ci_le16(pe + 6);
    optional_size = ci_le16(pe + 20);
    if (count == 0U || count > CI_MAX_SECTIONS ||
        optional_size < CI_MIN_OPTIONAL || optional_size > CI_MAX_OPTIONAL)
        return false;
    table = (int64_t)lfanew + CI_PE_HEADER + optional_size;
    if (table > available - (int64_t)count * CI_SECTION_SIZE ||
        !ci_read_at(format->device, base + lfanew + CI_PE_HEADER, optional,
                    sizeof(optional)) ||
        ci_le16(optional) != CI_PE32_MAGIC ||
        !ci_read_at(format->device, base + table, sections,
                    (size_t)count * CI_SECTION_SIZE))
        return false;
    /* SizeOfHeaders, then the end of every section's raw data. */
    image_end = ci_le32(optional + 60);
    for (index = 0U; index < count; ++index) {
        const uint8_t *section = sections + (size_t)index * CI_SECTION_SIZE;
        uint64_t raw_size = ci_le32(section + 16);
        uint64_t raw_pointer = ci_le32(section + 20);
        if (raw_size != 0U && raw_pointer + raw_size > image_end)
            image_end = raw_pointer + raw_size;
    }
    if (image_end == 0U || image_end >= (uint64_t)available ||
        (uint64_t)available - image_end < (uint64_t)CI_MIN_CONTAINER ||
        !ci_read_at(format->device, base + (int64_t)image_end, head,
                    sizeof(head)) ||
        xx_rt_memcmp(head, ci_signature, sizeof(head)) != 0)
        return false;
    if (available_out) *available_out = available;
    if (container_out) *container_out = (int64_t)image_end;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Container head: runtime, prelude, first record                          */

typedef struct ci_head_s {
    int64_t available;
    int64_t container;
    int64_t runtime_size;
    int64_t runtime_packed;
    int64_t prelude;
    int64_t candidates[2]; /**< Record chain starts that look right. */
    uint32_t candidate_count;
} ci_head;

/* A record header that the walk could start on: a known type, a method of 0
 * or 1 and, where a name follows, a printable one that fits. */
static bool ci_record_plausible(Abstractformat *format, int64_t available,
                                int64_t offset) {
    uint8_t header[CI_RECORD_HEADER];
    uint8_t name[CI_MAX_NAME];
    int32_t length, index;
    uint32_t type;
    if (offset < 0 || offset > available - CI_RECORD_HEADER ||
        !ci_read_at(format->device, format->base_address + offset, header,
                    sizeof(header)))
        return false;
    type = header[0];
    if (type < CI_RECORD_FILE || type > CI_RECORD_END || header[14] > 1U)
        return false;
    if (type == CI_RECORD_LEAVE || type == CI_RECORD_END) return true;
    length = (int32_t)(int16_t)(uint16_t)ci_le16(header + 15);
    if (length <= 0 || length > CI_MAX_NAME ||
        (int64_t)length > available - offset - CI_RECORD_HEADER ||
        !ci_read_at(format->device,
                    format->base_address + offset + CI_RECORD_HEADER, name,
                    (size_t)length))
        return false;
    for (index = 0; index < length; ++index)
        if (name[index] < 0x20U) return false;
    return true;
}

static bool ci_parse_head(Abstractformat *format, ci_codec *codec,
                          ci_head *head, xx_pd_struct *pd) {
    uint8_t scan[CI_SCAN_LIMIT + 4];
    uint64_t produced = 0U, consumed = 0U;
    int64_t start, fallback = -1, prelude = -1, first, second;
    size_t scan_size, index;
    uint32_t carrier_size;
    int32_t skip;
    xx_mem_zero(head, sizeof(*head));
    if (!ci_locate(format, &head->available, &head->container)) return false;

    /* 1. The runtime stream. */
    ci_input_open(&codec->in, format->device, NULL,
                  format->base_address + head->container,
                  (uint64_t)(head->available - head->container));
    ci_sink_none(codec);
    if (!ci_decode_stream(codec, (uint64_t)CI_MAX_RUNTIME, &produced,
                          &consumed, pd) ||
        produced == 0U || consumed == 0U)
        return false;
    head->runtime_size = (int64_t)produced;
    head->runtime_packed = (int64_t)consumed;

    /* 2. The prelude opens with the carrier's own size.  Behind the runtime
     *    the builder pads with '0' characters up to a multiple of 100, so
     *    the first dword whose top byte is zero is the fallback. */
    start = head->container + head->runtime_packed;
    if (start > head->available - CI_PRELUDE_HEAD) return false;
    scan_size = (size_t)(head->available - start < (int64_t)sizeof(scan)
                             ? head->available - start
                             : (int64_t)sizeof(scan));
    if (!ci_read_at(format->device, format->base_address + start, scan,
                    scan_size))
        return false;
    carrier_size = head->available > (int64_t)UINT32_MAX
                       ? 0U
                       : (uint32_t)head->available;
    for (index = 0U; index <= CI_SCAN_LIMIT && index + 4U <= scan_size;
         ++index) {
        uint32_t value = ci_le32(scan + index);
        if (carrier_size != 0U && value == carrier_size) {
            prelude = start + (int64_t)index;
            break;
        }
        if (fallback < 0 && (value >> 24U) == 0U)
            fallback = start + (int64_t)index;
    }
    if (prelude < 0) prelude = fallback;
    if (prelude < 0 || prelude > head->available - CI_PRELUDE_HEAD)
        return false;
    head->prelude = prelude;
    {
        uint8_t skip_bytes[4];
        if (!ci_read_at(format->device,
                        format->base_address + prelude + CI_PRELUDE_SKIP,
                        skip_bytes, sizeof(skip_bytes)))
            return false;
        skip = (int32_t)ci_le32(skip_bytes);
    }
    if (skip < 0 || (int64_t)skip > head->available - prelude) return false;

    /* 3. The prelude is 77 or 79 bytes long depending on the builder.  In
     *    the reference set a runtime larger than 64 KiB always comes with
     *    the 79-byte form; that order is tried first and the other kept as a
     *    second candidate for the walk. */
    if (head->runtime_size > 0x10000) {
        first = CI_PRELUDE_LONG;
        second = CI_PRELUDE_SHORT;
    } else {
        first = CI_PRELUDE_SHORT;
        second = CI_PRELUDE_LONG;
    }
    if (ci_record_plausible(format, head->available, prelude + first + skip))
        head->candidates[head->candidate_count++] = prelude + first + skip;
    if (ci_record_plausible(format, head->available, prelude + second + skip))
        head->candidates[head->candidate_count++] = prelude + second + skip;
    return head->candidate_count > 0U;
}

/* ---------------------------------------------------------------------- */
/* Member table                                                            */

typedef struct ci_member_s {
    char *name;    /**< UTF-8, '/' separated, relative. */
    char *key;     /**< Case-folded form used to keep names apart. */
    int64_t header_offset;
    int64_t data_offset;
    int64_t packed_size;
    int64_t unpacked_size;
    uint64_t filetime;
    uint32_t attributes;
    uint8_t flag;
    uint8_t method; /**< 0 packed, 1 stored. */
    bool unsafe;    /**< Listed, but never written to disk. */
} ci_member;

typedef struct ci_walk_s {
    ci_member *items;
    size_t count;
    size_t capacity;
    size_t index;
    size_t name_bytes;
    uint32_t *slots; /**< Open-addressing set of member indices + 1. */
    size_t slot_count;
    int64_t container;
    int64_t runtime_size;
    int64_t runtime_packed;
    int64_t records_offset;
    int64_t archive_size;
} ci_walk;

static void ci_walk_free(ci_walk *walk) {
    size_t index;
    if (!walk) return;
    for (index = 0U; index < walk->count; ++index) {
        if (walk->items[index].name) xx_mem_free(walk->items[index].name);
        if (walk->items[index].key) xx_mem_free(walk->items[index].key);
    }
    if (walk->items) xx_mem_free(walk->items);
    if (walk->slots) xx_mem_free(walk->slots);
    xx_mem_free(walk);
}

static void ci_walk_free_opaque(void *opaque) {
    ci_walk_free((ci_walk *)opaque);
}

static uint32_t ci_hash(const char *key) {
    uint32_t hash = 2166136261U;
    while (*key) {
        hash ^= (uint8_t)*key++;
        hash *= 16777619U;
    }
    return hash;
}

static bool ci_same(const char *left, const char *right) {
    while (*left && *left == *right) {
        ++left;
        ++right;
    }
    return *left == *right;
}

/* Index + 1 of the member already using @p key, or 0. */
static uint32_t ci_set_find(const ci_walk *walk, const char *key) {
    size_t mask, slot;
    if (!walk->slots) return 0U;
    mask = walk->slot_count - 1U;
    slot = ci_hash(key) & mask;
    while (walk->slots[slot] != 0U) {
        if (ci_same(walk->items[walk->slots[slot] - 1U].key, key))
            return walk->slots[slot];
        slot = (slot + 1U) & mask;
    }
    return 0U;
}

static bool ci_set_insert(ci_walk *walk, size_t member) {
    size_t mask, slot, index;
    if (walk->slot_count == 0U || (walk->count + 1U) * 2U > walk->slot_count) {
        size_t grown = walk->slot_count ? walk->slot_count * 2U : 64U;
        uint32_t *slots = (uint32_t *)xx_mem_calloc(grown, sizeof(uint32_t));
        if (!slots) return false;
        if (walk->slots) xx_mem_free(walk->slots);
        walk->slots = slots;
        walk->slot_count = grown;
        /* Re-insert everything already published. */
        for (index = 0U; index < walk->count; ++index) {
            if (index == member) continue;
            mask = grown - 1U;
            slot = ci_hash(walk->items[index].key) & mask;
            while (slots[slot] != 0U) slot = (slot + 1U) & mask;
            slots[slot] = (uint32_t)(index + 1U);
        }
    }
    mask = walk->slot_count - 1U;
    slot = ci_hash(walk->items[member].key) & mask;
    while (walk->slots[slot] != 0U) slot = (slot + 1U) & mask;
    walk->slots[slot] = (uint32_t)(member + 1U);
    return true;
}

/* Append ".<index>" (and "_<k>" when even that is taken) to both strings. */
static bool ci_suffix(char **text, const char *suffix) {
    char *joined = xx_str_concat(*text, suffix);
    char *copy;
    size_t length;
    if (!joined) return false;
    length = xx_str_len(joined);
    copy = (char *)xx_mem_alloc(length + 1U);
    if (!copy) {
        xx_str_free(joined);
        return false;
    }
    xx_rt_memcpy(copy, joined, length + 1U);
    xx_str_free(joined);
    xx_mem_free(*text);
    *text = copy;
    return true;
}

static void ci_decimal(char *out, const char *prefix, size_t value,
                       unsigned width) {
    char digits[24];
    unsigned count = 0U;
    size_t position = 0U;
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value && count < sizeof(digits));
    while (count < width && count < sizeof(digits)) digits[count++] = '0';
    while (*prefix) out[position++] = *prefix++;
    while (count) out[position++] = digits[--count];
    out[position] = 0;
}

/* Two members that fold to the same name must not overwrite each other: the
 * later one gets its member index appended. */
static bool ci_publish(ci_walk *walk, ci_member *member) {
    ci_member *grown;
    char suffix[40];
    unsigned attempt;
    if (walk->count >= CI_MAX_RECORDS) return false;
    if (walk->count == walk->capacity) {
        size_t capacity = walk->capacity ? walk->capacity * 2U : 32U;
        grown = (ci_member *)xx_mem_realloc(walk->items,
                                            capacity * sizeof(*grown));
        if (!grown) return false;
        walk->items = grown;
        walk->capacity = capacity;
    }
    if (ci_set_find(walk, member->key) != 0U) {
        ci_decimal(suffix, ".", walk->count, 4U);
        if (!ci_suffix(&member->name, suffix) ||
            !ci_suffix(&member->key, suffix))
            return false;
        for (attempt = 1U; ci_set_find(walk, member->key) != 0U; ++attempt) {
            if (attempt > 16U) return false;
            ci_decimal(suffix, "_", attempt, 1U);
            if (!ci_suffix(&member->name, suffix) ||
                !ci_suffix(&member->key, suffix))
                return false;
        }
    }
    walk->name_bytes += xx_str_len(member->name) + xx_str_len(member->key);
    if (walk->name_bytes > CI_MAX_NAME_BYTES) return false;
    walk->items[walk->count] = *member;
    if (!ci_set_insert(walk, walk->count)) return false;
    ++walk->count;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Names                                                                   */

/* Windows-1252 bytes 0x80..0x9F; 0 marks the five unassigned positions. */
static const uint16_t ci_cp1252_high[32] = {
    0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017D, 0x0000,
    0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178};

static uint32_t ci_cp1252(uint8_t value) {
    if (value >= 0x80U && value < 0xA0U) return ci_cp1252_high[value - 0x80U];
    return value;
}

/* Lower-case fold of a Windows-1252 byte, as the file system would see it. */
static uint8_t ci_fold(uint8_t value) {
    if (value >= 'A' && value <= 'Z') return (uint8_t)(value + 0x20U);
    if (value >= 0xC0U && value <= 0xDEU && value != 0xD7U)
        return (uint8_t)(value + 0x20U);
    if (value == 0x8AU || value == 0x8CU || value == 0x8EU)
        return (uint8_t)(value + 0x10U);
    if (value == 0x9FU) return 0xFFU;
    return value;
}

static uint8_t ci_upper_ascii(uint8_t value) {
    return (value >= 'a' && value <= 'z') ? (uint8_t)(value - 0x20U) : value;
}

/* CON, PRN, AUX, NUL, COM1-9, LPT1-9, CONIN$, CONOUT$ and CLOCK$, with or
 * without an extension, in any case. */
static bool ci_is_device(const uint8_t *text, size_t length) {
    static const char *const names[] = {"CON",    "PRN",     "AUX",
                                        "NUL",    "CONIN$",  "CONOUT$",
                                        "CLOCK$"};
    size_t stem = 0U, index, position;
    while (stem < length && text[stem] != '.') ++stem;
    while (stem > 0U && text[stem - 1U] == ' ') --stem;
    for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        const char *name = names[index];
        for (position = 0U; position < stem && name[position]; ++position)
            if (ci_upper_ascii(text[position]) != (uint8_t)name[position])
                break;
        if (position == stem && name[position] == 0) return true;
    }
    if (stem == 4U && text[3] >= '1' && text[3] <= '9') {
        uint8_t a = ci_upper_ascii(text[0]), b = ci_upper_ascii(text[1]),
                c = ci_upper_ascii(text[2]);
        if ((a == 'C' && b == 'O' && c == 'M') ||
            (a == 'L' && b == 'P' && c == 'T'))
            return true;
    }
    return false;
}

/* A component that could escape the output directory or that Windows would
 * not create as an ordinary file. */
static bool ci_component_unsafe(const uint8_t *text, size_t length) {
    size_t index;
    bool meaningful = false;
    for (index = 0U; index < length; ++index) {
        uint8_t c = text[index];
        if (c < 0x20U || c == 0x7FU || c == '<' || c == '>' || c == ':' ||
            c == '"' || c == '|' || c == '?' || c == '*')
            return true;
        if (c != '.' && c != ' ') meaningful = true;
    }
    return !meaningful || ci_is_device(text, length);
}

/* The characters Windows reserves in a name, and control bytes. */
static bool ci_reserved_char(uint8_t c) {
    return c < 0x20U || c == 0x7FU || c == '<' || c == '>' || c == ':' ||
           c == '"' || c == '|' || c == '?' || c == '*';
}

/* One component in UTF-8 and its fold key.  The listed name is already
 * harmless: reserved characters become '_', a component of nothing but dots
 * and spaces (".." among them) becomes underscores and a device name gets a
 * leading '_'.  An unsafe member is still never written; this only keeps a
 * caller that writes names itself out of trouble.  The key drops trailing
 * dots and spaces, as Windows does, so "a.txt." and "A.TXT" collide. */
static bool ci_component_strings(const uint8_t *text, size_t length,
                                 char **utf8_out, char **key_out) {
    uint8_t *clean = (uint8_t *)xx_mem_alloc(length + 2U);
    char *utf8 = (char *)xx_mem_alloc((length + 1U) * 3U + 1U);
    char *key = (char *)xx_mem_alloc(length + 2U);
    size_t clean_length = 0U, out = 0U, index, key_length;
    bool meaningful = false;
    if (!clean || !utf8 || !key) {
        if (clean) xx_mem_free(clean);
        if (utf8) xx_mem_free(utf8);
        if (key) xx_mem_free(key);
        return false;
    }
    for (index = 0U; index < length; ++index)
        if (text[index] != '.' && text[index] != ' ') meaningful = true;
    if (meaningful && ci_is_device(text, length)) clean[clean_length++] = '_';
    for (index = 0U; index < length; ++index) {
        uint8_t c = text[index];
        clean[clean_length++] =
            (!meaningful || ci_reserved_char(c)) ? (uint8_t)'_' : c;
    }
    for (index = 0U; index < clean_length; ++index) {
        uint32_t code = ci_cp1252(clean[index]);
        if (code == 0U) code = '_';
        if (code < 0x80U) {
            utf8[out++] = (char)code;
        } else if (code < 0x800U) {
            utf8[out++] = (char)(0xC0U | (code >> 6U));
            utf8[out++] = (char)(0x80U | (code & 0x3FU));
        } else {
            utf8[out++] = (char)(0xE0U | (code >> 12U));
            utf8[out++] = (char)(0x80U | ((code >> 6U) & 0x3FU));
            utf8[out++] = (char)(0x80U | (code & 0x3FU));
        }
    }
    utf8[out] = 0;
    key_length = clean_length;
    while (key_length > 0U && (clean[key_length - 1U] == '.' ||
                               clean[key_length - 1U] == ' '))
        --key_length;
    for (index = 0U; index < key_length; ++index)
        key[index] = (char)ci_fold(clean[index]);
    key[key_length] = 0;
    xx_mem_free(clean);
    *utf8_out = utf8;
    *key_out = key;
    return true;
}

typedef struct ci_dirs_s {
    char *name[CI_MAX_DEPTH];
    char *key[CI_MAX_DEPTH];
    bool unsafe[CI_MAX_DEPTH];
    uint32_t depth;
} ci_dirs;

static void ci_dirs_pop(ci_dirs *dirs) {
    if (dirs->depth == 0U) return;
    --dirs->depth;
    xx_mem_free(dirs->name[dirs->depth]);
    xx_mem_free(dirs->key[dirs->depth]);
    dirs->name[dirs->depth] = NULL;
    dirs->key[dirs->depth] = NULL;
}

static void ci_dirs_clear(ci_dirs *dirs) {
    while (dirs->depth) ci_dirs_pop(dirs);
}

/* Splits a record name into components.  Script-derived names are stored
 * quoted; both quotes have to be present for them to be removed.  '%' is
 * kept: "%installpath%\Uninstal.exe" and "Uninstal.exe" are different
 * members.  A leading separator (an absolute or UNC path), a drive letter,
 * ".." or a reserved name makes the member unsafe: it is listed, but never
 * written.  @p push receives each component. */
typedef bool (*ci_component_fn)(void *context, const uint8_t *text,
                                size_t length, bool unsafe);

static bool ci_split_name(const uint8_t *raw, size_t length, void *context,
                          ci_component_fn push, bool *any_unsafe) {
    size_t start, index;
    bool pushed = false, leading_unsafe = false;
    if (length >= 2U && raw[0] == '"' && raw[length - 1U] == '"') {
        ++raw;
        length -= 2U;
    }
    if (length > 0U && (raw[0] == '\\' || raw[0] == '/')) leading_unsafe = true;
    start = 0U;
    for (index = 0U; index <= length; ++index) {
        if (index == length || raw[index] == '\\' || raw[index] == '/') {
            size_t part = index - start;
            if (part != 0U && !(part == 1U && raw[start] == '.')) {
                bool unsafe = ci_component_unsafe(raw + start, part) ||
                              leading_unsafe;
                if (!push(context, raw + start, part, unsafe)) return false;
                if (unsafe) *any_unsafe = true;
                pushed = true;
            }
            start = index + 1U;
        }
    }
    if (!pushed) {
        /* Nothing usable: keep the structure with a placeholder. */
        if (!push(context, (const uint8_t *)"_", 1U, true)) return false;
        *any_unsafe = true;
    }
    return true;
}

static bool ci_push_dir(void *context, const uint8_t *text, size_t length,
                        bool unsafe) {
    ci_dirs *dirs = (ci_dirs *)context;
    if (dirs->depth >= CI_MAX_DEPTH) return false;
    if (!ci_component_strings(text, length, &dirs->name[dirs->depth],
                              &dirs->key[dirs->depth]))
        return false;
    dirs->unsafe[dirs->depth] = unsafe;
    ++dirs->depth;
    return true;
}

typedef struct ci_path_s {
    char name[CI_MAX_PATH + 1U];
    char key[CI_MAX_PATH + 1U];
    size_t name_length;
    size_t key_length;
    bool overflow;
} ci_path;

static void ci_path_add(ci_path *path, const char *name, const char *key) {
    size_t name_length = xx_str_len(name), key_length = xx_str_len(key);
    size_t separator = path->name_length ? 1U : 0U;
    if (path->overflow ||
        path->name_length + separator + name_length > CI_MAX_PATH ||
        path->key_length + separator + key_length > CI_MAX_PATH) {
        path->overflow = true;
        return;
    }
    if (separator) {
        path->name[path->name_length++] = '/';
        path->key[path->key_length++] = '/';
    }
    xx_rt_memcpy(path->name + path->name_length, name, name_length);
    xx_rt_memcpy(path->key + path->key_length, key, key_length);
    path->name_length += name_length;
    path->key_length += key_length;
    path->name[path->name_length] = 0;
    path->key[path->key_length] = 0;
}

static bool ci_push_path(void *context, const uint8_t *text, size_t length,
                         bool unsafe) {
    ci_path *path = (ci_path *)context;
    char *name = NULL, *key = NULL;
    (void)unsafe;
    if (!ci_component_strings(text, length, &name, &key)) return false;
    ci_path_add(path, name, key);
    xx_mem_free(name);
    xx_mem_free(key);
    return true;
}

static char *ci_strdup(const char *text, size_t length) {
    char *copy = (char *)xx_mem_alloc(length + 1U);
    if (!copy) return NULL;
    xx_rt_memcpy(copy, text, length);
    copy[length] = 0;
    return copy;
}

/* ---------------------------------------------------------------------- */
/* Walk                                                                    */

static bool ci_walk_records(Abstractformat *format, ci_codec *codec,
                            const ci_head *head, int64_t records,
                            ci_walk **out, xx_pd_struct *pd) {
    ci_walk *walk;
    ci_dirs dirs;
    ci_path *path = NULL;
    ci_member member;
    int64_t position = records;
    const int64_t available = head->available;
    bool ended = false, result = false;
    uint8_t header[CI_RECORD_HEADER];
    uint8_t raw[CI_MAX_NAME];

    walk = (ci_walk *)xx_mem_calloc(1U, sizeof(*walk));
    path = (ci_path *)xx_mem_alloc(sizeof(*path));
    xx_mem_zero(&dirs, sizeof(dirs));
    if (!walk || !path) goto done;
    walk->container = head->container;
    walk->runtime_size = head->runtime_size;
    walk->runtime_packed = head->runtime_packed;
    walk->records_offset = records;

    /* Member 0: the installer runtime. */
    xx_mem_zero(&member, sizeof(member));
    member.name = ci_strdup("instcrin.dll", 12U);
    member.key = ci_strdup("instcrin.dll", 12U);
    member.header_offset = head->container;
    member.data_offset = head->container;
    member.packed_size = head->runtime_packed;
    member.unpacked_size = head->runtime_size;
    if (!member.name || !member.key || !ci_publish(walk, &member)) {
        if (member.name) xx_mem_free(member.name);
        if (member.key) xx_mem_free(member.key);
        goto done;
    }

    while (!ended) {
        uint32_t type, method;
        int32_t length;
        int64_t size;
        bool unsafe = false;
        uint32_t level;
        if (pd && xx_pd_is_stopped(pd)) goto done;
        if (position > available - CI_RECORD_HEADER ||
            !ci_read_at(format->device, format->base_address + position,
                        header, sizeof(header)))
            goto done;
        position += CI_RECORD_HEADER;
        type = header[0];
        method = header[14];
        if (method > 1U) goto done;
        if (type == CI_RECORD_END) {
            ended = true;
            break;
        }
        if (type == CI_RECORD_LEAVE) {
            ci_dirs_pop(&dirs);
            continue;
        }
        if (type != CI_RECORD_FILE && type != CI_RECORD_ENTER) goto done;
        length = (int32_t)(int16_t)(uint16_t)ci_le16(header + 15);
        if (length <= 0 || length > CI_MAX_NAME ||
            (int64_t)length > available - position ||
            !ci_read_at(format->device, format->base_address + position, raw,
                        (size_t)length))
            goto done;
        position += length;

        if (type == CI_RECORD_ENTER) {
            /* The directory becomes the current directory plus the name. */
            if (!ci_split_name(raw, (size_t)length, &dirs, ci_push_dir,
                               &unsafe))
                goto done;
            continue;
        }

        /* A file: the current directory, then the name. */
        path->name_length = 0U;
        path->key_length = 0U;
        path->name[0] = 0;
        path->key[0] = 0;
        path->overflow = false;
        for (level = 0U; level < dirs.depth; ++level) {
            ci_path_add(path, dirs.name[level], dirs.key[level]);
            if (dirs.unsafe[level]) unsafe = true;
        }
        if (!ci_split_name(raw, (size_t)length, path, ci_push_path, &unsafe) ||
            path->overflow)
            goto done;

        if (position > available - 4) goto done;
        {
            uint8_t size_bytes[4];
            if (!ci_read_at(format->device, format->base_address + position,
                            size_bytes, sizeof(size_bytes)))
                goto done;
            size = (int64_t)(int32_t)ci_le32(size_bytes);
        }
        position += 4;
        if (size < 0 || size > CI_MAX_MEMBER) goto done;

        xx_mem_zero(&member, sizeof(member));
        member.header_offset = position - 4 - length - CI_RECORD_HEADER;
        member.data_offset = position;
        member.unpacked_size = size;
        member.attributes = ci_le32(header + 2);
        member.filetime = ci_le64(header + 6);
        member.flag = header[1];
        member.method = (uint8_t)method;
        member.unsafe = unsafe;
        if (method == 1U || size == 0) {
            /* Stored, or empty: a zero-length member carries no stream. */
            if (size > available - position) goto done;
            member.packed_size = size;
        } else {
            uint64_t consumed = 0U;
            if (position >= available) goto done;
            ci_input_open(&codec->in, format->device, NULL,
                          format->base_address + position,
                          (uint64_t)(available - position));
            ci_sink_none(codec);
            if (!ci_decode_chain(codec, (uint64_t)size, &consumed, pd) ||
                consumed == 0U || consumed > (uint64_t)(available - position))
                goto done;
            member.packed_size = (int64_t)consumed;
        }
        position += member.packed_size;
        member.name = ci_strdup(path->name, path->name_length);
        member.key = ci_strdup(path->key, path->key_length);
        if (!member.name || !member.key || !ci_publish(walk, &member)) {
            if (member.name) xx_mem_free(member.name);
            if (member.key) xx_mem_free(member.key);
            goto done;
        }
    }
    if (!ended) goto done;
    walk->archive_size = position;
    result = true;
done:
    ci_dirs_clear(&dirs);
    if (path) xx_mem_free(path);
    if (result) {
        if (walk->slots) {
            xx_mem_free(walk->slots);
            walk->slots = NULL;
            walk->slot_count = 0U;
        }
        *out = walk;
    } else {
        ci_walk_free(walk);
    }
    return result;
}

static bool ci_walk_container(Abstractformat *format, ci_walk **out, xx_pd_struct *pd) {
    ci_codec *codec;
    ci_head head;
    uint32_t index;
    bool result = false;
    if (!format || !out) return false;
    *out = NULL;
    codec = ci_codec_create();
    if (!codec) return false;
    if (ci_parse_head(format, codec, &head, pd)) {
        for (index = 0U; index < head.candidate_count && !result; ++index)
            result = ci_walk_records(format, codec, &head,
                                     head.candidates[index], out, pd);
    }
    xx_mem_free(codec);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Member extraction                                                       */

static bool ci_unpack_member(Abstractformat *format, const ci_member *member,
                             xx_io_device *destination, xx_pd_struct *pd) {
    ci_codec *codec;
    bool result = false;
    if (member->unpacked_size == 0) return true;
    if (member->method == 1U) {
        uint8_t buffer[0x4000];
        int64_t done = 0;
        while (done < member->unpacked_size) {
            size_t amount = member->unpacked_size - done < (int64_t)sizeof(buffer)
                                ? (size_t)(member->unpacked_size - done)
                                : sizeof(buffer);
            size_t written = 0U;
            if (pd && xx_pd_is_stopped(pd)) return false;
            if (!ci_read_at(format->device,
                            format->base_address + member->data_offset + done,
                            buffer, amount))
                return false;
            while (destination && written < amount) {
                ssize_t wrote = xx_io_write(destination, buffer + written,
                                            amount - written);
                if (wrote <= 0 || (size_t)wrote > amount - written)
                    return false;
                written += (size_t)wrote;
            }
            done += (int64_t)amount;
        }
        return true;
    }
    codec = ci_codec_create();
    if (!codec) return false;
    ci_input_open(&codec->in, format->device, NULL,
                  format->base_address + member->data_offset,
                  (uint64_t)member->packed_size);
    ci_sink_none(codec);
    codec->emit = destination != NULL;
    codec->sink_device = destination;
    {
        uint64_t consumed = 0U;
        result = ci_decode_chain(codec, (uint64_t)member->unpacked_size,
                                 &consumed, pd) &&
                 consumed == (uint64_t)member->packed_size &&
                 (!destination ||
                  codec->written == (uint64_t)member->unpacked_size);
    }
    xx_mem_free(codec);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Records                                                                 */

static bool ci_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *ci_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool ci_set_record(Abstractformat *format, xx_archive_record *record,
                          const ci_member *member, size_t index) {
    uint64_t method = (member->method == 1U || member->unpacked_size == 0)
                          ? 0U
                          : 1U;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = format->base_address + member->header_offset;
    record->header_size = index == 0U
                              ? 0
                              : member->data_offset - member->header_offset;
    record->data_offset = format->base_address + member->data_offset;
    record->compressed_size = member->packed_size;
    if (!xx_archive_record_set_original_name(record, member->name) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                        (uint64_t)member->packed_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                        (uint64_t)member->unpacked_size) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                        method) ||
        !xx_archive_record_set_meta_u64(record, XX_META_ID_FLAGS,
                                        member->flag) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                         false) ||
        !xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false))
        return false;
    if (member->attributes != 0U &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_ATTRIBUTES,
                                        member->attributes))
        return false;
    if (member->filetime > CI_FILETIME_MIN &&
        member->filetime < CI_FILETIME_MAX &&
        !xx_archive_record_set_meta_u64(record, XX_META_ID_TIMESTAMP,
                                        member->filetime))
        return false;
    return true;
}

/* The walk already refused traversal; this is the last gate before a name
 * reaches the file system. */
static bool ci_safe_output_name(const char *name) {
    size_t index, start = 0U, length;
    if (!name || !name[0] || name[0] == '/' || name[0] == '\\') return false;
    length = xx_str_len(name);
    for (index = 0U; index <= length; ++index) {
        if (index == length || name[index] == '/') {
            size_t part = index - start;
            if (part == 0U ||
                ci_component_unsafe((const uint8_t *)name + start, part))
                return false;
            start = index + 1U;
        } else if (name[index] == '\\') {
            return false;
        }
    }
    return true;
}

void xx_createinstall_instcrin_extractor_init(
    xx_createinstall_instcrin_extractor *archive, xx_io_device *device,
    int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_LITTLE;
    archive->format.file_type = CI_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format,
                            "application/x-createinstall-installer");
    xx_format_set_extension(&archive->format, "exe");
    archive->format.check_is_valid =
        xx_createinstall_instcrin_extractor_check_is_valid;
    archive->format.handle_base_info =
        xx_createinstall_instcrin_extractor_handle_base_info;
    archive->format.get_format_size =
        xx_createinstall_instcrin_extractor_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_createinstall_instcrin_extractor_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_createinstall_instcrin_extractor_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_createinstall_instcrin_extractor_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_createinstall_instcrin_extractor_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_createinstall_instcrin_extractor_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_createinstall_instcrin_extractor_free_archive_records_reading;
    archive->container_offset = -1;
    archive->records_offset = -1;
}

xx_createinstall_instcrin_extractor *xx_createinstall_instcrin_extractor_create(
    xx_io_device *device, int64_t base_address) {
    xx_createinstall_instcrin_extractor *archive =
        (xx_createinstall_instcrin_extractor *)xx_mem_alloc(sizeof(*archive));
    if (archive)
        xx_createinstall_instcrin_extractor_init(archive, device, base_address);
    return archive;
}

void xx_createinstall_instcrin_extractor_destroy(
    xx_createinstall_instcrin_extractor *archive) {
    if (!archive) return;
    if (archive->walk) {
        ci_walk_free((ci_walk *)archive->walk);
        archive->walk = NULL;
    }
    xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_createinstall_instcrin_extractor_free(
    xx_createinstall_instcrin_extractor *archive) {
    if (!archive) return;
    xx_createinstall_instcrin_extractor_destroy(archive);
    xx_mem_free(archive);
}

bool xx_createinstall_instcrin_extractor_check_is_valid(Abstractformat *format,
                                                        xx_pd_struct *pd) {
    ci_codec *codec;
    ci_head head;
    bool result;
    /* The PE headers and eight overlay bytes first: nothing is allocated for
     * a file that does not carry the runtime signature. */
    if (!ci_locate(format, NULL, NULL)) return false;
    codec = ci_codec_create();
    if (!codec) return false;
    result = ci_parse_head(format, codec, &head, pd);
    xx_mem_free(codec);
    return result;
}

bool xx_createinstall_instcrin_extractor_handle_base_info(
    Abstractformat *format, xx_pd_struct *pd) {
    xx_createinstall_instcrin_extractor *archive;
    ci_walk *walk = NULL;
    if (!format) return false;
    archive = (xx_createinstall_instcrin_extractor *)format;
    if (archive->walk) {
        ci_walk_free((ci_walk *)archive->walk);
        archive->walk = NULL;
    }
    if (!ci_walk_container(format, &walk, pd)) {
        format->is_valid = false;
        format->base_info_handled = false;
        format->format_size = -1;
        format->number_of_archive_records = 0U;
        return false;
    }
    archive->walk = walk;
    archive->number_of_records = walk->count;
    archive->container_offset = walk->container;
    archive->runtime_size = walk->runtime_size;
    archive->runtime_packed_size = walk->runtime_packed;
    archive->records_offset = walk->records_offset;
    format->number_of_archive_records = walk->count;
    format->format_size = walk->archive_size;
    format->file_type = CI_FILE_TYPE;
    format->format_type = XX_TYPE_ARCHIVE;
    format->is_archive = true;
    format->is_valid = true;
    format->base_info_handled = true;
    return true;
}

int64_t xx_createinstall_instcrin_extractor_get_format_size(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_createinstall_instcrin_extractor_handle_base_info(
                          format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_createinstall_instcrin_extractor_get_number_of_archive_records(
    Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_createinstall_instcrin_extractor_handle_base_info(
                          format, pd))
               ? ((xx_createinstall_instcrin_extractor *)format)
                     ->number_of_records
               : 0U;
}

xx_archive_record_state *
xx_createinstall_instcrin_extractor_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    xx_createinstall_instcrin_extractor *archive;
    xx_archive_record_state *state;
    ci_walk *walk = NULL;
    if (!format) return NULL;
    archive = (xx_createinstall_instcrin_extractor *)format;
    /* The table handle_base_info decoded is handed over rather than decoded
     * a second time; a later listing walks again. */
    if (archive->walk) {
        walk = (ci_walk *)archive->walk;
        archive->walk = NULL;
    } else if (!ci_walk_container(format, &walk, pd)) {
        return NULL;
    }
    walk->index = 0U;
    if (walk->count == 0U) {
        ci_walk_free(walk);
        return NULL;
    }
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        ci_walk_free(walk);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = walk;
    state->free_internal = ci_walk_free_opaque;
    state->total_records = (int64_t)walk->count;
    if (!ci_copy_options(&state->options, options) ||
        !ci_set_record(format, &state->current_record, &walk->items[0], 0U)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *
xx_createinstall_instcrin_extractor_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_createinstall_instcrin_extractor_archive_record_move_to_next(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    ci_walk *walk;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(walk = (ci_walk *)state->internal_state) ||
        ++walk->index >= walk->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record = ci_set_record(format, &state->current_record,
                                      &walk->items[walk->index], walk->index);
    return state->has_record;
}

bool xx_createinstall_instcrin_extractor_unpack_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state, xx_pd_struct *pd) {
    ci_walk *walk;
    const ci_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    bool result = false;
    bool created = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(walk = (ci_walk *)state->internal_state) ||
        walk->index >= walk->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &walk->items[walk->index];
    path_option = ci_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_option) return ci_unpack_member(format, member, NULL, pd);
    if (member->unsafe || !ci_safe_output_name(member->name)) return false;
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
        result = ci_unpack_member(format, member, destination, pd);
        if (xx_io_close(destination) != 0) result = false;
    }
done:
    if (!result && path && created) xx_io_file_remove_a(path);
    if (path) xx_str_free(path);
    if (owned_base) xx_str_free(owned_base);
    return result;
}

void xx_createinstall_instcrin_extractor_free_archive_records_reading(
    Abstractformat *format, xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
