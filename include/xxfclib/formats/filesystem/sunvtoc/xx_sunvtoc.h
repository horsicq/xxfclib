/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_sunvtoc.h @brief Sun disk label / VTOC reader (SunOS and
 * Solaris SPARC labels, Solaris x86 VTOC). */

/* A Sun disk label is one 512-byte sector that ends with the magic 0xDABE
 * at +508 and a checksum at +510 chosen so that the XOR of all 256 16-bit
 * words of the sector is zero. Two layouts exist.
 *
 *   SPARC label (sector 0 of the disk, every field big endian)
 *     +0   128  ASCII label ("... cyl N alt N hd N sec N")
 *     +128 u32  VTOC version (1)
 *     +132 8    volume name
 *     +140 u16  number of slices (8)
 *     +142 8 x {u16 tag, u16 flag}
 *     +188 u32  VTOC sanity, 0x600DDEEE (absent on SunOS 4 labels)
 *     +420 u16  rpm, +422 pcyl, +424 alternates/cyl, +430 interleave,
 *     +432 u16  ncyl, +434 acyl, +436 heads, +438 sectors per track
 *     +444 8 x {u32 start cylinder, u32 sector count}
 *     +508 u16  0xDABE
 *     +510 u16  checksum
 *   A slice starts at start_cylinder * heads * sectors (512-byte sectors).
 *   The tags are used only when the VTOC sanity word is present.
 *
 *   x86 VTOC (sector 1 of a Solaris fdisk partition, little endian)
 *     +0   12   boot info
 *     +12  u32  sanity, 0x600DDEEE
 *     +16  u32  version (1)
 *     +20  8    volume name
 *     +28  u16  sector size (512)
 *     +30  u16  number of slices (<= 16)
 *     +72  16 x {u16 tag, u16 flag, u32 start sector, u32 sector count}
 *     +328 128  ASCII label
 *     +508 u16  0xDABE
 *     +510 u16  checksum
 *   Slice starts are relative to the start of the fdisk partition, which
 *   is the start of the format here.
 *
 * Every slice with a non-zero size that starts on the device is published
 * as a record named "slice<n>", n being its 0-based slot. The tag name
 * ("root", "swap", "backup", ...) goes to the record comment. Slices may
 * overlap (slice 2, "backup", normally covers the whole disk); each is
 * carried verbatim, clamped to the bytes the device holds. A label with no
 * such slice is refused.
 *
 * The format size runs to the end of the farthest slice or of the label
 * sector, never past the device.
 */

#ifndef XXFCLIB_FORMAT_SUNVTOC_H
#define XXFCLIB_FORMAT_SUNVTOC_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Most slices a label can hold (x86 VTOC). */
#define XX_SUNVTOC_MAX_SLICES 16U

/** Label layout, as reported by xx_sunvtoc_get_layout(). */
#define XX_SUNVTOC_LAYOUT_NONE 0U
#define XX_SUNVTOC_LAYOUT_SPARC 1U
#define XX_SUNVTOC_LAYOUT_X86 2U

typedef struct xx_sunvtoc xx_sunvtoc;
typedef struct xx_sunvtoc xx_sunvtoc_t;
typedef struct xx_sunvtoc XSunVtoc;

/** One published slice. */
typedef struct xx_sunvtoc_slice_info {
    int64_t offset;         /**< Absolute device offset of the payload. */
    int64_t size;           /**< Bytes actually present on the device. */
    uint64_t declared_size; /**< Sector count * 512. */
    uint64_t start_sector;  /**< First sector, relative to the label base. */
    uint32_t sector_count;  /**< Sectors declared by the label. */
    uint16_t tag;           /**< VTOC tag, 0 when the label has none. */
    uint16_t flag;          /**< VTOC flag, 0 when the label has none. */
    uint32_t slot;          /**< 0-based slot in the label. */
    const char *name;       /**< Record name, e.g. "slice2". */
} xx_sunvtoc_slice_info;

struct xx_sunvtoc {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t layout;               /**< XX_SUNVTOC_LAYOUT_*. */
    bool has_vtoc;                 /**< VTOC sanity word present. */
    uint32_t sectors_per_cylinder; /**< SPARC heads * sectors, else 0. */
    int64_t archive_end;           /**< End of the farthest slice, or -1. */
    void *internal;
};

XXFC_API void xx_sunvtoc_init(xx_sunvtoc *sunvtoc, xx_io_device *dev, int64_t base_address);
XXFC_API xx_sunvtoc *xx_sunvtoc_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_sunvtoc_destroy(xx_sunvtoc *sunvtoc);
XXFC_API void xx_sunvtoc_free(xx_sunvtoc *sunvtoc);

XXFC_API bool xx_sunvtoc_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_sunvtoc_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_sunvtoc_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_sunvtoc_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_sunvtoc_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_sunvtoc_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sunvtoc_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sunvtoc_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sunvtoc_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_sunvtoc_get_number_of_records(const xx_sunvtoc *sunvtoc);
XXFC_API uint32_t xx_sunvtoc_get_layout(const xx_sunvtoc *sunvtoc);
XXFC_API int64_t xx_sunvtoc_get_archive_end(const xx_sunvtoc *sunvtoc);

/** Fill info for the index-th published slice. Requires that base info has
 * already been handled. Returns false for an out-of-range index. */
XXFC_API bool xx_sunvtoc_get_slice_info(const xx_sunvtoc *sunvtoc, uint64_t index, xx_sunvtoc_slice_info *info);

static inline Abstractformat *xx_sunvtoc_to_format(xx_sunvtoc *sunvtoc)
{
    return sunvtoc ? &sunvtoc->format : NULL;
}
static inline void XSunVtoc_init(xx_sunvtoc *sunvtoc, xx_io_device *dev, int64_t base_address)
{
    xx_sunvtoc_init(sunvtoc, dev, base_address);
}
static inline xx_sunvtoc *XSunVtoc_create(xx_io_device *dev, int64_t base_address)
{
    return xx_sunvtoc_create(dev, base_address);
}
static inline void XSunVtoc_free(xx_sunvtoc *sunvtoc)
{
    xx_sunvtoc_free(sunvtoc);
}
static inline bool XSunVtoc_is_valid(xx_sunvtoc *sunvtoc, xx_pd_struct *pd)
{
    return sunvtoc ? xx_format_is_valid(&sunvtoc->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SUNVTOC_H */
