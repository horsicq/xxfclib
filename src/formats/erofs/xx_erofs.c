/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * EROFS core, simple chunk-map, and bounded compressed-file reader.
 * Format descriptions and producer format definitions:
 * https://erofs.docs.kernel.org/en/latest/ondisk/core_ondisk.html
 * https://erofs.docs.kernel.org/en/latest/ondisk/chunked_format.html
 * https://github.com/erofs/erofs-utils/blob/v1.9.1/include/erofs_fs.h
 */
#include "xxfclib/rt/xx_rt.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/formats/erofs/xx_erofs.h"
#include "xxfclib/algo/store/xx_store.h"
#include "xxfclib/algo/lz4/xx_lz4.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lzma/xx_lzma.h"
#include "xxfclib/algo/zstd/xx_zstd.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/strings/xx_string.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "xxfclib/data/xx_data.h"

#define ER_SB_OFFSET 1024U
#define ER_SB_BYTES 128U
#define ER_MAX_MEMBERS 100000U
#define ER_MAX_DIRS 100000U
#define ER_MAX_DEPTH 64U
#define ER_MAX_DIR_BYTES (16U * 1024U * 1024U)
#define ER_MAX_PATH 4096U
#define ER_MAX_PATH_BYTES (64U * 1024U * 1024U)
#define ER_MAX_CHUNKS 1000000U
#define ER_MAX_WORK 4000000U
#define ER_MAX_Z_DECODE (12U * 1024U * 1024U)
#define ER_MAGIC UINT32_C(0xe0f5e1e2)
#define ER_CHUNKED_FLAG UINT32_C(0x4)

typedef struct er_inode_s {
    uint64_t nid, offset, size;
    uint64_t z_max_decoded, z_max_packed;
    uint32_t startblk, chunk_info;
    uint8_t inode_bytes, layout;
    bool directory;
} er_inode;
typedef struct er_member_s {
    char *path;
    er_inode inode;
} er_member;
typedef struct er_view_s {
    xx_io_device *device;
    int64_t base;
    uint64_t bytes, format_bytes, work, path_bytes, retained;
    uint32_t block_size, meta_blkaddr, incompat;
    uint32_t lzma_dict;
    uint8_t algorithm;
    uint64_t root_nid;
    er_member *members;
    size_t count, capacity, index;
    uint64_t *directories;
    size_t dir_count, dir_capacity;
} er_view;

static bool er_stopped(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool er_work(er_view *v, xx_pd_struct *pd) {
    return !er_stopped(pd) && ++v->work <= ER_MAX_WORK;
}
static bool er_read(er_view *v, uint64_t offset, void *out, size_t size,
                    xx_pd_struct *pd) {
    int64_t saved;
    uint8_t *bytes = (uint8_t *)out;
    size_t done = 0U;
    bool ok = true;
    if (!v || !v->device || (!out && size) || offset > v->bytes ||
        size > v->bytes - offset || offset > (uint64_t)(INT64_MAX - v->base) ||
        !er_work(v, pd)) return false;
    saved = xx_io_tell(v->device);
    if (saved < 0) return false;
    if (xx_io_seek64(v->device, v->base + (int64_t)offset, SEEK_SET)) ok = false;
    while (ok && done < size && !er_stopped(pd)) {
        ssize_t n = xx_io_read(v->device, bytes + done, size - done);
        if (n <= 0 || (size_t)n > size - done) { ok = false; break; }
        done += (size_t)n;
    }
    if (xx_io_seek64(v->device, saved, SEEK_SET)) ok = false;
    return ok && done == size && !er_stopped(pd);
}
static bool er_range(const er_view *v, uint64_t offset, uint64_t size) {
    return offset <= v->bytes && size <= v->bytes - offset;
}
static bool er_zero(const uint8_t *data, size_t size) {
    size_t i;
    for (i = 0U; i < size; ++i) if (data[i]) return false;
    return true;
}
/* EROFS stores raw CRC-32C with seed ~0 and no final inversion. */
static uint32_t er_crc32c(const uint8_t *data, size_t size) {
    return xx_crc32c_calc(0U, data, size) ^ UINT32_MAX;
}
static bool er_super(er_view *v, xx_pd_struct *pd) {
    uint8_t sb[ER_SB_BYTES], *checksum_block = NULL;
    uint32_t compat, declared_checksum;
    uint16_t algorithms;
    uint8_t block_bits;
    bool ok = false;
    if (!er_range(v, ER_SB_OFFSET, ER_SB_BYTES) ||
        !er_read(v, ER_SB_OFFSET, sb, sizeof(sb), pd) || xx_data_get_u32(sb, 4, 0, false) != ER_MAGIC)
        return false;
    compat = xx_data_get_u32(sb + 0x08U, 4, 0, false);
    v->incompat = xx_data_get_u32(sb + 0x50U, 4, 0, false);
    algorithms = xx_data_get_u16(sb + 0x54U, 2, 0, false);
    if (algorithms != UINT16_MAX && algorithms &&
        (algorithms & (algorithms - 1U))) return false;
    v->algorithm = algorithms == UINT16_MAX &&
        (v->incompat & 3U) == 1U ? 0U : algorithms == 1U ? 0U :
        algorithms == 2U ? 1U :
        algorithms == 4U ? 2U : algorithms == 8U ? 3U : 255U;
    block_bits = sb[0x0CU];
    if (block_bits < 9U || block_bits > 16U || sb[0x0DU] != 0U ||
        (compat & ~UINT32_C(3)) ||
        (v->incompat & ~(ER_CHUNKED_FLAG | UINT32_C(3) | UINT32_C(16))) ||
        (algorithms && v->algorithm == 255U) ||
        (!(v->incompat & 1U) ? algorithms != 0U : algorithms == 0U) ||
        (algorithms && v->algorithm != 0U && !(v->incompat & 2U)) ||
        sb[0x5AU] != 0U ||
        sb[0x68U] || xx_data_get_u16(sb + 0x6AU, 2, 0, false) || !er_zero(sb + 0x78U, 8U))
        return false;
    v->block_size = UINT32_C(1) << block_bits;
    if (v->incompat & 2U) {
        uint8_t cfg[16];
        if (!(v->incompat & 1U) ||
            !er_read(v, ER_SB_OFFSET + ER_SB_BYTES, cfg, sizeof(cfg), pd))
            return false;
        if (v->algorithm == 0U) {
            if (xx_data_get_u16(cfg, 2, 0, false) != 14U || xx_data_get_u16(cfg + 2U, 2, 0, false) == 0U ||
                xx_data_get_u16(cfg + 4U, 2, 0, false) < 2U || xx_data_get_u16(cfg + 4U, 2, 0, false) > 256U ||
                !er_zero(cfg + 6U, 10U)) return false;
        } else if (v->algorithm == 1U) {
            v->lzma_dict = xx_data_get_u32(cfg + 2U, 4, 0, false);
            if (xx_data_get_u16(cfg, 2, 0, false) != 14U || v->lzma_dict < 4096U ||
                v->lzma_dict > ER_MAX_Z_DECODE || xx_data_get_u16(cfg + 6U, 2, 0, false) != 0U ||
                !er_zero(cfg + 8U, 8U)) return false;
        } else if (v->algorithm == 2U) {
            if (xx_data_get_u16(cfg, 2, 0, false) != 6U || cfg[2U] != 15U ||
                !er_zero(cfg + 3U, 5U)) return false;
        } else if (v->algorithm == 3U) {
            if (xx_data_get_u16(cfg, 2, 0, false) != 6U || cfg[2U] != 0U || cfg[3U] > 14U ||
                !er_zero(cfg + 4U, 4U)) return false;
        } else return false;
    }
    v->root_nid = xx_data_get_u16(sb + 0x0EU, 2, 0, false);
    v->meta_blkaddr = xx_data_get_u32(sb + 0x28U, 4, 0, false);
    /* `blocks` is a statvfs field, not an address bound in the core spec.
     * An image may use data beyond it, so the source device bounds reads. */
    if ((uint64_t)v->meta_blkaddr * v->block_size >= v->bytes) return false;
    v->format_bytes = v->bytes;
    if (compat & 1U) {
        size_t span = v->block_size - (ER_SB_OFFSET % v->block_size);
        if (!er_range(v, ER_SB_OFFSET, span)) return false;
        checksum_block = (uint8_t *)xx_mem_alloc(span);
        if (!checksum_block || !er_read(v, ER_SB_OFFSET, checksum_block, span, pd))
            goto finish;
        declared_checksum = xx_data_get_u32(checksum_block + 4U, 4, 0, false);
        xx_mem_zero(checksum_block + 4U, 4U);
        if (er_crc32c(checksum_block, span) != declared_checksum) goto finish;
    }
    ok = true;
finish:
    xx_mem_free(checksum_block);
    return ok;
}
typedef struct er_zhead_s {
    uint64_t logical, physical, index;
    uint32_t packed_bytes;
    uint8_t type;
} er_zhead;

/* Compact indexes store either two 4-byte or sixteen 2-byte entries per
 * pack.  The final four bytes of each pack anchor its physical block run. */
static bool er_zindex(er_view *v, const er_inode *node, uint64_t index,
                      uint64_t count, uint64_t indexes, unsigned initial4,
                      uint64_t twos, unsigned lobits, bool big, uint8_t *type,
                      unsigned *ofs, uint64_t *physical,
                      uint64_t *after_pack, xx_pd_struct *pd) {
    uint64_t pack_start, position;
    unsigned perpack, entry_bytes, bits, rel, j, before = 0U;
    uint8_t bytes[32]; uint32_t word, anchor;
    if (index >= count) return false;
    if (node->layout == 1U) {
        if (index > (UINT64_MAX - indexes) / 8U ||
            !er_read(v, indexes + index * 8U, bytes, 8U, pd))
            return false;
        *type = (uint8_t)(xx_data_get_u16(bytes, 2, 0, false) & 3U);
        *ofs = *type == 2U ? xx_data_get_u16(bytes + 4U, 2, 0, false) : xx_data_get_u16(bytes + 2U, 2, 0, false);
        *physical = xx_data_get_u32(bytes + 4U, 4, 0, false);
        *after_pack = indexes + (index + 1U) * 8U;
        return true;
    }
    if (index < initial4) {
        pack_start = index / 2U * 2U;
        position = indexes + pack_start * 4U;
        perpack = 2U; entry_bytes = 4U;
    } else if (index < (uint64_t)initial4 + twos) {
        pack_start = initial4 + (index - initial4) / 16U * 16U;
        position = indexes + (uint64_t)initial4 * 4U +
            (pack_start - initial4) * 2U;
        perpack = 16U; entry_bytes = 2U;
    } else {
        pack_start = (uint64_t)initial4 + twos +
            (index - initial4 - twos) / 2U * 2U;
        position = indexes + (uint64_t)initial4 * 4U + twos * 2U +
            (pack_start - initial4 - twos) * 4U;
        perpack = 2U; entry_bytes = 4U;
    }
    if (!er_range(v, position, perpack * entry_bytes) ||
        !er_read(v, position, bytes, perpack * entry_bytes, pd)) return false;
    *after_pack = position + perpack * entry_bytes;
    rel = (unsigned)(index - pack_start);
    bits = (perpack * entry_bytes - 4U) * 8U / perpack;
    if (lobits + 2U > bits) return false;
    word = xx_data_get_u32(bytes + rel * bits / 8U, 4, 0, false);
    word >>= rel * bits % 8U;
    *ofs = word & ((1U << lobits) - 1U);
    *type = (uint8_t)((word >> lobits) & 3U);
    anchor = xx_data_get_u32(bytes + perpack * entry_bytes - 4U, 4, 0, false);
    for (j = 0U; j < rel; ++j) {
        uint32_t next;
        unsigned span = 1U;
        word = xx_data_get_u32(bytes + j * bits / 8U, 4, 0, false) >> (j * bits % 8U);
        if (((word >> lobits) & 3U) == 2U) {
            if (big && (word & (1U << 11U))) {
                uint32_t previous = j ?
                    xx_data_get_u32(bytes + (j - 1U) * bits / 8U, 4, 0, false) >>
                        ((j - 1U) * bits % 8U) : 0U;
                if (!j || ((previous >> lobits) & 3U) == 2U) {
                    span = word & ((1U << 11U) - 1U);
                    if (!span || span > 256U) return false;
                    before += span;
                }
            }
            continue;
        }
        if (big) {
            next = xx_data_get_u32(bytes + (j + 1U) * bits / 8U, 4, 0, false) >>
                ((j + 1U) * bits % 8U);
            if (((next >> lobits) & 3U) == 2U &&
                (next & (1U << 11U))) {
                span = next & ((1U << 11U) - 1U);
                if (!span || span > 256U) return false;
            }
        }
        before += span;
    }
    *physical = (uint64_t)anchor + before + (big ? 0U : 1U);
    return true;
}

static bool er_zfinish(er_view *v, const er_zhead *head, uint64_t end,
                       uint64_t *maximum, uint64_t *max_packed,
                       bool decode, uint8_t *plain,
                       uint8_t *packed, xx_io_device *output,
                       xx_pd_struct *pd) {
    uint64_t length;
    size_t lead = 0U, produced = 0U, out_done = 0U;
    if (end <= head->logical) return false;
    length = end - head->logical;
    if (length > ER_MAX_Z_DECODE ||
        (head->type == 0U && length > v->block_size) ||
        !head->packed_bytes || head->packed_bytes > 1024U * 1024U ||
        !er_range(v, head->physical, head->packed_bytes)) return false;
    if (length > *maximum) *maximum = length;
    if (head->packed_bytes > *max_packed) *max_packed = head->packed_bytes;
    if (!decode) return true;
    if (!plain || !er_work(v, pd)) return false;
    if (head->type == 0U) {
        if (!er_read(v, head->physical, plain, (size_t)length, pd))
            return false;
    } else {
        if (!packed || !er_read(v, head->physical, packed,
                                 head->packed_bytes, pd)) return false;
        while (lead < head->packed_bytes && !packed[lead]) ++lead;
        if (lead == head->packed_bytes) return false;
        if (v->algorithm == 0U) {
            if (!xx_lz4_decompress_block(packed + lead,
                                         head->packed_bytes - lead,
                                         plain, (size_t)length, &produced))
                return false;
        } else if (v->algorithm == 1U) {
            uint8_t props[5];
            unsigned lc, lp;
            props[0] = (uint8_t)~packed[lead];
            if (props[0] >= 225U) return false;
            lc = props[0] % 9U;
            lp = (props[0] / 9U) % 5U;
            if (lc + lp > 4U) return false;
            props[1] = (uint8_t)v->lzma_dict;
            props[2] = (uint8_t)(v->lzma_dict >> 8U);
            props[3] = (uint8_t)(v->lzma_dict >> 16U);
            props[4] = (uint8_t)(v->lzma_dict >> 24U);
            packed[lead] = 0U;
            if (!xx_lzma_decompress_memory(packed + lead,
                                            head->packed_bytes - lead,
                                            props, sizeof(props), (int64_t)length,
                                            plain, (size_t)length, &produced))
                return false;
        } else if (v->algorithm == 2U) {
            if (!xx_deflate_decompress_memory(packed + lead,
                                               head->packed_bytes - lead,
                                               plain, (size_t)length,
                                               &produced, false)) return false;
        } else if (v->algorithm == 3U) {
            if (!xx_zstd_decompress_memory(packed + lead,
                                            head->packed_bytes - lead,
                                            plain, (size_t)length, &produced))
                return false;
        } else return false;
        if (produced != length) return false;
    }
    while (output && out_done < length && !er_stopped(pd)) {
        ssize_t n = xx_io_write(output, plain + out_done,
                                (size_t)length - out_done);
        if (n <= 0 || (size_t)n > length - out_done) return false;
        out_done += (size_t)n;
    }
    return !er_stopped(pd);
}

static bool er_zscan(er_view *v, const er_inode *node, bool decode,
                     uint8_t *plain, uint8_t *packed,
                     xx_io_device *output, uint64_t *maximum,
                     uint64_t *max_packed,
                     xx_pd_struct *pd) {
    uint8_t header[8]; er_zhead prior = {0};
    uint64_t aligned, indexes, total, twos = 0U, i, last_pack_end = 0U;
    unsigned initial4, lobits, advise;
    unsigned tail_bytes;
    bool big, tail;
    bool have = false, sentinel = false;
    if (!node->size || node->size / v->block_size >= ER_MAX_CHUNKS ||
        node->offset > UINT64_MAX - node->inode_bytes - 15U)
        return false;
    aligned = (node->offset + node->inode_bytes + 7U) & ~UINT64_C(7);
    if (!er_read(v, aligned, header, sizeof(header), pd)) return false;
    advise = xx_data_get_u16(header + 4U, 2, 0, false);
    big = (advise & 6U) == 6U;
    tail = (advise & 8U) != 0U;
    tail_bytes = xx_data_get_u16(header + 2U, 2, 0, false);
    if (xx_data_get_u16(header, 2, 0, false) ||
        (tail ? (!tail_bytes || tail_bytes > v->block_size ||
                 !(v->incompat & 16U)) : tail_bytes != 0U) ||
        (node->layout == 1U ? (advise & ~2U) != 0U :
                              (advise & ~15U) != 0U) ||
        (node->layout != 1U && (advise & 6U) != 0U &&
         (advise & 6U) != 6U) ||
        (big && !(v->incompat & 2U)) ||
        header[6U] != v->algorithm || header[7U])
        return false; /* One supported algorithm, supported index/cluster flags. */
    total = (node->size + v->block_size - 1U) / v->block_size;
    if (total > ER_MAX_CHUNKS) return false;
    indexes = aligned + (node->layout == 1U ? 16U : 8U);
    initial4 = (unsigned)(((32U - indexes % 32U) / 4U) & 7U);
    if ((advise & 1U) && total > initial4)
        twos = (total - initial4) / 16U * 16U;
    lobits = 12U;
    while ((UINT32_C(1) << lobits) < v->block_size) ++lobits;
    *maximum = 0U;
    *max_packed = 0U;
    for (i = 0U; i < total; ++i) {
        uint8_t type; unsigned ofs; uint64_t pblk, logical, pack_end;
        if (!er_work(v, pd) ||
            !er_zindex(v, node, i, total, indexes, initial4, twos,
                       lobits, big, &type, &ofs, &pblk, &pack_end, pd))
            return false;
        if (i + 1U == total) last_pack_end = pack_end;
        if (type == 2U) {
            if (!have || !ofs || sentinel) return false;
            if (big && (ofs & (1U << 11U))) {
                unsigned blocks = ofs & ((1U << 11U) - 1U);
                if (i != prior.index + 1U || !blocks || blocks > 256U)
                    return false;
                prior.packed_bytes = blocks * v->block_size;
            }
            continue;
        }
        if (type > 1U || ofs >= v->block_size) return false;
        logical = i * (uint64_t)v->block_size + ofs;
        if (logical > node->size || (i && logical == 0U)) return false;
        if (tail && logical == node->size && have) {
            prior.physical = pack_end;
            prior.packed_bytes = tail_bytes;
        }
        if (have && !er_zfinish(v, &prior, logical, maximum, max_packed,
                                decode,
                                plain, packed, output, pd)) return false;
        if (!have && i && !sentinel) return false;
        if (logical == node->size) {
            if (i + 1U != total) return false;
            have = false; sentinel = true;
        } else {
            if (sentinel) return false;
            prior.logical = logical;
            prior.physical = pblk * v->block_size;
            prior.index = i;
            prior.packed_bytes = v->block_size;
            prior.type = type;
            have = true;
        }
    }
    if (have && tail) {
        prior.physical = last_pack_end;
        prior.packed_bytes = tail_bytes;
    }
    if (have && !er_zfinish(v, &prior, node->size, maximum, max_packed,
                            decode,
                            plain, packed, output, pd)) return false;
    return (have || sentinel) && !er_stopped(pd);
}
static bool er_inode_read(er_view *v, uint64_t nid, er_inode *out,
                          xx_pd_struct *pd) {
    uint8_t data[64];
    uint64_t offset, whole, tail, map_entries, map_bytes;
    uint16_t format, mode, xattrs;
    uint32_t info;
    uint8_t layout, inode_bytes;
    if (!out || nid > (UINT64_MAX - (uint64_t)v->meta_blkaddr * v->block_size) / 32U)
        return false;
    offset = (uint64_t)v->meta_blkaddr * v->block_size + nid * 32U;
    if (!er_range(v, offset, 32U) || !er_read(v, offset, data, 32U, pd)) return false;
    format = xx_data_get_u16(data, 2, 0, false);
    if (format & 0xFFF0U) return false;
    layout = (uint8_t)((format >> 1U) & 7U);
    inode_bytes = (format & 1U) ? 64U : 32U;
    if (layout > 4U)
        return false;
    if (inode_bytes == 64U &&
        (!er_range(v, offset, 64U) || !er_read(v, offset, data, 64U, pd)))
        return false;
    xattrs = xx_data_get_u16(data + 2U, 2, 0, false);
    mode = xx_data_get_u16(data + 4U, 2, 0, false);
    if (xattrs || ((mode & 0xF000U) != 0x4000U &&
                   (mode & 0xF000U) != 0x8000U)) return false;
    if (inode_bytes == 32U) {
        if (!er_zero(data + 0x1CU, 4U) || !xx_data_get_u16(data + 6U, 2, 0, false)) return false;
    } else {
        if (!er_zero(data + 6U, 2U) || !er_zero(data + 0x30U, 16U) ||
            !xx_data_get_u32(data + 0x2CU, 4, 0, false)) return false;
    }
    xx_mem_zero(out, sizeof(*out));
    out->nid = nid; out->offset = offset;
    out->size = inode_bytes == 32U ? xx_data_get_u32(data + 8U, 4, 0, false) : xx_data_get_u64(data + 8U, 8, 0, false);
    out->startblk = xx_data_get_u32(data + 0x10U, 4, 0, false);
    out->chunk_info = out->startblk;
    out->inode_bytes = inode_bytes; out->layout = layout;
    out->directory = (mode & 0xF000U) == 0x4000U;
    if (out->directory && (out->size > ER_MAX_DIR_BYTES ||
                           layout == 1U || layout == 3U || layout == 4U))
        return false;
    if (layout == 1U || layout == 3U) {
        if (!(v->incompat & 1U) ||
            !er_zscan(v, out, false, NULL, NULL, NULL,
                      &out->z_max_decoded, &out->z_max_packed, pd))
            return false;
    } else if (layout == 4U) {
        uint32_t chunk_bits = info = out->chunk_info;
        if (!(v->incompat & ER_CHUNKED_FLAG) || (info & ~UINT32_C(31)) ||
            chunk_bits > 16U) return false;
        map_entries = out->size / ((uint64_t)v->block_size << chunk_bits) +
            (out->size % ((uint64_t)v->block_size << chunk_bits) != 0U);
        if (map_entries > ER_MAX_CHUNKS) return false;
        map_bytes = map_entries * 4U;
        if (!er_range(v, offset + inode_bytes, map_bytes)) return false;
        /* Validate every address before exposing a record; shared chunks are
         * legitimate and do not imply conflicting ownership. */
        {
            uint64_t i;
            for (i = 0U; i < map_entries; ++i) {
                uint8_t raw[4];
                uint64_t remaining, chunk_size = (uint64_t)v->block_size << chunk_bits;
                uint32_t block;
                if (!er_read(v, offset + inode_bytes + i * 4U, raw, 4U, pd))
                    return false;
                block = xx_data_get_u32(raw, 4, 0, false);
                remaining = out->size - i * chunk_size;
                if (remaining > chunk_size) remaining = chunk_size;
                if (block != UINT32_MAX &&
                    !er_range(v, (uint64_t)block * v->block_size, remaining))
                    return false;
            }
        }
    } else {
        whole = layout == 2U ? out->size / v->block_size * v->block_size : out->size;
        tail = layout == 2U ? out->size - whole : 0U;
        if (whole && !er_range(v, (uint64_t)out->startblk * v->block_size, whole))
            return false;
        if (tail && (offset + inode_bytes) % v->block_size + tail > v->block_size)
            return false;
        if (tail && !er_range(v, offset + inode_bytes, tail)) return false;
    }
    return true;
}
static bool er_logical(er_view *v, const er_inode *node, uint64_t offset,
                       void *out, size_t size, xx_pd_struct *pd) {
    size_t done = 0U;
    if (offset > node->size || size > node->size - offset) return false;
    while (done < size) {
        uint64_t logical = offset + done, physical, available;
        size_t part;
        if (!er_work(v, pd)) return false;
        if (node->layout == 0U) {
            physical = (uint64_t)node->startblk * v->block_size + logical;
            available = node->size - logical;
        } else if (node->layout == 2U) {
            uint64_t whole = node->size / v->block_size * v->block_size;
            if (logical < whole) {
                physical = (uint64_t)node->startblk * v->block_size + logical;
                available = whole - logical;
            } else {
                physical = node->offset + node->inode_bytes + logical - whole;
                available = node->size - logical;
            }
        } else {
            uint64_t chunk_size = (uint64_t)v->block_size << node->chunk_info;
            uint64_t index = logical / chunk_size, within = logical % chunk_size;
            uint8_t raw[4]; uint32_t block;
            if (!er_read(v, node->offset + node->inode_bytes + index * 4U,
                         raw, 4U, pd)) return false;
            block = xx_data_get_u32(raw, 4, 0, false);
            available = chunk_size - within;
            if (block == UINT32_MAX) {
                part = (size_t)(available < size - done ? available : size - done);
                xx_mem_zero((uint8_t *)out + done, part);
                done += part;
                continue;
            }
            physical = (uint64_t)block * v->block_size + within;
        }
        part = (size_t)(available < size - done ? available : size - done);
        if (!er_read(v, physical, (uint8_t *)out + done, part, pd)) return false;
        done += part;
    }
    return !er_stopped(pd);
}
static int er_compare(const uint8_t *a, size_t an, const uint8_t *b, size_t bn) {
    size_t i, n = an < bn ? an : bn;
    for (i = 0U; i < n; ++i) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return an == bn ? 0 : (an < bn ? -1 : 1);
}
static bool er_equal_fold(const char *a, const char *b) {
    while (*a && *b) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false;
    }
    return !*a && !*b;
}
static bool er_device_name(const char *name) {
    static const char *const reserved[] = {
        "CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"
    };
    char stem[32]; size_t i = 0U, j;
    while (name[i] && name[i] != '.' && i + 1U < sizeof(stem)) {
        stem[i] = name[i]; ++i;
    }
    stem[i] = 0;
    if (name[i] && name[i] != '.') return false;
    for (j = 0U; j < sizeof(reserved) / sizeof(reserved[0]); ++j)
        if (er_equal_fold(stem, reserved[j])) return true;
    return i == 4U && stem[3] >= '0' && stem[3] <= '9' &&
        (((stem[0] == 'C' || stem[0] == 'c') &&
          (stem[1] == 'O' || stem[1] == 'o') &&
          (stem[2] == 'M' || stem[2] == 'm')) ||
         ((stem[0] == 'L' || stem[0] == 'l') &&
          (stem[1] == 'P' || stem[1] == 'p') &&
          (stem[2] == 'T' || stem[2] == 't')));
}
/* Encode bytes that could be interpreted as a host separator or reserved
 * punctuation. Encoding '%' itself keeps the mapping injective. */
static bool er_safe_leaf(const uint8_t *raw, size_t length, char leaf[768]) {
    static const char hex[] = "0123456789ABCDEF";
    size_t i, used = 0U;
    if (!length || length > 255U) return false;
    for (i = 0U; i < length; ++i) {
        unsigned c = raw[i];
        bool plain = c >= 0x21U && c <= 0x7EU && c != '%' &&
            c != '/' && c != '\\' && c != ':' && c != '<' && c != '>' &&
            c != '"' && c != '|' && c != '?' && c != '*' &&
            !(c == '.' && i + 1U == length);
        if (c == 0U || c == '/') return false;
        if (plain) leaf[used++] = (char)c;
        else { leaf[used++] = '%'; leaf[used++] = hex[c >> 4U]; leaf[used++] = hex[c & 15U]; }
    }
    leaf[used] = 0;
    if (er_device_name(leaf)) {
        if (used + 3U >= 768U) return false;
        for (i = used + 1U; i; --i) leaf[i + 2U] = leaf[i - 1U];
        leaf[0] = '%'; leaf[1] = '0'; leaf[2] = '0';
    }
    return true;
}
static char *er_unique_path(er_view *v, const char *prefix,
                            const char *leaf, xx_pd_struct *pd) {
    unsigned suffix;
    for (suffix = 0U; suffix < ER_MAX_MEMBERS; ++suffix) {
        char name[800], *candidate;
        size_t n, i;
        bool used = false;
        if (!er_work(v, pd)) return NULL;
        if (suffix) xx_rt_snprintf(name, sizeof(name), "%s~%u", leaf, suffix + 1U);
        else xx_rt_snprintf(name, sizeof(name), "%s", leaf);
        n = xx_str_len(prefix) + (prefix[0] ? 1U : 0U) + xx_str_len(name) + 1U;
        if (n > ER_MAX_PATH || n > ER_MAX_PATH_BYTES - v->path_bytes) return NULL;
        candidate = prefix[0] ? xx_str_concat3(prefix, "/", name) : xx_str_dup(name);
        if (!candidate) return NULL;
        for (i = 0U; i < v->count; ++i) {
            if (!er_work(v, pd)) { xx_str_free(candidate); return NULL; }
            if (er_equal_fold(candidate, v->members[i].path)) { used = true; break; }
        }
        if (!used) { v->path_bytes += n; return candidate; }
        xx_str_free(candidate);
    }
    return NULL;
}
static bool er_append(er_view *v, char *path, const er_inode *inode) {
    er_member *grown;
    size_t next;
    if (v->count >= ER_MAX_MEMBERS) return false;
    if (v->count == v->capacity) {
        next = v->capacity ? v->capacity * 2U : 32U;
        if (next > ER_MAX_MEMBERS) next = ER_MAX_MEMBERS;
        grown = (er_member *)xx_mem_realloc(v->members, next * sizeof(*grown));
        if (!grown) return false;
        v->members = grown; v->capacity = next;
    }
    v->members[v->count].path = path;
    v->members[v->count].inode = *inode;
    ++v->count;
    return true;
}
static bool er_seen_directory(er_view *v, uint64_t nid, xx_pd_struct *pd) {
    size_t i;
    for (i = 0U; i < v->dir_count; ++i) {
        if (!er_work(v, pd)) return true;
        if (v->directories[i] == nid) return true;
    }
    return false;
}
static bool er_mark_directory(er_view *v, uint64_t nid, xx_pd_struct *pd) {
    uint64_t *grown; size_t next;
    if (er_seen_directory(v, nid, pd) || v->dir_count >= ER_MAX_DIRS) return false;
    if (v->dir_count == v->dir_capacity) {
        next = v->dir_capacity ? v->dir_capacity * 2U : 32U;
        if (next > ER_MAX_DIRS) next = ER_MAX_DIRS;
        grown = (uint64_t *)xx_mem_realloc(v->directories, next * sizeof(*grown));
        if (!grown) return false;
        v->directories = grown; v->dir_capacity = next;
    }
    v->directories[v->dir_count++] = nid;
    return true;
}
static bool er_walk(er_view *v, uint64_t nid, uint64_t parent,
                    const char *prefix, unsigned depth, xx_pd_struct *pd) {
    er_inode directory;
    uint8_t *block = NULL, previous[256];
    size_t previous_length = 0U;
    uint64_t position;
    bool dot = false, dotdot = false, ok = false;
    if (depth > ER_MAX_DEPTH || !er_work(v, pd) ||
        !er_mark_directory(v, nid, pd) || !er_inode_read(v, nid, &directory, pd) ||
        !directory.directory || !directory.size) return false;
    block = (uint8_t *)xx_mem_alloc(v->block_size);
    if (!block) return false;
    for (position = 0U; position < directory.size; position += v->block_size) {
        size_t span = (size_t)(directory.size - position);
        uint16_t first_offset;
        size_t entries, j;
        if (span > v->block_size) span = v->block_size;
        if (span < 12U || !er_logical(v, &directory, position, block, span, pd))
            goto finish;
        first_offset = xx_data_get_u16(block + 8U, 2, 0, false);
        if (!first_offset || first_offset % 12U || first_offset > span)
            goto finish;
        entries = first_offset / 12U;
        for (j = 0U; j < entries; ++j) {
            uint8_t *entry = block + j * 12U;
            uint16_t name_offset = xx_data_get_u16(entry + 8U, 2, 0, false);
            size_t end, length;
            uint64_t child_nid = xx_data_get_u64(entry, 8, 0, false);
            bool is_folder;
            er_inode child;
            char leaf[768], *path;
            if (!er_work(v, pd) || entry[11U] || name_offset < first_offset ||
                name_offset >= span || entry[10U] > 7U) goto finish;
            if (j + 1U < entries) {
                end = xx_data_get_u16(block + (j + 1U) * 12U + 8U, 2, 0, false);
                if (end <= name_offset || end > span) goto finish;
                length = end - name_offset;
                if (memchr(block + name_offset, 0, length)) goto finish;
            } else {
                size_t i;
                end = span;
                for (i = name_offset; i < span; ++i)
                    if (!block[i]) { end = i; break; }
                length = end - name_offset;
            }
            if (!length || length > 255U ||
                (previous_length && er_compare(previous, previous_length,
                     block + name_offset, length) >= 0)) goto finish;
            xx_mem_copy(previous, block + name_offset, length);
            previous_length = length;
            if (length == 1U && block[name_offset] == '.') {
                if (dot || child_nid != nid ||
                    (entry[10U] != 0U && entry[10U] != 2U)) goto finish;
                dot = true; continue;
            }
            if (length == 2U && block[name_offset] == '.' &&
                block[name_offset + 1U] == '.') {
                if (dotdot || child_nid != parent ||
                    (entry[10U] != 0U && entry[10U] != 2U))
                    goto finish;
                dotdot = true; continue;
            }
            if (!er_safe_leaf(block + name_offset, length, leaf) ||
                !er_inode_read(v, child_nid, &child, pd)) goto finish;
            is_folder = child.directory;
            if (entry[10U] > 2U ||
                (entry[10U] && (entry[10U] == 2U) != is_folder))
                goto finish;
            path = er_unique_path(v, prefix, leaf, pd);
            if (!path) goto finish;
            if (!er_append(v, path, &child)) { xx_str_free(path); goto finish; }
            if (is_folder && !er_walk(v, child_nid, nid, path, depth + 1U, pd))
                goto finish;
        }
    }
    ok = dot && dotdot && !er_stopped(pd);
finish:
    xx_mem_free(block);
    return ok;
}
static void er_view_free(void *ptr) {
    er_view *v = (er_view *)ptr; size_t i;
    if (!v) return;
    for (i = 0U; i < v->count; ++i) xx_str_free(v->members[i].path);
    xx_mem_free(v->members); xx_mem_free(v->directories); xx_mem_free(v);
}
static er_view *er_parse(Abstractformat *self, xx_pd_struct *pd) {
    er_view *v; int64_t total;
    if (!self || !self->device || self->base_address < 0 || er_stopped(pd) ||
        (total = xx_io_total_size(self->device)) < self->base_address)
        return NULL;
    v = (er_view *)xx_mem_alloc(sizeof(*v));
    if (!v) return NULL;
    xx_mem_zero(v, sizeof(*v));
    v->device = self->device; v->base = self->base_address;
    v->bytes = (uint64_t)(total - v->base);
    if (!er_super(v, pd) || !er_walk(v, v->root_nid, v->root_nid,
                                    "", 0U, pd)) {
        er_view_free(v); return NULL;
    }
    v->retained = sizeof(*v) + v->capacity * sizeof(er_member) +
        v->dir_capacity * sizeof(uint64_t) + v->path_bytes;
    return v;
}

static void er_vtable_destroy(Abstractformat *self) {
    xx_erofs_destroy((xx_erofs *)self);
}
void xx_erofs_init(xx_erofs *disk, xx_io_device *device, int64_t base) {
    if (!disk) return;
    xx_mem_zero(disk, sizeof(*disk));
    xx_format_init(&disk->format, device, base);
    disk->format.endian = XX_ENDIAN_LITTLE;
    disk->format.file_type = XX_FILE_TYPE_EROFS;
    disk->format.format_type = XX_TYPE_ARCHIVE;
    disk->format.is_archive = true;
    xx_format_set_mime_type(&disk->format, "application/x-erofs");
    xx_format_set_extension(&disk->format, "img");
    disk->format.check_is_valid = xx_erofs_check_is_valid;
    disk->format.handle_base_info = xx_erofs_handle_base_info;
    disk->format.get_format_size = xx_erofs_get_format_size;
    disk->format.get_number_of_archive_records = xx_erofs_get_number_of_archive_records;
    disk->format.create_archive_records_reading = xx_erofs_create_archive_records_reading;
    disk->format.get_current_archive_record = xx_erofs_get_current_archive_record;
    disk->format.archive_record_move_to_next = xx_erofs_archive_record_move_to_next;
    disk->format.unpack_current_archive_record = xx_erofs_unpack_current_archive_record;
    disk->format.free_archive_records_reading = xx_erofs_free_archive_records_reading;
    disk->format.destroy = er_vtable_destroy;
}
xx_erofs *xx_erofs_create(xx_io_device *device, int64_t base) {
    xx_erofs *disk = (xx_erofs *)xx_mem_alloc(sizeof(*disk));
    if (disk) xx_erofs_init(disk, device, base);
    return disk;
}
void xx_erofs_destroy(xx_erofs *disk) {
    if (disk) xx_format_cleanup_extra_parameters(&disk->format);
}
void xx_erofs_free(xx_erofs *disk) {
    if (disk) { xx_erofs_destroy(disk); xx_mem_free(disk); }
}
bool xx_erofs_check_is_valid(Abstractformat *self, xx_pd_struct *pd) {
    er_view *v = er_parse(self, pd);
    bool valid = v != NULL;
    er_view_free(v);
    return valid;
}
bool xx_erofs_handle_base_info(Abstractformat *self, xx_pd_struct *pd) {
    xx_erofs *disk = (xx_erofs *)self;
    er_view *v = er_parse(self, pd);
    int64_t end, total;
    if (!self) return false;
    if (!v) { self->is_valid = false; self->base_info_handled = false; return false; }
    disk->block_size = v->block_size;
    disk->number_of_records = v->count;
    self->format_size = (int64_t)v->format_bytes;
    self->number_of_archive_records = v->count;
    total = xx_io_total_size(self->device);
    end = self->base_address + self->format_size;
    self->overlay_offset = total > end ? end : -1;
    self->overlay_size = total > end ? total - end : 0;
    self->is_valid = true; self->base_info_handled = true;
    er_view_free(v);
    return true;
}
int64_t xx_erofs_get_format_size(Abstractformat *self, xx_pd_struct *pd) {
    return xx_erofs_handle_base_info(self, pd) ? self->format_size : -1;
}
uint64_t xx_erofs_get_number_of_archive_records(Abstractformat *self,
                                                  xx_pd_struct *pd) {
    return xx_erofs_handle_base_info(self, pd)
        ? ((xx_erofs *)self)->number_of_records : 0U;
}
static bool er_record(xx_archive_record *record, const er_view *v,
                      const er_member *m) {
    const er_inode *n = &m->inode;
    uint64_t data = n->layout == 4U ? n->offset + n->inode_bytes :
        (n->layout == 2U && n->size < v->block_size ?
            n->offset + n->inode_bytes : (uint64_t)n->startblk * v->block_size);
    xx_archive_record_cleanup(record); xx_archive_record_init(record);
    record->header_offset = v->base + (int64_t)n->offset;
    record->header_size = n->inode_bytes;
    record->data_offset = n->size ? v->base + (int64_t)data : -1;
    record->compressed_size = n->directory ? 0U : n->size;
    return xx_archive_record_set_original_name(record, m->path) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_UNCOMPRESSED_SIZE,
                                       n->directory ? 0U : n->size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSED_SIZE,
                                       n->directory ? 0U : n->size) &&
        xx_archive_record_set_meta_u64(record, XX_META_ID_COMPRESSION_METHOD, 0U) &&
        xx_archive_record_set_meta_bool(record, XX_META_ID_IS_FOLDER,
                                        n->directory);
}
static bool er_copy_options(xx_list_s *target, const xx_list_s *source) {
    size_t i;
    if (!source) return true;
    for (i = 0U; i < source->count; ++i) {
        const xx_meta *item = (const xx_meta *)xx_list_at(source, i);
        xx_meta copy;
        if (!item) continue;
        xx_meta_init(&copy, item->meta_id);
        if (!xx_var_copy(&copy.var, &item->var) ||
            !xx_list_append(target, &copy)) {
            xx_meta_cleanup(&copy); return false;
        }
    }
    return true;
}
xx_archive_record_state *xx_erofs_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd) {
    er_view *v = er_parse(self, pd);
    xx_archive_record_state *state;
    if (!v) return NULL;
    state = (xx_archive_record_state *)xx_mem_alloc(sizeof(*state));
    if (!state) { er_view_free(v); return NULL; }
    xx_archive_record_state_init(state, self);
    state->internal_state = v; state->free_internal = er_view_free;
    state->total_records = (int64_t)v->count;
    if (!er_copy_options(&state->options, options) ||
        (v->count && !er_record(&state->current_record, v, v->members))) {
        xx_archive_record_state_free(state); return NULL;
    }
    state->has_record = v->count != 0U;
    state->current_index = v->count ? 0 : -1;
    return state;
}
const xx_archive_record *xx_erofs_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state) {
    return self && state && state->format == self && state->has_record
        ? &state->current_record : NULL;
}
bool xx_erofs_archive_record_move_to_next(Abstractformat *self,
    xx_archive_record_state *state, xx_pd_struct *pd) {
    er_view *v;
    if (!self || !state || state->format != self || !state->has_record ||
        !(v = (er_view *)state->internal_state) || er_stopped(pd)) return false;
    if (v->index + 1U >= v->count) {
        v->index = v->count; state->has_record = false;
        state->current_index = -1;
        xx_archive_record_cleanup(&state->current_record);
        xx_archive_record_init(&state->current_record);
        return false;
    }
    if (!er_record(&state->current_record, v, v->members + v->index + 1U)) {
        state->has_record = false; return false;
    }
    ++v->index; ++state->current_index;
    return true;
}
static uint64_t er_limit(Abstractformat *self, const xx_list_s *options,
                         uint32_t id, uint64_t fallback) {
    const xx_var *value = xx_format_resolve_extra_parameter(self, options, id);
    if (!value) return fallback;
    switch (value->type) {
    case XX_VAR_TYPE_UINT8: case XX_VAR_TYPE_UINT16:
    case XX_VAR_TYPE_UINT32: case XX_VAR_TYPE_UINT64:
        return xx_var_get_u64(value);
    case XX_VAR_TYPE_INT8: case XX_VAR_TYPE_INT16:
    case XX_VAR_TYPE_INT32: case XX_VAR_TYPE_INT64: {
        int64_t n = xx_var_get_i64(value);
        return n < 0 ? fallback : (uint64_t)n;
    }
    default: return fallback;
    }
}
static bool er_limits(Abstractformat *self, xx_archive_record_state *state,
                      const er_view *v, const er_member *m) {
    uint64_t copy = m->inode.directory || !m->inode.size ? 0U :
        (m->inode.size < v->block_size ? m->inode.size : v->block_size);
    if (m->inode.layout == 1U || m->inode.layout == 3U) {
        uint64_t codec = 0U;
        copy = m->inode.z_max_decoded + m->inode.z_max_packed;
        if (v->algorithm == 1U) {
            uint64_t io_buffer = xx_get_file_buffer_size();
            if (io_buffer > (UINT64_MAX - v->lzma_dict - 65536U) / 2U)
                return false;
            codec = v->lzma_dict + 2U * io_buffer + 65536U;
        } else if (v->algorithm == 2U) codec = 32768U;
        else if (v->algorithm == 3U) codec = 131072U;
        if (codec > UINT64_MAX - copy) return false;
        copy += codec;
    }
    uint64_t baseline = v->retained + sizeof(*state);
    return (m->inode.directory ||
        m->inode.size <= er_limit(self, &state->options,
                                 XX_META_ID_OPT_MAX_MEMBER_SIZE, UINT64_MAX)) &&
        baseline <= er_limit(self, &state->options,
                            XX_META_ID_OPT_MEMORY_LIMIT, UINT64_MAX) &&
        copy <= er_limit(self, &state->options,
                         XX_META_ID_OPT_MEMORY_LIMIT, UINT64_MAX) - baseline;
}
bool xx_erofs_extract_record_to_device(Abstractformat *self,
    xx_archive_record_state *state, xx_io_device *destination, xx_pd_struct *pd) {
    er_view *v; const er_member *m; uint8_t *buffer = NULL;
    uint64_t done = 0U;
    bool ok = false;
    if (!self || !self->device || destination == self->device || !state ||
        state->format != self || !state->has_record ||
        !(v = (er_view *)state->internal_state) || v->index >= v->count ||
        er_stopped(pd)) return false;
    m = v->members + v->index;
    if (!er_limits(self, state, v, m) || m->inode.directory) return false;
    if (m->inode.layout == 1U || m->inode.layout == 3U) {
        uint8_t *plain = (uint8_t *)xx_mem_alloc((size_t)m->inode.z_max_decoded);
        uint8_t *packed = (uint8_t *)xx_mem_alloc((size_t)m->inode.z_max_packed);
        uint64_t maximum = 0U, max_packed = 0U;
        if (!plain || !packed) {
            xx_mem_free(plain); xx_mem_free(packed); return false;
        }
        ok = er_zscan(v, &m->inode, true, plain, packed,
                      destination, &maximum, &max_packed, pd) &&
            maximum == m->inode.z_max_decoded &&
            max_packed == m->inode.z_max_packed && !er_stopped(pd);
        xx_mem_free(plain); xx_mem_free(packed);
        return ok;
    }
    if (m->inode.size) {
        size_t capacity = (size_t)(m->inode.size < v->block_size ?
                                    m->inode.size : v->block_size);
        buffer = (uint8_t *)xx_mem_alloc(capacity);
        if (!buffer) return false;
        while (done < m->inode.size) {
            size_t part = (size_t)(m->inode.size - done < capacity ?
                                   m->inode.size - done : capacity);
            size_t written = 0U;
            if (!er_logical(v, &m->inode, done, buffer, part, pd)) goto finish;
            while (destination && written < part && !er_stopped(pd)) {
                ssize_t n = xx_io_write(destination, buffer + written,
                                        part - written);
                if (n <= 0 || (size_t)n > part - written) goto finish;
                written += (size_t)n;
            }
            if (er_stopped(pd)) goto finish;
            done += part;
        }
    }
    ok = !er_stopped(pd) && done == m->inode.size;
finish:
    xx_mem_free(buffer);
    return ok;
}
static xx_io_device *er_stage(const char *destination, char **stage_path) {
    char *directory = xx_str_dup(destination);
    size_t i, parent = 0U;
    unsigned attempt;
    *stage_path = NULL;
    if (!directory) return NULL;
    for (i = 0U; directory[i]; ++i)
        if (directory[i] == '/' || directory[i] == '\\') parent = i + 1U;
    directory[parent] = 0;
    for (attempt = 0U; attempt < 128U; ++attempt) {
        char suffix[48], *candidate;
        xx_io_device *output;
        xx_rt_snprintf(suffix, sizeof(suffix), ".xx_erofs.tmp.%u", attempt);
        candidate = xx_str_concat(directory, suffix);
        if (!candidate) break;
        if (er_equal_fold(candidate, destination)) {
            xx_str_free(candidate); continue;
        }
        output = xx_io_file_open(candidate, "wbx");
        if (output) {
            *stage_path = candidate;
            xx_str_free(directory);
            return output;
        }
        xx_str_free(candidate);
    }
    xx_str_free(directory);
    return NULL;
}
bool xx_erofs_unpack_current_archive_record(Abstractformat *self,
    xx_archive_record_state *state, xx_pd_struct *pd) {
    er_view *v; const er_member *m;
    const xx_var *option, *overwrite_option;
    const char *base = NULL;
    char *owned = NULL, *path = NULL, *stage_path = NULL;
    bool overwrite, ok = false;
    if (!self || !state || state->format != self || !state->has_record ||
        !(v = (er_view *)state->internal_state) || v->index >= v->count ||
        er_stopped(pd)) return false;
    m = v->members + v->index;
    if (!er_limits(self, state, v, m)) return false;
    option = xx_format_resolve_extra_parameter(self, &state->options,
                                                XX_META_ID_OPT_UNPACK_PATH);
    overwrite_option = xx_format_resolve_extra_parameter(self, &state->options,
                                                   XX_META_ID_OPT_OVERWRITE);
    overwrite = overwrite_option && xx_var_get_bool(overwrite_option);
    if (!option) return m->inode.directory ||
        xx_erofs_extract_record_to_device(self, state, NULL, pd);
    if (option->type == XX_VAR_TYPE_STRING ||
        option->type == XX_VAR_TYPE_STRING_VIEW) base = xx_var_get_str(option);
    else if (option->type == XX_VAR_TYPE_WSTRING ||
             option->type == XX_VAR_TYPE_WSTRING_VIEW) {
        owned = xx_str_unicode_to_utf8(xx_var_get_wstr(option));
        base = owned;
    }
    if (!base) goto finish;
    path = (*base && base[xx_str_len(base) - 1U] != '/' &&
            base[xx_str_len(base) - 1U] != '\\')
        ? xx_str_concat3(base, "/", m->path) : xx_str_concat(base, m->path);
    if (!path) goto finish;
    if (m->inode.directory) {
        ok = !er_stopped(pd) && xx_store_create_dirs_a(path, true);
        goto finish;
    }
    if ((!overwrite && xx_io_file_exists_a(path)) ||
        !xx_store_create_dirs_a(path, false)) goto finish;
    {
        xx_io_device *output = er_stage(path, &stage_path);
        if (!output) goto finish;
        ok = xx_erofs_extract_record_to_device(self, state, output, pd);
        if (xx_io_close(output)) ok = false;
    }
    if (er_stopped(pd)) ok = false;
    if (ok) ok = xx_io_file_replace_a(stage_path, path, overwrite);
finish:
    if (!ok && stage_path) xx_io_file_remove_a(stage_path);
    xx_str_free(stage_path); xx_str_free(path); xx_str_free(owned);
    return ok;
}
void xx_erofs_free_archive_records_reading(Abstractformat *self,
    xx_archive_record_state *state) {
    (void)self;
    xx_archive_record_state_free(state);
}
