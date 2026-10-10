/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/formats/xz/xx_xz.h"
#include "xx_xz_defs.h"

#include "xxfclib/algo/crc/xx_crc.h"
#include "../../algo/lzma/xx_lzma_stream_internal.h"
#include "../../algo/lzma/xx_lzma2_filters_internal.h"
#include "../7zip/xx_7zip_branch.h"
#include "../7zip/xx_7zip_defs.h"
#include "xx_xz_riscv_native.h"
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
    uint64_t bcj_method;
    uint8_t bcj_properties[4];
    uint8_t bcj_properties_size;
    bool delta_after_bcj;
    /* Reversible filters before LZMA2 in a multi-filter Block. */
    uint8_t chain_kind[3]; /* 1 = BCJ, 2 = Delta */
    uint16_t chain_distance[3];
    uint64_t chain_method[3];
    uint8_t chain_properties[3][4];
    uint8_t chain_property_size[3];
    bool extractable;
} xx_xz_block;

/* Filter chains are decoded as a whole block so branch filters see one
 * continuous address space. Keep the two temporary buffers bounded. */
#define XX_XZ_FILTER_CHAIN_MAX_BLOCK_BYTES (64U * 1024U * 1024U)

static uint64_t xx_xz_bcj_method(uint64_t id, uint32_t *alignment)
{
    if (!alignment) return 0U;
    switch (id) {
        case 0x04U: *alignment = 1U; return XX_7ZIP_METHOD_BCJ;
        case 0x05U: *alignment = 4U; return XX_7ZIP_METHOD_PPC;
        case 0x06U: *alignment = 16U; return XX_7ZIP_METHOD_IA64;
        case 0x07U: *alignment = 4U; return XX_7ZIP_METHOD_ARM;
        case 0x08U: *alignment = 2U; return XX_7ZIP_METHOD_ARMT;
        case 0x09U: *alignment = 4U; return XX_7ZIP_METHOD_SPARC;
        case 0x0AU: *alignment = 4U; return XX_7ZIP_METHOD_ARM64;
        case 0x0BU: *alignment = 2U; return XX_7ZIP_METHOD_RISCV;
        default: return 0U;
    }
}

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

#include "../xx_single_stream_writer.h"

static void xx_xz_vtable_destroy(Abstractformat *self);

static bool xx_xz_vli(const uint8_t *data, size_t limit, size_t *cursor, uint64_t *value)
{
    uint64_t result = 0U;
    size_t i;
    if (!data || !cursor || !value) return false;
    for (i = 0U; i < 9U; ++i) {
        uint8_t byte;
        if (*cursor >= limit) return false;
        byte = data[(*cursor)++];
        result |= (uint64_t)(byte & 0x7fU) << (i * 7U);
        if ((byte & 0x80U) == 0U) {
            if ((i != 0U && byte == 0U) || result > (uint64_t)INT64_MAX) return false;
            *value = result;
            return true;
        }
    }
    return false;
}

static size_t xx_xz_check_size(uint8_t check_type)
{
    if (check_type == XX_XZ_CHECK_NONE) return 0U;
    if (check_type > 15U) return SIZE_MAX;
    return (size_t)4U << ((check_type - 1U) / 3U);
}

static bool xx_xz_crc32_cancellable(const uint8_t *data, size_t size, xx_pd_struct *pd, uint32_t *result)
{
    const size_t capacity = xx_get_file_buffer_size();
    size_t position = 0U;
    uint32_t crc = 0U;
    if ((!data && size != 0U) || !result || capacity == 0U) return false;
    while (position < size) {
        size_t amount = size - position;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (amount > capacity) amount = capacity;
        crc = xx_crc32_calc(crc, data + position, amount);
        position += amount;
    }
    *result = crc;
    return true;
}

static bool xx_xz_validate_stream_header(xx_io_device *device, int64_t offset, uint8_t *check_type, xx_pd_struct *pd)
{
    uint8_t header[XX_XZ_STREAM_HEADER_SIZE];
    uint32_t expected;
    if (!device || !check_type || !xx_lzma_stream_read_exact_at(device, offset, header, sizeof(header), pd) || xx_rt_memcmp(header, XX_XZ_MAGIC, XX_XZ_MAGIC_SIZE) != 0 ||
        header[6] != 0U || (header[7] & 0xf0U) != 0U) {
        return false;
    }
    expected = xx_data_get_u32(header, sizeof(header), 8U, false);
    if (expected != xx_crc32_calc(0U, header + 6U, 2U)) return false;
    *check_type = header[7] & 0x0fU;
    return xx_xz_check_size(*check_type) != SIZE_MAX;
}

static bool xx_xz_parse_block_header(const uint8_t *header, size_t header_size, xx_xz_block *block, bool *has_compressed, uint64_t *declared_compressed,
                                     bool *has_uncompressed, uint64_t *declared_uncompressed)
{
    size_t cursor = 2U;
    size_t limit;
    uint8_t count;
    uint64_t ids[XX_XZ_MAX_FILTERS];
    uint64_t prop_sizes[XX_XZ_MAX_FILTERS];
    const uint8_t *props[XX_XZ_MAX_FILTERS];
    size_t i;
    uint32_t expected_crc;
    if (!header || !block || !has_compressed || !declared_compressed || !has_uncompressed || !declared_uncompressed || header_size < 8U || header_size > 1024U ||
        header[0] == 0U || header_size != ((size_t)header[0] + 1U) * 4U) {
        return false;
    }
    limit = header_size - 4U;
    expected_crc = xx_data_get_u32(header, header_size, limit, false);
    if (expected_crc != xx_crc32_calc(0U, header, limit) || (header[1] & 0x3cU) != 0U) {
        return false;
    }
    block->flags = header[1];
    count = (uint8_t)((header[1] & 3U) + 1U);
    block->filter_count = count;
    *has_compressed = (header[1] & 0x40U) != 0U;
    *has_uncompressed = (header[1] & 0x80U) != 0U;
    if (*has_compressed && !xx_xz_vli(header, limit, &cursor, declared_compressed)) return false;
    if (*has_uncompressed && !xx_xz_vli(header, limit, &cursor, declared_uncompressed)) return false;
    for (i = 0U; i < count; ++i) {
        if (!xx_xz_vli(header, limit, &cursor, &ids[i]) || !xx_xz_vli(header, limit, &cursor, &prop_sizes[i]) || prop_sizes[i] > XX_XZ_MAX_FILTER_PROPERTIES ||
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
    block->bcj_method = 0U;
    block->bcj_properties_size = 0U;
    block->delta_after_bcj = false;
    xx_mem_zero(block->chain_kind, sizeof(block->chain_kind));
    xx_mem_zero(block->chain_distance, sizeof(block->chain_distance));
    xx_mem_zero(block->chain_method, sizeof(block->chain_method));
    xx_mem_zero(block->chain_properties, sizeof(block->chain_properties));
    xx_mem_zero(block->chain_property_size, sizeof(block->chain_property_size));
    if (ids[count - 1U] == XX_XZ_FILTER_LZMA2 && prop_sizes[count - 1U] == 1U && xx_lzma2_parse_property(props[count - 1U][0], NULL)) {
        block->lzma2_property = props[count - 1U][0];
        if (count == 1U) {
            block->extractable = true;
        } else if (count == 2U && ids[0] == XX_XZ_FILTER_DELTA && prop_sizes[0] == 1U) {
            block->delta_distance = (uint16_t)props[0][0] + 1U;
            block->extractable = true;
        } else if (count == 2U || count == 3U) {
            size_t bcj_index = SIZE_MAX;
            uint16_t distance = 0U;
            bool delta_after = false;
            uint32_t alignment = 0U;
            uint64_t method;
            uint32_t start = 0U;
            if (count == 2U) {
                bcj_index = 0U;
            } else if (ids[0] == XX_XZ_FILTER_DELTA) {
                if (prop_sizes[0] != 1U) return false;
                bcj_index = 1U;
                distance = (uint16_t)props[0][0] + 1U;
                delta_after = true;
            } else if (ids[1] == XX_XZ_FILTER_DELTA) {
                if (prop_sizes[1] != 1U) return false;
                bcj_index = 0U;
                distance = (uint16_t)props[1][0] + 1U;
            }
            if (bcj_index != SIZE_MAX) {
                method = xx_xz_bcj_method(ids[bcj_index], &alignment);
                if (method != 0U) {
                    if (!xx_7zip_branch_method_supported(method, (size_t)prop_sizes[bcj_index])) return false;
                    if (prop_sizes[bcj_index] == 4U) {
                        start = xx_data_get_u32(props[bcj_index], 4U, 0U, false);
                        if (start % alignment != 0U) return false;
                        xx_mem_copy(block->bcj_properties, props[bcj_index], 4U);
                    }
                    block->bcj_method = method;
                    block->bcj_properties_size = (uint8_t)prop_sizes[bcj_index];
                    block->delta_distance = distance;
                    block->delta_after_bcj = delta_after;
                    block->extractable = true;
                }
            }
            if (count == 3U && !block->extractable) {
                /* Two branch or Delta filters are a valid liblzma chain. */
                for (i = 0U; i < 2U; ++i) {
                    uint32_t chain_alignment = 0U;
                    uint64_t chain_method = xx_xz_bcj_method(ids[i], &chain_alignment);
                    if (ids[i] == XX_XZ_FILTER_DELTA) {
                        if (prop_sizes[i] != 1U) return false;
                        block->chain_kind[i] = 2U;
                        block->chain_distance[i] = (uint16_t)props[i][0] + 1U;
                        continue;
                    }
                    if (chain_method == 0U) break;
                    if (!xx_7zip_branch_method_supported(chain_method, (size_t)prop_sizes[i])) return false;
                    if (prop_sizes[i] == 4U) {
                        uint32_t chain_start = xx_data_get_u32(props[i], 4U, 0U, false);
                        if (chain_start % chain_alignment != 0U) return false;
                        xx_mem_copy(block->chain_properties[i], props[i], 4U);
                    }
                    block->chain_kind[i] = 1U;
                    block->chain_method[i] = chain_method;
                    block->chain_property_size[i] = (uint8_t)prop_sizes[i];
                }
                if (i == 2U) {
                    block->bcj_method = block->chain_method[0] != 0U ? block->chain_method[0] : block->chain_method[1];
                    block->extractable = true;
                }
            }
        } else if (count == 4U) {
            size_t bcj_count = 0U;
            size_t delta_count = 0U;
            for (i = 0U; i < 3U; ++i) {
                uint32_t alignment = 0U;
                uint64_t method;
                if (ids[i] == XX_XZ_FILTER_DELTA) {
                    if (prop_sizes[i] != 1U) return false;
                    block->chain_kind[i] = 2U;
                    block->chain_distance[i] = (uint16_t)props[i][0] + 1U;
                    ++delta_count;
                    continue;
                }
                method = xx_xz_bcj_method(ids[i], &alignment);
                if (method == 0U) break;
                if (!xx_7zip_branch_method_supported(method, (size_t)prop_sizes[i])) return false;
                if (prop_sizes[i] == 4U) {
                    uint32_t start = xx_data_get_u32(props[i], 4U, 0U, false);
                    if (start % alignment != 0U) return false;
                    xx_mem_copy(block->bcj_properties, props[i], 4U);
                    xx_mem_copy(block->chain_properties[i], props[i], 4U);
                }
                block->chain_kind[i] = 1U;
                block->bcj_method = method;
                block->bcj_properties_size = (uint8_t)prop_sizes[i];
                block->chain_method[i] = method;
                block->chain_property_size[i] = (uint8_t)prop_sizes[i];
                ++bcj_count;
            }
            block->extractable = i == 3U && bcj_count + delta_count == 3U;
        }
    }
    return true;
}

static void xx_xz_candidate_cleanup(xx_xz_candidate *candidate)
{
    if (!candidate) return;
    if (candidate->blocks) xx_mem_free(candidate->blocks);
    xx_mem_zero(candidate, sizeof(*candidate));
}

static bool xx_xz_try_footer(Abstractformat *self, int64_t stream_start, uint8_t check_type, int64_t footer_offset, xx_xz_candidate *candidate,
                             uint64_t *index_work_remaining, xx_pd_struct *pd)
{
    uint8_t footer[XX_XZ_STREAM_FOOTER_SIZE];
    uint8_t index_indicator;
    uint8_t *index = NULL;
    uint64_t *unpadded = NULL;
    uint64_t *uncompressed = NULL;
    uint32_t index_crc;
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
    if (!self || !candidate || footer_offset < stream_start + 12 || (pd && xx_pd_is_stopped(pd)) ||
        !xx_lzma_stream_read_exact_at(self->device, footer_offset, footer, sizeof(footer), pd) || footer[10] != 0x59U || footer[11] != 0x5aU || footer[8] != 0U ||
        footer[9] != check_type || xx_data_get_u32(footer, sizeof(footer), 0U, false) != xx_crc32_calc(0U, footer + 4U, 6U)) {
        return false;
    }
    backward = xx_data_get_u32(footer, sizeof(footer), 4U, false);
    index_size_u64 = ((uint64_t)backward + 1U) * 4U;
    if (index_size_u64 < 8U || index_size_u64 > XX_XZ_MAX_INDEX_SIZE || index_size_u64 > (uint64_t)SIZE_MAX ||
        index_size_u64 > (uint64_t)(footer_offset - stream_start - 12)) {
        return false;
    }
    index_size = (size_t)index_size_u64;
    index_offset = footer_offset - (int64_t)index_size;
    /* Reject most forged Footer signatures before allocating or checksumming
       the candidate Index.  Charge the remaining candidates for every byte
       checksummed so overlapping fake Index ranges cannot make the scan
       quadratic in the input size. */
    if (!xx_lzma_stream_read_exact_at(self->device, index_offset, &index_indicator, sizeof(index_indicator), pd) || index_indicator != 0U || !index_work_remaining ||
        index_size_u64 > *index_work_remaining) {
        return false;
    }
    *index_work_remaining -= index_size_u64;
    index = (uint8_t *)xx_mem_alloc(index_size);
    if (!index || !xx_lzma_stream_read_exact_at(self->device, index_offset, index, index_size, pd) || index[0] != 0U ||
        !xx_xz_crc32_cancellable(index, index_size - 4U, pd, &index_crc) || xx_data_get_u32(index, index_size, index_size - 4U, false) != index_crc) {
        goto cleanup;
    }
    cursor = 1U;
    if (!xx_xz_vli(index, index_size - 4U, &cursor, &record_count_u64) || record_count_u64 > XX_XZ_MAX_BLOCKS || record_count_u64 > (uint64_t)SIZE_MAX) {
        goto cleanup;
    }
    record_count = (size_t)record_count_u64;
    if (record_count != 0U) {
        if (record_count > SIZE_MAX / sizeof(uint64_t) || record_count > SIZE_MAX / sizeof(xx_xz_block)) goto cleanup;
        unpadded = (uint64_t *)xx_mem_alloc(record_count * sizeof(uint64_t));
        uncompressed = (uint64_t *)xx_mem_alloc(record_count * sizeof(uint64_t));
        candidate->blocks = (xx_xz_block *)xx_mem_calloc(record_count, sizeof(xx_xz_block));
        if (!unpadded || !uncompressed || !candidate->blocks) goto cleanup;
    }
    for (size_t i = 0U; i < record_count; ++i) {
        if ((pd && xx_pd_is_stopped(pd)) || !xx_xz_vli(index, index_size - 4U, &cursor, &unpadded[i]) || !xx_xz_vli(index, index_size - 4U, &cursor, &uncompressed[i]) ||
            unpadded[i] == 0U)
            goto cleanup;
    }
    expected_padding = (4U - (cursor & 3U)) & 3U;
    if (cursor + expected_padding != index_size - 4U) goto cleanup;
    while (cursor < index_size - 4U) {
        if (pd && xx_pd_is_stopped(pd)) goto cleanup;
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
        if (!xx_lzma_stream_read_exact_at(self->device, block_offset, &first, 1U, pd) || first == 0U) goto cleanup;
        header_size = ((size_t)first + 1U) * 4U;
        if (block_offset > index_offset - (int64_t)header_size) goto cleanup;
        header = (uint8_t *)xx_mem_alloc(header_size);
        if (!header || !xx_lzma_stream_read_exact_at(self->device, block_offset, header, header_size, pd) ||
            !xx_xz_parse_block_header(header, header_size, block, &has_compressed, &declared_compressed, &has_uncompressed, &declared_uncompressed)) {
            if (header) xx_mem_free(header);
            goto cleanup;
        }
        xx_mem_free(header);
        if (unpadded[i] < (uint64_t)header_size + check_size) goto cleanup;
        compressed = unpadded[i] - (uint64_t)header_size - check_size;
        if (compressed == 0U || compressed > (uint64_t)INT64_MAX || (has_compressed && declared_compressed != compressed) ||
            (has_uncompressed && declared_uncompressed != uncompressed[i])) {
            goto cleanup;
        }
        if (unpadded[i] > UINT64_MAX - 3U) goto cleanup;
        aligned = (unpadded[i] + 3U) & ~UINT64_C(3);
        if (aligned > (uint64_t)INT64_MAX || block_offset > index_offset - (int64_t)aligned) goto cleanup;
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
            if (!xx_lzma_stream_read_exact_at(self->device, block->padding_offset, padding, (size_t)block->padding_size, pd)) goto cleanup;
            for (int64_t p = 0; p < block->padding_size; ++p)
                if (padding[p] != 0U) goto cleanup;
        }
        block_offset += (int64_t)aligned;
    }
    if (block_offset != index_offset || (pd && xx_pd_is_stopped(pd))) goto cleanup;

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

static bool xx_xz_find_stream_buffered(Abstractformat *self, int64_t stream_start, int64_t total_size, xx_xz_candidate *candidate, xx_pd_struct *pd, uint8_t *buffer,
                                       size_t buffer_capacity)
{
    uint8_t check_type;
    int64_t offset;
    uint64_t index_work_remaining;
    uint8_t previous = 0U;
    bool have_previous = false;
    if (!self || !candidate || !buffer || buffer_capacity == 0U || (pd && xx_pd_is_stopped(pd)) || stream_start < 0 || total_size < stream_start ||
        total_size - stream_start < 32 || !xx_xz_validate_stream_header(self->device, stream_start, &check_type, pd)) {
        return false;
    }
    index_work_remaining = 0U;
    offset = stream_start + XX_XZ_STREAM_HEADER_SIZE;
    while (offset < total_size) {
        size_t amount = (uint64_t)(total_size - offset) > (uint64_t)buffer_capacity ? buffer_capacity : (size_t)(total_size - offset);
        uint64_t work_credit;
        if (!xx_lzma_stream_read_exact_at(self->device, offset, buffer, amount, pd)) return false;
        /* Each scanned byte supplies a fixed amount of validation credit.
           This token bucket permits a real Index as large as the preceding
           stream data while bounding all overlapping fake Index CRC work by
           a constant multiple of the bytes scanned. */
        work_credit = (uint64_t)amount * XX_XZ_INDEX_VALIDATION_WORK_FACTOR;
        if (UINT64_MAX - index_work_remaining < work_credit) index_work_remaining = UINT64_MAX;
        else index_work_remaining += work_credit;
        for (size_t i = 0U; i < amount; ++i) {
            int64_t absolute = offset + (int64_t)i;
            if (have_previous && previous == 0x59U && buffer[i] == 0x5aU && absolute >= stream_start + 11) {
                int64_t footer_offset = absolute - 11;
                if (footer_offset >= stream_start + 20 && ((footer_offset - stream_start) & 3) == 0) {
                    xx_mem_zero(candidate, sizeof(*candidate));
                    if (xx_xz_try_footer(self, stream_start, check_type, footer_offset, candidate, &index_work_remaining, pd) && !(pd && xx_pd_is_stopped(pd))) {
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

static bool xx_xz_find_stream(Abstractformat *self, int64_t stream_start, int64_t total_size, xx_xz_candidate *candidate, xx_pd_struct *pd)
{
    const size_t buffer_capacity = xx_get_file_buffer_size();
    uint8_t *buffer = (uint8_t *)xx_mem_alloc(buffer_capacity);
    bool result;
    if (!buffer) return false;
    result = xx_xz_find_stream_buffered(self, stream_start, total_size, candidate, pd, buffer, buffer_capacity);
    xx_mem_free(buffer);
    return result;
}

static void xx_xz_private_free(xx_xz_private *priv)
{
    if (!priv) return;
    if (priv->streams) xx_mem_free(priv->streams);
    if (priv->blocks) xx_mem_free(priv->blocks);
    xx_mem_free(priv);
}

static bool xx_xz_append_candidate(xx_xz_private *priv, xx_xz_candidate *candidate)
{
    xx_xz_stream *streams;
    xx_xz_block *blocks = priv ? priv->blocks : NULL;
    size_t new_block_count;
    if (!priv || !candidate || priv->stream_count >= XX_XZ_MAX_STREAMS || candidate->block_count > XX_XZ_MAX_BLOCKS - priv->block_count ||
        priv->stream_count + 1U > SIZE_MAX / sizeof(xx_xz_stream)) {
        return false;
    }
    new_block_count = priv->block_count + candidate->block_count;
    if (new_block_count != 0U) {
        if (new_block_count > SIZE_MAX / sizeof(xx_xz_block)) return false;
        blocks = (xx_xz_block *)xx_mem_realloc(priv->blocks, new_block_count * sizeof(xx_xz_block));
        if (!blocks) return false;
        priv->blocks = blocks;
        if (candidate->block_count != 0U) {
            xx_rt_memcpy(priv->blocks + priv->block_count, candidate->blocks, candidate->block_count * sizeof(xx_xz_block));
        }
    }
    streams = (xx_xz_stream *)xx_mem_realloc(priv->streams, (priv->stream_count + 1U) * sizeof(xx_xz_stream));
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

static bool xx_xz_is_magic_at(xx_io_device *device, int64_t total_size, int64_t offset, xx_pd_struct *pd)
{
    uint8_t magic[XX_XZ_MAGIC_SIZE];
    return offset >= 0 && total_size - offset >= XX_XZ_MAGIC_SIZE && xx_lzma_stream_read_exact_at(device, offset, magic, sizeof(magic), pd) &&
           xx_rt_memcmp(magic, XX_XZ_MAGIC, sizeof(magic)) == 0;
}

static xx_xz_private *xx_xz_parse(Abstractformat *self, xx_pd_struct *pd)
{
    xx_xz_private *priv;
    int64_t total_size;
    int64_t stream_start;
    if (!self || !self->device || self->base_address < 0) return NULL;
    total_size = xx_io_total_size(self->device);
    if (total_size < self->base_address || total_size - self->base_address < 32) return NULL;
    priv = (xx_xz_private *)xx_mem_calloc(1U, sizeof(*priv));
    if (!priv) return NULL;
    priv->can_extract = true;
    stream_start = self->base_address;
    for (;;) {
        xx_xz_candidate candidate;
        int64_t cursor;
        xx_mem_zero(&candidate, sizeof(candidate));
        if (!xx_xz_find_stream(self, stream_start, total_size, &candidate, pd) || !xx_xz_append_candidate(priv, &candidate)) {
            xx_xz_candidate_cleanup(&candidate);
            xx_xz_private_free(priv);
            return NULL;
        }
        if (priv->streams[priv->stream_count - 1U].check_type != XX_XZ_CHECK_NONE && priv->streams[priv->stream_count - 1U].check_type != XX_XZ_CHECK_CRC32 &&
            priv->streams[priv->stream_count - 1U].check_type != XX_XZ_CHECK_CRC64 && priv->streams[priv->stream_count - 1U].check_type != XX_XZ_CHECK_SHA256) {
            priv->can_extract = false;
        }
        for (size_t i = priv->streams[priv->stream_count - 1U].first_block; i < priv->block_count; ++i) {
            if (UINT64_MAX - priv->uncompressed_size < priv->blocks[i].uncompressed_size ||
                UINT64_MAX - priv->compressed_data_size < (uint64_t)priv->blocks[i].compressed_size) {
                xx_xz_private_free(priv);
                return NULL;
            }
            priv->uncompressed_size += priv->blocks[i].uncompressed_size;
            priv->compressed_data_size += (uint64_t)priv->blocks[i].compressed_size;
            if (!priv->blocks[i].extractable) priv->can_extract = false;
        }
        cursor = priv->streams[priv->stream_count - 1U].stream_end;
        while (total_size - cursor >= 4) {
            uint8_t padding[4];
            if (!xx_lzma_stream_read_exact_at(self->device, cursor, padding, sizeof(padding), pd)) {
                xx_xz_private_free(priv);
                return NULL;
            }
            if (padding[0] != 0U || padding[1] != 0U || padding[2] != 0U || padding[3] != 0U) break;
            cursor += 4;
        }
        priv->streams[priv->stream_count - 1U].padding_size = cursor - priv->streams[priv->stream_count - 1U].stream_end;
        priv->format_end = cursor;
        if (!xx_xz_is_magic_at(self->device, total_size, cursor, pd)) break;
        stream_start = cursor;
    }
    return priv;
}

static uint64_t xx_xz_limit(Abstractformat *self, const xx_list_s *options, uint32_t id, uint64_t fallback)
{
    const xx_var *value = xx_format_resolve_extra_parameter(self, options, id);
    if (!value) return fallback;
    switch (value->type) {
        case XX_VAR_TYPE_UINT8:
        case XX_VAR_TYPE_UINT16:
        case XX_VAR_TYPE_UINT32:
        case XX_VAR_TYPE_UINT64: return xx_var_get_u64(value);
        case XX_VAR_TYPE_INT8:
        case XX_VAR_TYPE_INT16:
        case XX_VAR_TYPE_INT32:
        case XX_VAR_TYPE_INT64: {
            int64_t signed_value = xx_var_get_i64(value);
            return signed_value < 0 ? fallback : (uint64_t)signed_value;
        }
        default: return fallback;
    }
}

static bool xx_xz_delta_decode(uint8_t *data, size_t size, uint16_t distance, xx_pd_struct *pd)
{
    uint8_t history[256] = {0};
    size_t position;
    if ((!data && size != 0U) || distance == 0U || distance > 256U) return false;
    for (position = 0U; position < size; ++position) {
        size_t slot;
        uint8_t value;
        if ((position & 65535U) == 0U && pd && xx_pd_is_stopped(pd)) return false;
        slot = position % distance;
        value = (uint8_t)(data[position] + history[slot]);
        data[position] = value;
        history[slot] = value;
    }
    return !pd || !xx_pd_is_stopped(pd);
}

static bool xx_xz_verify_filter_chain_block(Abstractformat *self, const xx_xz_stream *stream, const xx_xz_block *block, const xx_list_s *options, xx_io_device *target,
                                            xx_pd_struct *pd)
{
    uint8_t *filtered = NULL;
    uint8_t *plain = NULL;
    uint8_t *result = NULL;
    xx_io_device *buffer = NULL;
    xx_lzma2_decoded_info decoded;
    uint8_t expected[XX_SHA256_DIGEST_SIZE];
    uint8_t digest[XX_SHA256_DIGEST_SIZE];
    size_t size;
    bool converted;
    bool valid = false;
    if (!self || !stream || !block || (block->bcj_method == 0U && block->chain_kind[0] == 0U) || block->uncompressed_size > XX_XZ_FILTER_CHAIN_MAX_BLOCK_BYTES ||
        block->uncompressed_size > SIZE_MAX || (pd && xx_pd_is_stopped(pd)))
        return false;
    size = (size_t)block->uncompressed_size;
    if (2U * (uint64_t)(size ? size : 1U) + 4096U > xx_xz_limit(self, options, XX_META_ID_OPT_MEMORY_LIMIT, UINT64_MAX)) return false;
    filtered = (uint8_t *)xx_mem_alloc(size ? size : 1U);
    plain = (uint8_t *)xx_mem_alloc(size ? size : 1U);
    if (!filtered || !plain || !(buffer = xx_io_mem_open(filtered, size))) goto cleanup;
    if (!xx_lzma2_unpack_filtered_checked_device(self->device, block->data_offset, block->compressed_size, block->lzma2_property,
                                                 block->delta_after_bcj ? 0U : block->delta_distance, block->uncompressed_size, buffer, 0U, &decoded, pd) ||
        decoded.written != size || (pd && xx_pd_is_stopped(pd)))
        goto cleanup;
    if (xx_io_close(buffer) != 0) {
        buffer = NULL;
        goto cleanup;
    }
    buffer = NULL;
    if (block->chain_kind[0] != 0U) {
        uint8_t *current = filtered;
        uint8_t *scratch = plain;
        size_t index = (size_t)block->filter_count - 1U;
        converted = true;
        while (index-- > 0U) {
            if (block->chain_kind[index] == 2U) {
                converted = xx_xz_delta_decode(current, size, block->chain_distance[index], pd);
            } else if (block->chain_kind[index] == 1U && block->chain_method[index] == XX_7ZIP_METHOD_RISCV) {
                converted = xx_xz_riscv_decode(current, size, block->chain_properties[index], block->chain_property_size[index]);
            } else if (block->chain_kind[index] == 1U) {
                uint8_t *next;
                converted =
                    xx_7zip_branch_decode(block->chain_method[index], block->chain_properties[index], block->chain_property_size[index], current, size, scratch, size);
                next = current;
                current = scratch;
                scratch = next;
            } else {
                converted = false;
            }
            if (!converted || (pd && xx_pd_is_stopped(pd))) goto cleanup;
        }
        result = current;
    } else if (block->bcj_method == XX_7ZIP_METHOD_RISCV) {
        if (size) xx_mem_copy(plain, filtered, size);
        converted = xx_xz_riscv_decode(plain, size, block->bcj_properties, block->bcj_properties_size);
        result = plain;
    } else {
        converted = xx_7zip_branch_decode(block->bcj_method, block->bcj_properties, block->bcj_properties_size, filtered, size, plain, size);
        result = plain;
    }
    if (!converted || (block->chain_kind[0] == 0U && block->delta_after_bcj && !xx_xz_delta_decode(result, size, block->delta_distance, pd)) ||
        !xx_lzma_stream_read_exact_at(self->device, block->check_offset, expected, (size_t)block->check_size, pd) || (pd && xx_pd_is_stopped(pd)))
        goto cleanup;
    switch (stream->check_type) {
        case XX_XZ_CHECK_NONE: valid = true; break;
        case XX_XZ_CHECK_CRC32: valid = xx_data_get_u32(expected, sizeof(expected), 0U, false) == xx_crc32_calc(0U, result, size); break;
        case XX_XZ_CHECK_CRC64: valid = xx_data_get_u64(expected, sizeof(expected), 0U, false) == xx_crc64_xz_calc(0U, result, size); break;
        case XX_XZ_CHECK_SHA256: valid = xx_sha256_memory(result, size, digest) && xx_hash_equal(expected, digest, sizeof(digest)); break;
        default: valid = false; break;
    }
    if (valid && target) {
        size_t offset = 0U;
        while (offset < size) {
            size_t length = size - offset;
            ssize_t written;
            if (pd && xx_pd_is_stopped(pd)) {
                valid = false;
                break;
            }
            if (length > 65536U) length = 65536U;
            written = xx_io_write(target, result + offset, length);
            if (written <= 0 || (size_t)written > length) {
                valid = false;
                break;
            }
            offset += (size_t)written;
        }
    }
cleanup:
    if (buffer) xx_io_close(buffer);
    if (plain) xx_mem_free(plain);
    if (filtered) xx_mem_free(filtered);
    return valid;
}

static bool xx_xz_verify_block(Abstractformat *self, const xx_xz_stream *stream, const xx_xz_block *block, const xx_list_s *options, xx_io_device *target,
                               xx_pd_struct *pd)
{
    xx_lzma2_decoded_info decoded;
    uint8_t expected[XX_SHA256_DIGEST_SIZE];
    unsigned check_mask;
    bool valid = false;
    if (!self || !stream || !block || !block->extractable) return false;
    if (block->bcj_method != 0U || block->chain_kind[0] != 0U) return xx_xz_verify_filter_chain_block(self, stream, block, options, target, pd);
    switch (stream->check_type) {
        case XX_XZ_CHECK_NONE: check_mask = 0U; break;
        case XX_XZ_CHECK_CRC32: check_mask = XX_LZMA2_CALC_CRC32; break;
        case XX_XZ_CHECK_CRC64: check_mask = XX_LZMA2_CALC_CRC64; break;
        case XX_XZ_CHECK_SHA256: check_mask = XX_LZMA2_CALC_SHA256; break;
        default: return false;
    }
    if (!xx_lzma2_unpack_filtered_checked_device(self->device, block->data_offset, block->compressed_size, block->lzma2_property, block->delta_distance,
                                                 block->uncompressed_size, target, check_mask, &decoded, pd) ||
        !xx_lzma_stream_read_exact_at(self->device, block->check_offset, expected, (size_t)block->check_size, pd)) {
        return false;
    }
    switch (stream->check_type) {
        case XX_XZ_CHECK_NONE: valid = true; break;
        case XX_XZ_CHECK_CRC32: valid = xx_data_get_u32(expected, sizeof(expected), 0U, false) == decoded.crc32; break;
        case XX_XZ_CHECK_CRC64: valid = xx_data_get_u64(expected, sizeof(expected), 0U, false) == decoded.crc64; break;
        case XX_XZ_CHECK_SHA256: valid = xx_hash_equal(expected, decoded.sha256, sizeof(decoded.sha256)); break;
        default: valid = false; break;
    }
    return valid;
}

static bool xx_xz_extract_all(Abstractformat *self, const xx_list_s *options, xx_io_device *target, xx_pd_struct *pd)
{
    xx_xz_private *priv;
    if (!self || !(priv = (xx_xz_private *)((xx_xz *)self)->internal) || !priv->can_extract || (pd && xx_pd_is_stopped(pd))) return false;
    if (priv->uncompressed_size > xx_xz_limit(self, options, XX_META_ID_OPT_MAX_MEMBER_SIZE, UINT64_MAX)) return false;
    for (size_t s = 0U; s < priv->stream_count; ++s) {
        const xx_xz_stream *stream = &priv->streams[s];
        for (size_t b = 0U; b < stream->block_count; ++b) {
            const xx_xz_block *block = &priv->blocks[stream->first_block + b];
            if (!xx_xz_verify_block(self, stream, block, options, target, pd)) return false;
        }
    }
    return true;
}

static bool xx_xz_copy_options(xx_list_s *destination, const xx_list_s *source)
{
    if (!destination || !source) return source == NULL;
    for (size_t i = 0U; i < source->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at((const xx_list_t *)source, i);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) || !xx_list_append(destination, &copy)) {
            xx_meta_cleanup(&copy);
            return false;
        }
    }
    return true;
}

static bool xx_xz_same_host_path(const char *left, const char *right)
{
    if (!left || !right) return false;
    while (*left && *right) {
        unsigned char a = (unsigned char)*left++;
        unsigned char b = (unsigned char)*right++;
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b) return false;
    }
    return *left == '\0' && *right == '\0';
}

static xx_io_device *xx_xz_open_stage(const char *destination, char **stage_path)
{
    char *directory;
    size_t parent = 0U;
    size_t i;
    unsigned attempt;
    if (!destination || !stage_path) return NULL;
    *stage_path = NULL;
    directory = xx_str_dup(destination);
    if (!directory) return NULL;
    for (i = 0U; directory[i]; ++i) {
        if (directory[i] == '/' || directory[i] == '\\') parent = i + 1U;
    }
    directory[parent] = '\0';
    for (attempt = 0U; attempt < 128U; ++attempt) {
        char suffix[48];
        char *candidate;
        xx_io_device *file;
        xx_rt_snprintf(suffix, sizeof(suffix), ".xx_xz.tmp.%u", attempt);
        candidate = xx_str_concat(directory, suffix);
        if (!candidate) break;
        if (xx_xz_same_host_path(candidate, destination)) {
            xx_str_free(candidate);
            continue;
        }
        file = xx_io_file_open(candidate, "wbx");
        if (file) {
            *stage_path = candidate;
            xx_str_free(directory);
            return file;
        }
        xx_str_free(candidate);
    }
    xx_str_free(directory);
    return NULL;
}

static bool xx_xz_populate_record(xx_archive_record *record, const xx_xz_private *priv)
{
    int64_t data_offset;
    if (!record || !priv || priv->compressed_data_size > (uint64_t)INT64_MAX || priv->stream_count == 0U) return false;
    data_offset = priv->block_count != 0U ? priv->blocks[0].data_offset : priv->streams[0].index_offset;
    xx_archive_record_cleanup(record);
    xx_archive_record_init(record);
    record->header_offset = priv->streams[0].header_offset;
    record->header_size = XX_XZ_STREAM_HEADER_SIZE;
    record->data_offset = data_offset;
    record->compressed_size = (int64_t)priv->compressed_data_size;
    return xx_archive_record_set_meta_str(record, XX_META_ID_ORIGINAL_NAME, XX_XZ_PAYLOAD_NAME) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE, priv->uncompressed_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE, priv->compressed_data_size) &&
           xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, XX_XZ_COMPRESSION_METHOD) &&
           xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER, false) && xx_archive_record_set_meta_bool(record, XX_META_ID_IS_ENCRYPTED, false);
}

void xx_xz_init(xx_xz *xz, xx_io_device *dev, int64_t base_address)
{
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
    xz->format.get_number_of_archive_records = xx_xz_get_number_of_archive_records;
    xz->format.create_archive_records_reading = xx_xz_create_archive_records_reading;
    xz->format.get_current_archive_record = xx_xz_get_current_archive_record;
    xz->format.unpack_current_archive_record = xx_xz_unpack_current_archive_record;
    xz->format.archive_record_move_to_next = xx_xz_archive_record_move_to_next;
    xz->format.free_archive_records_reading = xx_xz_free_archive_records_reading;
    xz->format.data_struct_id_to_string = xx_xz_data_struct_id_to_string;
    xz->format.data_struct_string_to_id = xx_xz_data_struct_string_to_id;
    xz->format.create_data_structs_reading = xx_xz_create_data_structs_reading;
    xz->format.get_current_data_struct = xx_xz_get_current_data_struct;
    xz->format.data_struct_move_to_next = xx_xz_data_struct_move_to_next;
    xz->format.free_data_structs_reading = xx_xz_free_data_structs_reading;
    xz->format.create_data_struct_records_reading = xx_xz_create_data_struct_records_reading;
    xz->format.get_current_data_struct_record = xx_xz_get_current_data_struct_record;
    xz->format.data_struct_record_move_to_next = xx_xz_data_struct_record_move_to_next;
    xz->format.free_data_struct_records_reading = xx_xz_free_data_struct_records_reading;
    xz->format.create_archive_records_writing = xx_xz_create_archive_records_writing;
    xz->format.pack_archive_record = xx_xz_pack_archive_record;
    xz->format.finalize_archive_records_writing = xx_xz_finalize_archive_records_writing;
    xz->format.free_archive_records_writing = xx_xz_free_archive_records_writing;
    xz->format.destroy = xx_xz_vtable_destroy;
    xz->index_offset = -1;
    xz->footer_offset = -1;
}

xx_xz *xx_xz_create(xx_io_device *dev, int64_t base_address)
{
    xx_xz *xz = (xx_xz *)xx_mem_alloc(sizeof(*xz));
    if (xz) xx_xz_init(xz, dev, base_address);
    return xz;
}

void xx_xz_destroy(xx_xz *xz)
{
    if (!xz) return;
    if (xz->internal) {
        xx_xz_private_free((xx_xz_private *)xz->internal);
        xz->internal = NULL;
    }
    if (xz->format.close) xz->format.close(&xz->format);
    xx_format_cleanup_extra_parameters(&xz->format);
}

static void xx_xz_vtable_destroy(Abstractformat *self)
{
    xx_xz_destroy((xx_xz *)self);
}

void xx_xz_free(xx_xz *xz)
{
    if (!xz) return;
    xx_xz_destroy(xz);
    xx_mem_free(xz);
}

bool xx_xz_check_is_valid(Abstractformat *self, xx_pd_struct *pd)
{
    uint8_t check_type;
    int64_t total_size;
    (void)pd;
    if (!self || !self->device || self->base_address < 0) return false;
    total_size = xx_io_total_size(self->device);
    return total_size >= self->base_address && total_size - self->base_address >= 32 && xx_xz_validate_stream_header(self->device, self->base_address, &check_type, pd);
}

bool xx_xz_handle_base_info(Abstractformat *self, xx_pd_struct *pd)
{
    xx_xz_private *priv;
    xx_xz *xz;
    int64_t total_size;
    if (!self || !self->device || !(priv = xx_xz_parse(self, pd)) || priv->compressed_data_size > (uint64_t)INT64_MAX) {
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

int64_t xx_xz_get_format_size(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) return -1;
    return self->format_size;
}

uint64_t xx_xz_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd)
{
    if (!self || (!self->base_info_handled && !xx_format_handle_base_info(self, pd))) return 0U;
    return 1U;
}

bool xx_xz_unpack_to_device(xx_xz *xz, xx_io_device *destination, xx_pd_struct *pd)
{
    if (!xz || !destination || destination == xz->format.device || (!xz->format.base_info_handled && !xx_format_handle_base_info(&xz->format, pd)) ||
        !xz->format.is_valid || !xz->can_extract) {
        return false;
    }
    return xx_xz_extract_all(&xz->format, NULL, destination, pd);
}

xx_archive_record_state *xx_xz_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    xx_archive_record_state *state;
    xx_xz_private *priv;
    if (!self || !self->device || (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) || !self->is_valid ||
        !(priv = (xx_xz_private *)((xx_xz *)self)->internal))
        return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) return NULL;
    xx_archive_record_state_init(state, self);
    if (!xx_xz_copy_options(&state->options, options) || !xx_xz_populate_record(&state->current_record, priv)) {
        xx_archive_record_state_free(state);
        return NULL;
    }
    state->has_record = true;
    state->current_index = 0;
    state->total_records = 1;
    return state;
}

const xx_archive_record *xx_xz_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_xz_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    const xx_var *path_value;
    const xx_var *overwrite_value;
    const char *base = NULL;
    char *owned_base = NULL;
    char *destination = NULL;
    char *stage_path = NULL;
    xx_io_device *target = NULL;
    bool overwrite;
    bool result = false;
    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    path_value = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_UNPACK_PATH);
    if (!path_value) return xx_xz_extract_all(self, &state->options, NULL, pd);
    if (path_value->type == XX_VAR_TYPE_STRING || path_value->type == XX_VAR_TYPE_STRING_VIEW) {
        base = xx_var_get_str(path_value);
    } else if (path_value->type == XX_VAR_TYPE_WSTRING || path_value->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned_base = xx_str_unicode_to_utf8(xx_var_get_wstr(path_value));
        base = owned_base;
    }
    if (!base) goto cleanup;
    overwrite_value = xx_format_resolve_extra_parameter(self, &state->options, XX_META_ID_OPT_OVERWRITE);
    overwrite = overwrite_value && xx_var_get_bool(overwrite_value);
    if (base[0] != '\0' && base[xx_str_len(base) - 1U] != '/' && base[xx_str_len(base) - 1U] != '\\') {
        destination = xx_str_concat3(base, "/", XX_XZ_PAYLOAD_NAME);
    } else {
        destination = xx_str_concat(base, XX_XZ_PAYLOAD_NAME);
    }
    if (!destination || (!overwrite && xx_io_file_exists_a(destination)) || !xx_io_create_dirs_a(destination, false) ||
        !(target = xx_xz_open_stage(destination, &stage_path)))
        goto cleanup;
    result = xx_xz_extract_all(self, &state->options, target, pd);
    if (xx_io_close(target) != 0) result = false;
    target = NULL;
    if (pd && xx_pd_is_stopped(pd)) result = false;
    if (result) result = xx_io_file_replace_a(stage_path, destination, overwrite);

cleanup:
    if (target) xx_io_close(target);
    if (!result && stage_path) xx_io_file_remove_a(stage_path);
    if (stage_path) xx_str_free(stage_path);
    if (owned_base) xx_str_free(owned_base);
    if (destination) xx_str_free(destination);
    return result;
}

bool xx_xz_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd)
{
    if (!self || !state || state->format != self || !state->has_record || (pd && xx_pd_is_stopped(pd))) return false;
    state->has_record = false;
    return false;
}

void xx_xz_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state)
{
    (void)self;
    xx_archive_record_state_free(state);
}

typedef struct xx_xz_ds_name_s {
    xx_xz_data_struct_id_t id;
    const char *name;
} xx_xz_ds_name;

static const xx_xz_ds_name g_xx_xz_ds_names[] = {
    {XX_XZ_DS_UNKNOWN, "UNKNOWN"},       {XX_XZ_DS_STREAM_HEADER, "STREAM_HEADER"}, {XX_XZ_DS_BLOCK_HEADER, "BLOCK_HEADER"},
    {XX_XZ_DS_BLOCK_DATA, "BLOCK_DATA"}, {XX_XZ_DS_BLOCK_PADDING, "BLOCK_PADDING"}, {XX_XZ_DS_BLOCK_CHECK, "BLOCK_CHECK"},
    {XX_XZ_DS_INDEX, "INDEX"},           {XX_XZ_DS_STREAM_FOOTER, "STREAM_FOOTER"}, {XX_XZ_DS_STREAM_PADDING, "STREAM_PADDING"}};

const char *xx_xz_data_struct_id_to_string(Abstractformat *self, uint32_t id)
{
    (void)self;
    for (size_t i = 0U; i < sizeof(g_xx_xz_ds_names) / sizeof(g_xx_xz_ds_names[0]); ++i) {
        if ((uint32_t)g_xx_xz_ds_names[i].id == id) return g_xx_xz_ds_names[i].name;
    }
    return "UNKNOWN";
}

uint32_t xx_xz_data_struct_string_to_id(Abstractformat *self, const char *name)
{
    (void)self;
    if (name) {
        for (size_t i = 0U; i < sizeof(g_xx_xz_ds_names) / sizeof(g_xx_xz_ds_names[0]); ++i) {
            if (xx_str_cmp(name, g_xx_xz_ds_names[i].name) == 0) return (uint32_t)g_xx_xz_ds_names[i].id;
        }
    }
    return (uint32_t)XX_XZ_DS_UNKNOWN;
}

static void xx_xz_ds_stream_free(void *pointer)
{
    xx_xz_ds_stream *stream = (xx_xz_ds_stream *)pointer;
    if (!stream) return;
    if (stream->items) xx_mem_free(stream->items);
    xx_mem_free(stream);
}

static void xx_xz_set_ds(xx_data_struct *item, uint32_t id, int64_t offset, int64_t size, xx_data_struct_type_t type, bool is_mapped)
{
    item->id = id;
    item->offset = offset;
    item->address = is_mapped ? offset : -1;
    item->entry_size = size;
    item->total_size = size;
    item->count = 1U;
    item->type = type;
}

xx_data_struct_state *xx_xz_create_data_structs_reading(Abstractformat *self, xx_pd_struct *pd)
{
    xx_xz_private *priv;
    xx_data_struct_state *state;
    xx_xz_ds_stream *stream;
    size_t capacity;
    if (!self || !self->device || (!self->base_info_handled && !xx_format_handle_base_info(self, pd)) || !(priv = (xx_xz_private *)((xx_xz *)self)->internal) ||
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
    stream->items = (xx_data_struct *)xx_mem_alloc((capacity ? capacity : 1U) * sizeof(*stream->items));
    if (!stream->items) {
        xx_mem_free(stream);
        xx_data_struct_state_free(state);
        return NULL;
    }
    for (size_t s = 0U; s < priv->stream_count; ++s) {
        const xx_xz_stream *xz_stream = &priv->streams[s];
        xx_xz_set_ds(&stream->items[stream->count++], XX_XZ_DS_STREAM_HEADER, xz_stream->header_offset, XX_XZ_STREAM_HEADER_SIZE, XX_DATA_STRUCT_TYPE_STRUCT,
                     self->is_mapped);
        for (size_t b = 0U; b < xz_stream->block_count; ++b) {
            const xx_xz_block *block = &priv->blocks[xz_stream->first_block + b];
            xx_xz_set_ds(&stream->items[stream->count++], XX_XZ_DS_BLOCK_HEADER, block->header_offset, block->header_size, XX_DATA_STRUCT_TYPE_STRUCT, self->is_mapped);
            if (block->compressed_size != 0) {
                xx_xz_set_ds(&stream->items[stream->count++], XX_XZ_DS_BLOCK_DATA, block->data_offset, block->compressed_size, XX_DATA_STRUCT_TYPE_RAW_DATA,
                             self->is_mapped);
            }
            if (block->padding_size != 0) {
                xx_xz_set_ds(&stream->items[stream->count++], XX_XZ_DS_BLOCK_PADDING, block->padding_offset, block->padding_size, XX_DATA_STRUCT_TYPE_RAW_DATA,
                             self->is_mapped);
            }
            if (block->check_size != 0) {
                xx_xz_set_ds(&stream->items[stream->count++], XX_XZ_DS_BLOCK_CHECK, block->check_offset, block->check_size, XX_DATA_STRUCT_TYPE_STRUCT, self->is_mapped);
            }
        }
        xx_xz_set_ds(&stream->items[stream->count++], XX_XZ_DS_INDEX, xz_stream->index_offset, xz_stream->index_size, XX_DATA_STRUCT_TYPE_STRUCT, self->is_mapped);
        xx_xz_set_ds(&stream->items[stream->count++], XX_XZ_DS_STREAM_FOOTER, xz_stream->footer_offset, XX_XZ_STREAM_FOOTER_SIZE, XX_DATA_STRUCT_TYPE_FOOTER,
                     self->is_mapped);
        if (xz_stream->padding_size != 0) {
            xx_xz_set_ds(&stream->items[stream->count++], XX_XZ_DS_STREAM_PADDING, xz_stream->padding_offset, xz_stream->padding_size, XX_DATA_STRUCT_TYPE_RAW_DATA,
                         self->is_mapped);
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

const xx_data_struct *xx_xz_get_current_data_struct(Abstractformat *self, xx_data_struct_state *state)
{
    return self && state && state->format == self && state->has_struct ? &state->current_struct : NULL;
}

bool xx_xz_data_struct_move_to_next(Abstractformat *self, xx_data_struct_state *state, xx_pd_struct *pd)
{
    xx_xz_ds_stream *stream;
    int64_t next;
    if (!self || !state || state->format != self || !state->has_struct || !state->internal_state || (pd && xx_pd_is_stopped(pd))) return false;
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

void xx_xz_free_data_structs_reading(Abstractformat *self, xx_data_struct_state *state)
{
    (void)self;
    xx_data_struct_state_free(state);
}

static const xx_data_struct_field_desc g_xz_stream_header_fields[] = {{L"magic", L"uint8[6]", 0, 6, XX_DATA_STRUCT_RECORD_PROPERTY_ID},
                                                                      {L"stream_flags", L"uint16", 6, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS},
                                                                      {L"header_crc32", L"uint32", 8, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE}};

static const xx_data_struct_field_desc g_xz_block_header_fields[] = {{L"header_size", L"uint8", 0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
                                                                     {L"block_flags", L"uint8", 1, 1, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS}};

static const xx_data_struct_field_desc g_xz_index_fields[] = {{L"indicator", L"uint8", 0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_ID}};

static const xx_data_struct_field_desc g_xz_footer_fields[] = {{L"footer_crc32", L"uint32", 0, 4, XX_DATA_STRUCT_RECORD_PROPERTY_NONE},
                                                               {L"backward_size", L"uint32", 4, 4, XX_DATA_STRUCT_RECORD_PROPERTY_SIZE},
                                                               {L"stream_flags", L"uint16", 8, 2, XX_DATA_STRUCT_RECORD_PROPERTY_FLAGS},
                                                               {L"magic", L"uint16", 10, 2, XX_DATA_STRUCT_RECORD_PROPERTY_ID}};

static void xx_xz_record_stream_free(void *pointer)
{
    if (pointer) xx_mem_free(pointer);
}

xx_data_struct_record_state *xx_xz_create_data_struct_records_reading(Abstractformat *self, const xx_data_struct *ds, xx_pd_struct *pd)
{
    xx_data_struct_record_state *state;
    xx_xz_record_stream *stream;
    const xx_data_struct_field_desc *fields;
    size_t count;
    (void)pd;
    if (!self || !self->device || !ds) return NULL;
    if (ds->id == XX_XZ_DS_STREAM_HEADER) {
        fields = g_xz_stream_header_fields;
        count = sizeof(g_xz_stream_header_fields) / sizeof(g_xz_stream_header_fields[0]);
    } else if (ds->id == XX_XZ_DS_BLOCK_HEADER) {
        fields = g_xz_block_header_fields;
        count = sizeof(g_xz_block_header_fields) / sizeof(g_xz_block_header_fields[0]);
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
    if (count != 0U && xx_data_struct_record_populate(&state->current_record, self->device, ds->offset, &fields[0], false)) {
        state->has_record = true;
        state->current_index = 0;
    }
    return state;
}

const xx_data_struct_record *xx_xz_get_current_data_struct_record(Abstractformat *self, xx_data_struct_record_state *state)
{
    return self && state && state->format == self && state->has_record ? &state->current_record : NULL;
}

bool xx_xz_data_struct_record_move_to_next(Abstractformat *self, xx_data_struct_record_state *state, xx_pd_struct *pd)
{
    xx_xz_record_stream *stream;
    int64_t next;
    if (!self || !state || state->format != self || !state->has_record || !state->internal_state || (pd && xx_pd_is_stopped(pd))) return false;
    stream = (xx_xz_record_stream *)state->internal_state;
    next = state->current_index + 1;
    if (next < 0 || (size_t)next >= stream->count) {
        state->has_record = false;
        return false;
    }
    xx_data_struct_record_cleanup(&state->current_record);
    if (!xx_data_struct_record_populate(&state->current_record, self->device, state->parent_struct.offset, &stream->fields[next], false)) {
        state->has_record = false;
        return false;
    }
    state->current_index = next;
    return true;
}

void xx_xz_free_data_struct_records_reading(Abstractformat *self, xx_data_struct_record_state *state)
{
    (void)self;
    xx_data_struct_record_state_free(state);
}

uint64_t xx_xz_get_number_of_blocks(const xx_xz *xz)
{
    return xz ? xz->number_of_blocks : 0U;
}

uint64_t xx_xz_get_uncompressed_size(const xx_xz *xz)
{
    return xz ? xz->uncompressed_size : 0U;
}

uint8_t xx_xz_get_check_type(const xx_xz *xz)
{
    return xz ? xz->check_type : 0U;
}

bool xx_xz_can_extract(const xx_xz *xz)
{
    return xz && xz->can_extract;
}

xx_archive_write_state *xx_xz_create_archive_records_writing(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd)
{
    return ss_create(self, options, pd);
}
bool xx_xz_pack_archive_record(Abstractformat *self, xx_archive_write_state *state, const xx_archive_record *record, xx_io_device *source, xx_pd_struct *pd)
{
    return ss_pack(self, state, record, source, pd);
}
bool xx_xz_finalize_archive_records_writing(Abstractformat *self, xx_archive_write_state *state, xx_pd_struct *pd)
{
    return ss_finalize(self, state, pd);
}
void xx_xz_free_archive_records_writing(Abstractformat *self, xx_archive_write_state *state)
{
    ss_free(self, state);
}
