/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/formats/xz/xx_xz.h"
#include "xx_xz_defs.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/data/xx_data.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef struct xx_xz_block_s {
    int64_t header_offset;
    int64_t header_size;
    int64_t data_offset;
    int64_t compressed_size;
    int64_t padding_offset;
    int64_t padding_size;
    int64_t check_offset;
    int64_t check_size;
    uint64_t uncompressed_size;
    uint64_t unpadded_size;
    uint8_t flags;
    uint8_t filter_count;
    uint8_t lzma2_property;
    uint16_t delta_distance;
    bool extractable;
} xx_xz_block;

typedef struct xx_xz_stream_s {
    int64_t header_offset;
    int64_t index_offset;
    int64_t index_size;
    int64_t footer_offset;
    int64_t stream_end;
    int64_t padding_offset;
    int64_t padding_size;
    size_t first_block;
    size_t block_count;
    uint8_t check_type;
} xx_xz_stream;

typedef struct xx_xz_private_s {
    xx_xz_stream *streams;
    size_t stream_count;
    xx_xz_block *blocks;
    size_t block_count;
    int64_t format_end;
    uint64_t uncompressed_size;
    uint64_t compressed_data_size;
    bool can_extract;
} xx_xz_private;

typedef struct xx_xz_candidate_s {
    xx_xz_stream stream;
    xx_xz_block *blocks;
    size_t block_count;
} xx_xz_candidate;

typedef struct xx_xz_ds_stream_s {
    xx_data_struct *items;
    size_t count;
} xx_xz_ds_stream;

typedef struct xx_xz_record_stream_s {
    const xx_data_struct_field_desc *fields;
    size_t count;
} xx_xz_record_stream;

typedef struct xx_xz_sha256_s {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t buffer[64];
    size_t used;
} xx_xz_sha256;

typedef struct xx_xz_sink_s {
    xx_io_device device;
    xx_io_device *target;
    uint64_t expected;
    uint64_t written;
    uint32_t crc32;
    uint64_t crc64;
    xx_xz_sha256 sha256;
    uint8_t delta_history[256];
    uint64_t delta_position;
    uint16_t delta_distance;
    bool failed;
} xx_xz_sink;

static void xx_xz_vtable_destroy(Abstractformat *self);

static uint32_t xx_xz_rotr32(uint32_t value, unsigned int shift) {
    return (value >> shift) | (value << (32U - shift));
}

static void xx_xz_sha256_transform(xx_xz_sha256 *ctx,
                                   const uint8_t block[64]) {
    static const uint32_t constants[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
    };
    uint32_t words[64];
    uint32_t a, b, c, d, e, f, g, h;
    size_t i;
    for (i = 0U; i < 16U; ++i) {
        size_t p = i * 4U;
        words[i] = ((uint32_t)block[p] << 24U) |
                   ((uint32_t)block[p + 1U] << 16U) |
                   ((uint32_t)block[p + 2U] << 8U) |
                   (uint32_t)block[p + 3U];
    }
    for (i = 16U; i < 64U; ++i) {
        uint32_t s0 = xx_xz_rotr32(words[i - 15U], 7U) ^
                      xx_xz_rotr32(words[i - 15U], 18U) ^
                      (words[i - 15U] >> 3U);
        uint32_t s1 = xx_xz_rotr32(words[i - 2U], 17U) ^
                      xx_xz_rotr32(words[i - 2U], 19U) ^
                      (words[i - 2U] >> 10U);
        words[i] = words[i - 16U] + s0 + words[i - 7U] + s1;
    }
    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2];
    d = ctx->state[3]; e = ctx->state[4]; f = ctx->state[5];
    g = ctx->state[6]; h = ctx->state[7];
    for (i = 0U; i < 64U; ++i) {
        uint32_t sum1 = xx_xz_rotr32(e, 6U) ^ xx_xz_rotr32(e, 11U) ^
                        xx_xz_rotr32(e, 25U);
        uint32_t choose = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + sum1 + choose + constants[i] + words[i];
        uint32_t sum0 = xx_xz_rotr32(a, 2U) ^ xx_xz_rotr32(a, 13U) ^
                        xx_xz_rotr32(a, 22U);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = sum0 + majority;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c;
    ctx->state[3] += d; ctx->state[4] += e; ctx->state[5] += f;
    ctx->state[6] += g; ctx->state[7] += h;
}

static void xx_xz_sha256_init(xx_xz_sha256 *ctx) {
    static const uint32_t initial[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
    };
    xx_mem_zero(ctx, sizeof(*ctx));
    xx_rt_memcpy(ctx->state, initial, sizeof(initial));
}

static void xx_xz_sha256_update(xx_xz_sha256 *ctx, const uint8_t *data,
                                size_t size) {
    if (!ctx || (!data && size != 0U)) return;
    ctx->bit_count += (uint64_t)size * 8U;
    while (size != 0U) {
        size_t take = 64U - ctx->used;
        if (take > size) take = size;
        xx_rt_memcpy(ctx->buffer + ctx->used, data, take);
        ctx->used += take;
        data += take;
        size -= take;
        if (ctx->used == 64U) {
            xx_xz_sha256_transform(ctx, ctx->buffer);
            ctx->used = 0U;
        }
    }
}

static void xx_xz_sha256_final(xx_xz_sha256 *ctx, uint8_t digest[32]) {
    uint64_t bits = ctx->bit_count;
    size_t i;
    ctx->buffer[ctx->used++] = 0x80U;
    if (ctx->used > 56U) {
        xx_rt_memset(ctx->buffer + ctx->used, 0, 64U - ctx->used);
        xx_xz_sha256_transform(ctx, ctx->buffer);
        ctx->used = 0U;
    }
    xx_rt_memset(ctx->buffer + ctx->used, 0, 56U - ctx->used);
    for (i = 0U; i < 8U; ++i) {
        ctx->buffer[63U - i] = (uint8_t)(bits >> (i * 8U));
    }
    xx_xz_sha256_transform(ctx, ctx->buffer);
    for (i = 0U; i < 8U; ++i) {
        digest[i * 4U] = (uint8_t)(ctx->state[i] >> 24U);
        digest[i * 4U + 1U] = (uint8_t)(ctx->state[i] >> 16U);
        digest[i * 4U + 2U] = (uint8_t)(ctx->state[i] >> 8U);
        digest[i * 4U + 3U] = (uint8_t)ctx->state[i];
    }
}

static bool xx_xz_read_exact_at(xx_io_device *device, int64_t offset,
                                void *buffer, size_t size) {
    uint8_t *bytes = (uint8_t *)buffer;
    size_t done = 0U;
    if (!device || (!buffer && size != 0U) || offset < 0 || offset > LONG_MAX ||
        xx_io_seek(device, (long)offset, SEEK_SET) != 0) {
        return false;
    }
    while (done < size) {
        ssize_t got = xx_io_read(device, bytes + done, size - done);
        if (got <= 0) return false;
        done += (size_t)got;
    }
    return true;
}

static bool xx_xz_vli(const uint8_t *data, size_t limit, size_t *cursor,
                      uint64_t *value) {
    uint64_t result = 0U;
    size_t i;
    if (!data || !cursor || !value) return false;
    for (i = 0U; i < 9U; ++i) {
        uint8_t byte;
        if (*cursor >= limit) return false;
        byte = data[(*cursor)++];
        result |= (uint64_t)(byte & 0x7fU) << (i * 7U);
        if ((byte & 0x80U) == 0U) {
            if ((i != 0U && byte == 0U) || result > (uint64_t)INT64_MAX)
                return false;
            *value = result;
            return true;
        }
    }
    return false;
}

static size_t xx_xz_check_size(uint8_t check_type) {
    if (check_type == XX_XZ_CHECK_NONE) return 0U;
    if (check_type > 15U) return SIZE_MAX;
    return (size_t)4U << ((check_type - 1U) / 3U);
}

static bool xx_xz_lzma2_property_supported(uint8_t property) {
    uint64_t dictionary;
    if (property > 40U) return false;
    dictionary = property == 40U
                     ? UINT32_MAX
                     : ((uint64_t)2U | (property & 1U))
                           << (property / 2U + 11U);
    return dictionary <= XX_LZMA_MAX_DICT_SIZE;
}

static bool xx_xz_validate_stream_header(xx_io_device *device,
                                         int64_t offset,
                                         uint8_t *check_type) {
    uint8_t header[XX_XZ_STREAM_HEADER_SIZE];
    uint32_t expected;
    if (!device || !check_type ||
        !xx_xz_read_exact_at(device, offset, header, sizeof(header)) ||
        xx_rt_memcmp(header, XX_XZ_MAGIC, XX_XZ_MAGIC_SIZE) != 0 ||
        header[6] != 0U || (header[7] & 0xf0U) != 0U) {
        return false;
    }
    expected = xx_data_get_u32(header, sizeof(header), 8U, false);
    if (expected != xx_crc32_calc(0U, header + 6U, 2U)) return false;
    *check_type = header[7] & 0x0fU;
    return xx_xz_check_size(*check_type) != SIZE_MAX;
}

static bool xx_xz_parse_block_header(const uint8_t *header,
                                     size_t header_size,
                                     xx_xz_block *block,
                                     bool *has_compressed,
                                     uint64_t *declared_compressed,
                                     bool *has_uncompressed,
                                     uint64_t *declared_uncompressed) {
    size_t cursor = 2U;
    size_t limit;
    uint8_t count;
    uint64_t ids[XX_XZ_MAX_FILTERS];
    uint64_t prop_sizes[XX_XZ_MAX_FILTERS];
    const uint8_t *props[XX_XZ_MAX_FILTERS];
    size_t i;
    uint32_t expected_crc;
    if (!header || !block || !has_compressed || !declared_compressed ||
        !has_uncompressed || !declared_uncompressed || header_size < 8U ||
        header_size > 1024U || header[0] == 0U ||
        header_size != ((size_t)header[0] + 1U) * 4U) {
        return false;
    }
    limit = header_size - 4U;
    expected_crc = xx_data_get_u32(header, header_size, limit, false);
    if (expected_crc != xx_crc32_calc(0U, header, limit) ||
        (header[1] & 0x3cU) != 0U) {
        return false;
    }
    block->flags = header[1];
    count = (uint8_t)((header[1] & 3U) + 1U);
    block->filter_count = count;
    *has_compressed = (header[1] & 0x40U) != 0U;
    *has_uncompressed = (header[1] & 0x80U) != 0U;
    if (*has_compressed &&
        !xx_xz_vli(header, limit, &cursor, declared_compressed)) return false;
    if (*has_uncompressed &&
        !xx_xz_vli(header, limit, &cursor, declared_uncompressed)) return false;
    for (i = 0U; i < count; ++i) {
        if (!xx_xz_vli(header, limit, &cursor, &ids[i]) ||
            !xx_xz_vli(header, limit, &cursor, &prop_sizes[i]) ||
            prop_sizes[i] > XX_XZ_MAX_FILTER_PROPERTIES ||
            prop_sizes[i] > (uint64_t)(limit - cursor)) {
            return false;
        }
        props[i] = header + cursor;
        cursor += (size_t)prop_sizes[i];
    }
    while (cursor < limit) {
        if (header[cursor++] != 0U) return false;
    }
    block->extractable = false;
    block->delta_distance = 0U;
    if (ids[count - 1U] == XX_XZ_FILTER_LZMA2 &&
        prop_sizes[count - 1U] == 1U &&
        xx_xz_lzma2_property_supported(props[count - 1U][0])) {
        block->lzma2_property = props[count - 1U][0];
        if (count == 1U) {
            block->extractable = true;
        } else if (count == 2U && ids[0] == XX_XZ_FILTER_DELTA &&
                   prop_sizes[0] == 1U) {
            block->delta_distance = (uint16_t)props[0][0] + 1U;
            block->extractable = true;
        }
    }
    return true;
}

static void xx_xz_candidate_cleanup(xx_xz_candidate *candidate) {
    if (!candidate) return;
    if (candidate->blocks) xx_mem_free(candidate->blocks);
    xx_mem_zero(candidate, sizeof(*candidate));
}

static bool xx_xz_try_footer(Abstractformat *self, int64_t stream_start,
                             uint8_t check_type, int64_t footer_offset,
                             xx_xz_candidate *candidate,
                             uint64_t *index_work_remaining,
                             xx_pd_struct *pd) {
    uint8_t footer[XX_XZ_STREAM_FOOTER_SIZE];
    uint8_t index_indicator;
    uint8_t *index = NULL;
    uint64_t *unpadded = NULL;
    uint64_t *uncompressed = NULL;
    uint32_t backward;
    uint64_t index_size_u64;
    int64_t index_offset;
    size_t index_size;
    size_t cursor;
    uint64_t record_count_u64;
    size_t record_count;
    size_t expected_padding;
    size_t check_size = xx_xz_check_size(check_type);
    int64_t block_offset;
    bool ok = false;
    if (!self || !candidate || footer_offset < stream_start + 12 ||
        !xx_xz_read_exact_at(self->device, footer_offset, footer,
                             sizeof(footer)) ||
        footer[10] != 0x59U || footer[11] != 0x5aU || footer[8] != 0U ||
        footer[9] != check_type ||
        xx_data_get_u32(footer, sizeof(footer), 0U, false) !=
            xx_crc32_calc(0U, footer + 4U, 6U)) {
        return false;
    }
    backward = xx_data_get_u32(footer, sizeof(footer), 4U, false);
    index_size_u64 = ((uint64_t)backward + 1U) * 4U;
    if (index_size_u64 < 8U || index_size_u64 > XX_XZ_MAX_INDEX_SIZE ||
        index_size_u64 > (uint64_t)SIZE_MAX ||
        index_size_u64 > (uint64_t)(footer_offset - stream_start - 12)) {
        return false;
    }
    index_size = (size_t)index_size_u64;
    index_offset = footer_offset - (int64_t)index_size;
    /* Reject most forged Footer signatures before allocating or checksumming
       the candidate Index.  Charge the remaining candidates for every byte
       checksummed so overlapping fake Index ranges cannot make the scan
       quadratic in the input size. */
    if (!xx_xz_read_exact_at(self->device, index_offset, &index_indicator,
                             sizeof(index_indicator)) ||
        index_indicator != 0U || !index_work_remaining ||
        index_size_u64 > *index_work_remaining) {
        return false;
    }
    *index_work_remaining -= index_size_u64;
    index = (uint8_t *)xx_mem_alloc(index_size);
    if (!index || !xx_xz_read_exact_at(self->device, index_offset, index,
                                        index_size) || index[0] != 0U ||
        xx_data_get_u32(index, index_size, index_size - 4U, false) !=
            xx_crc32_calc(0U, index, index_size - 4U)) {
        goto cleanup;
    }
    cursor = 1U;
    if (!xx_xz_vli(index, index_size - 4U, &cursor, &record_count_u64) ||
        record_count_u64 > XX_XZ_MAX_BLOCKS ||
        record_count_u64 > (uint64_t)SIZE_MAX) {
        goto cleanup;
    }
    record_count = (size_t)record_count_u64;
    if (record_count != 0U) {
        if (record_count > SIZE_MAX / sizeof(uint64_t) ||
            record_count > SIZE_MAX / sizeof(xx_xz_block)) goto cleanup;
        unpadded = (uint64_t *)xx_mem_alloc(record_count * sizeof(uint64_t));
        uncompressed =
            (uint64_t *)xx_mem_alloc(record_count * sizeof(uint64_t));
        candidate->blocks = (xx_xz_block *)xx_mem_calloc(
            record_count, sizeof(xx_xz_block));
        if (!unpadded || !uncompressed || !candidate->blocks) goto cleanup;
    }
    for (size_t i = 0U; i < record_count; ++i) {
        if (!xx_xz_vli(index, index_size - 4U, &cursor, &unpadded[i]) ||
            !xx_xz_vli(index, index_size - 4U, &cursor, &uncompressed[i]) ||
            unpadded[i] == 0U) goto cleanup;
    }
    expected_padding = (4U - (cursor & 3U)) & 3U;
    if (cursor + expected_padding != index_size - 4U) goto cleanup;
    while (cursor < index_size - 4U) {
        if (index[cursor++] != 0U) goto cleanup;
    }

    block_offset = stream_start + XX_XZ_STREAM_HEADER_SIZE;
    for (size_t i = 0U; i < record_count; ++i) {
        uint8_t first;
        uint8_t *header = NULL;
        size_t header_size;
        bool has_compressed = false;
        bool has_uncompressed = false;
        uint64_t declared_compressed = 0U;
        uint64_t declared_uncompressed = 0U;
        uint64_t compressed;
        uint64_t aligned;
        xx_xz_block *block = &candidate->blocks[i];
        if (pd && xx_pd_is_stopped(pd)) goto cleanup;
        if (!xx_xz_read_exact_at(self->device, block_offset, &first, 1U) ||
            first == 0U) goto cleanup;
        header_size = ((size_t)first + 1U) * 4U;
        if (block_offset > index_offset - (int64_t)header_size) goto cleanup;
        header = (uint8_t *)xx_mem_alloc(header_size);
        if (!header || !xx_xz_read_exact_at(self->device, block_offset,
                                             header, header_size) ||
            !xx_xz_parse_block_header(header, header_size, block,
                                      &has_compressed,
                                      &declared_compressed,
                                      &has_uncompressed,
                                      &declared_uncompressed)) {
            if (header) xx_mem_free(header);
            goto cleanup;
        }
        xx_mem_free(header);
        if (unpadded[i] < (uint64_t)header_size + check_size) goto cleanup;
        compressed = unpadded[i] - (uint64_t)header_size - check_size;
        if (compressed == 0U || compressed > (uint64_t)INT64_MAX ||
            (has_compressed && declared_compressed != compressed) ||
            (has_uncompressed && declared_uncompressed != uncompressed[i])) {
            goto cleanup;
        }
        if (unpadded[i] > UINT64_MAX - 3U) goto cleanup;
        aligned = (unpadded[i] + 3U) & ~UINT64_C(3);
        if (aligned > (uint64_t)INT64_MAX ||
            block_offset > index_offset - (int64_t)aligned) goto cleanup;
        block->header_offset = block_offset;
        block->header_size = (int64_t)header_size;
        block->data_offset = block_offset + (int64_t)header_size;
        block->compressed_size = (int64_t)compressed;
        block->padding_offset = block->data_offset + block->compressed_size;
        block->padding_size = (int64_t)(aligned - unpadded[i]);
        block->check_offset = block->padding_offset + block->padding_size;
        block->check_size = (int64_t)check_size;
        block->uncompressed_size = uncompressed[i];
        block->unpadded_size = unpadded[i];
        if (block->padding_size != 0) {
            uint8_t padding[3];
            if (!xx_xz_read_exact_at(self->device, block->padding_offset,
                                     padding, (size_t)block->padding_size))
                goto cleanup;
            for (int64_t p = 0; p < block->padding_size; ++p)
                if (padding[p] != 0U) goto cleanup;
        }
        block_offset += (int64_t)aligned;
    }
    if (block_offset != index_offset) goto cleanup;

    candidate->stream.header_offset = stream_start;
    candidate->stream.index_offset = index_offset;
    candidate->stream.index_size = (int64_t)index_size;
    candidate->stream.footer_offset = footer_offset;
    candidate->stream.stream_end = footer_offset + XX_XZ_STREAM_FOOTER_SIZE;
    candidate->stream.padding_offset = candidate->stream.stream_end;
    candidate->stream.padding_size = 0;
    candidate->stream.first_block = 0U;
    candidate->stream.block_count = record_count;
    candidate->stream.check_type = check_type;
    candidate->block_count = record_count;
    ok = true;

cleanup:
    if (index) xx_mem_free(index);
    if (unpadded) xx_mem_free(unpadded);
    if (uncompressed) xx_mem_free(uncompressed);
    if (!ok) xx_xz_candidate_cleanup(candidate);
    return ok;
}

static bool xx_xz_find_stream(Abstractformat *self, int64_t stream_start,
                              int64_t total_size, xx_xz_candidate *candidate,
                              xx_pd_struct *pd) {
    uint8_t check_type;
    uint8_t buffer[65536];
    int64_t offset;
    uint64_t index_work_remaining;
    uint8_t previous = 0U;
    bool have_previous = false;
    if (!self || !candidate || stream_start < 0 || total_size < stream_start ||
        total_size - stream_start < 32 ||
        !xx_xz_validate_stream_header(self->device, stream_start,
                                      &check_type)) {
        return false;
    }
    index_work_remaining = 0U;
    offset = stream_start + XX_XZ_STREAM_HEADER_SIZE;
    while (offset < total_size) {
        size_t amount = (size_t)(total_size - offset);
        uint64_t work_credit;
        if (amount > sizeof(buffer)) amount = sizeof(buffer);
        if (!xx_xz_read_exact_at(self->device, offset, buffer, amount))
            return false;
        /* Each scanned byte supplies a fixed amount of validation credit.
           This token bucket permits a real Index as large as the preceding
           stream data while bounding all overlapping fake Index CRC work by
           a constant multiple of the bytes scanned. */
        work_credit = (uint64_t)amount *
                      XX_XZ_INDEX_VALIDATION_WORK_FACTOR;
        if (UINT64_MAX - index_work_remaining < work_credit)
            index_work_remaining = UINT64_MAX;
        else
            index_work_remaining += work_credit;
        for (size_t i = 0U; i < amount; ++i) {
            int64_t absolute = offset + (int64_t)i;
            if (have_previous && previous == 0x59U && buffer[i] == 0x5aU &&
                absolute >= stream_start + 11) {
                int64_t footer_offset = absolute - 11;
                if (footer_offset >= stream_start + 20 &&
                    ((footer_offset - stream_start) & 3) == 0) {
                    xx_mem_zero(candidate, sizeof(*candidate));
                    if (xx_xz_try_footer(self, stream_start, check_type,
                                         footer_offset, candidate,
                                         &index_work_remaining, pd)) {
                        return true;
                    }
                }
            }
            previous = buffer[i];
            have_previous = true;
        }
        offset += (int64_t)amount;
        if (pd && xx_pd_is_stopped(pd)) return false;
    }
    return false;
}

static void xx_xz_private_free(xx_xz_private *priv) {
    if (!priv) return;
    if (priv->streams) xx_mem_free(priv->streams);
    if (priv->blocks) xx_mem_free(priv->blocks);
    xx_mem_free(priv);
}

static bool xx_xz_append_candidate(xx_xz_private *priv,
                                   xx_xz_candidate *candidate) {
    xx_xz_stream *streams;
    xx_xz_block *blocks = priv ? priv->blocks : NULL;
    size_t new_block_count;
    if (!priv || !candidate || priv->stream_count >= XX_XZ_MAX_STREAMS ||
        candidate->block_count > XX_XZ_MAX_BLOCKS - priv->block_count ||
        priv->stream_count + 1U > SIZE_MAX / sizeof(xx_xz_stream)) {
        return false;
    }
    new_block_count = priv->block_count + candidate->block_count;
    if (new_block_count != 0U) {
        if (new_block_count > SIZE_MAX / sizeof(xx_xz_block)) return false;
        blocks = (xx_xz_block *)xx_mem_realloc(
            priv->blocks, new_block_count * sizeof(xx_xz_block));
        if (!blocks) return false;
        priv->blocks = blocks;
        if (candidate->block_count != 0U) {
            xx_rt_memcpy(priv->blocks + priv->block_count, candidate->blocks,
                   candidate->block_count * sizeof(xx_xz_block));
        }
    }
    streams = (xx_xz_stream *)xx_mem_realloc(
        priv->streams, (priv->stream_count + 1U) * sizeof(xx_xz_stream));
    if (!streams) return false;
    priv->streams = streams;
    candidate->stream.first_block = priv->block_count;
    priv->streams[priv->stream_count++] = candidate->stream;
    priv->block_count = new_block_count;
    if (candidate->blocks) {
        xx_mem_free(candidate->blocks);
        candidate->blocks = NULL;
    }
    return true;
}

static bool xx_xz_is_magic_at(xx_io_device *device, int64_t total_size,
                              int64_t offset) {
    uint8_t magic[XX_XZ_MAGIC_SIZE];
    return offset >= 0 && total_size - offset >= XX_XZ_MAGIC_SIZE &&
           xx_xz_read_exact_at(device, offset, magic, sizeof(magic)) &&
           xx_rt_memcmp(magic, XX_XZ_MAGIC, sizeof(magic)) == 0;
}

static xx_xz_private *xx_xz_parse(Abstractformat *self, xx_pd_struct *pd) {
    xx_xz_private *priv;
    int64_t total_size;
    int64_t stream_start;
    if (!self || !self->device || self->base_address < 0) return NULL;
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address ||
        total_size - self->base_address < 32) return NULL;
    priv = (xx_xz_private *)xx_mem_calloc(1U, sizeof(*priv));
    if (!priv) return NULL;
    priv->can_extract = true;
    stream_start = self->base_address;
    for (;;) {
        xx_xz_candidate candidate;
        int64_t cursor;
        xx_mem_zero(&candidate, sizeof(candidate));
        if (!xx_xz_find_stream(self, stream_start, total_size, &candidate,
                               pd) ||
            !xx_xz_append_candidate(priv, &candidate)) {
            xx_xz_candidate_cleanup(&candidate);
            xx_xz_private_free(priv);
            return NULL;
        }
        if (priv->streams[priv->stream_count - 1U].check_type !=
                XX_XZ_CHECK_NONE &&
            priv->streams[priv->stream_count - 1U].check_type !=
                XX_XZ_CHECK_CRC32 &&
            priv->streams[priv->stream_count - 1U].check_type !=
                XX_XZ_CHECK_CRC64 &&
            priv->streams[priv->stream_count - 1U].check_type !=
                XX_XZ_CHECK_SHA256) {
            priv->can_extract = false;
        }
        for (size_t i = priv->streams[priv->stream_count - 1U].first_block;
             i < priv->block_count; ++i) {
            if (UINT64_MAX - priv->uncompressed_size <
                    priv->blocks[i].uncompressed_size ||
                UINT64_MAX - priv->compressed_data_size <
                    (uint64_t)priv->blocks[i].compressed_size) {
                xx_xz_private_free(priv);
                return NULL;
            }
            priv->uncompressed_size += priv->blocks[i].uncompressed_size;
            priv->compressed_data_size +=
                (uint64_t)priv->blocks[i].compressed_size;
            if (!priv->blocks[i].extractable) priv->can_extract = false;
        }
        cursor = priv->streams[priv->stream_count - 1U].stream_end;
        while (total_size - cursor >= 4) {
            uint8_t padding[4];
            if (!xx_xz_read_exact_at(self->device, cursor, padding,
                                     sizeof(padding))) {
                xx_xz_private_free(priv);
                return NULL;
            }
            if (padding[0] != 0U || padding[1] != 0U ||
                padding[2] != 0U || padding[3] != 0U) break;
            cursor += 4;
        }
        priv->streams[priv->stream_count - 1U].padding_size =
            cursor - priv->streams[priv->stream_count - 1U].stream_end;
        priv->format_end = cursor;
        if (!xx_xz_is_magic_at(self->device, total_size, cursor)) break;
        stream_start = cursor;
    }
    return priv;
}

static ssize_t xx_xz_sink_write(xx_io_device *device, const void *data,
                                size_t size) {
    xx_xz_sink *sink = device ? (xx_xz_sink *)device->priv : NULL;
    const uint8_t *source = (const uint8_t *)data;
    uint8_t transformed[4096];
    size_t original_size = size;
    if (!sink || (!data && size != 0U) || sink->written > sink->expected ||
        (uint64_t)size > sink->expected - sink->written) {
        if (sink) sink->failed = true;
        return -1;
    }
    while (size != 0U) {
        size_t amount = size > sizeof(transformed) ? sizeof(transformed) : size;
        const uint8_t *output = source;
        if (sink->delta_distance != 0U) {
            for (size_t i = 0U; i < amount; ++i) {
                size_t prior = (size_t)((sink->delta_position -
                    sink->delta_distance) & 0xffU);
                uint8_t value = (uint8_t)(source[i] +
                                           sink->delta_history[prior]);
                transformed[i] = value;
                sink->delta_history[sink->delta_position & 0xffU] = value;
                ++sink->delta_position;
            }
            output = transformed;
        }
        if (sink->target &&
            xx_io_write(sink->target, output, amount) != (ssize_t)amount) {
            sink->failed = true;
            return -1;
        }
        sink->crc32 = xx_crc32_calc(sink->crc32, output, amount);
        sink->crc64 = xx_crc64_xz_calc(sink->crc64, output, amount);
        xx_xz_sha256_update(&sink->sha256, output, amount);
        sink->written += amount;
        source += amount;
        size -= amount;
    }
    return (ssize_t)original_size;
}

static int64_t xx_xz_sink_size(xx_io_device *device) {
    xx_xz_sink *sink = device ? (xx_xz_sink *)device->priv : NULL;
    return sink && sink->written <= (uint64_t)INT64_MAX
               ? (int64_t)sink->written : -1;
}

static void xx_xz_sink_init(xx_xz_sink *sink, xx_io_device *target,
                            const xx_xz_block *block) {
    xx_mem_zero(sink, sizeof(*sink));
    sink->target = target;
    sink->expected = block->uncompressed_size;
    sink->delta_distance = block->delta_distance;
    sink->device.write = xx_xz_sink_write;
    sink->device.total_size = xx_xz_sink_size;
    sink->device.get_total_size = xx_xz_sink_size;
    sink->device.size = xx_xz_sink_size;
    sink->device.priv = sink;
    xx_xz_sha256_init(&sink->sha256);
}

static bool xx_xz_verify_block(Abstractformat *self,
                               const xx_xz_stream *stream,
                               const xx_xz_block *block,
                               xx_io_device *target, xx_pd_struct *pd) {
    xx_xz_sink sink;
    uint8_t expected[32];
    uint8_t digest[32];
    bool valid = false;
    if (!self || !stream || !block || !block->extractable) return false;
    xx_xz_sink_init(&sink, target, block);
    if (!xx_lzma2_unpack_device(self->device, block->data_offset,
                                block->compressed_size,
                                block->lzma2_property,
                                &sink.device, pd) || sink.failed ||
        sink.written != block->uncompressed_size ||
        !xx_xz_read_exact_at(self->device, block->check_offset, expected,
                             (size_t)block->check_size)) {
        return false;
    }
    switch (stream->check_type) {
        case XX_XZ_CHECK_NONE:
            valid = true;
            break;
        case XX_XZ_CHECK_CRC32:
            valid = xx_data_get_u32(expected, sizeof(expected), 0U, false) ==
                    sink.crc32;
            break;
        case XX_XZ_CHECK_CRC64:
            valid = xx_data_get_u64(expected, sizeof(expected), 0U, false) ==
                    sink.crc64;
            break;
        case XX_XZ_CHECK_SHA256:
            xx_xz_sha256_final(&sink.sha256, digest);
            valid = xx_rt_memcmp(expected, digest, sizeof(digest)) == 0;
            break;
        default:
            valid = false;
            break;
    }
    return valid;
}

static bool xx_xz_extract_all(Abstractformat *self, xx_io_device *target,
                              xx_pd_struct *pd) {
    xx_xz_private *priv;
    if (!self || !(priv = (xx_xz_private *)((xx_xz *)self)->internal) ||
        !priv->can_extract) return false;
    for (size_t s = 0U; s < priv->stream_count; ++s) {
        const xx_xz_stream *stream = &priv->streams[s];
        for (size_t b = 0U; b < stream->block_count; ++b) {
            const xx_xz_block *block =
                &priv->blocks[stream->first_block + b];
            if (!xx_xz_verify_block(self, stream, block, target, pd))
                return false;
        }
    }
    return true;
}

static bool xx_xz_copy_options(xx_list_s *destination,
                               const xx_list_s *source) {
    if (!destination || !source) return source == NULL;
    for (size_t i = 0U; i < source->count; ++i) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)source, i);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static const xx_var *xx_xz_find_option(const xx_list_s *options,
                                       uint32_t meta_id) {
    if (!options) return NULL;
    for (size_t i = 0U; i < options->count; ++i) {
        const xx_meta *item =
            (const xx_meta *)xx_list_at((const xx_list_t *)options, i);
        if (item && item->meta_id == meta_id) return &item->var;
    }
    return NULL;
}

static bool xx_xz_populate_record(xx_archive_record *record,
                                  const xx_xz_private *priv) {
    int64_t data_offset;
    if (!record || !priv || priv->compressed_data_size > (uint64_t)INT64_MAX ||
        priv->stream_count == 0U) return false;
    data_offset = priv->block_count != 0U
                      ? priv->blocks[0].data_offset
                      : priv->streams[0].index_offset;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = priv->streams[0].header_offset;
    record->header_size = XX_XZ_STREAM_HEADER_SIZE;
    record->data_offset = data_offset;
    record->compressed_size = (int64_t)priv->compressed_data_size;
    return xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME,
                                          XX_XZ_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record,
               XX_META_ID_UNCOMPRESSED_SIZE, priv->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record,
               XX_META_ID_COMPRESSED_SIZE, priv->compressed_data_size) &&
           xx_archive_record_set_meta_u64(record,
               XX_META_ID_COMPRESSION_METHOD, XX_XZ_COMPRESSION_METHOD) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                           false) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED,
                                           false);
}

void xx_xz_init(xx_xz *xz, xx_io_device *dev, int64_t base_address) {
    if (!xz) return;
    xx_mem_zero(xz, sizeof(*xz));
    xx_format_init(&xz->format, dev, base_address);
    xz->format.endian = XX_ENDIAN_LITTLE;
    xz->format.file_type = XX_FILE_TYPE_XZ;
    xz->format.format_type = XX_TYPE_ARCHIVE;
    xz->format.is_archive = true;
    xx_format_set_mime_type(&xz->format, "application/x-xz");
    xx_format_set_extension(&xz->format, "xz");
    xz->format.check_is_valid = xx_xz_check_is_valid;
    xz->format.handle_base_info = xx_xz_handle_base_info;
    xz->format.get_format_size = xx_xz_get_format_size;
    xz->format.get_number_of_archive_records =
        xx_xz_get_number_of_archive_records;
    xz->format.create_archive_records_reading =
        xx_xz_create_archive_records_reading;
    xz->format.get_current_archive_record = xx_xz_get_current_archive_record;
    xz->format.unpack_current_archive_record =
        xx_xz_unpack_current_archive_record;
    xz->format.archive_record_move_to_next =
        xx_xz_archive_record_move_to_next;
    xz->format.free_archive_records_reading =
        xx_xz_free_archive_records_reading;
    xz->format.data_struct_id_to_string = xx_xz_data_struct_id_to_string;
    xz->format.data_struct_string_to_id = xx_xz_data_struct_string_to_id;
    xz->format.create_data_structs_reading =
        xx_xz_create_data_structs_reading;
    xz->format.get_current_data_struct = xx_xz_get_current_data_struct;
    xz->format.data_struct_move_to_next = xx_xz_data_struct_move_to_next;
    xz->format.free_data_structs_reading =
        xx_xz_free_data_structs_reading;
    xz->format.create_data_struct_records_reading =
        xx_xz_create_data_struct_records_reading;
    xz->format.get_current_data_struct_record =
        xx_xz_get_current_data_struct_record;
    xz->format.data_struct_record_move_to_next =
        xx_xz_data_struct_record_move_to_next;
    xz->format.free_data_struct_records_reading =
        xx_xz_free_data_struct_records_reading;
    xz->format.destroy = xx_xz_vtable_destroy;
    xz->index_offset = -1;
    xz->footer_offset = -1;
}

xx_xz *xx_xz_create(xx_io_device *dev, int64_t base_address) {
    xx_xz *xz = (xx_xz *)xx_mem_alloc(sizeof(*xz));
    if (xz) xx_xz_init(xz, dev, base_address);
    return xz;
}

void xx_xz_destroy(xx_xz *xz) {
    if (!xz) return;
    if (xz->internal) {
        xx_xz_private_free((xx_xz_private *)xz->internal);
        xz->internal = NULL;
    }
    if (xz->format.close) xz->format.close(&xz->format);
    xx_format_cleanup_extra_parameters(&xz->format);
}

static void xx_xz_vtable_destroy(Abstractformat *self) {
    xx_xz_destroy((xx_xz *)self);
}

void xx_xz_free(xx_xz *xz) {
    if (!xz) return;
    xx_xz_destroy(xz);
    xx_mem_free(xz);
}

bool xx_xz_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    uint8_t check_type;
    int64_t total_size;
    (void)pd;
    if (!self || !self->device || self->base_address < 0) return false;
    total_size = xx_io_total_size(self->device);
    return total_size >= self->base_address &&
           total_size - self->base_address >= 32 &&
           xx_xz_validate_stream_header(self->device, self->base_address,
                                        &check_type);
}

bool xx_xz_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_xz_private *priv;
    xx_xz *xz;
    int64_t total_size;
    if (!self || !self->device || !(priv = xx_xz_parse(self, pd)) ||
        priv->compressed_data_size > (uint64_t)INT64_MAX) {
        if (self) self->is_valid = false;
        return false;
    }
    xz = (xx_xz *)self;
    if (xz->internal) xx_xz_private_free((xx_xz_private *)xz->internal);
    xz->internal = priv;
    xz->number_of_blocks = (uint64_t)priv->block_count;
    xz->uncompressed_size = priv->uncompressed_size;
    xz->compressed_data_size = priv->compressed_data_size;
    xz->index_offset = priv->streams[0].index_offset;
    xz->footer_offset = priv->streams[0].footer_offset;
    xz->check_type = priv->streams[0].check_type;
    for (size_t i = 1U; i < priv->stream_count; ++i) {
        if (priv->streams[i].check_type != xz->check_type) {
            xz->check_type = 0xffU;
            break;
        }
    }
    xz->can_extract = priv->can_extract;
    self->format_size = priv->format_end - self->base_address;
    total_size = xx_io_total_size(self->device);
    if (total_size > priv->format_end) {
        self->overlay_offset = priv->format_end;
        self->overlay_size = total_size - priv->format_end;
    } else {
        self->overlay_offset = -1;
        self->overlay_size = 0;
    }
    self->number_of_archive_records = 1U;
    self->file_type = XX_FILE_TYPE_XZ;
    self->format_type = XX_TYPE_ARCHIVE;
    self->is_archive = true;
    self->is_executable = false;
    self->is_crypted = false;
    self->is_valid = true;
    self->base_info_handled = true;
    return true;
}

int64_t xx_xz_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_xz_get_number_of_archive_records(Abstractformat *self,
                                              xx_pd_struct *pd) {
    if (!self || (!self->base_info_handled &&
                  !xx_format_handle_base_info(self, pd))) return 0U;
    return 1U;
}

bool xx_xz_unpack_to_device(xx_xz *xz, xx_io_device *destination,
                            xx_pd_struct *pd) {
    if (!xz || !destination ||
        (!xz->format.base_info_handled &&
         !xx_format_handle_base_info(&xz->format, pd)) ||
        !xz->format.is_valid || !xz->can_extract) {
        return false;
    }
    return xx_xz_extract_all(&xz->format, destination, pd);
}

xx_archive_record_state *xx_xz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    xx_archive_record_state *state;
    xx_xz_private *priv;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !self->is_valid ||
        !(priv = (xx_xz_private *)((xx_xz *)self)->internal)) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!xx_xz_copy_options(&state->options, options) ||
        !xx_xz_populate_record(&state->current_record, priv)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_xz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_xz_unpack_current_archive_record(Abstractformat *self,
                                         xx_archive_record_state *state,
                                         xx_pd_struct *pd) {
    const xx_var *path_value;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    xx_io_device *target = NULL;
    bool result;
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    path_value = xx_xz_find_option(&state->options,
                                   XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) return xx_xz_extract_all(self, NULL, pd);
    if (path_value->type == XX_VAR_TYPE_STRING ||
        path_value->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_value);
    } else if (path_value->type == XX_VAR_TYPE_WSTRING ||
               path_value->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_value));
        base = owned_base;
    }
    if (!base) goto cleanup;
    if (base[0] != '\0' && base[xx_str_len(base) - 1U] != '/' &&
        base[xx_str_len(base) - 1U] != '\\') {
        destination = xx_str_concat3(base, "/", XX_XZ_PAYLOAD_NAME);
    } else {
        destination = xx_str_concat(base, XX_XZ_PAYLOAD_NAME);
    }
    if (!destination || !xx_io_create_dirs_a(destination, false) ||
        !(target = xx_io_file_open(destination, "wb"))) goto cleanup;
    result = xx_xz_extract_all(self, target, pd);
    xx_io_close(target);
    target = NULL;
    if (!result) xx_rt_remove(destination);
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;

cleanup:
    if (target) xx_io_close(target);
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return false;
}

bool xx_xz_archive_record_move_to_next(Abstractformat *self,
                                       xx_archive_record_state *state,
                                       xx_pd_struct *pd) {
    if (!self || !state || state->format != self || !state->has_record ||
        (pd && xx_pd_is_stopped(pd))) return false;
    state->has_record = false;
    return false;
}

void xx_xz_free_archive_records_reading(Abstractformat *self,
                                        xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}

typedef struct xx_xz_ds_name_s {
    xx_xz_data_struct_id_t id;
    const char *name;
} xx_xz_ds_name;

static const xx_xz_ds_name g_xx_xz_ds_names[] = {
    {XX_XZ_DS_UNKNOWN, "UNKNOWN"},
    {XX_XZ_DS_STREAM_HEADER, "STREAM_HEADER"},
    {XX_XZ_DS_BLOCK_HEADER, "BLOCK_HEADER"},
    {XX_XZ_DS_BLOCK_DATA, "BLOCK_DATA"},
    {XX_XZ_DS_BLOCK_PADDING, "BLOCK_PADDING"},
    {XX_XZ_DS_BLOCK_CHECK, "BLOCK_CHECK"},
    {XX_XZ_DS_INDEX, "INDEX"},
    {XX_XZ_DS_STREAM_FOOTER, "STREAM_FOOTER"},
    {XX_XZ_DS_STREAM_PADDING, "STREAM_PADDING"}
};

const char *xx_xz_data_struct_id_to_string(Abstractformat *self,
                                            uint32_t id) {
    (void)self;
    for (size_t i = 0U;
         i < sizeof(g_xx_xz_ds_names) / sizeof(g_xx_xz_ds_names[0]); ++i) {
        if ((uint32_t)g_xx_xz_ds_names[i].id == id)
            return g_xx_xz_ds_names[i].name;
    }
    return "UNKNOWN";
}

uint32_t xx_xz_data_struct_string_to_id(Abstractformat *self,
                                        const char *name) {
    (void)self;
    if (name) {
        for (size_t i = 0U;
             i < sizeof(g_xx_xz_ds_names) / sizeof(g_xx_xz_ds_names[0]);
             ++i) {
            if (xx_str_cmp(name, g_xx_xz_ds_names[i].name) == 0)
                return (uint32_t)g_xx_xz_ds_names[i].id;
        }
    }
    return (uint32_t)XX_XZ_DS_UNKNOWN;
}

static void xx_xz_ds_stream_free(void *pointer) {
    xx_xz_ds_stream *stream = (xx_xz_ds_stream *)pointer;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static void xx_xz_set_ds(xx_data_struct *item, uint32_t id, int64_t offset,
                         int64_t size, xx_data_struct_type_t type,
                         bool is_mapped) {
    item->id = id;
    item->offset = offset;
    item->address = is_mapped ? offset : -1;
    item->entry_size = size;
    item->total_size = size;
    item->count = 1U;
    item->type = type;
}

xx_data_struct_state *xx_xz_create_data_structs_reading(
    Abstractformat *self, xx_pd_struct *pd) {
    xx_xz_private *priv;
    xx_data_struct_state *state;
    xx_xz_ds_stream *stream;
    size_t capacity;
    if (!self || !self->device ||
        (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) ||
        !(priv = (xx_xz_private *)((xx_xz *)self)->internal) ||
        priv->stream_count > (SIZE_MAX - priv->block_count * 4U) / 4U) {
        return NULL;
    }
    capacity = priv->stream_count * 4U + priv->block_count * 4U;
    if (capacity > SIZE_MAX / sizeof(xx_data_struct)) return NULL;
    state = (xx_data_struct_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_xz_ds_stream *)xx_mem_calloc(1U, sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_data_struct_state_init(state, self);
    stream->items = (xx_data_struct *)xx_mem_alloc(
        (capacity ? capacity : 1U) * sizeof(*stream->items));
    if (!stream->items) {
        xx_mem_free(stream);
        xx_data_struct_state_free(state);
        return NULL;
    }
    for (size_t s = 0U; s < priv->stream_count; ++s) {
        const xx_xz_stream *xz_stream = &priv->streams[s];
        xx_xz_set_ds(&stream->items[stream->count++],
                     XX_XZ_DS_STREAM_HEADER, xz_stream->header_offset,
                     XX_XZ_STREAM_HEADER_SIZE, XX_DATA_STRUCT_TYPE_STRUCT,
                     self->is_mapped);
        for (size_t b = 0U; b < xz_stream->block_count; ++b) {
            const xx_xz_block *block =
                &priv->blocks[xz_stream->first_block + b];
            xx_xz_set_ds(&stream->items[stream->count++],
                         XX_XZ_DS_BLOCK_HEADER, block->header_offset,
                         block->header_size, XX_DATA_STRUCT_TYPE_STRUCT,
                         self->is_mapped);
            if (block->compressed_size != 0) {
                xx_xz_set_ds(&stream->items[stream->count++],
                             XX_XZ_DS_BLOCK_DATA, block->data_offset,
                             block->compressed_size,
                             XX_DATA_STRUCT_TYPE_RAW_DATA,
                             self->is_mapped);
            }
            if (block->padding_size != 0) {
                xx_xz_set_ds(&stream->items[stream->count++],
                             XX_XZ_DS_BLOCK_PADDING,
                             block->padding_offset, block->padding_size,
                             XX_DATA_STRUCT_TYPE_RAW_DATA,
                             self->is_mapped);
            }
            if (block->check_size != 0) {
                xx_xz_set_ds(&stream->items[stream->count++],
                             XX_XZ_DS_BLOCK_CHECK, block->check_offset,
                             block->check_size, XX_DATA_STRUCT_TYPE_STRUCT,
                             self->is_mapped);
            }
        }
        xx_xz_set_ds(&stream->items[stream->count++], XX_XZ_DS_INDEX,
                     xz_stream->index_offset, xz_stream->index_size,
                     XX_DATA_STRUCT_TYPE_STRUCT, self->is_mapped);
        xx_xz_set_ds(&stream->items[stream->count++],
                     XX_XZ_DS_STREAM_FOOTER, xz_stream->footer_offset,
                     XX_XZ_STREAM_FOOTER_SIZE, XX_DATA_STRUCT_TYPE_FOOTER,
                     self->is_mapped);
        if (xz_stream->padding_size != 0) {
            xx_xz_set_ds(&stream->items[stream->count++],
                         XX_XZ_DS_STREAM_PADDING,
                         xz_stream->padding_offset, xz_stream->padding_size,
                         XX_DATA_STRUCT_TYPE_RAW_DATA, self->is_mapped);
        }
    }
    state->internal_state = stream;
    state->free_internal = xx_xz_ds_stream_free;
    state->total_structs = (int64_t)stream->count;
    state->current_index = 0;
    state->has_struct = stream->count != 0U;
    if (state->has_struct) state->current_struct = stream->items[0];
    return state;
}

const xx_data_struct *xx_xz_get_current_data_struct(
    Abstractformat *self, xx_data_struct_state *state) {
    return self && state && state->format == self && state->has_struct
               ? &state->current_struct : NULL;
}

bool xx_xz_data_struct_move_to_next(Abstractformat *self,
                                    xx_data_struct_state *state,
                                    xx_pd_struct *pd) {
    xx_xz_ds_stream *stream;
    int64_t next;
    if (!self || !state || state->format != self || !state->has_struct ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_xz_ds_stream *)state->internal_state;
    next = state->current_index + 1;
    if (next < 0 || (size_t)next >= stream->count) {
        state->has_struct = false;
        return false;
    }
    state->current_index = next;
    state->current_struct = stream->items[next];
    return true;
}

void xx_xz_free_data_structs_reading(Abstractformat *self,
                                     xx_data_struct_state *state) {
    (void)self;
    xx_data_struct_state_free(state);
}

static const xx_data_struct_field_desc g_xz_stream_header_fields[] = {
    {L"magic", L"uint8[6]", 0, 6, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
    {L"stream_flags", L"uint16", 6, 2,
     XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS},
    {L"header_crc32", L"uint32", 8, 4,
     XX_DATA_STRUCT_RECORD_PROPERTY_NONE}
};

static const xx_data_struct_field_desc g_xz_block_header_fields[] = {
    {L"header_size", L"uint8", 0, 1,
     XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"block_flags", L"uint8", 1, 1,
     XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS}
};

static const xx_data_struct_field_desc g_xz_index_fields[] = {
    {L"indicator", L"uint8", 0, 1,
     XX_DATA_STRUCT_RECORD_PROPERTY_ID}
};

static const xx_data_struct_field_desc g_xz_footer_fields[] = {
    {L"footer_crc32", L"uint32", 0, 4,
     XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
    {L"backward_size", L"uint32", 4, 4,
     XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
    {L"stream_flags", L"uint16", 8, 2,
     XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS},
    {L"magic", L"uint16", 10, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID}
};

static void xx_xz_record_stream_free(void *pointer) {
    if (pointer) xx_mem_free(pointer);
}

xx_data_struct_record_state *xx_xz_create_data_struct_records_reading(
    Abstractformat *self, const xx_data_struct *ds, xx_pd_struct *pd) {
    xx_data_struct_record_state *state;
    xx_xz_record_stream *stream;
    const xx_data_struct_field_desc *fields;
    size_t count;
    (void)pd;
    if (!self || !self->device || !ds) return NULL;
    if (ds->id == XX_XZ_DS_STREAM_HEADER) {
        fields = g_xz_stream_header_fields;
        count = sizeof(g_xz_stream_header_fields) /
                sizeof(g_xz_stream_header_fields[0]);
    } else if (ds->id == XX_XZ_DS_BLOCK_HEADER) {
        fields = g_xz_block_header_fields;
        count = sizeof(g_xz_block_header_fields) /
                sizeof(g_xz_block_header_fields[0]);
    } else if (ds->id == XX_XZ_DS_INDEX) {
        fields = g_xz_index_fields;
        count = sizeof(g_xz_index_fields) / sizeof(g_xz_index_fields[0]);
    } else if (ds->id == XX_XZ_DS_STREAM_FOOTER) {
        fields = g_xz_footer_fields;
        count = sizeof(g_xz_footer_fields) / sizeof(g_xz_footer_fields[0]);
    } else {
        return NULL;
    }
    state = (xx_data_struct_record_state *)xx_mem_alloc(sizeof(*state));
    stream = (xx_xz_record_stream *)xx_mem_alloc(sizeof(*stream));
    if (!state || !stream) {
        if (state) xx_mem_free(state);
        if (stream) xx_mem_free(stream);
        return NULL;
    }
    xx_data_struct_record_state_init(state, self, ds);
    stream->fields = fields;
    stream->count = count;
    state->internal_state = stream;
    state->free_internal = xx_xz_record_stream_free;
    state->total_records = (int64_t)count;
    if (count != 0U && xx_data_struct_record_populate(
            &state->current_record, self->device, ds->offset,
            &fields[0], false)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_data_struct_record *xx_xz_get_current_data_struct_record(
    Abstractformat *self, xx_data_struct_record_state *state) {
    return self && state && state->format == self && state->has_record
               ? &state->current_record : NULL;
}

bool xx_xz_data_struct_record_move_to_next(
    Abstractformat *self, xx_data_struct_record_state *state,
    xx_pd_struct *pd) {
    xx_xz_record_stream *stream;
    int64_t next;
    if (!self || !state || state->format != self || !state->has_record ||
        !state->internal_state || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_xz_record_stream *)state->internal_state;
    next = state->current_index + 1;
    if (next < 0 || (size_t)next >= stream->count) {
        state->has_record = false;
        return false;
    }
    xx_data_struct_record_cleanup(&state->current_record);
    if (!xx_data_struct_record_populate(
            &state->current_record, self->device,
            state->parent_struct.offset, &stream->fields[next], false)) {
        state->has_record = false;
        return false;
    }
    state->current_index = next;
    return true;
}

void xx_xz_free_data_struct_records_reading(
    Abstractformat *self, xx_data_struct_record_state *state) {
    (void)self;
    xx_data_struct_record_state_free(state);
}

uint64_t xx_xz_get_number_of_blocks(const xx_xz *xz) {
    return xz ? xz->number_of_blocks : 0U;
}

uint64_t xx_xz_get_uncompressed_size(const xx_xz *xz) {
    return xz ? xz->uncompressed_size : 0U;
}

uint8_t xx_xz_get_check_type(const xx_xz *xz) {
    return xz ? xz->check_type : 0U;
}

bool xx_xz_can_extract(const xx_xz *xz) {
    return xz && xz->can_extract;
}
