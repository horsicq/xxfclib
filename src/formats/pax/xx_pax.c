/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * Native reader for the GEM-View PAX container ("LZF0").  Despite the name
 * this has nothing to do with POSIX pax -- that dialect is a tar variant and
 * is handled by the tar reader's 'x'/'g' typeflag path.
 *
 * A PAX file is a bare chain of members, each of them
 *
 *   +0x00  "LZF0"
 *   +0x04  4      first four bytes of the PLAINTEXT, repeated verbatim
 *   +0x08  u32be  uncompressed size
 *   +0x1c  u16be  name size, NUL terminator included
 *   +0x1e  name   printable ASCII, NUL terminated
 *   then   the coded bit stream, MSB first
 *
 * The compressed length is stored NOWHERE.  A member ends where its own bit
 * stream ends, so the only way to delimit one is to decode it; the next
 * member starts at the following "LZF0" that parses as a header.  The copy of
 * the plaintext prefix at +0x04 is what turns a plausible decode into a
 * verified one and it is checked for every member.
 *
 * The codec is a pair of adaptive Huffman models (a main model over
 * 256 + 2^(windowBits-lowBits) symbols and a length model) driving an LZ77
 * window whose size is 2^windowBits minus a stored subtractor.  Layout and
 * codec both come from the XArchive reference implementation
 * (core/xlegacystorearchive.cpp and Algos/xpaxdecoder.cpp).
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/pax/xx_pax.h"

#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#ifdef PAX
#define XX_PAX_FILE_TYPE XX_FILE_TYPE_PAX
#else
#define XX_PAX_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define PAX_HEADER_FIXED 30U
#define PAX_MIN_RECORD 38U
#define PAX_MAX_MEMBERS 65536U
#define PAX_MAX_NAME_SIZE 1024U
#define PAX_MAX_INPUT ((int64_t)64 * 1024 * 1024)
#define PAX_MAX_OUTPUT ((int64_t)256 * 1024 * 1024)
#define PAX_MAX_ALPHABET 2048

typedef struct pax_member_s {
    char *name;
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t packed_size;
    uint64_t unpacked_size;
} pax_member;

typedef struct pax_stream_s {
    pax_member *items;
    size_t count;
    size_t index;
    uint8_t *image;
    int64_t image_size;
    int64_t archive_size;
} pax_stream;

/* --- bit reader, MSB first --------------------------------------------- */

typedef struct pax_bits_s {
    const uint8_t *data;
    int64_t size;
    int64_t position;
} pax_bits;

static bool pax_bits_get(pax_bits *bits, uint32_t count, uint32_t *value) {
    uint32_t result = 0U;
    uint32_t index;
    if (!bits || !value || count > 24U || bits->position < 0 ||
        bits->position > bits->size * 8 - (int64_t)count)
        return false;
    for (index = 0U; index < count; ++index) {
        int64_t bit = bits->position++;
        result = (result << 1U) |
                 ((uint32_t)(bits->data[bit >> 3] >> (7U - (bit & 7))) & 1U);
    }
    *value = result;
    return true;
}

static int64_t pax_bits_consumed(const pax_bits *bits) {
    return bits ? (bits->position + 7) / 8 : 0;
}

/* --- adaptive model ----------------------------------------------------- */

typedef struct pax_node_s {
    int32_t left;
    int32_t right;
    int32_t parent;
    uint32_t frequency;
} pax_node;

typedef struct pax_pair_s {
    uint32_t frequency;
    int32_t node;
} pax_pair;

typedef struct pax_model_s {
    pax_bits *bits;
    int32_t alphabet_size;
    int32_t node_count;
    int32_t root;
    uint32_t maximum;
    uint32_t count;
    uint32_t next_rebuild;
    pax_node *nodes;
    pax_pair *leaves;
    pax_pair *high;
    bool valid;
} pax_model;

static bool pax_pair_less(const pax_pair *a, const pax_pair *b) {
    return a->frequency < b->frequency ||
           (a->frequency == b->frequency && a->node < b->node);
}

static void pax_heap_sift(pax_pair *items, int32_t start, int32_t count) {
    int32_t root = start;
    for (;;) {
        int32_t child = root * 2 + 1;
        int32_t swap = root;
        pax_pair temp;
        if (child >= count) break;
        if (pax_pair_less(&items[swap], &items[child])) swap = child;
        if (child + 1 < count && pax_pair_less(&items[swap], &items[child + 1]))
            swap = child + 1;
        if (swap == root) break;
        temp = items[root];
        items[root] = items[swap];
        items[swap] = temp;
        root = swap;
    }
}

/* Ascending heap sort: the model rebuild runs often enough that a quadratic
 * sort over a 2048-symbol alphabet would dominate the decode. */
static void pax_sort(pax_pair *items, int32_t count) {
    int32_t index;
    if (!items || count < 2) return;
    for (index = count / 2 - 1; index >= 0; --index)
        pax_heap_sift(items, index, count);
    for (index = count - 1; index > 0; --index) {
        pax_pair temp = items[0];
        items[0] = items[index];
        items[index] = temp;
        pax_heap_sift(items, 0, index);
    }
}

static void pax_model_cleanup(pax_model *model) {
    if (!model) return;
    if (model->nodes) xx_mem_free(model->nodes);
    if (model->leaves) xx_mem_free(model->leaves);
    if (model->high) xx_mem_free(model->high);
    xx_mem_zero(model, sizeof(*model));
}

static bool pax_model_pop_lowest(pax_model *model, int32_t *leaf_position,
                                 int32_t *internal_position, int32_t output,
                                 pax_pair *pair) {
    uint32_t leaf_frequency;
    uint32_t internal_frequency;
    if (!model || !leaf_position || !internal_position || !pair) return false;
    leaf_frequency = *leaf_position < model->alphabet_size
                         ? model->leaves[*leaf_position].frequency
                         : 0x7fffU;
    internal_frequency = *internal_position < output
                             ? model->nodes[*internal_position].frequency
                             : 0x7fffU;
    if (internal_frequency < leaf_frequency) {
        pair->frequency = internal_frequency;
        pair->node = (*internal_position)++;
    } else {
        if (*leaf_position >= model->alphabet_size) return false;
        *pair = model->leaves[(*leaf_position)++];
    }
    return pair->node >= 0 && pair->node < model->node_count;
}

static bool pax_model_rebuild(pax_model *model) {
    int32_t symbol;
    int32_t leaf_count = 0;
    int32_t high_count = 0;
    int32_t leaf_position = 0;
    int32_t internal_position;
    int32_t output;
    if (!model) return false;
    for (symbol = model->alphabet_size - 1; symbol >= 0; --symbol) {
        pax_node *leaf = &model->nodes[symbol];
        leaf->parent = -1;
        if (leaf->frequency <= 1U) {
            model->leaves[leaf_count].frequency = leaf->frequency;
            model->leaves[leaf_count].node = symbol;
            ++leaf_count;
        } else {
            uint32_t original = leaf->frequency;
            leaf->frequency = (original + 1U) >> 1U;
            model->high[high_count].frequency = original;
            model->high[high_count].node = symbol;
            ++high_count;
        }
    }
    pax_sort(model->high, high_count);
    for (symbol = 0; symbol < high_count; ++symbol)
        model->leaves[leaf_count + symbol] = model->high[symbol];
    if (leaf_count + high_count != model->alphabet_size) return false;

    internal_position = model->alphabet_size;
    output = model->alphabet_size;
    while (output <= model->root) {
        pax_pair first;
        pax_pair second;
        model->nodes[output].frequency = 0x7fffU;
        if (!pax_model_pop_lowest(model, &leaf_position, &internal_position,
                                  output, &first) ||
            !pax_model_pop_lowest(model, &leaf_position, &internal_position,
                                  output, &second) ||
            first.frequency > UINT32_MAX - second.frequency)
            return false;
        /* The lowest candidate becomes the zero-bit (right) child; that
         * ordering is part of PAX's deterministic initial tree. */
        model->nodes[output].right = first.node;
        model->nodes[output].left = second.node;
        model->nodes[first.node].parent = output;
        model->nodes[second.node].parent = output;
        model->nodes[output].frequency = first.frequency + second.frequency;
        ++output;
    }
    model->nodes[model->root].parent = -1;
    return true;
}

static bool pax_model_init(pax_model *model, pax_bits *bits,
                           int32_t alphabet_size, uint32_t maximum) {
    int32_t index;
    if (!model) return false;
    xx_mem_zero(model, sizeof(*model));
    if (!bits || alphabet_size < 2 || alphabet_size > PAX_MAX_ALPHABET ||
        maximum == 0U)
        return false;
    model->bits = bits;
    model->alphabet_size = alphabet_size;
    model->node_count = alphabet_size * 2 - 1;
    model->root = alphabet_size * 2 - 2;
    model->maximum = maximum;
    model->count = 0U;
    model->next_rebuild = (uint32_t)(alphabet_size / 2);
    model->nodes =
        (pax_node *)xx_mem_calloc((size_t)model->node_count, sizeof(pax_node));
    model->leaves =
        (pax_pair *)xx_mem_calloc((size_t)alphabet_size, sizeof(pax_pair));
    model->high =
        (pax_pair *)xx_mem_calloc((size_t)alphabet_size, sizeof(pax_pair));
    if (!model->nodes || !model->leaves || !model->high) {
        pax_model_cleanup(model);
        return false;
    }
    for (index = 0; index < alphabet_size; ++index)
        model->nodes[index].frequency = 1U;
    model->valid = pax_model_rebuild(model);
    if (!model->valid) {
        pax_model_cleanup(model);
        return false;
    }
    return true;
}

static bool pax_model_decode(pax_model *model, int32_t *symbol) {
    int32_t node;
    uint32_t old_count;
    if (!model || !symbol || !model->valid) return false;
    node = model->root;
    while (node >= model->alphabet_size) {
        uint32_t bit = 0U;
        if (!pax_bits_get(model->bits, 1U, &bit)) return false;
        node = bit ? model->nodes[node].left : model->nodes[node].right;
        if (node < 0 || node >= model->node_count) return false;
    }
    if (model->nodes[node].frequency == UINT32_MAX) return false;
    ++model->nodes[node].frequency;
    old_count = model->count++;
    if (old_count >= model->next_rebuild) {
        if (!pax_model_rebuild(model)) return false;
        if (model->next_rebuild <= UINT32_MAX / 2U)
            model->next_rebuild *= 2U;
        else
            model->next_rebuild = model->maximum;
        if (model->next_rebuild > model->maximum)
            model->next_rebuild = model->maximum;
        model->count = 0U;
    }
    *symbol = node;
    return true;
}

/* --- stream parameters -------------------------------------------------- */

typedef struct pax_params_s {
    uint32_t window_bits;
    uint32_t low_bits;
    uint32_t subtractor;
    uint32_t length_symbols;
    uint32_t minimum_length;
    uint32_t main_maximum;
    uint32_t length_maximum;
} pax_params;

static bool pax_read_params(pax_bits *bits, pax_params *params) {
    if (!bits || !params) return false;
    if (!pax_bits_get(bits, 4U, &params->window_bits) ||
        !pax_bits_get(bits, 4U, &params->low_bits) ||
        !pax_bits_get(bits, 5U, &params->subtractor) ||
        !pax_bits_get(bits, 10U, &params->length_symbols) ||
        !pax_bits_get(bits, 3U, &params->minimum_length) ||
        !pax_bits_get(bits, 16U, &params->main_maximum) ||
        !pax_bits_get(bits, 16U, &params->length_maximum))
        return false;
    return params->window_bits >= 9U && params->window_bits <= 15U &&
           params->low_bits <= params->window_bits &&
           (params->window_bits - params->low_bits) <= 10U &&
           params->subtractor != 0U &&
           params->subtractor < (1U << params->window_bits) &&
           params->length_symbols >= 2U && params->length_symbols <= 2048U &&
           params->minimum_length >= 2U && params->main_maximum >= 1U &&
           params->length_maximum >= 1U;
}

static void pax_put_byte(uint8_t *window, int32_t window_size,
                         int32_t mirror_size, int32_t *window_position,
                         uint8_t *result, int64_t *produced, uint8_t value) {
    window[*window_position] = value;
    if (*window_position < mirror_size)
        window[*window_position + window_size] = value;
    if (++*window_position >= window_size) *window_position = 0;
    if (result) result[*produced] = value;
    ++*produced;
}

/* Decode one member.  output must hold expected_size bytes. */
static bool pax_decode(const uint8_t *packed, int64_t packed_size,
                       int64_t expected_size, uint8_t *output,
                       int64_t *consumed, xx_pd_struct *pd) {
    pax_bits bits;
    pax_params params;
    pax_model main_model;
    pax_model length_model;
    uint8_t *window = NULL;
    uint8_t *overlap = NULL;
    int32_t window_size;
    int32_t mirror_size;
    int32_t main_symbols;
    int32_t window_position = 0;
    int64_t produced = 0;
    bool result = false;

    xx_mem_zero(&main_model, sizeof(main_model));
    xx_mem_zero(&length_model, sizeof(length_model));
    if (!packed || !output || expected_size < 1 || packed_size < 8) return false;

    bits.data = packed;
    bits.size = packed_size;
    bits.position = 0;
    if (!pax_read_params(&bits, &params)) return false;

    window_size = (int32_t)((1U << params.window_bits) - params.subtractor);
    mirror_size = (int32_t)(params.minimum_length + params.length_symbols - 1U);
    main_symbols = 256 + (1 << (params.window_bits - params.low_bits));
    if (window_size < 1 || mirror_size < 1 || mirror_size > window_size ||
        main_symbols < 258 || main_symbols > PAX_MAX_ALPHABET ||
        window_size > INT32_MAX - mirror_size)
        return false;

    if (!pax_model_init(&main_model, &bits, main_symbols, params.main_maximum) ||
        !pax_model_init(&length_model, &bits, (int32_t)params.length_symbols,
                        params.length_maximum))
        goto done;

    window = (uint8_t *)xx_mem_alloc((size_t)(window_size + mirror_size));
    overlap = (uint8_t *)xx_mem_alloc((size_t)mirror_size);
    if (!window || !overlap) goto done;
    xx_rt_memset(window, ' ', (size_t)(window_size + mirror_size));

    while (produced < expected_size) {
        int32_t symbol = 0;
        uint32_t low_distance = 0U;
        int32_t length_symbol = 0;
        int32_t length;
        uint32_t high_distance;
        int32_t distance;
        int32_t source;
        const uint8_t *copy_source;
        int32_t index;
        if ((produced & 0x3fff) == 0 && pd && xx_pd_is_stopped(pd)) goto done;
        if (!pax_model_decode(&main_model, &symbol)) goto done;
        if (symbol < 256) {
            pax_put_byte(window, window_size, mirror_size, &window_position,
                         output, &produced, (uint8_t)symbol);
            continue;
        }
        if (!pax_bits_get(&bits, params.low_bits, &low_distance) ||
            !pax_model_decode(&length_model, &length_symbol))
            goto done;
        length = (int32_t)params.minimum_length + length_symbol;
        if (length < 1 || (int64_t)length > expected_size - produced) goto done;
        high_distance = (uint32_t)(symbol - 256) << params.low_bits;
        if (high_distance > (uint32_t)(window_size - 1) - low_distance)
            goto done;
        distance = (int32_t)(high_distance + low_distance);
        source = window_position - distance;
        if (source < 0) source += window_size;
        source -= length;
        if (source < 0) source += window_size;
        if (source < 0 || source >= window_size ||
            source > (window_size + mirror_size) - length)
            goto done;
        copy_source = window + source;
        if (source < window_position && window_position < source + length) {
            if (length > mirror_size) goto done;
            xx_rt_memcpy(overlap, copy_source, (size_t)length);
            copy_source = overlap;
        }
        for (index = 0; index < length; ++index)
            pax_put_byte(window, window_size, mirror_size, &window_position,
                         output, &produced, copy_source[index]);
    }
    if (consumed) *consumed = pax_bits_consumed(&bits);
    result = true;
done:
    pax_model_cleanup(&main_model);
    pax_model_cleanup(&length_model);
    if (window) xx_mem_free(window);
    if (overlap) xx_mem_free(overlap);
    return result;
}

/* --- header scan -------------------------------------------------------- */

static uint32_t pax_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) |
           ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

static uint32_t pax_be16(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 8U) | (uint32_t)bytes[1];
}

/* Validates one "LZF0" header and the stream parameters that follow it.
 * Returns the offset of the first coded byte in *stream_offset. */
static bool pax_scan_record(const uint8_t *image, int64_t size, int64_t offset,
                            int64_t *stream_offset, int64_t *raw_size,
                            uint32_t *name_size) {
    int64_t name_offset;
    uint32_t declared_name;
    int64_t declared_size;
    uint32_t index;
    pax_bits bits;
    pax_params params;
    if (!image || offset < 0 || size < (int64_t)PAX_MIN_RECORD ||
        offset > size - (int64_t)PAX_MIN_RECORD ||
        xx_rt_memcmp(image + offset, "LZF0", 4U) != 0)
        return false;
    declared_size = (int64_t)pax_be32(image + offset + 8);
    declared_name = pax_be16(image + offset + 28);
    if (declared_size < 1 || declared_size > PAX_MAX_OUTPUT ||
        declared_name < 2U || declared_name > PAX_MAX_NAME_SIZE ||
        offset > size - (int64_t)PAX_HEADER_FIXED - (int64_t)declared_name - 8)
        return false;
    name_offset = offset + (int64_t)PAX_HEADER_FIXED;
    if (image[name_offset + declared_name - 1U] != 0U) return false;
    for (index = 0U; index + 1U < declared_name; ++index) {
        uint8_t c = image[name_offset + index];
        if (c < 0x20U || c > 0x7eU) return false;
    }
    bits.data = image + name_offset + declared_name;
    bits.size = size - (name_offset + (int64_t)declared_name);
    bits.position = 0;
    if (!pax_read_params(&bits, &params)) return false;
    *stream_offset = name_offset + (int64_t)declared_name;
    *raw_size = declared_size;
    *name_size = declared_name;
    return true;
}

/* --- shared helpers ----------------------------------------------------- */

static bool pax_read_at(xx_io_device *device, int64_t offset, void *buffer,
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

static char *pax_normalize_name(const uint8_t *bytes, size_t size) {
    char *name;
    size_t input = 0U, output = 0U;
    if ((!bytes && size != 0U) || size > SIZE_MAX - 2U) return NULL;
    name = (char *)xx_mem_alloc(size + 2U);
    if (!name) return NULL;
    while (input < size) {
        size_t start, end, component_start;
        while (input < size && (bytes[input] == '/' || bytes[input] == '\\'))
            ++input;
        start = input;
        while (input < size && bytes[input] != '/' && bytes[input] != '\\')
            ++input;
        end = input;
        if (end == start || (end - start == 1U && bytes[start] == '.')) continue;
        if (end - start == 2U && bytes[start] == '.' &&
            bytes[start + 1U] == '.') {
            if (output != 0U) {
                while (output != 0U && name[output - 1U] != '/') --output;
                if (output != 0U) --output;
            }
            continue;
        }
        if (output != 0U) name[output++] = '/';
        component_start = output;
        while (start < end) {
            uint8_t c = bytes[start++];
            if (c < 0x20U || c == '"' || c == '*' || c == ':' || c == '<' ||
                c == '>' || c == '?' || c == '|' || c == 0U)
                name[output++] = '_';
            else
                name[output++] = (char)c;
        }
        while (output > component_start &&
               (name[output - 1U] == ' ' || name[output - 1U] == '.'))
            --output;
        if (output == component_start) name[output++] = '_';
    }
    if (output == 0U) name[output++] = '_';
    name[output] = 0;
    return name;
}

static bool pax_safe_output_name(const char *name) {
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

static void pax_stream_free(void *opaque) {
    pax_stream *stream = (pax_stream *)opaque;
    size_t index;
    if (!stream) return;
    for (index = 0U; index < stream->count; ++index)
        if (stream->items[index].name) xx_str_free(stream->items[index].name);
    if (stream->items) xx_mem_free(stream->items);
    if (stream->image) xx_mem_free(stream->image);
    xx_mem_free(stream);
}

static bool pax_add_member(pax_stream *stream, const pax_member *member) {
    pax_member *grown;
    if (!stream || !member || stream->count >= PAX_MAX_MEMBERS ||
        stream->count > SIZE_MAX / sizeof(*grown) - 1U)
        return false;
    grown = (pax_member *)xx_mem_realloc(stream->items,
                                         (stream->count + 1U) * sizeof(*grown));
    if (!grown) return false;
    stream->items = grown;
    stream->items[stream->count++] = *member;
    return true;
}

/* Finds the next offset at or after `from` that opens a well-formed header. */
static int64_t pax_find_next(const uint8_t *image, int64_t size, int64_t from) {
    int64_t offset;
    for (offset = from; offset + (int64_t)PAX_MIN_RECORD <= size; ++offset) {
        int64_t stream_offset = 0;
        int64_t raw_size = 0;
        uint32_t name_size = 0U;
        if (image[offset] != 'L' || image[offset + 1] != 'Z' ||
            image[offset + 2] != 'F' || image[offset + 3] != '0')
            continue;
        if (pax_scan_record(image, size, offset, &stream_offset, &raw_size,
                            &name_size))
            return offset;
    }
    return -1;
}

static bool pax_parse(Abstractformat *format, bool decode_members,
                      pax_stream **result, xx_pd_struct *pd) {
    pax_stream *stream = NULL;
    int64_t total, size;
    int64_t record_offset = 0;
    if (!format || !format->device || !result || format->base_address < 0)
        return false;
    total = xx_io_total_size(format->device);
    if (total < format->base_address) return false;
    size = total - format->base_address;
    if (size < (int64_t)PAX_MIN_RECORD || size > PAX_MAX_INPUT) return false;

    stream = (pax_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!stream) return false;
    stream->image = (uint8_t *)xx_mem_alloc((size_t)size);
    if (!stream->image ||
        !pax_read_at(format->device, format->base_address, stream->image,
                     (size_t)size))
        goto fail;
    stream->image_size = size;

    while (record_offset < size) {
        int64_t stream_offset = 0;
        int64_t raw_size = 0;
        int64_t next_offset;
        int64_t data_end;
        uint32_t name_size = 0U;
        pax_member member;
        if (pd && xx_pd_is_stopped(pd)) goto fail;
        if (!pax_scan_record(stream->image, size, record_offset, &stream_offset,
                             &raw_size, &name_size))
            goto fail;
        next_offset = pax_find_next(stream->image, size, stream_offset + 1);
        data_end = next_offset >= 0 ? next_offset : size;
        if (data_end <= stream_offset) goto fail;

        if (decode_members) {
            uint8_t *plain = (uint8_t *)xx_mem_alloc((size_t)raw_size);
            int64_t consumed = 0;
            bool decoded;
            if (!plain) goto fail;
            decoded = pax_decode(stream->image + stream_offset,
                                 data_end - stream_offset, raw_size, plain,
                                 &consumed, pd);
            /* The stored copy of the plaintext prefix is the anchor that
             * separates a correct decode from a plausible one. */
            if (decoded &&
                (raw_size < 4 ||
                 xx_rt_memcmp(plain, stream->image + record_offset + 4, 4U) !=
                     0 ||
                 consumed <= 0 || consumed > data_end - stream_offset))
                decoded = false;
            xx_mem_free(plain);
            if (!decoded) {
                /* A physically truncated final record is common in the wild;
                 * earlier independent records stay valid.  Corruption in the
                 * middle is not accepted. */
                if (next_offset < 0 && stream->count != 0U) break;
                goto fail;
            }
        }

        xx_mem_zero(&member, sizeof(member));
        member.name = pax_normalize_name(stream->image + record_offset +
                                             PAX_HEADER_FIXED,
                                         (size_t)(name_size - 1U));
        if (!member.name) goto fail;
        member.header_offset = format->base_address + record_offset;
        member.header_size = stream_offset - record_offset;
        member.data_offset = format->base_address + stream_offset;
        member.packed_size = data_end - stream_offset;
        member.unpacked_size = (uint64_t)raw_size;
        if (!pax_add_member(stream, &member)) {
            xx_str_free(member.name);
            goto fail;
        }
        if (next_offset < 0) {
            record_offset = size;
            break;
        }
        record_offset = next_offset;
    }
    if (stream->count == 0U) goto fail;
    stream->archive_size = size;
    *result = stream;
    return true;
fail:
    pax_stream_free(stream);
    return false;
}

static bool pax_copy_options(xx_list_s *destination, const xx_list_s *source) {
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

static const xx_var *pax_option(const xx_list_s *options, uint32_t id) {
    size_t index;
    if (!options) return NULL;
    for (index = 0U; index < options->count; ++index) {
        const xx_meta *meta =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, index);
        if (meta && meta->meta_id == id) return &meta->var;
    }
    return NULL;
}

static bool pax_set_record(xx_archive_record *record, const pax_member *member) {
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = member->header_offset;
    record->header_size = member->header_size;
    record->data_offset = member->data_offset;
    record->compressed_size = member->packed_size;
    return xx_archive_record_set_original_name(record, member->name) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                          (uint64_t)member->packed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                          member->unpacked_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD,
                                          1U) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false);
}

/* --- vtable ------------------------------------------------------------- */

void xx_pax_init(xx_pax *archive, xx_io_device *device, int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    xx_format_init(&archive->format, device, base_address);
    archive->format.endian = XX_ENDIAN_BIG;
    archive->format.file_type = XX_PAX_FILE_TYPE;
    archive->format.format_type = XX_TYPE_ARCHIVE;
    archive->format.is_archive = true;
    xx_format_set_mime_type(&archive->format, "application/x-gemview-pax");
    xx_format_set_extension(&archive->format, "pax");
    archive->format.check_is_valid = xx_pax_check_is_valid;
    archive->format.handle_base_info = xx_pax_handle_base_info;
    archive->format.get_format_size = xx_pax_get_format_size;
    archive->format.get_number_of_archive_records =
        xx_pax_get_number_of_archive_records;
    archive->format.create_archive_records_reading =
        xx_pax_create_archive_records_reading;
    archive->format.get_current_archive_record =
        xx_pax_get_current_archive_record;
    archive->format.unpack_current_archive_record =
        xx_pax_unpack_current_archive_record;
    archive->format.archive_record_move_to_next =
        xx_pax_archive_record_move_to_next;
    archive->format.free_archive_records_reading =
        xx_pax_free_archive_records_reading;
    archive->archive_end = -1;
}

xx_pax *xx_pax_create(xx_io_device *device, int64_t base_address) {
    xx_pax *archive = (xx_pax *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_pax_init(archive, device, base_address);
    return archive;
}

void xx_pax_destroy(xx_pax *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}

void xx_pax_free(xx_pax *archive) {
    if (!archive) return;
    xx_pax_destroy(archive);
    xx_mem_free(archive);
}

bool xx_pax_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    pax_stream *stream;
    if (!pax_parse(format, true, &stream, pd)) return false;
    pax_stream_free(stream);
    return true;
}

bool xx_pax_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    pax_stream *stream;
    xx_pax *archive;
    if (!format || !pax_parse(format, true, &stream, pd)) return false;
    archive = (xx_pax *)format;
    archive->number_of_records = stream->count;
    archive->archive_end = format->base_address + stream->archive_size;
    format->number_of_archive_records = stream->count;
    format->format_size = stream->archive_size;
    format->is_valid = true;
    format->base_info_handled = true;
    pax_stream_free(stream);
    return true;
}

int64_t xx_pax_get_format_size(Abstractformat *format, xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_pax_handle_base_info(format, pd))
               ? format->format_size
               : -1;
}

uint64_t xx_pax_get_number_of_archive_records(Abstractformat *format,
                                              xx_pd_struct *pd) {
    return format && (format->base_info_handled ||
                      xx_pax_handle_base_info(format, pd))
               ? ((xx_pax *)format)->number_of_records
               : 0U;
}

xx_archive_record_state *xx_pax_create_archive_records_reading(
    Abstractformat *format, const xx_list_s *options, xx_pd_struct *pd) {
    pax_stream *stream;
    xx_archive_record_state *state;
    if (!pax_parse(format, true, &stream, pd)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) {
        pax_stream_free(stream);
        return NULL;
    }
    xx_archive_record_state_init(state, format);
    state->internal_state = stream;
    state->free_internal = pax_stream_free;
    state->total_records = (int64_t)stream->count;
    if (!pax_copy_options(&state->options, options) ||
        !pax_set_record(&state->current_record, &stream->items[0])) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    return state;
}

const xx_archive_record *xx_pax_get_current_archive_record(
    Abstractformat *format, xx_archive_record_state *state) {
    return format && state && state->format == format && state->has_record
               ? &state->current_record
               : NULL;
}

bool xx_pax_archive_record_move_to_next(Abstractformat *format,
                                        xx_archive_record_state *state,
                                        xx_pd_struct *pd) {
    pax_stream *stream;
    (void)pd;
    if (!format || !state || state->format != format ||
        !(stream = (pax_stream *)state->internal_state) ||
        ++stream->index >= stream->count) {
        if (state) state->has_record = false;
        return false;
    }
    ++state->current_index;
    state->has_record =
        pax_set_record(&state->current_record, &stream->items[stream->index]);
    return state->has_record;
}

bool xx_pax_unpack_current_archive_record(Abstractformat *format,
                                          xx_archive_record_state *state,
                                          xx_pd_struct *pd) {
    pax_stream *stream;
    pax_member *member;
    const xx_var *path_option;
    const char *base = NULL;
    char *owned_base = NULL;
    char *path = NULL;
    uint8_t *plain = NULL;
    int64_t consumed = 0;
    size_t written = 0U;
    bool result = false;
    if (!format || !state || state->format != format || !state->has_record ||
        !(stream = (pax_stream *)state->internal_state) ||
        stream->index >= stream->count || (pd && xx_pd_is_stopped(pd)))
        return false;
    member = &stream->items[stream->index];
    if (!pax_safe_output_name(member->name) ||
        member->unpacked_size > (uint64_t)PAX_MAX_OUTPUT)
        goto done;
    plain = (uint8_t *)xx_mem_alloc((size_t)member->unpacked_size);
    if (!plain) goto done;
    if (!pax_decode(stream->image +
                        (member->data_offset - format->base_address),
                    member->packed_size, (int64_t)member->unpacked_size, plain,
                    &consumed, pd))
        goto done;
    path_option = pax_option(&state->options, XX_META_ID_OPT_UNPACK_PATH);
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
        while (written < (size_t)member->unpacked_size) {
            ssize_t amount =
                xx_io_write(destination, plain + written,
                            (size_t)member->unpacked_size - written);
            if (amount <= 0 ||
                (size_t)amount > (size_t)member->unpacked_size - written) {
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

void xx_pax_free_archive_records_reading(Abstractformat *format,
                                         xx_archive_record_state *state) {
    (void)format;
    xx_archive_record_state_free(state);
}
