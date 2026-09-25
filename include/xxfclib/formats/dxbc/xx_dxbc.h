/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_dxbc.h @brief Direct3D shader container ("DXBC") reader. */

/* DXBC is the container fxc (D3DCompile) writes for every Direct3D 10/11/12
 * compiled shader object (.cso), and the same container dxc writes around
 * DXIL.  It is a small header, a table of chunk offsets and a run of
 * FourCC-tagged chunks:
 *
 *   header (32 bytes, little endian)
 *     +0x00  "DXBC"
 *     +0x04  16-byte checksum: MD5's compression function run over bytes
 *            0x14 .. total_size with a non-standard final block (see
 *            xx_dxbc.c); all zero in an unsigned DXIL container
 *     +0x14  u32  1 (DXIL spells it as u16 major 1, u16 minor 0 -- the same
 *            four bytes)
 *     +0x18  u32  total size of the container, header included
 *     +0x1C  u32  chunk count
 *     +0x20  u32  chunk offsets[count], from the start of the container
 *   chunk
 *     +0x00  FourCC  RDEF ISGN OSGN OSG5 PCSG SHDR SHEX STAT SFI0 Aon9 SPDB
 *                    RTS0 LIBF LIBH FX10 ISG1 OSG1 PSV0 HASH ILDB ILDN DXIL
 *     +0x04  u32  data size, header excluded
 *     +0x08  data
 *
 * Source: binwalk's src/signatures/dxbc.rs (magic and result.size),
 * src/structures/dxbc.rs (the header, the one == 1 test, the 32-chunk cap and
 * the chunk-id lookups) and src/extractors/dxbc.rs (carves total_size bytes
 * as shader.dxbc).  Header layout per Tim Jones, "Parsing Direct3D shader
 * bytecode" (2015), and checked against fxc/dxc output from the 10.0.26100
 * Windows SDK.
 *
 * Not an archive: binwalk's extractor carves the container itself, so the
 * reader validates it and reports total_size as the format size.  The
 * checksum is computed and reported (xx_dxbc_get_checksum_state) but is not
 * part of validation -- binwalk never checks it, and an unsigned DXIL
 * container carries sixteen zero bytes there.
 */

#ifndef XXFCLIB_FORMAT_DXBC_H
#define XXFCLIB_FORMAT_DXBC_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_DXBC_MAGIC "DXBC"
#define XX_DXBC_MAGIC_SIZE 4U
#define XX_DXBC_CHECKSUM_OFFSET 0x04U
#define XX_DXBC_CHECKSUM_SIZE 16U
#define XX_DXBC_ONE_OFFSET 0x14U
#define XX_DXBC_TOTAL_SIZE_OFFSET 0x18U
#define XX_DXBC_CHUNK_COUNT_OFFSET 0x1CU
#define XX_DXBC_HEADER_SIZE 0x20U
#define XX_DXBC_CHUNK_HEADER_SIZE 8U
/** binwalk's cap (structures/dxbc.rs): "at least 14 known chunks, but most
 *  likely no more than 32". */
#define XX_DXBC_MAX_CHUNKS 32U
/** Containers larger than this are validated and sized but their checksum is
 *  left unchecked, so a measure of a hostile multi-gigabyte candidate never
 *  reads the whole thing.  Real shaders, debug info included, are far below. */
#define XX_DXBC_CHECKSUM_MAX_SIZE UINT32_C(0x04000000)

/** FourCCs as the little endian u32 of their four ASCII bytes. */
#define XX_DXBC_FOURCC(a, b, c, d)                                         \
    ((uint32_t)(uint8_t)(a) | ((uint32_t)(uint8_t)(b) << 8U) |             \
     ((uint32_t)(uint8_t)(c) << 16U) | ((uint32_t)(uint8_t)(d) << 24U))
#define XX_DXBC_CHUNK_SHDR XX_DXBC_FOURCC('S', 'H', 'D', 'R')
#define XX_DXBC_CHUNK_SHEX XX_DXBC_FOURCC('S', 'H', 'E', 'X')
#define XX_DXBC_CHUNK_DXIL XX_DXBC_FOURCC('D', 'X', 'I', 'L')

/** Result of recomputing the container checksum. */
typedef enum xx_dxbc_checksum_state_e {
    XX_DXBC_CHECKSUM_NOT_CHECKED = 0, /**< not computed (or container too big) */
    XX_DXBC_CHECKSUM_VALID,           /**< stored value matches */
    XX_DXBC_CHECKSUM_ZERO,            /**< stored value is all zero (unsigned DXIL) */
    XX_DXBC_CHECKSUM_MISMATCH         /**< stored value is something else */
} xx_dxbc_checksum_state_t;

/** Program type from the version token of SHDR / SHEX / DXIL. */
typedef enum xx_dxbc_program_type_e {
    XX_DXBC_PROGRAM_PIXEL = 0,
    XX_DXBC_PROGRAM_VERTEX = 1,
    XX_DXBC_PROGRAM_GEOMETRY = 2,
    XX_DXBC_PROGRAM_HULL = 3,
    XX_DXBC_PROGRAM_DOMAIN = 4,
    XX_DXBC_PROGRAM_COMPUTE = 5,
    XX_DXBC_PROGRAM_NONE = 0xFFFF /**< no SHDR / SHEX / DXIL version token */
} xx_dxbc_program_type_t;

typedef struct xx_dxbc xx_dxbc;
typedef struct xx_dxbc xx_dxbc_t;
typedef struct xx_dxbc XDxbc;

struct xx_dxbc {
    Abstractformat format;
    uint32_t total_size;   /**< header field at 0x18; the format size */
    uint32_t chunk_count;  /**< header field at 0x1C, 1..32 */
    uint32_t chunk_ids[XX_DXBC_MAX_CHUNKS];     /**< FourCC, little endian */
    uint32_t chunk_offsets[XX_DXBC_MAX_CHUNKS]; /**< from the container start */
    uint32_t chunk_sizes[XX_DXBC_MAX_CHUNKS];   /**< data size, header excluded */
    uint8_t checksum[XX_DXBC_CHECKSUM_SIZE];    /**< stored checksum bytes */
    xx_dxbc_checksum_state_t checksum_state;
    uint32_t program_chunk_id; /**< SHDR, SHEX or DXIL, 0 if none */
    uint32_t program_version;  /**< raw version token, 0 if none */
};

XXFC_API void xx_dxbc_init(xx_dxbc *dxbc, xx_io_device *dev,
                           int64_t base_address);
XXFC_API xx_dxbc *xx_dxbc_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_dxbc_destroy(xx_dxbc *dxbc);
XXFC_API void xx_dxbc_free(xx_dxbc *dxbc);

XXFC_API bool xx_dxbc_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_dxbc_handle_base_info(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API int64_t xx_dxbc_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);

XXFC_API uint32_t xx_dxbc_get_total_size(const xx_dxbc *dxbc);
XXFC_API uint32_t xx_dxbc_get_chunk_count(const xx_dxbc *dxbc);
/** FourCC of chunk @p index as a little endian u32, 0 when out of range. */
XXFC_API uint32_t xx_dxbc_get_chunk_id(const xx_dxbc *dxbc, uint32_t index);
XXFC_API uint32_t xx_dxbc_get_chunk_offset(const xx_dxbc *dxbc,
                                           uint32_t index);
XXFC_API uint32_t xx_dxbc_get_chunk_size(const xx_dxbc *dxbc, uint32_t index);
/** True when a chunk with FourCC @p chunk_id is present. */
XXFC_API bool xx_dxbc_has_chunk(const xx_dxbc *dxbc, uint32_t chunk_id);
XXFC_API xx_dxbc_checksum_state_t xx_dxbc_get_checksum_state(
    const xx_dxbc *dxbc);
/** Copies the 16 stored checksum bytes; false when @p out is too small. */
XXFC_API bool xx_dxbc_get_checksum(const xx_dxbc *dxbc, void *out,
                                   size_t out_size);
/** SHDR (shader model 4), SHEX (shader model 5) or DXIL (6+), else 0. */
XXFC_API uint32_t xx_dxbc_get_program_chunk_id(const xx_dxbc *dxbc);
XXFC_API xx_dxbc_program_type_t xx_dxbc_get_program_type(const xx_dxbc *dxbc);
XXFC_API uint32_t xx_dxbc_get_shader_model_major(const xx_dxbc *dxbc);
XXFC_API uint32_t xx_dxbc_get_shader_model_minor(const xx_dxbc *dxbc);

static inline Abstractformat *xx_dxbc_to_format(xx_dxbc *dxbc) {
    return dxbc ? &dxbc->format : NULL;
}
static inline void XDxbc_init(xx_dxbc *dxbc, xx_io_device *dev,
                              int64_t base_address) {
    xx_dxbc_init(dxbc, dev, base_address);
}
static inline xx_dxbc *XDxbc_create(xx_io_device *dev, int64_t base_address) {
    return xx_dxbc_create(dev, base_address);
}
static inline void XDxbc_free(xx_dxbc *dxbc) {
    xx_dxbc_free(dxbc);
}
static inline bool XDxbc_is_valid(xx_dxbc *dxbc, xx_pd_struct *pd) {
    return dxbc ? xx_format_is_valid(&dxbc->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_DXBC_H */
