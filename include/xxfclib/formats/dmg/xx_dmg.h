/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_dmg.h @brief Apple UDIF disk image (DMG) reader. */

/* A UDIF image is read from the BACK.  The last 512 bytes of the file are the
 * "koly" trailer, and everything else is found through it:
 *
 *   koly trailer, 512 bytes, big endian
 *     +0    u32  "koly"
 *     +4    u32  version, always 4
 *     +8    u32  header length, always 512
 *     +12   u32  flags
 *     +16   u64  running data fork offset
 *     +24   u64  data fork offset
 *     +32   u64  data fork length
 *     +40   u64  resource fork offset
 *     +48   u64  resource fork length
 *     +56   u32  segment number
 *     +60   u32  segment count
 *     +64   16   segment id
 *     +80   136  data fork checksum descriptor
 *     +216  u64  XML property list offset
 *     +224  u64  XML property list length
 *     +296  u64  code signature offset
 *     +304  u64  code signature length
 *     +352  136  master checksum descriptor
 *     +488  u32  image variant
 *     +492  u64  sector count
 *
 * The property list holds `resource-fork` -> `blkx` -> an array of dicts, one
 * per partition, each carrying a display name and a base64 "mish" block table:
 *
 *   mish header, 204 bytes, big endian
 *     +0    u32  "mish"
 *     +4    u32  version, always 1
 *     +8    u64  first sector of this partition in the output image
 *     +16   u64  sectors this partition covers
 *     +24   u64  offset of this partition's data inside the data fork
 *     +32   u32  buffers needed
 *     +36   u32  descriptor blocks
 *     +64   136  checksum descriptor
 *     +200  u32  number of run descriptors that follow
 *
 *   run descriptor, 40 bytes, big endian
 *     +0    u32  type
 *     +8    u64  first sector, relative to the partition
 *     +16   u64  sectors this run covers
 *     +24   u64  offset of the run's bytes, relative to the partition's data
 *     +32   u64  length of the run's bytes
 *
 *   run types
 *     0x00000000  zero fill
 *     0x00000001  raw, stored verbatim
 *     0x00000002  ignore; expands to zeros just as zero fill does
 *     0x7FFFFFFE  comment; carries no sectors
 *     0x80000004  ADC
 *     0x80000005  zlib, RFC 1950 wrapped rather than raw Deflate
 *     0x80000006  bzip2
 *     0x80000007  LZFSE
 *     0x80000008  LZMA
 *     0xFFFFFFFF  terminator; must be the last descriptor
 *
 * Zero fill, ignore, raw, zlib, bzip2 and LZFSE runs are expanded.  ADC and
 * LZMA runs are refused: a run the decoder cannot read must never be written
 * out as fabricated zeros, because the result would look like a successful
 * extraction of the wrong image.
 *
 * The reader publishes one member per blkx table - one partition image each -
 * because that is the only separable unit a UDIF image has.
 *
 * @par Detection
 * The koly magic is at the END of the file, so a magic prefilter over the
 * first bytes of a device cannot see it.  Detection has to seek to
 * size - 512.
 *
 * @par Bounds
 * Sector counts and run lengths are 64-bit and attacker controlled.  Nothing
 * proportional to a declared size is allocated: run tables grow only as real
 * descriptors are read, expansion streams through a fixed staging buffer, and
 * a partition declaring an expansion past the ceiling is refused rather than
 * written out.
 */

#ifndef XXFCLIB_FORMAT_DMG_H
#define XXFCLIB_FORMAT_DMG_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The 512-byte trailer's magic, and its size. */
#define XX_DMG_KOLY_MAGIC UINT32_C(0x6B6F6C79)
#define XX_DMG_KOLY_SIZE 512
/** @brief The block table magic, and the sizes of its two structures. */
#define XX_DMG_MISH_MAGIC UINT32_C(0x6D697368)
#define XX_DMG_MISH_HEADER_SIZE 204
#define XX_DMG_RUN_SIZE 40
/** @brief The sector size every UDIF offset and count is expressed in. */
#define XX_DMG_SECTOR_SIZE 512

/** @brief Run descriptor types. */
#define XX_DMG_RUN_ZEROFILL UINT32_C(0x00000000)
#define XX_DMG_RUN_RAW UINT32_C(0x00000001)
#define XX_DMG_RUN_IGNORE UINT32_C(0x00000002)
#define XX_DMG_RUN_COMMENT UINT32_C(0x7FFFFFFE)
#define XX_DMG_RUN_ADC UINT32_C(0x80000004)
#define XX_DMG_RUN_ZLIB UINT32_C(0x80000005)
#define XX_DMG_RUN_BZIP2 UINT32_C(0x80000006)
#define XX_DMG_RUN_LZFSE UINT32_C(0x80000007)
#define XX_DMG_RUN_LZMA UINT32_C(0x80000008)
#define XX_DMG_RUN_TERMINATOR UINT32_C(0xFFFFFFFF)

typedef struct xx_dmg xx_dmg;
typedef struct xx_dmg xx_dmg_t;
typedef struct xx_dmg XDmg;

struct xx_dmg {
    Abstractformat format;
    uint64_t number_of_records;  /**< One per blkx table. */
    uint64_t number_of_members;
    uint64_t sector_count;       /**< The trailer's sector count. */
    uint32_t version;            /**< The trailer's version, always 4. */
    uint32_t flags;
    uint32_t image_variant;
    int64_t koly_offset;         /**< Where the trailer starts, or -1. */
    int64_t data_fork_offset;    /**< Relative to base_address. */
    int64_t data_fork_length;
    int64_t xml_offset;          /**< Relative to base_address. */
    int64_t xml_length;
    int64_t archive_end;         /**< End of the trailer, or -1. */
    void *internal;
};

XXFC_API void xx_dmg_init(xx_dmg *dmg, xx_io_device *dev, int64_t base_address);
XXFC_API xx_dmg *xx_dmg_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_dmg_destroy(xx_dmg *dmg);
XXFC_API void xx_dmg_free(xx_dmg *dmg);

XXFC_API bool xx_dmg_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_dmg_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_dmg_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_dmg_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

/**
 * @brief Whether the device carries a UDIF trailer in its last 512 bytes.
 *
 * The probe a magic prefilter cannot do: it seeks to size - 512 and checks
 * the magic, version and header length there.  It reads nothing else, so a
 * device that passes may still fail to parse.
 */
XXFC_API bool xx_dmg_probe_device(xx_io_device *dev, int64_t base_address);

/**
 * @brief Expand one partition into @p destination from its current position.
 *
 * @param index The partition, counted in blkx order.
 */
XXFC_API bool xx_dmg_unpack_partition_to_device(xx_dmg *dmg, size_t index,
                                                xx_io_device *destination,
                                                xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_dmg_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_dmg_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_dmg_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_dmg_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_dmg_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_dmg_get_number_of_records(const xx_dmg *dmg);
XXFC_API uint64_t xx_dmg_get_number_of_members(const xx_dmg *dmg);
XXFC_API uint64_t xx_dmg_get_sector_count(const xx_dmg *dmg);
XXFC_API int64_t xx_dmg_get_data_fork_length(const xx_dmg *dmg);
XXFC_API int64_t xx_dmg_get_xml_length(const xx_dmg *dmg);
XXFC_API int64_t xx_dmg_get_archive_end(const xx_dmg *dmg);

static inline Abstractformat *xx_dmg_to_format(xx_dmg *dmg) {
    return dmg ? &dmg->format : NULL;
}
static inline void XDmg_init(xx_dmg *dmg, xx_io_device *dev,
                             int64_t base_address) {
    xx_dmg_init(dmg, dev, base_address);
}
static inline xx_dmg *XDmg_create(xx_io_device *dev, int64_t base_address) {
    return xx_dmg_create(dev, base_address);
}
static inline void XDmg_free(xx_dmg *dmg) { xx_dmg_free(dmg); }
static inline bool XDmg_is_valid(xx_dmg *dmg, xx_pd_struct *pd) {
    return dmg ? xx_format_is_valid(&dmg->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_DMG_H */
