/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 *
 * DOS Navigator 1.x has DEFLATE-like blocks.  In dynamic blocks the three
 * counts appear in HDIST, HCLEN, HLIT order, rather than RFC 1951 order.
 * This bounded C decoder follows the local MIT XArchive xdndecoder.cpp
 * implementation; standard raw DEFLATE rejects the selected corpus.
 */
#include "xxfclib/formats/dn/xx_dn.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

#define DN_MAX_RECORDS 4096U
#define DN_MAX_NAME 512U
#define DN_MAX_PACKED (64U * 1024U * 1024U)
#define DN_MAX_PLAIN (256U * 1024U * 1024U)
#define DN_MAX_TOTAL_PLAIN (512U * 1024U * 1024U)

#ifdef DN
#define DN_FILE_TYPE XX_FILE_TYPE_DN
#else
#define DN_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

typedef struct dn_bits_s {
    const uint8_t *data;
    size_t size, bit;
} dn_bits;
typedef struct dn_huffman_s {
    uint16_t counts[16];
    uint16_t symbols[288];
    unsigned max_bits, symbol_count;
} dn_huffman;
typedef struct dn_decoder_s {
    dn_bits bits;
    uint8_t *output;
    size_t capacity, produced;
    xx_pd_struct *pd;
} dn_decoder;

static const uint16_t dn_length_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t dn_length_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const uint16_t dn_distance_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
    6145, 8193, 12289, 16385, 24577
};
static const uint8_t dn_distance_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};
static const uint8_t dn_code_length_order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

static bool dn_read_bits(dn_bits *bits, unsigned width, uint32_t *value) {
    uint32_t result = 0U;
    unsigned i;
    if (!bits || !value || width > 16U || bits->size > SIZE_MAX / 8U ||
        bits->bit > bits->size * 8U ||
        width > bits->size * 8U - bits->bit)
        return false;
    for (i = 0U; i < width; ++i) {
        size_t position = bits->bit + i;
        result |= (uint32_t)((bits->data[position >> 3U] >>
                              (position & 7U)) & 1U) << i;
    }
    bits->bit += width;
    *value = result;
    return true;
}

static bool dn_huffman_build(dn_huffman *tree, const uint8_t *lengths,
                              unsigned count, bool allow_empty) {
    uint16_t offsets[16] = {0};
    int32_t slots = 1;
    unsigned i, width;
    if (!tree || !lengths || count == 0U || count > 288U) return false;
    xx_mem_zero(tree, sizeof(*tree));
    for (i = 0U; i < count; ++i) {
        width = lengths[i];
        if (width > 15U) return false;
        if (width != 0U) {
            ++tree->counts[width];
            ++tree->symbol_count;
            if (width > tree->max_bits) tree->max_bits = width;
        }
    }
    if (tree->symbol_count == 0U) return allow_empty;
    for (width = 1U; width <= 15U; ++width) {
        slots = (slots << 1U) - tree->counts[width];
        if (slots < 0) return false;
    }
    for (width = 1U; width < 15U; ++width)
        offsets[width + 1U] =
            (uint16_t)(offsets[width] + tree->counts[width]);
    for (i = 0U; i < count; ++i) {
        width = lengths[i];
        if (width != 0U) {
            uint16_t index = offsets[width]++;
            if (index >= 288U) return false;
            tree->symbols[index] = (uint16_t)i;
        }
    }
    return true;
}

static bool dn_huffman_decode(dn_bits *bits, const dn_huffman *tree,
                               uint32_t *symbol) {
    uint32_t code = 0U, first = 0U, index = 0U;
    unsigned width;
    if (!bits || !tree || !symbol || tree->max_bits == 0U ||
        tree->symbol_count == 0U)
        return false;
    for (width = 1U; width <= tree->max_bits; ++width) {
        uint32_t bit;
        if (!dn_read_bits(bits, 1U, &bit)) return false;
        code |= bit;
        if (code >= first && code - first < tree->counts[width]) {
            uint32_t position = index + code - first;
            if (position >= tree->symbol_count) return false;
            *symbol = tree->symbols[position];
            return true;
        }
        index += tree->counts[width];
        first = (first + tree->counts[width]) << 1U;
        code <<= 1U;
    }
    return false;
}

static bool dn_emit(dn_decoder *decoder, uint8_t value) {
    if (!decoder || decoder->produced >= decoder->capacity ||
        ((decoder->produced & 0xfffU) == 0U && decoder->pd &&
         xx_pd_is_stopped(decoder->pd)))
        return false;
    decoder->output[decoder->produced++] = value;
    return true;
}
static bool dn_copy_match(dn_decoder *decoder, uint32_t distance,
                           uint32_t length) {
    uint32_t i;
    if (!decoder || distance == 0U || distance > 32768U ||
        distance > decoder->produced ||
        length > decoder->capacity - decoder->produced)
        return false;
    for (i = 0U; i < length; ++i) {
        if (!dn_emit(decoder,
                     decoder->output[decoder->produced - distance]))
            return false;
    }
    return true;
}

static bool dn_fixed_trees(dn_huffman *literal, dn_huffman *distance) {
    uint8_t lit_lengths[288], dist_lengths[32];
    unsigned i;
    for (i = 0U; i <= 143U; ++i) lit_lengths[i] = 8U;
    for (; i <= 255U; ++i) lit_lengths[i] = 9U;
    for (; i <= 279U; ++i) lit_lengths[i] = 7U;
    for (; i <= 287U; ++i) lit_lengths[i] = 8U;
    for (i = 0U; i < 32U; ++i) dist_lengths[i] = 5U;
    return dn_huffman_build(literal, lit_lengths, 288U, false) &&
           dn_huffman_build(distance, dist_lengths, 32U, false);
}

static bool dn_dynamic_trees(dn_decoder *decoder, dn_huffman *literal,
                              dn_huffman *distance) {
    uint8_t code_lengths[19] = {0};
    uint8_t lengths[286U + 30U] = {0};
    dn_huffman code_tree;
    uint32_t dist_count, code_count, lit_count, total, index = 0U;
    uint32_t previous = 0U, i;
    if (!dn_read_bits(&decoder->bits, 5U, &dist_count) ||
        !dn_read_bits(&decoder->bits, 4U, &code_count) ||
        !dn_read_bits(&decoder->bits, 5U, &lit_count))
        return false;
    ++dist_count;
    code_count += 4U;
    lit_count += 257U;
    if (dist_count > 30U || code_count > 19U || lit_count > 286U)
        return false;
    for (i = 0U; i < code_count; ++i) {
        uint32_t length;
        if (!dn_read_bits(&decoder->bits, 3U, &length)) return false;
        code_lengths[dn_code_length_order[i]] = (uint8_t)length;
    }
    if (!dn_huffman_build(&code_tree, code_lengths, 19U, false))
        return false;
    total = lit_count + dist_count;
    while (index < total) {
        uint32_t symbol, repeat = 0U, value = 0U;
        if (!dn_huffman_decode(&decoder->bits, &code_tree, &symbol))
            return false;
        if (symbol < 16U) {
            lengths[index++] = (uint8_t)symbol;
            previous = symbol;
            continue;
        }
        if (symbol == 16U) {
            if (!dn_read_bits(&decoder->bits, 2U, &repeat)) return false;
            repeat += 3U;
            value = previous;
        } else if (symbol == 17U) {
            if (!dn_read_bits(&decoder->bits, 3U, &repeat)) return false;
            repeat += 3U;
            previous = 0U;
        } else if (symbol == 18U) {
            if (!dn_read_bits(&decoder->bits, 7U, &repeat)) return false;
            repeat += 11U;
            previous = 0U;
        } else {
            return false;
        }
        if (repeat > total - index) return false;
        while (repeat-- != 0U) lengths[index++] = (uint8_t)value;
    }
    if (lengths[256] == 0U) return false;
    return dn_huffman_build(literal, lengths, lit_count, false) &&
           dn_huffman_build(distance, lengths + lit_count, dist_count, true);
}

static bool dn_huffman_block(dn_decoder *decoder,
                              const dn_huffman *literal,
                              const dn_huffman *distance) {
    for (;;) {
        uint32_t symbol;
        if (decoder->pd && xx_pd_is_stopped(decoder->pd)) return false;
        if (!dn_huffman_decode(&decoder->bits, literal, &symbol))
            return false;
        if (symbol < 256U) {
            if (!dn_emit(decoder, (uint8_t)symbol)) return false;
        } else if (symbol == 256U) {
            return true;
        } else if (symbol <= 285U) {
            uint32_t length_index = symbol - 257U;
            uint32_t length_extra, distance_symbol, distance_extra;
            uint32_t length, gap;
            if (!dn_read_bits(&decoder->bits,
                              dn_length_extra[length_index],
                              &length_extra) ||
                !dn_huffman_decode(&decoder->bits, distance,
                                    &distance_symbol) ||
                distance_symbol >= 30U ||
                !dn_read_bits(&decoder->bits,
                              dn_distance_extra[distance_symbol],
                              &distance_extra))
                return false;
            length = dn_length_base[length_index] + length_extra;
            gap = dn_distance_base[distance_symbol] + distance_extra;
            if (!dn_copy_match(decoder, gap, length)) return false;
        } else {
            return false;
        }
    }
}

static bool dn_stored_block(dn_decoder *decoder) {
    uint32_t length, complement, i;
    decoder->bits.bit = (decoder->bits.bit + 7U) & ~(size_t)7U;
    if (!dn_read_bits(&decoder->bits, 16U, &length) ||
        !dn_read_bits(&decoder->bits, 16U, &complement) ||
        (length ^ 0xffffU) != complement ||
        length > decoder->capacity - decoder->produced)
        return false;
    for (i = 0U; i < length; ++i) {
        uint32_t value;
        if (!dn_read_bits(&decoder->bits, 8U, &value) ||
            !dn_emit(decoder, (uint8_t)value))
            return false;
    }
    return true;
}

static bool dn_decode(const uint8_t *packed, size_t packed_size,
                       uint8_t *plain, size_t plain_size,
                       xx_pd_struct *pd) {
    dn_decoder decoder;
    dn_huffman fixed_literal, fixed_distance;
    bool final;
    if (!packed || !plain || packed_size == 0U ||
        packed_size > DN_MAX_PACKED || plain_size == 0U ||
        plain_size > DN_MAX_PLAIN ||
        !dn_fixed_trees(&fixed_literal, &fixed_distance))
        return false;
    xx_mem_zero(&decoder, sizeof(decoder));
    decoder.bits.data = packed;
    decoder.bits.size = packed_size;
    decoder.output = plain;
    decoder.capacity = plain_size;
    decoder.pd = pd;
    do {
        uint32_t final_bit, type;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !dn_read_bits(&decoder.bits, 1U, &final_bit) ||
            !dn_read_bits(&decoder.bits, 2U, &type))
            return false;
        final = final_bit != 0U;
        if (type == 0U) {
            if (!dn_stored_block(&decoder)) return false;
        } else if (type == 1U) {
            if (!dn_huffman_block(&decoder, &fixed_literal,
                                   &fixed_distance)) return false;
        } else if (type == 2U) {
            dn_huffman literal, distance;
            if (!dn_dynamic_trees(&decoder, &literal, &distance) ||
                !dn_huffman_block(&decoder, &literal, &distance))
                return false;
        } else {
            return false;
        }
    } while (!final);
    return decoder.produced == plain_size &&
           (decoder.bits.bit + 7U) / 8U == packed_size;
}

typedef struct dn_row_s {
    int64_t header_offset, data_offset;
    uint32_t packed_size, raw_size;
    uint16_t name_size;
    char name[DN_MAX_NAME + 1U];
} dn_row;

static bool dn_name_valid(const char *name, size_t length) {
    size_t i;
    if (!name || length == 0U || length > DN_MAX_NAME) return false;
    for (i = 0U; i < length; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c <= 0x20U || c >= 0x7fU || c == ':' || c == '<' ||
            c == '>' || c == '"' || c == '|' || c == '?' || c == '*')
            return false;
    }
    return true;
}

static bool pm_parse(Abstractformat *format, pm_stream *stream,
                     xx_pd_struct *pd) {
    const uint8_t data_magic[4] = {0x84, 0x8d, 0x01, 0x02};
    const uint8_t dir_magic[4] = {0x84, 0x8d, 0x03, 0x04};
    const uint8_t end_magic[4] = {0x84, 0x8d, 0x05, 0x06};
    dn_row *rows = NULL;
    uint8_t header[46];
    int64_t span = pm_available(format), pos = 0;
    size_t count = 0U, i;
    uint64_t total_plain = 0U;
    bool ok = false;
    if (span < 30 + 46 || !format || !stream) return false;
    rows = (dn_row *)xx_mem_calloc(DN_MAX_RECORDS, sizeof(*rows));
    if (!rows) return false;

    /* Physical data records run first.  Retain each original header offset
     * and name so the later directory can be cross-checked exactly. */
    while (span - pos >= 30) {
        dn_row *row;
        int64_t data_offset;
        uint32_t packed, raw;
        uint16_t name_size;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !pm_read(format, pos, header, 30U)) goto done;
        if (xx_rt_memcmp(header, data_magic, 4U) != 0) break;
        if (count >= DN_MAX_RECORDS) goto done;
        packed = xx_data_get_u32(header + 18U, 4, 0, false);
        raw = xx_data_get_u32(header + 22U, 4, 0, false);
        name_size = xx_data_get_u16(header + 28U, 2, 0, false);
        if (packed == 0U || raw == 0U ||
            packed > DN_MAX_PACKED || raw > DN_MAX_PLAIN ||
            name_size == 0U || name_size > DN_MAX_NAME ||
            total_plain > DN_MAX_TOTAL_PLAIN - raw)
            goto done;
        data_offset = pos + 30 + name_size;
        if (data_offset > span || packed > span - data_offset)
            goto done;
        row = &rows[count];
        row->header_offset = pos;
        row->data_offset = data_offset;
        row->packed_size = packed;
        row->raw_size = raw;
        row->name_size = name_size;
        if (!pm_read(format, pos + 30, row->name, name_size) ||
            !dn_name_valid(row->name, name_size))
            goto done;
        row->name[name_size] = 0;
        total_plain += raw;
        ++count;
        pos = data_offset + packed;
    }
    if (count == 0U) goto done;

    /* Directory records repeat the names and absolute data-header offsets;
     * this check is important because the data magic is only four bytes. */
    for (i = 0U; i < count; ++i) {
        uint16_t name_size;
        char name[DN_MAX_NAME];
        if ((pd && xx_pd_is_stopped(pd)) || span - pos < 46 ||
            !pm_read(format, pos, header, 46U) ||
            xx_rt_memcmp(header, dir_magic, 4U) != 0)
            goto done;
        name_size = xx_data_get_u16(header + 30U, 2, 0, false);
        if (name_size != rows[i].name_size ||
            (int64_t)xx_data_get_u32(header + 42U, 4, 0, false) != rows[i].header_offset ||
            name_size > span - pos - 46 ||
            !pm_read(format, pos + 46, name, name_size) ||
            xx_rt_memcmp(name, rows[i].name, name_size) != 0)
            goto done;
        pos += 46 + name_size;
    }
    if (span - pos != 46 || !pm_read(format, pos, header, 46U) ||
        xx_rt_memcmp(header, end_magic, 4U) != 0)
        goto done;

    for (i = 0U; i < count; ++i) {
        dn_row *row = &rows[i];
        pm_member *member;
        if ((pd && xx_pd_is_stopped(pd)) ||
            !pm_add(format, stream, row->name, row->data_offset,
                    row->packed_size))
            goto done;
        member = &stream->items[stream->count - 1U];
        member->size = row->raw_size;
        member->packed_size = row->packed_size;
        if (row->packed_size != row->raw_size) {
            uint8_t *packed =
                (uint8_t *)xx_mem_alloc(row->packed_size);
            uint8_t *plain =
                (uint8_t *)xx_mem_alloc(row->raw_size);
            if (!packed || !plain ||
                !pm_read(format, row->data_offset, packed,
                         row->packed_size) ||
                !dn_decode(packed, row->packed_size, plain,
                           row->raw_size, pd)) {
                if (packed) xx_mem_free(packed);
                if (plain) xx_mem_free(plain);
                goto done;
            }
            xx_mem_free(packed);
            member->memory = plain;
        }
    }
    stream->size = span;
    ok = true;
done:
    xx_mem_free(rows);
    return ok;
}

static bool dn_record(xx_archive_record_state *state) {
    pm_stream *stream = (pm_stream *)state->internal_state;
    const pm_member *member = &stream->items[stream->index];
    if (!pm_record(state)) return false;
    return xx_archive_record_set_meta_u64(&state->current_record,
        XX_META_ID_COMPRESSION_METHOD, member->memory ? 1U : 0U);
}
static xx_archive_record_state *dn_create_records(Abstractformat *format,
                                                   const xx_list_s *options,
                                                   xx_pd_struct *pd) {
    xx_archive_record_state *state = pm_create_records(format, options, pd);
    if (state && state->has_record && !dn_record(state)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    return state;
}
static bool dn_next(Abstractformat *format, xx_archive_record_state *state,
                    xx_pd_struct *pd) {
    if (!pm_next(format, state, pd)) return false;
    state->has_record = dn_record(state);
    return state->has_record;
}

void xx_dn_init(xx_dn *archive, xx_io_device *device,
                int64_t base_address) {
    if (!archive) return;
    xx_mem_zero(archive, sizeof(*archive));
    pm_init(&archive->format, device, base_address, DN_FILE_TYPE, "138");
    archive->format.endian = XX_ENDIAN_LITTLE;
    xx_format_set_mime_type(&archive->format,
                            "application/x-dos-navigator-archive");
    archive->format.create_archive_records_reading = dn_create_records;
    archive->format.archive_record_move_to_next = dn_next;
}
xx_dn *xx_dn_create(xx_io_device *device, int64_t base_address) {
    xx_dn *archive = (xx_dn *)xx_mem_alloc(sizeof(*archive));
    if (archive) xx_dn_init(archive, device, base_address);
    return archive;
}
void xx_dn_destroy(xx_dn *archive) {
    if (archive) xx_format_cleanup_extra_parameters(&archive->format);
}
void xx_dn_free(xx_dn *archive) {
    if (!archive) return;
    xx_dn_destroy(archive);
    xx_mem_free(archive);
}
bool xx_dn_check_is_valid(Abstractformat *format, xx_pd_struct *pd) {
    return pm_valid(format, pd);
}
bool xx_dn_handle_base_info(Abstractformat *format, xx_pd_struct *pd) {
    return pm_handle(format, pd);
}
