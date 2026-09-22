/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_volitionvpft.h
 *  @brief Volition "VP" package (Descent: FreeSpace / FreeSpace 2 .vp).
 */

/* WHERE THE LAYOUT COMES FROM.  U3's recognition predicate for its
 * VOLITION_VP_FT handler (FUN_006444a0, reached from VMT slot 0 at
 * 0x00644910) tests exactly four things, and they are reproduced verbatim
 * here:
 *
 *   u32 @ 0x00 == 0x50565056  ("VPVP")
 *   u32 @ 0x04 == 2           (the only version Volition ever shipped)
 *   i32 @ 0x08 >  0x10        (index offset, must clear the header)
 *   0 < i32 @ 0x0c < 0x10000  (entry count)
 *
 *   header, 16 bytes at offset 0, little endian:
 *     0x00  4    "VPVP"
 *     0x04  u32  version, 2
 *     0x08  u32  absolute offset of the index
 *     0x0c  u32  number of index entries
 *
 *   index entry, 44 bytes:
 *     0x00  u32  absolute data offset
 *     0x04  u32  data size; ZERO marks a directory entry
 *     0x08  32   name, NUL padded
 *     0x28  u32  UNIX timestamp
 *
 * The index is a pre-order walk of a directory tree: a zero-size entry named
 * ".." pops one level, any other zero-size entry pushes its name as a
 * subdirectory, and a nonzero-size entry is a stored file inside the current
 * directory.  Members are never compressed - there is no method field at all.
 *
 * CONFIRMED AGAINST THE CORPUS.  In all seven samples in
 * F:\ARC\ARC\VOLITION_VP_FT the index is the last thing in the file:
 * index_offset + 44 * count equals the file size exactly (44110592, 67431342,
 * 37933752, 3331292, 3331292, 3328118 and 2580475 bytes).  That is a strong
 * structural check and this reader enforces it as an upper bound, requiring
 * index_offset + 44 * count <= file size before a single entry is read, so a
 * four-byte count cannot ask for a large allocation.
 *
 * WHAT THIS READER DOES.  It publishes one record per FILE entry, with the
 * full path built from the directory walk, and extracts them by copying the
 * stored bytes.  Directory entries are not published as records: they carry
 * no data and their names already appear in the paths.
 */

#ifndef XXFCLIB_FORMAT_VOLITIONVPFT_H
#define XXFCLIB_FORMAT_VOLITIONVPFT_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Header magic. */
#define XX_VOLITIONVPFT_SIGNATURE "VPVP"
#define XX_VOLITIONVPFT_SIGNATURE_SIZE 4U
/** Header size in bytes. */
#define XX_VOLITIONVPFT_HEADER_SIZE 16U
/** Index entry size in bytes. */
#define XX_VOLITIONVPFT_ENTRY_SIZE 44U
/** Name field width inside an index entry. */
#define XX_VOLITIONVPFT_NAME_SIZE 32U
/** Only version ever shipped. */
#define XX_VOLITIONVPFT_VERSION 2U
/** Entry-count ceiling taken from U3's own predicate. */
#define XX_VOLITIONVPFT_MAX_ENTRIES 0xffffU
/** Deepest directory nesting this reader will follow. */
#define XX_VOLITIONVPFT_MAX_DEPTH 32U

typedef struct xx_volitionvpft xx_volitionvpft;
typedef struct xx_volitionvpft xx_volitionvpft_t;
typedef struct xx_volitionvpft XVolitionVpFt;

struct xx_volitionvpft {
    Abstractformat format;      /**< Base format structure (first member). */
    uint64_t number_of_records; /**< File entries, directories excluded. */
    uint64_t number_of_entries; /**< Raw index entry count from the header. */
    int64_t index_offset;       /**< Absolute offset of the index. */
};

XXFC_API void xx_volitionvpft_init(xx_volitionvpft *archive,
                                   xx_io_device *device,
                                   int64_t base_address);
XXFC_API xx_volitionvpft *xx_volitionvpft_create(xx_io_device *device,
                                                 int64_t base_address);
XXFC_API void xx_volitionvpft_destroy(xx_volitionvpft *archive);
XXFC_API void xx_volitionvpft_free(xx_volitionvpft *archive);

XXFC_API bool xx_volitionvpft_check_is_valid(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API bool xx_volitionvpft_handle_base_info(Abstractformat *self,
                                               xx_pd_struct *pd);
XXFC_API int64_t xx_volitionvpft_get_format_size(Abstractformat *self,
                                                 xx_pd_struct *pd);
XXFC_API uint64_t xx_volitionvpft_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *
xx_volitionvpft_create_archive_records_reading(Abstractformat *self,
                                               const xx_list_s *options,
                                               xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_volitionvpft_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_volitionvpft_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_volitionvpft_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_volitionvpft_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_VOLITIONVPFT_H */
